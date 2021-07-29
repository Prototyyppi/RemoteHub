/*
 * Copyright (C) 2021 Jani Laitinen
 *
 * This file is part of RemoteHub.
 *
 * RemoteHub is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RemoteHub is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RemoteHub.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __REMOTEHUB_USBIP_COMMAND_H__
#define __REMOTEHUB_USBIP_COMMAND_H__

#include <stdint.h>
#include <stdbool.h>

#include "cli_network.h"

bool exec_usbip_import_command(struct client_conn conn, char *busid, struct usbip_usb_device *dev,
			       struct est_conn *link);
bool exec_usbip_devlist_command(struct client_conn conn, struct usbip_usb_device **list,
				uint32_t *len);

#endif /* __REMOTEHUB_USBIP_COMMAND_H__ */
