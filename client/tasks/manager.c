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

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "vhci.h"
#include "event.h"
#include "logging.h"
#include "manager.h"
#include "cli_interface.h"
#include "cli_event.h"
#include "usbip_command.h"

static struct rh_task manager;

static pthread_t manager_thread;
static bool use_tls;
static char ca_path[PATH_MAX];

static struct client_usb_device *usb_device_head;

#define for_each_remote_device(dev) \
	struct client_usb_device *next = NULL; \
	for (dev = usb_device_head; next = dev != NULL ? dev->next : NULL, \
	     dev != NULL; dev = next)

static bool insert_device(struct client_usb_device *device)
{
	device->next = usb_device_head;
	usb_device_head = device;
	rh_trace(LVL_DBG, "Insert %s\n", device->info.udev.path);
	return true;
}

static void exit_fwd(struct client_usb_device *device)
{
	rh_trace(LVL_DBG, "Stopping forwarding [%s]\n", device->info.udev.path);

	if (!device->fwd_terminated) {
		if (device->vhci_link)
			network_shut_link(device->vhci_link);
		if (device->local_fwd_socket != -1)
			shutdown(device->local_fwd_socket, SHUT_RDWR);
	}

	if (device->local_fwd_thread)
		pthread_join(device->local_fwd_thread, NULL);

	device->fwd_terminated = false;
	network_close_link(device->vhci_link);
	free(device->vhci_link);
	device->vhci_link = NULL;
}

static bool delete_device(struct client_usb_device *device)
{
	struct client_usb_device *tmp;

	if (!strcmp(usb_device_head->info.udev.busid, device->info.udev.busid)) {
		usb_device_head = usb_device_head->next;
		rh_trace(LVL_DBG, "Delete [%s] from head\n", device->info.udev.path);
		free(device);
		return true;
	}

	tmp = usb_device_head;
	while (tmp->next) {
		if (!strcmp(tmp->next->info.udev.busid, device->info.udev.busid)) {
			tmp->next = device->next;
			rh_trace(LVL_DBG, "Delete device [%s]\n", device->info.udev.path);
			if (device->vhci_link) {
				network_shut_link(device->vhci_link);
				network_close_link(device->vhci_link);
				free(device->vhci_link);
			}
			free(device);
			return true;
		}
		tmp = tmp->next;
	}
	free(device);
	return false;
}

static void get_server_devicelist(struct interface_request *devlist_cmd)
{
	struct usbip_usb_device *usbip_devlist = NULL;
	struct client_conn conn = {.use_tls = use_tls};
	struct rh_event event = {0};
	bool ok;
	uint32_t len;
	int ret;

	ret = inet_pton(AF_INET, devlist_cmd->ipv4, &conn.ip);
	if (ret <= 0) {
		rh_trace(LVL_ERR, "Failed to read given IP address\n");
		event.type = EVENT_DEVICELIST_FAILED;
		event.sts.success = false;
		event.sts.port = devlist_cmd->port;
		strncpy(event.sts.remote_server, devlist_cmd->ipv4, RH_IP_NAME_MAX_LEN - 1);
		(void) event_enqueue(&event);
		return;
	}

	conn.port = devlist_cmd->port;
	strncpy(conn.ca_path, ca_path, PATH_MAX - 1);

	rh_trace(LVL_DBG, "Sending devlist query to [%s]\n", devlist_cmd->ipv4);

	ok = exec_usbip_devlist_command(conn, &usbip_devlist, &len);
	if (ok) {
		event.type = EVENT_DEVICELIST_READY;
		event.data = usbip_devlist;
		event.size = len;
	} else {
		event.type = EVENT_DEVICELIST_FAILED;
	}

	event.sts.success = ok;
	event.sts.port = devlist_cmd->port;
	strncpy(event.sts.remote_server, devlist_cmd->ipv4, RH_IP_NAME_MAX_LEN - 1);

	(void) event_enqueue(&event);
	if (ok)
		free(usbip_devlist);
}

static bool is_usb3(uint32_t speed)
{
	switch (speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		return true;
	default:
		return false;
	}
}

static void inform_detached(struct usbip_usb_device dev, char *server_ip, uint16_t port, bool ok)
{
	struct rh_event event = {0};

	if (ok)
		event.type = EVENT_DETACHED;
	else
		event.type = EVENT_DETACH_FAILED;

	event.sts.success = true;
	event.sts.port = port;
	event.data = &dev;
	event.size = sizeof(struct usbip_usb_device);

	strncpy(event.sts.remote_server, server_ip, RH_IP_NAME_MAX_LEN - 1);

	(void) event_enqueue(&event);
}

static void inform_attached(struct usbip_usb_device dev, char *server_ip, uint16_t port)
{
	struct rh_event event = {0};

	event.type = EVENT_ATTACHED;
	event.sts.success = true;
	event.sts.port = port;
	event.data = &dev;
	event.size = sizeof(struct usbip_usb_device);

	strncpy(event.sts.remote_server, server_ip, RH_IP_NAME_MAX_LEN - 1);

	(void) event_enqueue(&event);
}

static void inform_attach_failed(struct usbip_usb_device dev, char *server_ip, uint16_t port)
{
	struct rh_event event = {0};

	event.type = EVENT_ATTACH_FAILED;
	event.sts.success = false;
	event.sts.port = port;
	event.data = &dev;
	event.size = sizeof(struct usbip_usb_device);

	strncpy(event.sts.remote_server, server_ip, RH_IP_NAME_MAX_LEN - 1);

	(void) event_enqueue(&event);
}

static bool detach_remote_device(struct interface_request *detach_cmd)
{
	struct client_usb_device *dev;
	bool device_exists = false;

	rh_trace(LVL_DBG, "Detaching %s\n", detach_cmd->dev.busid);
	for_each_remote_device(dev) {
		if (!strcmp(detach_cmd->dev.busid, dev->info.udev.busid) &&
		    !strncmp(dev->server_ipv4, detach_cmd->ipv4, RH_IP_NAME_MAX_LEN) &&
		    detach_cmd->port == dev->ip_port) {
			device_exists = true;
			break;
		}
	}

	if (device_exists) {
		exit_fwd(dev);
		delete_device(dev);
		inform_detached(detach_cmd->dev, detach_cmd->ipv4, detach_cmd->port, true);
	} else {
		inform_detached(detach_cmd->dev, detach_cmd->ipv4, detach_cmd->port, false);
	}

	return device_exists;
}

static bool attach_remote_device(struct interface_request *attach_cmd)
{
	struct client_usb_device *dev, *item;
	struct usbip_usb_device dev_at_busid;
	struct client_conn conn = {.use_tls = use_tls};
	struct est_conn *link;
	int ret;
	bool ok, device_exists = false;

	rh_trace(LVL_DBG, "Attaching %s [%s]\n", attach_cmd->dev.busid, attach_cmd->dev.path);
	ret = inet_pton(AF_INET, attach_cmd->ipv4, &conn.ip);
	if (ret <= 0) {
		rh_trace(LVL_DBG, "Failed to read ip\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		return false;
	}

	conn.port = attach_cmd->port;
	strncpy(conn.ca_path, ca_path, PATH_MAX - 1);

	device_exists = false;

	for_each_remote_device(dev) {
		if (!strncmp(attach_cmd->dev.busid, dev->info.udev.busid, USBIP_BUSID_SIZE) &&
		    !strncmp(dev->server_ipv4, attach_cmd->ipv4, RH_IP_NAME_MAX_LEN) &&
		    attach_cmd->port == dev->ip_port) {
			device_exists = true;
			break;
		}
	}

	if (device_exists) {
		rh_trace(LVL_DBG, "Device already attached\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		return false;
	}

	link = calloc(1, sizeof(struct est_conn));
	if (!link) {
		rh_trace(LVL_ERR, "Out of memory\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		return false;
	}

	ok = exec_usbip_import_command(conn, attach_cmd->dev.busid, &dev_at_busid, link);
	if (!ok) {
		rh_trace(LVL_ERR, "Import command execution failed\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		free(link);
		return false;
	}

	if (attach_cmd->dev.idProduct != dev_at_busid.idProduct) {
		rh_trace(LVL_ERR, "Devicelist needed again\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		network_close_link(link);
		free(link);
		return false;
	}

	if (attach_cmd->dev.idVendor != dev_at_busid.idVendor) {
		rh_trace(LVL_ERR, "Devicelist needed again\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		network_close_link(link);
		free(link);
		return false;
	}

	item = calloc(1, sizeof(struct client_usb_device));
	if (!item) {
		rh_trace(LVL_ERR, "Out of memory\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		network_close_link(link);
		free(link);
		return false;
	}

	strncpy(item->server_ipv4, attach_cmd->ipv4, RH_IP_NAME_MAX_LEN - 1);
	item->ip_port = attach_cmd->port;
	item->vhci_link = link;
	item->info.udev = dev_at_busid;
	item->local_fwd_socket = -1;
	item->local_fwd_thread = 0;

	ok = vhci_attach_device(item, is_usb3(attach_cmd->dev.speed));
	if (!ok) {
		rh_trace(LVL_ERR, "VHCI attach failed\n");
		inform_attach_failed(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);
		exit_fwd(item);
		free(item);
		return false;
	}

	insert_device(item);
	inform_attached(attach_cmd->dev, attach_cmd->ipv4, attach_cmd->port);

	return true;
}

uint32_t handle_event(struct rh_event *ev)
{
	struct client_usb_device *dev;

	switch (ev->type) {
	case EVENT_TIMER_5S:
		rh_trace(LVL_TRC, "Updating port usage\n");
		for_each_remote_device(dev) {
			if (dev->fwd_terminated) {
				inform_detached(dev->info.udev, dev->server_ipv4,
						dev->ip_port, true);
				exit_fwd(dev);
				delete_device(dev);
			}
		}
		break;
	case EVENT_DEVICELIST_REQUEST:
		get_server_devicelist((struct interface_request *)ev->data);
		free(ev->data);
		break;
	case EVENT_ATTACH_REQUESTED:
		attach_remote_device((struct interface_request *)ev->data);
		free(ev->data);
		break;
	case EVENT_DETACH_REQUESTED:
		detach_remote_device((struct interface_request *)ev->data);
		free(ev->data);
	}
	return 0;
}

void *manager_handler(void *args)
{
	struct rh_event *event;
	struct client_usb_device *dev;
	bool ok;

	(void) args;
	rh_trace(LVL_TRC, "Manager starting\n");

	while (manager.running) {
		ok = event_dequeue(&manager, &event);
		if (!ok) {
			rh_trace(LVL_TRC, "Manager stopping\n");
			break;
		}
		handle_event(event);
		free(event);
	}

	rh_trace(LVL_TRC, "Terminate connections\n");
	for_each_remote_device(dev) {
		exit_fwd(dev);
		delete_device(dev);
	}

	rh_trace(LVL_TRC, "Manager exit\n");

	return NULL;
}

void manager_exit(void)
{
	manager.running = false;
	pthread_cond_signal(&manager.event_cond);
	if (manager_thread)
		pthread_join(manager_thread, NULL);
}

enum rh_error_status manager_task_init(bool is_tls, char *capath)
{
	if (!vhci_is_available()) {
		rh_trace(LVL_ERR, "Need to load the VHCI driver\n");
		return RH_FAIL_VHCI_DRIVER;
	}

	if (is_tls) {
		rh_trace(LVL_ERR, "Initializing with TLS\n");
		if (!capath || capath[0] == 0) {
			rh_trace(LVL_ERR, "CA cert usage is enforced and needed to use TLS\n");
			return RH_FAIL_CA_PATH_NOT_DEFINED;
		}
		if (access(capath, F_OK)) {
			rh_trace(LVL_ERR, "Given CA cert file does not exist\n");
			return RH_FAIL_CA_PATH_NOT_DEFINED;
		}
		strncpy(ca_path, capath, PATH_MAX - 1);
	}

	use_tls = is_tls;

	manager.event_mask = EVENT_TIMER_5S | EVENT_DEVICELIST_REQUEST |
			     EVENT_ATTACH_REQUESTED | EVENT_DETACH_REQUESTED;
	strcpy(manager.task_name, "Manager task");
	event_task_register(&manager);

	manager.running = true;
	if (pthread_create(&manager_thread, NULL, manager_handler, &use_tls)) {
		rh_trace(LVL_ERR, "Failed to start manager\n");
		return RH_FAIL_INIT_MANAGER;
	}

	return RH_OK;
}
