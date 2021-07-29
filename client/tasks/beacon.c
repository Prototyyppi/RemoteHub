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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "network.h"
#include "logging.h"
#include "beacon.h"
#include "event.h"
#include "task.h"

#include "cli_event.h"

static pthread_t beacon_thread;
static bool use_tls;

static struct rh_task beacon;

static int beacon_socket;

bool beacon_init(void)
{
	int ret;
	struct sockaddr_in beacon_socket_info;

	beacon_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (beacon_socket == -1) {
		rh_trace(LVL_ERR, "Beacon socket creation failed\n");
		return false;
	}

	beacon_socket_info.sin_family = AF_INET;
	beacon_socket_info.sin_addr.s_addr = htons(INADDR_ANY);
	beacon_socket_info.sin_port = htons(DEFAULT_PORT);

	ret = bind(beacon_socket, (struct sockaddr *)&beacon_socket_info,
		   sizeof(struct sockaddr_in));
	if (ret < 0) {
		rh_trace(LVL_ERR, "Beacon socket bind failed (%s)\n", strerror(errno));
		return false;
	}

	return true;
}

void beacon_from_network_order(struct beacon_packet *bc)
{
	bc->ident = ntohl(bc->ident);
	bc->id = ntohl(bc->id);
	bc->version_major = ntohl(bc->version_major);
	bc->version_minor = ntohl(bc->version_minor);
	bc->port = ntohs(bc->port);
	bc->attention = ntohl(bc->attention);
}

static void handle_packet(struct beacon_packet bcn, struct in_addr ip)
{
	struct rh_event event = {0};
	struct available_server srv = {0};

	beacon_from_network_order(&bcn);

	if ((bcn.ident == BEACON_IDENT) && (bcn.use_tls == use_tls)) {
		rh_trace(LVL_DBG, "Found %s at %s:%d, version %d.%d\n",
			 bcn.name, inet_ntoa(ip), bcn.port, bcn.version_major,
			 bcn.version_minor);

		if (bcn.version_major > REMOTEHUB_VERSION_MAJOR) {
			rh_trace(LVL_DBG, "Server is not compatible\n");
			return;
		}

		if (bcn.version_minor > REMOTEHUB_VERSION_MINOR)
			rh_trace(LVL_DBG, "Server may have unsupported features\n");

		memcpy(&srv.ip, inet_ntoa(ip), RH_IP_NAME_MAX_LEN - 1);
		memcpy(&srv.name, bcn.name, RH_SERVER_NAME_MAX_LEN - 1);
		srv.port = bcn.port;
		srv.version = bcn.port;
		srv.id = bcn.id;

		event.type = EVENT_SERVER_DISCOVERED;
		event.data = &srv;
		event.size = sizeof(struct available_server);
		(void) event_enqueue(&event);
	}
}

void beacon_receive(void)
{
	int ret;
	struct beacon_packet bcn;
	struct sockaddr_in server_beacon_info;
	socklen_t len = sizeof(server_beacon_info);

	while (beacon.running) {
		ret = recvfrom(beacon_socket, &bcn, sizeof(bcn), 0,
			       (struct sockaddr *)&server_beacon_info, &len);
		if (ret <= 0) {
			rh_trace(LVL_DBG, "Beacon receive failed (%d)\n", ret);
			continue;
		}
		rh_trace(LVL_DBG, "Beacon received\n");
		handle_packet(bcn, server_beacon_info.sin_addr);
	}
}

void beacon_exit(void)
{
	rh_trace(LVL_TRC, "Beacon task terminate\n");

	if (beacon_socket) {
		shutdown(beacon_socket, SHUT_RDWR);
		close(beacon_socket);
	}

	beacon.running = false;
	if (beacon_thread) {
		pthread_cond_signal(&beacon.event_cond);
		pthread_join(beacon_thread, NULL);
	}
}

static void *beacon_task(void *args)
{
	rh_trace(LVL_TRC, "Beacon task starting\n");
	(void) args;

	while (beacon.running)
		beacon_receive();

	rh_trace(LVL_TRC, "Beacon task exit\n");

	return NULL;
}

bool beacon_recv_init(bool is_tls)
{
	if (!beacon_init()) {
		// Only one beacon listener is allowed
		rh_trace(LVL_WARN, "Beacon not supported\n");
		return true;
	}

	use_tls = is_tls;
	beacon.event_mask = 0;
	beacon.running = true;
	strcpy(beacon.task_name, "Beacon task");
	event_task_register(&beacon);

	if (pthread_create(&beacon_thread, NULL, beacon_task, NULL)) {
		rh_trace(LVL_ERR, "Failed to start beacon\n");
		return false;
	}

	return true;
}
