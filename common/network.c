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

#include "network.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>

#include "logging.h"

// TODO: IPv6 support

int network_send(struct est_conn *link, uint8_t *data, uint32_t len)
{
	errno = 0;
	return link->encrypted ? network_tls_send(link, data, len) :
				 send(link->socket, data, len, MSG_NOSIGNAL);
}

int network_recv(struct est_conn *link, uint8_t *data, uint32_t len)
{
	errno = 0;
	return link->encrypted ? network_tls_recv(link, data, len) :
				 recv(link->socket, data, len, MSG_NOSIGNAL);
}

bool network_send_data(struct est_conn *link, uint8_t *data, uint32_t len)
{
	int ret;
	uint32_t snt = 0;

	while (snt < len) {
		ret = network_send(link, &data[snt], len - snt);
		if (ret <= 0) {
			rh_trace(LVL_WARN, "Network send fail %d sent:%d, %d\n", errno, snt, ret);
			return false;
		}
		snt += ret;
	}

	return true;
}

bool network_recv_data(struct est_conn *link, uint8_t *data, uint32_t len)
{
	int ret;
	uint32_t rcvd = 0;

	while (rcvd < len) {
		ret = network_recv(link, &data[rcvd], len - rcvd);
		if (ret <= 0) {
			rh_trace(LVL_WARN, "Network rcv fail %d rcvd:%d, %d\n",
				 errno, rcvd, ret);
			return false;
		}
		rcvd += ret;
	}

	return true;
}

void network_close_link(struct est_conn *link)
{
	if (!link->encrypted)
		network_close_tcp(link);
	else
		network_close_tls(link);
}

void network_shut_link(struct est_conn *link)
{
	if (!link->encrypted)
		network_shut_tcp(link);
	else
		network_shut_tls(link);
}

/* Pass zero for infinite value */
void network_send_timeout_seconds_set(int socket, uint32_t seconds)
{
	struct timeval timeout = {0};

	timeout.tv_sec = seconds;
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

/* Pass zero for infinite value */
void network_recv_timeout_seconds_set(int socket, uint32_t seconds)
{
	struct timeval timeout = {0};

	timeout.tv_sec = seconds;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}
