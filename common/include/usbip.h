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

#ifndef __REMOTEHUB_USBIP_H__
#define __REMOTEHUB_USBIP_H__

/*
 * USBIP protocol by Takahiro Hirofuchi. For protocol definitions, see:
 * https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html
 *
 * For more information about the USBIP protocol see:
 * https://github.com/torvalds/linux/blob/master/drivers/usb/usbip/usbip_common.h
 *
 */

#include <stdint.h>

#include "remotehub.h"
#include "network.h"

/* USBIP USB data transfer definitions */

#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004

#define USBIP_DIR_OUT		0x00
#define USBIP_DIR_IN		0x01

struct usbip_header_basic {
	uint32_t command;
	uint32_t seqnum;
	uint32_t devid;
	uint32_t direction;
	uint32_t ep;
} __attribute__((packed));

struct usbip_header_cmd_submit {
	uint32_t transfer_flags;
	int32_t transfer_buffer_length;
	int32_t start_frame;
	int32_t number_of_packets;
	int32_t interval;
	uint8_t setup[8];
} __attribute__((packed));

struct usbip_header_ret_submit {
	int32_t status;
	int32_t actual_length;
	int32_t start_frame;
	int32_t number_of_packets;
	int32_t error_count;
} __attribute__((packed));

struct usbip_header_cmd_unlink {
	uint32_t seqnum;
} __attribute__((packed));

struct usbip_header_ret_unlink {
	int32_t status;
} __attribute__((packed));

struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit cmd_submit;
		struct usbip_header_ret_submit ret_submit;
		struct usbip_header_cmd_unlink cmd_unlink;
		struct usbip_header_ret_unlink ret_unlink;
	} u;
} __attribute__((packed));

struct usbip_iso_packet_descriptor {
	uint32_t offset;
	uint32_t length;
	uint32_t actual_length;
	uint32_t status;
} __attribute__((packed));


/* USBIP version v1.1.1 command protocol definitions */

#define USBIP_DEFAULT_PROTOCOL_VERSION	0x0111

struct usbip_op_common {
	uint16_t version;
	uint16_t code;
	uint32_t status;
} __attribute__((packed));

#define USBIP_ST_OK			0x00
#define USBIP_ST_NA			0x01
#define USBIP_ST_DEV_BUSY		0x02
#define USBIP_ST_DEV_ERR		0x03
#define USBIP_ST_NODEV			0x04
#define USBIP_ST_ERROR			0x05

#define USBIP_OP_REQ_IMPORT		0x8003
#define USBIP_OP_REP_IMPORT		0x0003

struct usbip_op_import_request {
	char busid[USBIP_BUSID_SIZE];
} __attribute__((packed));

struct usbip_op_import_reply {
	struct usbip_usb_device udev;
} __attribute__((packed));

#define USBIP_OP_REQ_DEVLIST		0x8005
#define USBIP_OP_REP_DEVLIST		0x0005

struct usbip_op_devlist_reply {
	uint32_t ndev;
} __attribute__((packed));

struct usb_packet {
	bool				ready;
	bool				submitted;
	uint32_t			unlinked;
	struct usbip_header		hdr;
	struct libusb_transfer		*xfer;
	struct forward_info		*f_dev;
	struct usb_packet		*next;
};

bool usbip_net_send_usbip_header(struct est_conn *link, struct usbip_op_common *hdr);
bool usbip_net_recv_usbip_header(struct est_conn *link, struct usbip_op_common *hdr);

void usbip_net_devlist_reply_to_network_order(struct usbip_op_devlist_reply *hdr);
void usbip_net_dev_to_network_order(struct usbip_usb_device *dev);
void usbip_net_import_request_to_network_order(void);
void usbip_net_import_reply_to_network_order(struct usbip_op_import_reply *hdr);
void usbip_net_import_reply_from_network_order(struct usbip_op_import_reply *hdr);
void usbip_net_devlist_reply_from_network_order(struct usbip_op_devlist_reply *hdr);
void usbip_net_dev_from_network_order(struct usbip_usb_device *dev);

#endif /* __REMOTEHUB_USBIP_H__ */
