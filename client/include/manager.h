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

#ifndef __REMOTEHUB_CLI_MANAGER_H__
#define __REMOTEHUB_CLI_MANAGER_H__

#include "remotehub.h"
#include "network.h"

struct client_usb_device {
	struct usb_device_info		info;
	char				server_ipv4[RH_IP_NAME_MAX_LEN];
	int				ip_port;
	struct est_conn			*vhci_link;
	int				vhci_port;
	bool				fwd_terminated;
	int				local_fwd_socket;
	pthread_t			local_fwd_thread;
	struct client_usb_device	*next;
};

enum rh_error_status manager_task_init(bool use_tls, char *ca_path);
void manager_exit(void);

#endif /* __REMOTEHUB_CLI_MANAGER_H__*/
