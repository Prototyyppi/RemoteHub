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

#include "cli_network.h"
#include "logging.h"

static bool try_connect(int socket, struct client_conn *conn)
{
	int ret;
	struct sockaddr_in servaddr = {0};

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr = conn->ip;
	servaddr.sin_port = htons(conn->port);

	rh_trace(LVL_TRC, "Try - Address: %s, port %d\n", inet_ntoa(conn->ip), conn->port);

	network_send_timeout_seconds_set(socket, 2);
	network_recv_timeout_seconds_set(socket, 2);

	ret = connect(socket, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if (ret != 0) {
		rh_trace(LVL_ERR, "Connect fail - Address: %s, port %d\n",
			 inet_ntoa(conn->ip), conn->port);
		close(socket);
		return false;
	}

	rh_trace(LVL_DBG, "Client connect - Address: %s, port %d\n",
			   inet_ntoa(conn->ip), conn->port);
	return true;
}

bool network_connect_tcp(struct client_conn conn, struct est_conn *link)
{
	link->encrypted = false;

	link->socket = socket(AF_INET, SOCK_STREAM, 0);
	if (link->socket == -1) {
		rh_trace(LVL_ERR, "Socket creation failed\n");
		return false;
	}

	return try_connect(link->socket, &conn);
}
