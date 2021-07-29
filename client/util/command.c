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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cli_network.h"
#include "logging.h"
#include "usbip.h"

bool exec_usbip_devlist_command(struct client_conn conn, struct usbip_usb_device **list,
				uint32_t *len)
{
	struct usbip_op_common cmd = {0};
	struct usbip_usb_interface interface;
	struct usbip_usb_device usbip_dev;
	struct est_conn link = {0};
	struct usbip_op_devlist_reply rep_hdr = {0};

	*len = 0;

	cmd.version = USBIP_DEFAULT_PROTOCOL_VERSION;
	cmd.code = USBIP_OP_REQ_DEVLIST;
	cmd.status = USBIP_ST_OK;

	if (!network_connect(conn, &link)) {
		rh_trace(LVL_ERR, "Connect failed\n");
		return false;
	}

	if (!usbip_net_send_usbip_header(&link, &cmd)) {
		network_close_link(&link);
		return false;
	}

	if (!usbip_net_recv_usbip_header(&link, &cmd)) {
		network_close_link(&link);
		return false;
	}

	if (cmd.code != USBIP_OP_REP_DEVLIST) {
		rh_trace(LVL_ERR, "Incorrect header 0x%x\n", cmd.code);
		network_close_link(&link);
		return false;
	}

	if (cmd.status != USBIP_ST_OK) {
		rh_trace(LVL_ERR, "Devicelisting failed with 0x%x\n", cmd.status);
		network_close_link(&link);
		return false;
	}

	if (!network_recv_data(&link, (uint8_t *)&rep_hdr, sizeof(rep_hdr))) {
		rh_trace(LVL_ERR, "Failed to receive data\n");
		network_close_link(&link);
		return false;
	}

	usbip_net_devlist_reply_from_network_order(&rep_hdr);

	rh_trace(LVL_DBG, "Incoming %d devices\n", rep_hdr.ndev);

	*list = calloc(rep_hdr.ndev, sizeof(struct usbip_usb_device));
	if (!(*list)) {
		rh_trace(LVL_ERR, "Out of memory\n");
		return false;
	}

	for (uint32_t i = 0; i < rep_hdr.ndev; i++) {
		if (!network_recv_data(&link, (uint8_t *)&usbip_dev, sizeof(usbip_dev))) {
			rh_trace(LVL_ERR, "Failed to receive data\n");
			network_close_link(&link);
			free(*list);
			return false;
		}
		usbip_net_dev_from_network_order(&usbip_dev);

		(*list)[i] = usbip_dev;
		for (int j = 0; j < usbip_dev.bNumInterfaces; j++) {
			if (!network_recv_data(&link, (uint8_t *)&interface, sizeof(interface))) {
				rh_trace(LVL_ERR, "Failed to receive data\n");
				network_close_link(&link);
				free(*list);
				return false;
			}
		}

	}
	*len = rep_hdr.ndev * sizeof(struct usbip_usb_device);
	network_close_link(&link);
	return true;
}

bool exec_usbip_import_command(struct client_conn conn, char *busid, struct usbip_usb_device *dev,
			       struct est_conn *link)
{
	struct usbip_op_common cmd = {0};
	struct usbip_op_import_request import_req = {0};
	struct usbip_op_import_reply import_rep;

	cmd.version = USBIP_DEFAULT_PROTOCOL_VERSION;
	cmd.code = USBIP_OP_REQ_IMPORT;
	cmd.status = USBIP_ST_OK;

	strncpy(import_req.busid, busid, USBIP_BUSID_SIZE - 1);

	if (!network_connect(conn, link)) {
		rh_trace(LVL_ERR, "Connect failed\n");
		return false;
	}

	if (!usbip_net_send_usbip_header(link, &cmd)) {
		network_close_link(link);
		return false;
	}

	if (!network_send_data(link, (uint8_t *)&import_req, sizeof(import_req))) {
		rh_trace(LVL_ERR, "Failed to receive data\n");
		network_close_link(link);
		return false;
	}

	if (!usbip_net_recv_usbip_header(link, &cmd)) {
		network_close_link(link);
		return false;
	}

	if (cmd.code != USBIP_OP_REP_IMPORT) {
		rh_trace(LVL_ERR, "Incorrect header 0x%x\n", cmd.code);
		network_close_link(link);
		return false;
	}

	if (cmd.status != USBIP_ST_OK) {
		rh_trace(LVL_ERR, "Attaching failed with 0x%x\n", cmd.status);
		network_close_link(link);
		return false;
	}

	if (!network_recv_data(link, (uint8_t *)&import_rep, sizeof(import_rep))) {
		rh_trace(LVL_ERR, "Failed to receive data\n");
		network_close_link(link);
		return false;
	}
	usbip_net_import_reply_from_network_order(&import_rep);

	*dev = import_rep.udev;

	return true;
}
