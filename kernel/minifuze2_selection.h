/* SPDX-License-Identifier: GPL-2.0-only */
/* Caller supplies bool, strcmp and strlen (kernel or native test headers). */
#ifndef MINIFUZE2_SELECTION_H
#define MINIFUZE2_SELECTION_H

static inline bool mf2_selection_valid(const char *port, const char *serial)
{
	bool has_port = port && *port;
	bool has_serial = serial && *serial;

	return has_port != has_serial &&
		(!has_port || strlen(port) < 64) &&
		(!has_serial || strlen(serial) < 128);
}

static inline bool mf2_identity_matches(unsigned int vendor, unsigned int product,
		const char *port, const char *serial,
		const char *selected_port, const char *selected_serial)
{
	if (vendor != 0x1c75 || product != 0xaf90 ||
	    !mf2_selection_valid(selected_port, selected_serial))
		return false;
	if (selected_port && *selected_port)
		return port && !strcmp(port, selected_port);
	return serial && !strcmp(serial, selected_serial);
}

#endif
