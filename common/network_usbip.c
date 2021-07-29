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

#include <stdint.h>
#include <stdbool.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "logging.h"
#include "usbip.h"

static void usbip_common_header_to_network_order(struct usbip_op_common *hdr)
{
	hdr->code = htons(hdr->code);
	hdr->status = htonl(hdr->status);
	hdr->version = htons(hdr->version);
}

static void usbip_common_header_from_network_order(struct usbip_op_common *hdr)
{
	hdr->code = ntohs(hdr->code);
	hdr->status = ntohl(hdr->status);
	hdr->version = ntohs(hdr->version);
}

bool usbip_net_send_usbip_header(struct est_conn *link, struct usbip_op_common *hdr)
{
	usbip_common_header_to_network_order(hdr);
	return network_send_data(link, (uint8_t *)hdr, sizeof(*hdr));
}

bool usbip_net_recv_usbip_header(struct est_conn *link, struct usbip_op_common *hdr)
{
	bool ok = network_recv_data(link, (uint8_t *)hdr, sizeof(*hdr));

	if (ok)
		usbip_common_header_from_network_order(hdr);
	return ok;
}

void usbip_net_dev_to_network_order(struct usbip_usb_device *dev)
{
	dev->bcdDevice = htons(dev->bcdDevice);
	dev->busnum = htonl(dev->busnum);
	dev->devnum = htonl(dev->devnum);
	dev->idProduct = htons(dev->idProduct);
	dev->idVendor = htons(dev->idVendor);
	dev->speed = htonl(dev->speed);
}

void usbip_net_dev_from_network_order(struct usbip_usb_device *dev)
{
	dev->bcdDevice = ntohs(dev->bcdDevice);
	dev->busnum = ntohl(dev->busnum);
	dev->devnum = ntohl(dev->devnum);
	dev->idProduct = ntohs(dev->idProduct);
	dev->idVendor = ntohs(dev->idVendor);
	dev->speed = ntohl(dev->speed);
}

void usbip_net_devlist_reply_to_network_order(struct usbip_op_devlist_reply *hdr)
{
	hdr->ndev = htonl(hdr->ndev);
}

void  usbip_net_devlist_reply_from_network_order(struct usbip_op_devlist_reply *hdr)
{
	hdr->ndev = ntohl(hdr->ndev);
}

void usbip_net_import_reply_to_network_order(struct usbip_op_import_reply *hdr)
{
	usbip_net_dev_to_network_order(&hdr->udev);
}

void usbip_net_import_reply_from_network_order(struct usbip_op_import_reply *hdr)
{
	usbip_net_dev_from_network_order(&hdr->udev);
}
