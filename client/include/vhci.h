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

#ifndef __REMOTEHUB_VHCI_H__
#define __REMOTEHUB_VHCI_H__

#include <stdint.h>
#include <stdbool.h>

#include "manager.h"

struct vhci_port {
	uint32_t	port;
	int32_t		connfd;
	uint32_t	devid;
	int32_t		status;
	uint32_t	speed;
	char		hub[3];
	char		local_busid[USBIP_BUSID_SIZE];
};

#define VHCI_MAX_PORTS		16
#define VHCI_PORT_AVAILABLE	0x04

#define USB_SPEED_LOW		1
#define USB_SPEED_FULL		2
#define USB_SPEED_HIGH		3
#define USB_SPEED_WIRELESS	4
#define USB_SPEED_SUPER		5
#define USB_SPEED_SUPER_PLUS	6

bool vhci_is_available(void);
bool vhci_detach_device(struct client_usb_device *dev);
bool vhci_attach_device(struct client_usb_device *dev, bool usb3_port);

#endif /* __REMOTEHUB_VHCI_H__ */
