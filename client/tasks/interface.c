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

#include <stdbool.h>

#include "remotehub.h"
#include "logging.h"
#include "cli_interface.h"
#include "client.h"
#include "cli_event.h"
#include "event.h"
#include "beacon.h"
#include "task.h"

static pthread_t intf_thread;
static pthread_mutex_t intf_lock;

static void (*ATTACH_cb)(bool success, char *server_ip, uint16_t port, struct usbip_usb_device dev);
static void (*DETACH_cb)(bool success, char *server_ip, uint16_t port, struct usbip_usb_device dev);
static void (*GET_USBIP_DEVICELIST_cb)(bool success, char *server_ip, uint16_t port,
				       struct usbip_usb_device *dev, uint32_t dev_size);
static void (*SERVER_DISCOVERED_cb)(char *server_ip, uint16_t port, char *name);

static struct rh_task intf;

static void handle_event(struct rh_event *ev)
{
	struct available_server *srv;

	switch (ev->type) {
	case EVENT_ATTACHED:
		if (ATTACH_cb)
			ATTACH_cb(true, ev->sts.remote_server, ev->sts.port,
				  *(struct usbip_usb_device *)ev->data);
		free(ev->data);
		break;
	case EVENT_DETACHED:
		if (DETACH_cb)
			DETACH_cb(true, ev->sts.remote_server, ev->sts.port,
				  *(struct usbip_usb_device *)ev->data);
		free(ev->data);
		break;
	case EVENT_ATTACH_FAILED:
		if (ATTACH_cb)
			ATTACH_cb(false, ev->sts.remote_server, ev->sts.port,
				  *(struct usbip_usb_device *)ev->data);
		free(ev->data);
		break;
	case EVENT_DETACH_FAILED:
		if (DETACH_cb)
			DETACH_cb(false, ev->sts.remote_server, ev->sts.port,
				  *(struct usbip_usb_device *)ev->data);
		free(ev->data);
		break;
	case EVENT_DEVICELIST_FAILED:
		if (GET_USBIP_DEVICELIST_cb)
			GET_USBIP_DEVICELIST_cb(false, ev->sts.remote_server,
						ev->sts.port, NULL, 0);
		break;
	case EVENT_DEVICELIST_READY:
		if (GET_USBIP_DEVICELIST_cb)
			GET_USBIP_DEVICELIST_cb(true, ev->sts.remote_server, ev->sts.port,
						(struct usbip_usb_device *)ev->data,
						ev->size / sizeof(struct usbip_usb_device));
		break;
	case EVENT_SERVER_DISCOVERED:
		srv = (struct available_server *)ev->data;
		if (SERVER_DISCOVERED_cb)
			SERVER_DISCOVERED_cb(srv->ip, srv->port, srv->name);
		free(ev->data);
		break;
	default:
		return;
	}
}

void rh_free_client_devlist(struct usbip_usb_device *devlist)
{
	free(devlist);
}

void rh_usbip_devicelist_subscribe(void (*callback)
	(bool success, char *server_ip, uint16_t port, struct usbip_usb_device *list, uint32_t len))
{
	pthread_mutex_lock(&intf_lock);
	GET_USBIP_DEVICELIST_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_attach_subscribe(void (*callback)
			(bool success, char *server_ip, uint16_t port, struct usbip_usb_device dev))
{
	pthread_mutex_lock(&intf_lock);
	ATTACH_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_detach_subscribe(void (*callback)
			(bool success, char *server_ip, uint16_t port, struct usbip_usb_device dev))
{
	pthread_mutex_lock(&intf_lock);
	DETACH_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_server_discovery_subscribe(void (*callback)(char *server_ip, uint16_t port, char *name))
{
	pthread_mutex_lock(&intf_lock);
	SERVER_DISCOVERED_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

bool rh_get_devicelist(char *ip, uint16_t port)
{
	struct rh_event event = {0};
	struct interface_request req = {0};

	strncpy(req.ipv4, ip, RH_IP_NAME_MAX_LEN - 1);
	req.port = port;

	event.type = EVENT_DEVICELIST_REQUEST;
	event.size = sizeof(struct interface_request);
	event.data = &req;

	(void) event_enqueue(&event);

	return true;
}

void rh_attach_device(char *server_ip, uint16_t port, struct usbip_usb_device dev)
{
	struct rh_event event = {0};
	struct interface_request req = {0};

	strncpy(req.ipv4, server_ip, RH_IP_NAME_MAX_LEN - 1);
	req.port = port;
	req.dev = dev;

	event.type = EVENT_ATTACH_REQUESTED;
	event.size = sizeof(struct interface_request);
	event.data = &req;

	(void) event_enqueue(&event);
}

void rh_detach_device(char *server_ip, uint16_t port, struct usbip_usb_device dev)
{
	struct rh_event event = {0};
	struct interface_request req = {0};

	strncpy(req.ipv4, server_ip, RH_IP_NAME_MAX_LEN - 1);
	req.port = port;
	req.dev = dev;

	event.type = EVENT_DETACH_REQUESTED;
	event.size = sizeof(struct interface_request);
	event.data = &req;

	(void) event_enqueue(&event);
}

static void *intf_loop(void *args)
{
	struct rh_event *event;
	bool ok;

	intf.running = true;
	(void) args;
	rh_trace(LVL_TRC, "Client interface starting\n");

	while (intf.running) {
		ok = event_dequeue(&intf, &event);
		if (!ok) {
			rh_trace(LVL_TRC, "Client interface stopping\n");
			break;
		}

		pthread_mutex_lock(&intf_lock);
		handle_event(event);
		pthread_mutex_unlock(&intf_lock);
		free(event);
	}

	rh_trace(LVL_TRC, "Client interface quit\n");

	return NULL;
}

void interface_exit(void)
{
	rh_trace(LVL_TRC, "Client interface terminate\n");
	intf.running = false;
	pthread_cond_signal(&intf.event_cond);
	if (intf_thread)
		pthread_join(intf_thread, NULL);
	pthread_mutex_destroy(&intf_lock);
}

bool interface_task_init(void)
{
	rh_trace(LVL_TRC, "Client interface init\n");

	pthread_mutex_init(&intf_lock, NULL);
	intf.event_mask = EVENT_SERVER_DISCOVERED | EVENT_DEVICELIST_READY |
			  EVENT_DEVICELIST_FAILED | EVENT_ATTACHED |
			  EVENT_DETACHED | EVENT_ATTACH_FAILED;
	strcpy(intf.task_name, "Client interface");
	event_task_register(&intf);

	if (pthread_create(&intf_thread, NULL, intf_loop, NULL)) {
		rh_trace(LVL_ERR, "Failed to start client interface\n");
		return false;
	}

	return true;
}
