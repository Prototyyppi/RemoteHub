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

#include "srv_network.h"

bool network_listen(struct server_conn *conn, struct est_conn *link)
{
	return conn->encryption ? network_listen_tls(conn, link) :
				  network_listen_tcp(conn, link);
}

bool network_create_server(struct server_conn *conn)
{
	return conn->encryption ? network_create_tls_server(conn) :
				  network_create_tcp_server(conn);
}
