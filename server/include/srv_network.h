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

#ifndef __REMOTEHUB_SRV_NETWORK_H__
#define __REMOTEHUB_SRV_NETWORK_H__

#include <stdbool.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include "network.h"
#include "beacon.h"
#include "server.h"

struct server_conn {
	struct in_addr		ip;
	uint16_t		port;
	uint8_t			encryption;
	uint32_t		attention;
	int			socket;
	struct tls		tls;
	struct server_info	info;
};

bool network_listen(struct server_conn *conn, struct est_conn *link);
bool network_create_server(struct server_conn *conn);
bool network_create_tcp_server(struct server_conn *conn);
bool network_listen_tcp(struct server_conn *conn, struct est_conn *link);

bool network_create_tls_server(struct server_conn *conn);
bool network_listen_tls(struct server_conn *conn, struct est_conn *link);
void network_exit_server_tls(struct server_conn *conn);

#endif /* __REMOTEHUB_SRV_NETWORK_H__ */
