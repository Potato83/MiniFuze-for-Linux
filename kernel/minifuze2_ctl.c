// SPDX-License-Identifier: GPL-2.0-only
/* Bounded EP0 access without binding a USB driver or detaching snd-usb-audio.
 * Experimental. Every operation acquires its own USB reference and PM usage.
 */
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include "minifuze2_uapi.h"
#include "minifuze2_selection.h"

static char *device_path;
module_param(device_path, charp, 0444);
MODULE_PARM_DESC(device_path, "Explicit USB port path; use this OR device_serial; only 1c75:af90 accepted");
static char *device_serial;
module_param(device_serial, charp, 0444);
MODULE_PARM_DESC(device_serial, "Explicit USB serial; follows this unit across ports; rejects duplicate matches");
static DEFINE_MUTEX(control_mutex);

struct mf2_lookup {
	struct usb_device *device;
	bool ambiguous;
};

static int match_device(struct usb_device *dev, void *context)
{
	struct mf2_lookup *found = context;

	if (!mf2_identity_matches(le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct), dev_name(&dev->dev),
			dev->serial, device_path, device_serial))
		return 0;
	if (found->device) {
		found->ambiguous = true;
		return 1;
	}
	found->device = usb_get_dev(dev);
	return 0;
}

static bool valid_selector(u16 selector)
{
	switch (selector) {
	case 0x0000: case 0x0001: case 0x0400: case 0x0500:
	case 0x1300: case 0x1400: case 0x1500:
		return true;
	default:
		return false;
	}
}

static int transfer(struct usb_device *dev, bool input, u8 request,
		    u16 selector, void *buffer, u16 length)
{
	unsigned int pipe = input ? usb_rcvctrlpipe(dev, 0) : usb_sndctrlpipe(dev, 0);
	u8 type = USB_TYPE_CLASS | USB_RECIP_INTERFACE | (input ? USB_DIR_IN : USB_DIR_OUT);
	int result;

	result = usb_control_msg(dev, pipe, request, type, selector, 0,
				 buffer, length, 1000);
	if (result < 0)
		return result;
	return result == length ? 0 : -EREMOTEIO;
}

static long mf2_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
	void __user *user = (void __user *)argument;
	struct mf2_switch control = {};
	struct mf2_state state = { .version = MF2_ABI_VERSION };
	struct mf2_meters meters = { .version = MF2_ABI_VERSION };
	struct mf2_info info = { .version = MF2_ABI_VERSION };
	struct usb_device *dev = NULL;
	struct mf2_lookup lookup = {};
	__le16 *buffer;
	u16 requested = 0;
	int result;

	/* Validate the entire ABI before device lookup or any USB operation. */
	switch (command) {
	case MF2_GET_SWITCH:
	case MF2_SET_INST:
	case MF2_SET_DIRECT_MONO:
	case MF2_SET_PHANTOM48V:
	case MF2_SET_ROUTING:
		if (copy_from_user(&control, user, sizeof(control)))
			return -EFAULT;
		if (control.version != MF2_ABI_VERSION || !valid_selector(control.selector))
			return -EINVAL;
		if (command == MF2_SET_INST && (control.selector > 1 || control.value > 1))
			return -EINVAL;
		if (command == MF2_SET_DIRECT_MONO && (control.selector != 0x0500 || control.value > 1))
			return -EINVAL;
		if (command == MF2_SET_PHANTOM48V && (control.selector != 0x0400 || control.value > 1))
			return -EINVAL;
		if (command == MF2_SET_ROUTING &&
		    ((control.selector != 0x1300 && control.selector != 0x1400 &&
		      control.selector != 0x1500) || control.value > 1))
			return -EINVAL;
		break;
	case MF2_GET_STATE:
	case MF2_GET_METERS:
	case MF2_GET_INFO:
		break;
	default:
		return -ENOTTY;
	}

	/* USB DMA buffers must come from the heap, not the kernel stack. */
	buffer = kzalloc(sizeof(state.data), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	result = mutex_lock_interruptible(&control_mutex);
	if (result)
		goto free_buffer;
	usb_for_each_dev(&lookup, match_device);
	dev = lookup.device;
	if (lookup.ambiguous) {
		result = -ENOTUNIQ;
		goto put_device;
	}
	if (!dev) {
		result = -ENODEV;
		goto unlock;
	}
	result = pm_runtime_resume_and_get(&dev->dev);
	if (result < 0)
		goto put_device;

	switch (command) {
	case MF2_GET_INFO:
		info.vid = le16_to_cpu(dev->descriptor.idVendor);
		info.pid = le16_to_cpu(dev->descriptor.idProduct);
		info.bcd_device = le16_to_cpu(dev->descriptor.bcdDevice);
		info.capabilities = MF2_CAP_DIRECT_MONO | MF2_CAP_PHANTOM48V | MF2_CAP_ROUTING | MF2_CAP_METERS;
		strscpy(info.port, dev_name(&dev->dev), sizeof(info.port));
		result = copy_to_user(user, &info, sizeof(info)) ? -EFAULT : 0;
		break;
	case MF2_GET_STATE:
		result = transfer(dev, true, 0xb2, 0, buffer, sizeof(state.data));
		if (!result) {
			memcpy(state.data, buffer, sizeof(state.data));
			result = copy_to_user(user, &state, sizeof(state)) ? -EFAULT : 0;
		}
		break;
	case MF2_GET_METERS:
		result = transfer(dev, true, 0xc2, 0, buffer, sizeof(meters.data));
		if (!result) {
			memcpy(meters.data, buffer, sizeof(meters.data));
			result = copy_to_user(user, &meters, sizeof(meters)) ? -EFAULT : 0;
		}
		break;
	case MF2_SET_INST:
	case MF2_SET_DIRECT_MONO:
	case MF2_SET_PHANTOM48V:
	case MF2_SET_ROUTING:
		requested = control.value;
		*buffer = cpu_to_le16(requested);
		result = transfer(dev, false, 0x22, control.selector, buffer, 2);
		if (result)
			break;
		fallthrough;
	case MF2_GET_SWITCH:
		result = transfer(dev, true, 0xa2, control.selector, buffer, 2);
		if (!result) {
			control.value = le16_to_cpu(*buffer);
			if (command != MF2_GET_SWITCH && control.value != requested)
				result = -EIO;
			else
				result = copy_to_user(user, &control, sizeof(control)) ? -EFAULT : 0;
		}
		break;
	}
	pm_runtime_mark_last_busy(&dev->dev);
	pm_runtime_put(&dev->dev);
put_device:
	usb_put_dev(dev);
unlock:
	mutex_unlock(&control_mutex);
free_buffer:
	kfree(buffer);
	return result;
}

static const struct file_operations mf2_operations = {
	.owner = THIS_MODULE,
	.open = nonseekable_open,
	.unlocked_ioctl = mf2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
};

static struct miscdevice mf2_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "minifuze2ctl",
	.fops = &mf2_operations,
	.mode = 0600,
};

static int __init mf2_init(void)
{
	static_assert(sizeof(struct mf2_switch) == 8);
	static_assert(sizeof(struct mf2_state) == 52);
	static_assert(sizeof(struct mf2_meters) == 20);
	static_assert(sizeof(struct mf2_info) == 76);
	if (!mf2_selection_valid(device_path, device_serial))
		return -EINVAL;
	return misc_register(&mf2_device);
}

static void __exit mf2_exit(void)
{
	misc_deregister(&mf2_device);
}

module_init(mf2_init);
module_exit(mf2_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2.0");
MODULE_DESCRIPTION("Experimental bounded MiniFuse 2 controls preserving ALSA binding");
