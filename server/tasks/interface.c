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

#include "srv_interface.h"

#include <stdlib.h>
#include <string.h>

#include "remotehub.h"
#include "logging.h"
#include "server.h"
#include "srv_event.h"
#include "event.h"
#include "task.h"

static pthread_t intf_thread;
static pthread_mutex_t intf_lock;
static struct rh_task intf;

static void (*DEVICELIST_cb)(struct usb_device_info *devlist, int count);
static void (*DEVICE_EXPORTED_cb)(enum usb_dev_state state, struct usbip_usb_device dev);
static void (*DEVICE_UNEXPORTED_cb)(enum usb_dev_state state, struct usbip_usb_device dev);
static void (*DEVICE_ATTACHED_cb)(enum usb_dev_state state, struct usbip_usb_device dev);
static void (*DEVICE_DETACHED_cb)(enum usb_dev_state state, struct usbip_usb_device dev);

static void handle_event(struct rh_event *ev)
{
	switch (ev->type) {
	case EVENT_LOCAL_DEVICELIST:
		if (DEVICELIST_cb)
			DEVICELIST_cb((struct usb_device_info *)ev->data,
				     ev->size / sizeof(struct usb_device_info));
		else
			free(ev->data);
		break;
	case EVENT_DEVICE_EXPORTED:
		if (DEVICE_EXPORTED_cb)
			DEVICE_EXPORTED_cb(EXPORTED, *(struct usbip_usb_device *)(ev->data));
		free(ev->data);
		break;
	case EVENT_DEVICE_UNEXPORTED:
		if (DEVICE_UNEXPORTED_cb)
			DEVICE_UNEXPORTED_cb(UNEXPORTED, *(struct usbip_usb_device *)(ev->data));
		free(ev->data);
		break;
	case EVENT_DEVICE_ATTACHED:
		if (DEVICE_ATTACHED_cb)
			DEVICE_ATTACHED_cb(ATTACHED, *(struct usbip_usb_device *)(ev->data));
		free(ev->data);
		break;
	case EVENT_DEVICE_DETACHED:
		if (DEVICE_DETACHED_cb)
			DEVICE_DETACHED_cb(DETACHED, *(struct usbip_usb_device *)(ev->data));
		free(ev->data);
		break;
	default:
		rh_trace(LVL_DBG, "Unknown event received (%x)\n", ev->type);
		return;
	}
}

void rh_free_server_devlist(struct usb_device_info *devlist)
{
	free(devlist);
}

void rh_devicelist_subscribe(void (*callback)(struct usb_device_info *devlist, int count))
{
	pthread_mutex_lock(&intf_lock);
	DEVICELIST_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_devicelist_unsubscribe(void)
{
	pthread_mutex_lock(&intf_lock);
	DEVICELIST_cb = NULL;
	pthread_mutex_unlock(&intf_lock);
}

void rh_attached_subscribe(void (*callback)(enum usb_dev_state state, struct usbip_usb_device dev))
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_ATTACHED_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_attached_unsubscribe(void)
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_ATTACHED_cb = NULL;
	pthread_mutex_unlock(&intf_lock);
}

void rh_detached_subscribe(void (*callback)(enum usb_dev_state state, struct usbip_usb_device dev))
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_DETACHED_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_detached_unsubscribe(void)
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_DETACHED_cb = NULL;
	pthread_mutex_unlock(&intf_lock);
}

void rh_exported_subscribe(void (*callback)(enum usb_dev_state state, struct usbip_usb_device dev))
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_EXPORTED_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_exported_unsubscribe(void)
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_EXPORTED_cb = NULL;
	pthread_mutex_unlock(&intf_lock);
}

void rh_unexported_subscribe(void (*callback)(enum usb_dev_state state,
			     struct usbip_usb_device dev))
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_UNEXPORTED_cb = callback;
	pthread_mutex_unlock(&intf_lock);
}

void rh_unexported_unsubscribe(void)
{
	pthread_mutex_lock(&intf_lock);
	DEVICE_UNEXPORTED_cb = NULL;
	pthread_mutex_unlock(&intf_lock);
}


static void *intf_loop(void *args)
{
	struct rh_event *event;
	bool ok;

	(void) args;
	rh_trace(LVL_TRC, "Server interface starting\n");

	while (intf.running) {
		ok = event_dequeue(&intf, &event);
		if (!ok) {
			rh_trace(LVL_TRC, "Server interface stopping\n");
			break;
		}

		pthread_mutex_lock(&intf_lock);
		handle_event(event);
		pthread_mutex_unlock(&intf_lock);
		free(event);
	}

	rh_trace(LVL_TRC, "Server interface quit\n");
	return NULL;
}

void interface_exit(void)
{
	rh_attached_unsubscribe();
	rh_detached_unsubscribe();
	rh_exported_unsubscribe();
	rh_unexported_unsubscribe();

	rh_trace(LVL_TRC, "Server interface terminate\n");
	intf.running = false;
	pthread_cond_signal(&intf.event_cond);
	if (intf_thread)
		pthread_join(intf_thread, NULL);
	rh_trace(LVL_TRC, "Server interface terminated\n");
}

bool interface_task_init(void)
{
	rh_trace(LVL_TRC, "Server interface init\n");

	pthread_mutex_init(&intf_lock, NULL);

	intf.event_mask = EVENT_LOCAL_DEVICELIST | EVENT_DEVICE_EXPORTED |
			  EVENT_DEVICE_UNEXPORTED | EVENT_DEVICE_ATTACHED |
			  EVENT_DEVICE_DETACHED;
	strcpy(intf.task_name, "Server interface task");
	intf.running = true;
	event_task_register(&intf);

	if (pthread_create(&intf_thread, NULL, intf_loop, NULL)) {
		rh_trace(LVL_ERR, "Failed to start interface task\n");
		return false;
	}

	return true;
}
