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

#ifndef __REMOTEHUB_CLIENT_TASK_H__
#define __REMOTEHUB_CLIENT_TASK_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/limits.h>
#include <pthread.h>

#include "remotehub.h"
#include "logging.h"

struct client_info {
	bool tls_enabled;
	char ca_path[PATH_MAX];
};

bool rh_get_devicelist(char *ip, uint16_t port);
void rh_attach_device(char *ip, uint16_t port, struct usbip_usb_device dev);
void rh_detach_device(char *ip, uint16_t port, struct usbip_usb_device dev);
void rh_attach_subscribe(void (*callback)(bool success, char *server, uint16_t port,
			 struct usbip_usb_device dev));
void rh_detach_subscribe(void (*callback)(bool success, char *server, uint16_t port,
			 struct usbip_usb_device dev));
void rh_server_discovery_subscribe(void (*callback)(char *server_ip, uint16_t port, char *name));
void rh_usbip_devicelist_subscribe(void (*callback)(bool success, char *server, uint16_t port,
				   struct usbip_usb_device *devlist, uint32_t count));

void rh_free_client_devlist(struct usbip_usb_device *list);
char *rh_get_client_dependency_versions(void);
int rh_client_config_init(char *conf_path);
void rh_client_exit(void);

#endif /* __REMOTEHUB_CLIENT_TASK_H__ */
