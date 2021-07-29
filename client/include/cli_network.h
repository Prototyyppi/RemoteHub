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

#ifndef __REMOTEHUB_CLI_NETWORK_H__
#define __REMOTEHUB_CLI_NETWORK_H__

#include <unistd.h>

#include <linux/limits.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "network.h"

struct client_conn {
	struct in_addr		ip;
	uint16_t		port;
	uint8_t			use_tls;
	char			ca_path[PATH_MAX];
};

bool network_connect(struct client_conn conn, struct est_conn *link);
bool network_connect_tls(struct client_conn conn, struct est_conn *link);
bool network_connect_tcp(struct client_conn conn, struct est_conn *link);

#endif /*__REMOTEHUB_CLI_NETWORK_H__ */
