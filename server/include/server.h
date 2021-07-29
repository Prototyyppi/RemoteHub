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

#ifndef __REMOTEHUB_SERVER_H__
#define __REMOTEHUB_SERVER_H__

#include <stdint.h>

#include <linux/limits.h>

#include "remotehub.h"

#define KEY_PASSWORD_MAX_LEN	128

struct server_info {
	bool tls_enabled;
	bool bcast_enabled;
	uint16_t port;
	char server_name[RH_SERVER_NAME_MAX_LEN];
	char cert_path[PATH_MAX];
	char key_path[PATH_MAX];
	char ca_path[PATH_MAX];
	char key_pass[KEY_PASSWORD_MAX_LEN];
};

enum usb_dev_state {
	ATTACHED,
	DETACHED,
	EXPORTED,
	UNEXPORTED
};

void rh_devicelist_subscribe(void (*callback)(struct usb_device_info *devlist, int count));
void rh_attached_subscribe(void (*callback)(enum usb_dev_state state, struct usbip_usb_device dev));
void rh_attached_unsubscribe(void);
void rh_detached_subscribe(void (*callback)(enum usb_dev_state state, struct usbip_usb_device dev));
void rh_detached_unsubscribe(void);
void rh_exported_subscribe(void (*callback)(enum usb_dev_state state, struct usbip_usb_device dev));
void rh_exported_unsubscribe(void);
void rh_unexported_subscribe(void (*callback)(enum usb_dev_state state,
					      struct usbip_usb_device dev));
void rh_unexported_unsubscribe(void);

void rh_free_server_devlist(struct usb_device_info *devlist);
bool rh_disable_usb_bus(int bus);
int rh_server_config_init(char *conf_path);
char *rh_get_server_dependency_versions(void);
void rh_server_exit(void);

#endif /* __REMOTEHUB_SERVER_H__ */
