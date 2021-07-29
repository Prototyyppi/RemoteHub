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

#ifndef __REMOTEHUB_BEACON_H__
#define __REMOTEHUB_BEACON_H__

#include <stdint.h>
#include <stdbool.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "remotehub.h"

#define BEACON_IDENT		0x5248424E

struct beacon_packet {
	uint32_t	ident;
	uint32_t	id;
	uint32_t	version_major;
	uint32_t	version_minor;
	int8_t		name[RH_SERVER_NAME_MAX_LEN];
	uint16_t	port;
	uint8_t		use_tls;
	uint32_t	attention;
} __attribute__((packed));

struct available_server {
	char		ip[RH_IP_NAME_MAX_LEN];
	uint32_t	id;
	uint16_t	port;
	uint32_t	version;
	char		name[RH_SERVER_NAME_MAX_LEN];
};

void beacon_exit(void);
bool beacon_send_init(char *name, bool enabled, bool use_tls, uint16_t port);
bool beacon_recv_init(bool use_tls);

#endif /* __REMOTEHUB_BEACON_H__*/
