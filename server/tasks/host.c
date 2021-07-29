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

#include "srv_network.h"
#include "logging.h"
#include "event.h"
#include "server.h"
#include "srv_event.h"
#include "usbip.h"

static struct rh_task host_rx;
static struct server_conn conn;
static pthread_t server_rx_thread;
static bool server_started;

static void handle_usbip_op_devlist(struct est_conn *link)
{
	struct rh_event event = {0};

	event.type = EVENT_REQ_DEVICELIST;
	event.link = link;

	if (!event_enqueue(&event)) {
		network_close_link(link);
		free(link);
	}
}

static void handle_usbip_op_import(struct est_conn *link)
{
	struct rh_event event = {0};

	event.type = EVENT_REQ_IMPORT;
	event.link = link;

	if (!event_enqueue(&event)) {
		network_close_link(link);
		free(link);
	}
}

static void handle_usbip_command(struct est_conn *link)
{
	struct usbip_op_common hdr;

	if (!usbip_net_recv_usbip_header(link, &hdr)) {
		rh_trace(LVL_ERR, "Failed to receive usbip header\n");
		network_close_link(link);
		free(link);
		return;
	}

	switch (hdr.code) {
	case USBIP_OP_REQ_DEVLIST:
		rh_trace(LVL_DBG, "Received OP_REQ_DEVLIST\n");
		handle_usbip_op_devlist(link);
		break;
	case USBIP_OP_REQ_IMPORT:
		rh_trace(LVL_DBG, "Received OP_REQ_IMPORT\n");
		handle_usbip_op_import(link);
		break;
	default:
		rh_trace(LVL_ERR, "Unknown command %d\n", hdr.code);
		network_close_link(link);
		free(link);
		return;
	}
}

static void *usbip_rx_handler(void *args)
{
	struct est_conn *link = NULL;

	(void) args;

	while (host_rx.running) {
		link = calloc(1, sizeof(struct est_conn));
		if (!link) {
			rh_trace(LVL_ERR, "Out of memory\n");
			continue;
		}

		if (!network_listen(&conn, link)) {
			rh_trace(LVL_ERR, "Network listen failed\n");
			free(link);
			usleep(100000);
			continue;
		}
		handle_usbip_command(link);
	}

	rh_trace(LVL_TRC, "Host exit\n");

	return NULL;
}

void host_exit(void)
{
	rh_trace(LVL_TRC, "Host network terminate\n");

	if (server_started) {
		if (!conn.encryption)
			shutdown(conn.socket, SHUT_RDWR);
		else {
			mbedtls_ssl_close_notify(&conn.tls.ssl);
			shutdown(conn.tls.listen_fd.MBEDTLS_PRIVATE(fd), SHUT_RDWR);
		}
	}

	host_rx.running = false;
	pthread_cond_signal(&host_rx.event_cond);
	if (server_rx_thread)
		pthread_join(server_rx_thread, NULL);
	if (server_started) {
		if (conn.encryption)
			network_exit_server_tls(&conn);
		else
			close(conn.socket);
	}

	rh_trace(LVL_TRC, "Host network terminated\n");
}

bool host_task_init(struct server_info info)
{
	rh_trace(LVL_TRC, "Host network init\n");

	conn.encryption = info.tls_enabled;
	conn.port = info.port;
	conn.info = info;

	if (!network_create_server(&conn)) {
		rh_trace(LVL_ERR, "Failed to create server\n");
		return false;
	}

	host_rx.event_mask = 0;
	host_rx.running = true;
	strcpy(host_rx.task_name, "Host network task");
	event_task_register(&host_rx);

	if (pthread_create(&server_rx_thread, NULL, usbip_rx_handler, NULL)) {
		rh_trace(LVL_ERR, "Failed to start rx thread\n");
		return false;
	}

	server_started = true;
	return true;
}
