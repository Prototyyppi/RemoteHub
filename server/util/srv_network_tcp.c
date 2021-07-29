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

#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>

#include "srv_network.h"
#include "logging.h"

bool network_create_tcp_server(struct server_conn *conn)
{
	struct sockaddr_in srvaddr = {0};
	int ret, one = 1;

	conn->socket = socket(AF_INET, SOCK_STREAM, 0);
	if (conn->socket <= -1) {
		rh_trace(LVL_ERR, "Failed to create socket %d\n", conn->socket);
		return false;
	}

	srvaddr.sin_family = AF_INET;
	srvaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	srvaddr.sin_port = htons(conn->port);

	/* TCP_NODELAY a gives noticeable speed increase when using f.ex mouse */
	setsockopt(conn->socket, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(conn->socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(conn->socket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

	ret = bind(conn->socket, (struct sockaddr *)&srvaddr, sizeof(srvaddr));
	if (ret != 0) {
		rh_trace(LVL_ERR, "Server bind failed\n");
		return false;
	}
	rh_trace(LVL_DBG, "Server bound - Address: %s, port %d\n",
			  inet_ntoa(srvaddr.sin_addr), conn->port);

	return true;
}

bool network_listen_tcp(struct server_conn *conn, struct est_conn *link)
{
	struct sockaddr_in cli;
	socklen_t len;

	rh_trace(LVL_TRC, "Listening...\n");
	if ((listen(conn->socket, 5)) != 0) {
		rh_trace(LVL_ERR, "Listen failed\n");
		return false;
	}

	len = sizeof(struct sockaddr_in);

	// TODO: Implement keepalive for detecting broken connection

	link->socket = accept(conn->socket, (struct sockaddr *)&cli, &len);
	if (link->socket < 0) {
		rh_trace(LVL_ERR, "Accept failed\n");
		return false;
	}

	rh_trace(LVL_DBG, "Incoming connection from %s\n", inet_ntoa(cli.sin_addr));

	return true;
}
