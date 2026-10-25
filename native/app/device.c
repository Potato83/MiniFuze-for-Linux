/* SPDX-License-Identifier: MIT */
#include "../../kernel/minifuze2_uapi.h"
#include "mf.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(struct mf2_info) == 76, "MiniFuse info ABI");
_Static_assert(sizeof(struct mf2_switch) == 8, "MiniFuse switch ABI");
_Static_assert(sizeof(struct mf2_state) == 52, "MiniFuse state ABI");
_Static_assert(sizeof(struct mf2_meters) == 20, "MiniFuse meter ABI");
static const int selectors[] = {0, 1, 0x400, 0x500, 0x1300, 0x1400, 0x1500};
static const char *names[] = {"inst1",
                              "inst2",
                              "phantom48v",
                              "direct_mono",
                              "loopback",
                              "loopback_to_usb12",
                              "loopback_to_monitor"};
static int request(int fd, unsigned long command, void *data) {
  if (ioctl(fd, command, data))
    return fail("MiniFuse ioctl: %s", strerror(errno));
  uint32_t version;
  memcpy(&version, data, 4);
  if (version != 1)
    return fail("Unsupported MiniFuse ABI");
  return 0;
}
static int open_device(const char *path, struct mf2_info *info) {
  uint16_t endian = 1;
  if (*(unsigned char *)&endian != 1)
    return fail("MiniFuse ABI currently requires little-endian Linux");
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    return fail("Cannot open %s: %s", path, strerror(errno));
  struct stat st;
  if (fstat(fd, &st) || !S_ISCHR(st.st_mode)) {
    close(fd);
    return fail("Expected MiniFuse character device");
  }
  memset(info, 0, sizeof *info);
  if (request(fd, MF2_GET_INFO, info) || info->vid != 0x1c75 ||
      info->pid != 0xaf90 || !memchr(info->port, 0, sizeof info->port)) {
    close(fd);
    return fail("Endpoint is not a supported MiniFuse 2");
  }
  return fd;
}
J *device_status(const char *path) {
  struct mf2_info info;
  int fd = open_device(path, &info);
  if (fd < 0)
    return NULL;
  J *result = json_object_new_object(), *identity = json_object_new_object(),
    *controls = json_object_new_object(), *status = json_object_new_object(),
    *switches = json_object_new_object();
  put(result, "ok", json_object_new_boolean(true));
  put(result, "identity", identity);
  put(result, "controls", controls);
  put(result, "status", status);
  put(status, "switches", switches);
  char hex[128];
  snprintf(hex, sizeof hex, "%04x", info.vid);
  put(identity, "vid", json_object_new_string(hex));
  snprintf(hex, sizeof hex, "%04x", info.pid);
  put(identity, "pid", json_object_new_string(hex));
  snprintf(hex, sizeof hex, "%04x", info.bcd_device);
  put(identity, "bcd_device", json_object_new_string(hex));
  put(identity, "port", json_object_new_string((char *)info.port));
  put(controls, "direct_mono",
      json_object_new_boolean(info.capabilities & MF2_CAP_DIRECT_MONO));
  put(controls, "phantom48v",
      json_object_new_boolean(info.capabilities & MF2_CAP_PHANTOM48V));
  put(controls, "hardware_routing",
      json_object_new_boolean(info.capabilities & MF2_CAP_ROUTING));
  for (int i = 0; i < 7; i++) {
    struct mf2_switch s = {.version = 1, .selector = selectors[i]};
    if (request(fd, MF2_GET_SWITCH, &s) || s.selector != selectors[i] ||
        s.value > 1)
      goto bad;
    put(switches, names[i], json_object_new_boolean(s.value));
  }
  struct mf2_state state = {0};
  if (request(fd, MF2_GET_STATE, &state))
    goto bad;
  J *words = json_object_new_array();
  for (int i = 0; i < 46; i++)
    snprintf(hex + 2 * i, 3, "%02x", state.data[i]);
  put(status, "state_hex", json_object_new_string(hex));
  for (int i = 0; i < 23; i++)
    json_object_array_add(
        words,
        json_object_new_int(state.data[2 * i] | (state.data[2 * i + 1] << 8)));
  put(status, "state_words_u16_le", words);
  close(fd);
  return result;
bad:
  close(fd);
  json_object_put(result);
  return NULL;
}
int device_set(const char *path, int selector, bool enabled) {
  int index = -1;
  for (int i = 0; i < 7; i++)
    if (selector == selectors[i])
      index = i;
  if (index < 0)
    return fail("Unknown MiniFuse switch");
  struct mf2_info info;
  int fd = open_device(path, &info);
  if (fd < 0)
    return -1;
  unsigned long op = MF2_SET_INST;
  uint32_t cap = 0;
  if (index == 2) {
    op = MF2_SET_PHANTOM48V;
    cap = MF2_CAP_PHANTOM48V;
  } else if (index == 3) {
    op = MF2_SET_DIRECT_MONO;
    cap = MF2_CAP_DIRECT_MONO;
  } else if (index >= 4) {
    op = MF2_SET_ROUTING;
    cap = MF2_CAP_ROUTING;
  }
  if (cap && !(info.capabilities & cap)) {
    close(fd);
    return fail("Loaded kernel module does not support this control");
  }
  struct mf2_switch s = {.version = 1, .selector = selector, .value = enabled};
  int rc = request(fd, op, &s);
  if (!rc && (s.selector != selector || s.value != (unsigned)enabled))
    rc = fail("Kernel write readback mismatch");
  if (!rc) {
    s = (struct mf2_switch){.version = 1, .selector = selector};
    rc = request(fd, MF2_GET_SWITCH, &s);
    if (!rc && (s.selector != selector || s.value != (unsigned)enabled))
      rc = fail("Device write readback mismatch");
  }
  close(fd);
  return rc;
}
J *device_meters(const char *path, const char *port) {
  struct mf2_info info;
  int fd = open_device(path, &info);
  if (fd < 0)
    return NULL;
  if (port && strcmp(port, (char *)info.port)) {
    close(fd);
    fail("Audio device and control endpoint differ");
    return NULL;
  }
  if (!(info.capabilities & MF2_CAP_METERS)) {
    close(fd);
    fail("Hardware meters unsupported by loaded module");
    return NULL;
  }
  struct mf2_meters m = {0};
  int rc = request(fd, MF2_GET_METERS, &m);
  close(fd);
  if (rc)
    return NULL;
  J *o = json_object_new_object(), *raw = json_object_new_array(),
    *levels = json_object_new_array();
  put(o, "port", json_object_new_string((char *)info.port));
  put(o, "raw_s32", raw);
  put(o, "levels", levels);
  uint32_t bits = 0x34000001;
  float reciprocal;
  memcpy(&reciprocal, &bits, 4);
  for (int i = 0; i < 4; i++) {
    int32_t value;
    memcpy(&value, m.data + 4 * i, 4);
    int32_t shifted =
        value >= 0 ? value / 256 : -1 - (int32_t)((-(int64_t)value - 1) / 256);
    json_object_array_add(raw, json_object_new_int64(value));
    json_object_array_add(
        levels, json_object_new_double((float)((float)shifted * reciprocal)));
  }
  return o;
}
