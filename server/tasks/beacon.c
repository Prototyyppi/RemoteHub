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

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "remotehub.h"
#include "logging.h"
#include "beacon.h"
#include "event.h"
#include "task.h"
#include "network.h"
#include "server.h"
#include "srv_event.h"

static pthread_t beacon_thread;
static uint16_t port;
static struct rh_task beacon;
static bool beacon_enabled, server_is_tls;

static char server_name[RH_SERVER_NAME_MAX_LEN];
static int beacon_socket;
static struct sockaddr_in beacon_socket_info;

static bool beacon_init(void)
{
	int one = 1, ret;

	beacon_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (beacon_socket == -1)
		return false;

	beacon_socket_info.sin_family = AF_INET;
	beacon_socket_info.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	beacon_socket_info.sin_port = htons(port);

	ret = setsockopt(beacon_socket, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
	if (ret) {
		close(beacon_socket);
		return false;
	}

	return true;
}

static void beacon_to_network_order(struct beacon_packet *bc)
{
	bc->ident = htonl(bc->ident);
	bc->id = htonl(bc->id);
	bc->version_major = htonl(bc->version_major);
	bc->version_minor = htonl(bc->version_minor);
	bc->port = htons(bc->port);
	bc->attention = htonl(bc->attention);
}

static void beacon_send(void)
{
	int ret;
	struct beacon_packet server_info = {0};

	server_info.ident = BEACON_IDENT;
	server_info.version_major = REMOTEHUB_VERSION_MAJOR;
	server_info.version_minor = REMOTEHUB_VERSION_MINOR;
	server_info.port = port;
	server_info.use_tls = server_is_tls;
	server_info.id = 0;
	memcpy(&server_info.name, server_name, RH_SERVER_NAME_MAX_LEN - 1);

	beacon_to_network_order(&server_info);

	ret = sendto(beacon_socket, &server_info, sizeof(server_info), 0,
		     (struct sockaddr *)&beacon_socket_info,
		      sizeof(beacon_socket_info));
	if (ret < 0)
		rh_trace(LVL_WARN, "Beacon sendto failed\n");
}

static uint32_t handle_event(struct rh_event *ev)
{
	switch (ev->type) {
	case EVENT_TIMER_5S:
		rh_trace(LVL_TRC, "Received EVENT_TIMER_5S\n");
		beacon_send();
		break;
	default:
		rh_trace(LVL_DBG, "Received unwanted event %x\n", ev->type);
		break;
	}

	return 0;
}

static void *beacon_task(void *args)
{
	struct rh_event *event;
	bool ok;

	(void) args;
	rh_trace(LVL_TRC, "Beacon task starting\n");

	while (beacon.running) {
		ok = event_dequeue(&beacon, &event);
		if (!ok) {
			rh_trace(LVL_TRC, "Beacon task stopping\n");
			break;
		}
		handle_event(event);
		free(event);
	}

	rh_trace(LVL_TRC, "Beacon task exit\n");
	if (beacon_socket) {
		shutdown(beacon_socket, SHUT_RDWR);
		close(beacon_socket);
	}

	return NULL;
}

void beacon_exit(void)
{
	if (beacon_enabled) {
		rh_trace(LVL_TRC, "Beacon task terminate\n");
		beacon.running = false;
		pthread_cond_signal(&beacon.event_cond);
		pthread_join(beacon_thread, NULL);
	}
}

bool beacon_send_init(char *name, bool enabled, bool tls_enabled, uint16_t portnum)
{
	port = portnum;
	server_is_tls = tls_enabled;
	beacon_enabled = enabled;

	if (!beacon_enabled) {
		beacon.running = false;
		return true;
	}

	if (!beacon_init()) {
		rh_trace(LVL_WARN, "Beacon not supported\n");
		beacon_enabled = false;
		return false;
	}

	strncpy(server_name, name, RH_SERVER_NAME_MAX_LEN - 1);

	beacon.event_mask = EVENT_TIMER_5S;
	strcpy(beacon.task_name, "Beacon task");
	event_task_register(&beacon);

	beacon.running = true;
	if (pthread_create(&beacon_thread, NULL, beacon_task, NULL)) {
		rh_trace(LVL_ERR, "Failed to start beacon\n");
		beacon_enabled = false;
		return false;
	}

	return true;
}
