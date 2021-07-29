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

#include "vhci.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "cli_network.h"
#include "logging.h"

#define USBIP_VHCI_BUS_TYPE	"platform"
#define USBIP_VHCI_DEV_NAME	"vhci_hcd.0"

static bool read_vhci_sysfs_attribute(char *attr, char *result, int *len)
{
	int ret, fd;
	char location[PATH_MAX];

	ret = sprintf(location, "/sys/devices/%s/%s/%s", USBIP_VHCI_BUS_TYPE,
							 USBIP_VHCI_DEV_NAME,
							 attr);
	if (ret < 0) {
		rh_trace(LVL_DBG, "Sprintf failed\n");
		return false;
	}

	fd = open(location, O_RDONLY);
	if (fd < 0) {
		rh_trace(LVL_DBG, "Open failed\n");
		return false;
	}

	ret = read(fd, result, *len);
	if (ret < 0) {
		rh_trace(LVL_DBG, "Read failed\n");
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

static bool vhci_hub_parse(struct vhci_port *port, int port_count)
{
	char *status;
	char tmp_status[4096] = {0};
	int len = 4096, ret;
	bool ok;

	ok = read_vhci_sysfs_attribute("status", tmp_status, &len);
	if (!ok) {
		rh_trace(LVL_ERR, "Failed to open status node\n");
		return false;
	}

	/* Header skip */
	status = strchr(tmp_status, '\n');
	if (!status) {
		rh_trace(LVL_ERR, "Strchr failed\n");
		return false;
	}

	for (int i = 0; i < port_count; i++) {
		status++;
		ret = sscanf(status, "%2s %u %d %u %x %d %31s",
			    port[i].hub, &port[i].port, &port[i].status,
			    &port[i].speed, &port[i].devid, &port[i].connfd,
			    port[i].local_busid);
		if (ret != 7) {
			rh_trace(LVL_ERR, "Sscanf failed\n");
			return false;
		}

		status = strchr(status, '\n');
		if (!status)
			break;

	}
	return true;
}

static int vhci_get_free_port(bool usb3_port)
{
	bool ok;
	struct vhci_port port[VHCI_MAX_PORTS];

	ok = vhci_hub_parse(port, VHCI_MAX_PORTS);
	if (!ok) {
		rh_trace(LVL_ERR, "Failed to parse VHCI\n");
		return -1;
	}

	if (usb3_port) {
		for (int i = 0; i < VHCI_MAX_PORTS; i++) {
			if (!strcmp(port[i].hub, "ss") &&
			   (port[i].status == VHCI_PORT_AVAILABLE)) {
				return i;
			}
		}
		rh_trace(LVL_ERR, "No free USB3 ports\n");
		return -1;
	}

	for (int i = 0; i < VHCI_MAX_PORTS; i++) {
		if (!strcmp(port[i].hub, "hs") &&
			(port[i].status == VHCI_PORT_AVAILABLE)) {
			return i;
		}
	}

	rh_trace(LVL_ERR, "No free USB2 ports\n");
	return -1;
}

bool vhci_is_available(void)
{
	int ret;
	char location[PATH_MAX];

	ret = sprintf(location, "/sys/devices/%s/%s/%s", USBIP_VHCI_BUS_TYPE,
							 USBIP_VHCI_DEV_NAME,
							 "status");
	if (ret < 0) {
		rh_trace(LVL_DBG, "Sprintf failed\n");
		return false;
	}

	if (access(location, F_OK) == 0)
		return true;

	return false;
}

static bool write_vhci_sysfs_attribute(char *attr, char *value, int len)
{
	int ret, fd;
	char location[PATH_MAX];

	ret = sprintf(location, "/sys/devices/%s/%s/%s", USBIP_VHCI_BUS_TYPE,
							 USBIP_VHCI_DEV_NAME,
							 attr);
	if (ret < 0) {
		rh_trace(LVL_ERR, "Sprintf failed\n");
		return false;
	}

	fd = open(location, O_WRONLY);
	if (fd < 0) {
		rh_trace(LVL_ERR, "Open %s failed\n", location);
		return false;
	}

	ret = write(fd, value, len);
	if (ret < 0) {
		rh_trace(LVL_ERR, "Write %s failed\n", value);
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

static void *fwd_rx(void *device)
{
	struct client_usb_device *dev = (struct client_usb_device *) device;
	struct est_conn fwd_link = {0};
	int ret, act_read;
	uint8_t data[4096];

	fwd_link.encrypted = false;
	fwd_link.socket = dev->local_fwd_socket;

	while (1) {
		act_read = network_recv(dev->vhci_link, data, 4096);
		if (act_read <= 0) {
			rh_trace(LVL_DBG, "Failed to receive data (%d)\n", act_read);
			network_shut_link(&fwd_link);
			break;
		}
		ret = network_send_data(&fwd_link, data, act_read);
		if (ret <= 0) {
			rh_trace(LVL_DBG, "Failed to send to VHCI\n");
			network_shut_link(dev->vhci_link);
			break;
		}
	}

	rh_trace(LVL_DBG, "Local RX [%s] terminate now\n", dev->info.udev.path);

	return NULL;
}

static void *fwd_tx(void *device)
{
	struct client_usb_device *dev = (struct client_usb_device *) device;
	struct est_conn fwd_link = {0};
	int ret, act_read;
	uint8_t data[4096];

	fwd_link.encrypted = false;
	fwd_link.socket = dev->local_fwd_socket;

	while (1) {
		act_read = network_recv(&fwd_link, data, 4096);
		if (act_read <= 0) {
			rh_trace(LVL_DBG, "Failed to receive from VHCI\n");
			network_shut_link(dev->vhci_link);
			break;
		}
		ret = network_send_data(dev->vhci_link, data, act_read);
		if (ret <= 0) {
			rh_trace(LVL_DBG, "Failed to send data\n");
			network_shut_link(&fwd_link);
			break;
		}
	}

	rh_trace(LVL_DBG, "Local TX [%s] terminate now\n", dev->info.udev.path);

	return NULL;
}

static void *monitor_forward(void *device)
{
	pthread_t rx_fwd_thread, tx_fwd_thread;
	struct client_usb_device *dev = (struct client_usb_device *) device;

	if (dev->vhci_link->encrypted) {
		network_send_timeout_seconds_set(dev->vhci_link->tls.socket_fd.MBEDTLS_PRIVATE(fd),
						 0);
		network_recv_timeout_seconds_set(dev->vhci_link->tls.socket_fd.MBEDTLS_PRIVATE(fd),
						 0);
	} else {
		network_send_timeout_seconds_set(dev->vhci_link->socket, 0);
		network_recv_timeout_seconds_set(dev->vhci_link->socket, 0);
	}

	if (pthread_create(&tx_fwd_thread, NULL, fwd_tx, dev)) {
		rh_trace(LVL_ERR, "TX Create failed\n");
		goto fwd_monitor_exit;
	}

	if (pthread_create(&rx_fwd_thread, NULL, fwd_rx, dev)) {
		rh_trace(LVL_ERR, "RX Create failed\n");
		network_shut_link(dev->vhci_link);
		shutdown(dev->local_fwd_socket, SHUT_RDWR);
		pthread_join(tx_fwd_thread, NULL);
		goto fwd_monitor_exit;
	}

	pthread_join(tx_fwd_thread, NULL);
	pthread_join(rx_fwd_thread, NULL);

fwd_monitor_exit:
	network_close_link(dev->vhci_link);
	close(dev->local_fwd_socket);

	rh_trace(LVL_DBG, "Local forward [%s] terminate now\n", dev->info.udev.path);
	dev->fwd_terminated = true;

	return NULL;
}

static int setup_forward(struct client_usb_device *dev)
{
	int fd[2], ret;

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
	if (ret < 0) {
		rh_trace(LVL_ERR, "Socketpair creation failed\n");
		return ret;
	}

	dev->local_fwd_socket = fd[1];

	if (pthread_create(&dev->local_fwd_thread, NULL, monitor_forward, dev)) {
		rh_trace(LVL_ERR, "Forward thread creation failed\n");
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	return fd[0];
}

bool vhci_attach_device(struct client_usb_device *dev, bool usb3_port)
{
	bool ok;
	char value[128] = {0};
	int port, socket;

	port = vhci_get_free_port(usb3_port);
	if (port < 0) {
		rh_trace(LVL_ERR, "Failed to get free VHCI port\n");
		return false;
	}

	rh_trace(LVL_DBG, "Got VHCI port %d\n", port);

	socket = setup_forward(dev);
	if (socket < 0) {
		rh_trace(LVL_ERR, "Failed to create forwading sockets\n");
		return false;
	}

	snprintf(value, 128, "%u %d %u %u", port, socket,
					    dev->info.udev.devnum | dev->info.udev.busnum << 16,
					    dev->info.udev.speed);

	ok = write_vhci_sysfs_attribute("attach", value, sizeof(value));
	if (!ok) {
		rh_trace(LVL_ERR, "Failed to write attach\n");
		close(socket);
		return false;
	}

	close(socket);
	dev->vhci_port = port;

	return true;
}

bool vhci_detach_device(struct client_usb_device *dev)
{
	bool ret;
	char value[128] = {0};

	snprintf(value, 128, "%u", dev->vhci_port);

	rh_trace(LVL_DBG, "Detach port %d\n", dev->vhci_port);

	ret = write_vhci_sysfs_attribute("detach", value, sizeof(value));
	if (!ret) {
		rh_trace(LVL_ERR, "Failed to write detach\n");
		return false;
	}

	return true;
}
