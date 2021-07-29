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
#include <errno.h>

#include <pthread.h>
#include <libusb-1.0/libusb.h>

#include "usb.h"
#include "usbip.h"
#include "server.h"
#include "srv_event.h"
#include "event.h"
#include "logging.h"
#include "network.h"

static void enqueue_packet(struct forward_info *f_dev, struct usb_packet *packet)
{
	struct usb_packet *tmp;

	if (!f_dev->buffer_head) {
		f_dev->buffer_head = packet;
		f_dev->buffer_head->next = NULL;
	} else {
		tmp = f_dev->buffer_head;
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = packet;
	}
}

static bool dequeue_ready_packet(struct forward_info *f_dev, struct usb_packet **packet)
{
	struct usb_packet *tmp, *prev;
	bool found = false;

	pthread_mutex_lock(&f_dev->buffer_lock);

	if (!f_dev->buffer_head) {
		pthread_mutex_unlock(&f_dev->buffer_lock);
		return false;
	}

	if (f_dev->buffer_head->ready) {
		*packet = f_dev->buffer_head;
		f_dev->buffer_head = f_dev->buffer_head->next;
		f_dev->packets_ready--;
		found = true;
	} else {
		tmp = f_dev->buffer_head->next;
		prev = f_dev->buffer_head;
		while (tmp) {
			if (tmp->ready) {
				*packet = tmp;
				prev->next = tmp->next;
				found = true;
				f_dev->packets_ready--;
				break;
			}
			prev = tmp;
			tmp = tmp->next;
		}
	}

	pthread_mutex_unlock(&f_dev->buffer_lock);
	return found;
}

static bool dequeue_any_packet(struct forward_info *f_dev, struct usb_packet **packet)
{
	bool found = false;

	pthread_mutex_lock(&f_dev->buffer_lock);

	if (f_dev->buffer_head) {
		*packet = f_dev->buffer_head;
		f_dev->buffer_head = f_dev->buffer_head->next;
		f_dev->packets_ready--;
		found = true;
	}

	pthread_mutex_unlock(&f_dev->buffer_lock);
	return found;
}

static bool unlink_packet(struct forward_info *f_dev, uint32_t target_seqnum,
			  uint32_t unlink_seqnum)
{
	struct usb_packet *tmp, *unlink;
	bool found = false;

	pthread_mutex_lock(&f_dev->buffer_lock);

	if (!f_dev->buffer_head) {
		pthread_mutex_unlock(&f_dev->buffer_lock);
		return false;
	}

	if (f_dev->buffer_head->hdr.base.seqnum == target_seqnum) {
		unlink = f_dev->buffer_head;
		found = true;
	} else {
		tmp = f_dev->buffer_head;
		while (tmp) {
			if (tmp->hdr.base.seqnum == target_seqnum) {
				unlink = tmp;
				found = true;
				break;
			}
			tmp = tmp->next;
		}
	}

	if (found) {
		unlink->unlinked = unlink_seqnum;
		libusb_cancel_transfer(unlink->xfer);
	}

	pthread_mutex_unlock(&f_dev->buffer_lock);
	return found;
}

static int convert_libusb_status(enum libusb_transfer_status xfer_status)
{
	switch (xfer_status) {
	case LIBUSB_TRANSFER_COMPLETED:
		return 0;
	case LIBUSB_TRANSFER_ERROR:
		return -EIO;
	case LIBUSB_TRANSFER_TIMED_OUT:
		return -ETIMEDOUT;
	case LIBUSB_TRANSFER_CANCELLED:
		return -ECONNRESET;
	case LIBUSB_TRANSFER_STALL:
		return -EPIPE;
	case LIBUSB_TRANSFER_NO_DEVICE:
		return -ESHUTDOWN;
	case LIBUSB_TRANSFER_OVERFLOW:
		return -EOVERFLOW;
	}
	return -ENOENT;
}

static void intercept_control_packet(struct forward_info *f_dev, struct usbip_header *hdr)
{
	uint16_t interface, alternate;
	uint16_t wValue, clear_halt_ep;
	int ret = 0;

	struct usb_ctrlrequest *req = (struct usb_ctrlrequest *) hdr->u.cmd_submit.setup;

	wValue = libusb_le16_to_cpu(req->wValue);

	if ((req->bRequest == USB_REQ_CLEAR_FEATURE) && (req->bRequestType == USB_RECIP_ENDPOINT) &&
								(wValue == USB_ENDPOINT_HALT)) {
		clear_halt_ep = libusb_le16_to_cpu(req->wIndex) & 0x008F;
		rh_trace(LVL_DBG, "Clearing halt from ep 0x%x\n", clear_halt_ep);
		ret = libusb_clear_halt(f_dev->handle, clear_halt_ep);
		if (ret) {
			rh_trace(LVL_ERR, "Clearing halt from ep 0x%x failed\n", clear_halt_ep);
			return;
		}
	}

	if ((req->bRequest == USB_REQ_SET_FEATURE) && (req->bRequestType == USB_RT_PORT) &&
								(wValue == USB_PORT_FEAT_RESET)) {
		rh_trace(LVL_DBG, "Reset command received\n");
		libusb_reset_device(f_dev->handle);
	}

	if ((req->bRequest == USB_REQ_SET_CONFIGURATION) &&
							(req->bRequestType == USB_RECIP_DEVICE)) {
		rh_trace(LVL_DBG, "Config changing not supported (cfg %d)\n",
				   libusb_le16_to_cpu(req->wValue));
	}

	if ((req->bRequest == USB_REQ_SET_INTERFACE) &&
						(req->bRequestType == USB_RECIP_INTERFACE)) {
		alternate = libusb_le16_to_cpu(req->wValue);
		interface = libusb_le16_to_cpu(req->wIndex);
		ret = libusb_set_interface_alt_setting(f_dev->handle, interface, alternate);
		if (ret) {
			rh_trace(LVL_DBG, "Interface setting failed\n");
			return;
		}
		rh_trace(LVL_DBG, "Set interface %d, altsetting %d\n", interface, alternate);
	}
}

static void dump_packet(struct usb_packet *packet)
{
	rh_trace(LVL_TRC, "Cmd      : %x\n", packet->hdr.base.command);
	rh_trace(LVL_TRC, "Devid    : %x\n", packet->hdr.base.devid);
	rh_trace(LVL_TRC, "Dir      : %x\n", packet->hdr.base.direction);
	rh_trace(LVL_TRC, "ep (hdr) : %x\n", packet->hdr.base.ep);
	rh_trace(LVL_TRC, "Seqnum   : %d\n", packet->hdr.base.seqnum);
	rh_trace(LVL_TRC, "n-o-p    : %d\n", packet->hdr.u.cmd_submit.number_of_packets);
	rh_trace(LVL_TRC, "Endpoint : %x\n", packet->xfer->endpoint);
	rh_trace(LVL_TRC, "Type     : %x\n", packet->xfer->type);
	rh_trace(LVL_TRC, "Length   : %d\n", packet->xfer->length);
	rh_trace(LVL_TRC, "Flags    : %x\n", packet->xfer->flags);
}

static int claim_device(struct server_usb_device *dev)
{
	int ret;

	if (!dev->fwd.handle)
		return false;

	for (int i = 0; i < dev->info.udev.bNumInterfaces; i++) {
		if (libusb_kernel_driver_active(dev->fwd.handle, i)) {
			ret = libusb_detach_kernel_driver(dev->fwd.handle, i);
			if (ret) {
				rh_trace(LVL_ERR, "Failed to detach if %d\n", i);
				rh_trace(LVL_ERR, "%s\n", libusb_strerror(ret));
				return false;
			}
		}
		ret = libusb_claim_interface(dev->fwd.handle, i);
		if (ret) {
			rh_trace(LVL_ERR, "Failed to claim if %d\n", i);
			rh_trace(LVL_ERR, "%s\n", libusb_strerror(ret));
			return false;
		}
		rh_trace(LVL_DBG, "Claimed if %d\n", i);
	}

	return true;
}

static int release_device(struct server_usb_device *dev)
{
	int ret;

	for (int i = 0; i < dev->info.udev.bNumInterfaces; i++) {
		rh_trace(LVL_DBG, "Checking interface %d\n", i);
		if (!libusb_kernel_driver_active(dev->fwd.handle, i)) {
			ret = libusb_release_interface(dev->fwd.handle, i);
			if (ret) {
				rh_trace(LVL_DBG, "Failed to release if %d\n", i);
				rh_trace(LVL_DBG, "Reason: %s\n", libusb_strerror(ret));
			}
			ret = libusb_attach_kernel_driver(dev->fwd.handle, i);
			if (ret) {
				rh_trace(LVL_DBG, "Failed to attach kernel driver to if %d\n", i);
				rh_trace(LVL_DBG, "Reason: %s\n", libusb_strerror(ret));
			} else {
				rh_trace(LVL_DBG, "Kernel driver attached to if %d\n", i);
			}
		}
	}

	libusb_reset_device(dev->fwd.handle);

	return true;
}

static void xfer_completion_callback(struct libusb_transfer *transfer)
{
	struct usb_packet *packet = (struct usb_packet *)transfer->user_data;
	pthread_cond_t *buffer_cond;
	pthread_mutex_t *buffer_lock;
	uint32_t act_len = 0;

	pthread_mutex_lock(&packet->f_dev->buffer_lock);
	buffer_cond = &packet->f_dev->buffer_cond;
	buffer_lock = &packet->f_dev->buffer_lock;

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		rh_trace(LVL_DBG, "LIBUSB_TRANSFER_CANCELLED\n");
		packet->hdr.u.ret_submit.status = convert_libusb_status(transfer->status);
		goto end;
	}

	if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		rh_trace(LVL_DBG, "LIBUSB_TRANSFER_NO_DEVICE\n");
		network_shut_link(packet->f_dev->link);
		packet->f_dev->terminate = true;
		goto end;
	}

	packet->hdr.base.command = USBIP_RET_SUBMIT;
	packet->hdr.u.ret_submit.status = convert_libusb_status(transfer->status);
	packet->hdr.u.ret_submit.actual_length = transfer->actual_length;
	packet->hdr.u.ret_submit.start_frame = 0;
	packet->hdr.u.ret_submit.number_of_packets = transfer->num_iso_packets;
	packet->hdr.u.ret_submit.error_count = 0;

	if (transfer->num_iso_packets) {
		for (int i = 0; i < transfer->num_iso_packets; i++)
			act_len += transfer->iso_packet_desc[i].actual_length;

		packet->hdr.u.ret_submit.actual_length = act_len;
		rh_trace(LVL_DBG, "ISO ACT LEN changed %d\n", act_len);
	}

end:
	packet->f_dev->packets_ready++;
	packet->ready = true;
	pthread_mutex_unlock(buffer_lock);
	pthread_cond_signal(buffer_cond);
}

static uint8_t get_xfer_type(struct server_usb_device *dev, uint32_t dir, uint8_t ep)
{
	uint8_t epnum = ep & USB_ENDPOINT_NUMBER_MASK;

	if (ep == 0)
		return USB_ENDPOINT_XFER_CONTROL;

	if (dir == USBIP_DIR_IN) {
		rh_trace(LVL_DBG, "USB_DIR_IN - ep %d -> type %d\n",
				  ep, dev->info.ep_in_type[epnum]);
		return dev->info.ep_in_type[epnum];
	}

	rh_trace(LVL_DBG, "USB_DIR_OUT - ep %d -> %d\n",
			  ep, dev->info.ep_out_type[epnum]);

	return dev->info.ep_out_type[epnum];
}

static uint8_t set_endpoint(uint8_t ep, uint32_t dir)
{
	if (ep == 0)
		return 0;

	if (dir == USBIP_DIR_IN)
		ep |= USB_DIR_IN;

	return ep;
}

static int receive_iso(struct forward_info *f_dev, int num_iso, struct libusb_transfer *xfer)
{
	bool ok;
	struct usbip_iso_packet_descriptor *usbip_iso, tmp_iso;

	usbip_iso = calloc(num_iso * sizeof(struct usbip_iso_packet_descriptor), 1UL);
	if (!usbip_iso) {
		rh_trace(LVL_ERR, "Can't allocate memory\n");
		return -1;
	}

	ok = network_recv_data(f_dev->link, (uint8_t *)usbip_iso,
				num_iso * sizeof(struct usbip_iso_packet_descriptor));
	if (!ok) {
		rh_trace(LVL_ERR, "Isonchronous data receive failed\n");
		free(usbip_iso);
		return -1;
	}

	for (int i = 0; i < num_iso; i++) {
		tmp_iso = usbip_iso[i];
		xfer->iso_packet_desc[i].length = ntohl(tmp_iso.length);
		xfer->iso_packet_desc[i].actual_length = ntohl(tmp_iso.actual_length);
		xfer->iso_packet_desc[i].status = ntohl(tmp_iso.status);
	}

	free(usbip_iso);

	return 0;
}

static bool submit_xfer(struct server_usb_device *dev, struct usb_packet *packet,
			uint8_t *data_buffer)
{
	int ret, offset = packet->hdr.base.ep == 0 ? 8 : 0;
	struct libusb_transfer *xfer;

	uint32_t dir = packet->hdr.base.direction;
	uint8_t ep = packet->hdr.base.ep;
	int32_t num_iso = packet->hdr.u.cmd_submit.number_of_packets;
	uint8_t xfer_type = get_xfer_type(dev, dir, ep);

	if (xfer_type != USB_ENDPOINT_XFER_ISOC)
		num_iso = 0;

	xfer = libusb_alloc_transfer(num_iso);
	if (!xfer) {
		rh_trace(LVL_DBG, "Can't allocate memory\n");
		return false;
	}

	packet->xfer = xfer;
	xfer->buffer = data_buffer;

	packet->xfer->endpoint		= set_endpoint(ep, dir);
	packet->xfer->type		= xfer_type;
	packet->xfer->timeout		= 0;
	packet->xfer->user_data		= packet;
	packet->xfer->length		= packet->hdr.u.cmd_submit.transfer_buffer_length + offset;
	packet->xfer->callback		= xfer_completion_callback;
	packet->xfer->num_iso_packets	= num_iso;
	packet->xfer->flags		= 0; // TODO: Check flags
	packet->xfer->dev_handle	= dev->fwd.handle;

	if (packet->hdr.base.ep == 0)
		intercept_control_packet(&dev->fwd, &packet->hdr);

	dump_packet(packet);

	if (num_iso > 0) {
		ret = receive_iso(&dev->fwd, num_iso, xfer);
		if (ret != 0) {
			rh_trace(LVL_ERR, "ISO receive fail\n");
			libusb_free_transfer(xfer);
			return false;
		}
	}

	pthread_mutex_lock(&dev->fwd.buffer_lock);

	ret = libusb_submit_transfer(packet->xfer);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Submit failed %s\n", libusb_strerror(ret));
		libusb_free_transfer(xfer);
		pthread_mutex_unlock(&dev->fwd.buffer_lock);
		return false;
	}

	enqueue_packet(&dev->fwd, packet);
	pthread_mutex_unlock(&dev->fwd.buffer_lock);
	return true;
}

static bool handle_unlink(struct server_usb_device *dev, struct usbip_header *hdr)
{
	bool found;
	uint32_t unlink_seqnum, unlink_target_seqnum;
	struct usb_packet *packet;

	unlink_seqnum = hdr->base.seqnum;
	unlink_target_seqnum = hdr->u.cmd_unlink.seqnum;

	rh_trace(LVL_DBG, "Received UNLINK seq %u [for %u]\n", unlink_seqnum, unlink_target_seqnum);
	found = unlink_packet(&dev->fwd, unlink_target_seqnum, unlink_seqnum);
	if (found) {
		rh_trace(LVL_DBG, "Packet %u found and unlinked\n", unlink_target_seqnum);
		return true;
	}

	rh_trace(LVL_DBG, "Packet %u was not found for unlinking\n", unlink_target_seqnum);

	packet = calloc(1, sizeof(struct usb_packet));
	if (!packet) {
		rh_trace(LVL_DBG, "Can not allocate memory\n");
		return false;
	}

	/* The packet was likely already sent back */
	hdr->base.command = USBIP_RET_UNLINK;
	hdr->u.ret_unlink.status = 0;

	memcpy(&packet->hdr, hdr, sizeof(struct usbip_header));

	packet->f_dev = &dev->fwd;
	packet->f_dev->packets_ready++;
	packet->ready = true;

	pthread_mutex_lock(&dev->fwd.buffer_lock);
	enqueue_packet(&dev->fwd, packet);
	pthread_mutex_unlock(&dev->fwd.buffer_lock);
	pthread_cond_signal(&dev->fwd.buffer_cond);

	return true;
}

static bool handle_submit(struct server_usb_device *dev, struct usbip_header *hdr)
{
	bool ok;
	int offset;
	struct usb_packet *packet;
	uint8_t *data_buffer;
	uint32_t bufsize = hdr->u.cmd_submit.transfer_buffer_length;

	packet = calloc(1, sizeof(struct usb_packet));
	if (!packet) {
		rh_trace(LVL_DBG, "Can not allocate memory\n");
		return false;
	}

	memcpy(&packet->hdr, hdr, sizeof(struct usbip_header));
	packet->f_dev = &dev->fwd;

	data_buffer = calloc(bufsize + 8, sizeof(uint8_t));
	if (!data_buffer) {
		rh_trace(LVL_DBG, "Can not allocate memory\n");
		return false;
	}

	memcpy(data_buffer, hdr->u.cmd_submit.setup, 8);

	switch (hdr->base.direction) {
	case USBIP_DIR_IN:
		rh_trace(LVL_DBG, "Direction: IN\n");
		break;
	case USBIP_DIR_OUT:
		rh_trace(LVL_DBG, "Direction: OUT\n");
		if (bufsize) {
			offset = hdr->base.ep == 0 ? 8 : 0;
			ok = network_recv_data(dev->fwd.link, &data_buffer[offset], bufsize);
			if (!ok) {
				rh_trace(LVL_ERR, "Failed to receive data\n");
				free(data_buffer);
				free(packet);
				return false;
			}
		}
		break;
	default:
		rh_trace(LVL_DBG, "Unknown direction\n");
		free(data_buffer);
		free(packet);
		return false;
	}

	ok = submit_xfer(dev, packet, data_buffer);
	if (!ok) {
		rh_trace(LVL_ERR, "Failed to submit transfer\n");
		free(data_buffer);
		free(packet);
		return false;
	}

	return ok;
}

static void usbip_base_header_to_host_endian(struct usbip_header *hdr)
{
	hdr->base.command = ntohl(hdr->base.command);
	hdr->base.devid = ntohl(hdr->base.devid);
	hdr->base.direction = ntohl(hdr->base.direction);
	hdr->base.ep = ntohl(hdr->base.ep);
	hdr->base.seqnum = ntohl(hdr->base.seqnum);
}

static void usbip_base_header_to_network_endian(struct usbip_header *hdr)
{
	hdr->base.command = htonl(hdr->base.command);
	hdr->base.devid = htonl(hdr->base.devid);
	hdr->base.direction = htonl(hdr->base.direction);
	hdr->base.ep = htonl(hdr->base.ep);
	hdr->base.seqnum = htonl(hdr->base.seqnum);
}

static void usbip_cmd_submit_header_to_host_endian(struct usbip_header *hdr)
{
	hdr->u.cmd_submit.interval = ntohl(hdr->u.cmd_submit.interval);
	hdr->u.cmd_submit.number_of_packets = ntohl(hdr->u.cmd_submit.number_of_packets);
	hdr->u.cmd_submit.start_frame = ntohl(hdr->u.cmd_submit.start_frame);
	hdr->u.cmd_submit.transfer_buffer_length = ntohl(hdr->u.cmd_submit.transfer_buffer_length);
	hdr->u.cmd_submit.transfer_flags = ntohl(hdr->u.cmd_submit.transfer_flags);
}

static void usbip_ret_submit_header_to_network_endian(struct usbip_header *hdr)
{
	hdr->u.ret_submit.actual_length = htonl(hdr->u.ret_submit.actual_length);
	hdr->u.ret_submit.error_count = htonl(hdr->u.ret_submit.error_count);
	hdr->u.ret_submit.number_of_packets = htonl(hdr->u.ret_submit.number_of_packets);
	hdr->u.ret_submit.start_frame = htonl(hdr->u.ret_submit.start_frame);
	hdr->u.ret_submit.status = htonl(hdr->u.ret_submit.status);
}

static void usbip_cmd_unlink_header_to_host_endian(struct usbip_header *hdr)
{
	hdr->u.cmd_unlink.seqnum = ntohl(hdr->u.cmd_unlink.seqnum);
}

static void usbip_ret_unlink_header_to_network_endian(struct usbip_header *hdr)
{
	hdr->u.ret_unlink.status = ntohl(hdr->u.ret_unlink.status);
}

static void *rx_server(void *fwd_dev)
{
	bool ok;
	struct usbip_header hdr = {0};
	struct server_usb_device *dev = (struct server_usb_device *)fwd_dev;

	rh_trace(LVL_DBG, "Fwd RX started\n");

	while (1) {
		pthread_mutex_lock(&dev->fwd.buffer_lock);
		while (dev->fwd.packets_ready >= PACKET_BUF_SIZE && !dev->fwd.terminate)
			pthread_cond_wait(&dev->fwd.buffer_cond, &dev->fwd.buffer_lock);

		pthread_mutex_unlock(&dev->fwd.buffer_lock);

		if (dev->fwd.terminate)
			goto rx_exit;

		ok = network_recv_data(dev->fwd.link, (uint8_t *)&hdr, sizeof(struct usbip_header));
		if (!ok) {
			rh_trace(LVL_DBG, "Header receive failed\n");
			goto rx_exit;
		}

		usbip_base_header_to_host_endian(&hdr);

		switch (hdr.base.command) {
		case USBIP_CMD_UNLINK:
			usbip_cmd_unlink_header_to_host_endian(&hdr);
			ok = handle_unlink(dev, &hdr);
			if (!ok) {
				rh_trace(LVL_ERR, "Unlink failed\n");
				goto rx_exit;
			}
			break;
		case USBIP_CMD_SUBMIT:
			usbip_cmd_submit_header_to_host_endian(&hdr);
			rh_trace(LVL_DBG, "Received SUBMIT packet seqnum %d\n", hdr.base.seqnum);
			ok = handle_submit(dev, &hdr);
			if (!ok) {
				rh_trace(LVL_ERR, "Submit failed\n");
				goto rx_exit;
			}
			break;
		default:
			rh_trace(LVL_ERR, "Unknown header\n");
			goto rx_exit;
		}
	}
rx_exit:
	rh_trace(LVL_DBG, "Fwd RX terminate\n");
	dev->fwd.terminate = true;
	pthread_cond_signal(&dev->fwd.buffer_cond);
	return NULL;
}

static void fill_iso(struct libusb_iso_packet_descriptor libusb_iso,
		     struct usbip_iso_packet_descriptor *iso, uint32_t offset)
{
	iso->offset = htonl(offset);
	iso->status = htonl(convert_libusb_status(libusb_iso.status));
	iso->length = htonl(libusb_iso.length);
	iso->actual_length = htonl(libusb_iso.actual_length);
}

static bool send_iso_xfer_data(struct usb_packet *packet, uint32_t usb_direction)
{
	struct forward_info *f_dev = packet->f_dev;
	struct usbip_iso_packet_descriptor iso;
	uint32_t offset = 0, al = 0;
	bool ok;

	if (usb_direction == USBIP_DIR_IN) {
		for (int i = 0; i < packet->xfer->num_iso_packets; i++) {
			ok = network_send_data(f_dev->link, &packet->xfer->buffer[offset],
					       packet->xfer->iso_packet_desc[i].actual_length);
			if (!ok) {
				rh_trace(LVL_ERR, "ISO send failed\n");
				return false;
			}
			al += packet->xfer->iso_packet_desc[i].actual_length;
			offset += packet->xfer->iso_packet_desc[i].length;
		}
		rh_trace(LVL_DBG, "Sent iso data %d (offset %d)\n", al, offset);
	}

	offset = 0;
	for (int i = 0; i < packet->xfer->num_iso_packets; i++) {
		fill_iso(packet->xfer->iso_packet_desc[i], &iso, offset);
		ok = network_send_data(f_dev->link, (uint8_t *)&iso, 16UL);
		if (!ok) {
			rh_trace(LVL_ERR, "2nd ISO send failed\n");
			return false;
		}
		offset += packet->xfer->iso_packet_desc[i].length;
	}
	return true;
}

static bool send_xfer_data(struct usb_packet *packet, uint32_t usb_direction)
{
	bool ok;
	uint32_t data_offset = (packet->xfer->endpoint & 0x7f) == 0 ? 8 : 0;

	if (usb_direction == USBIP_DIR_IN) {
		ok = network_send_data(packet->f_dev->link, &packet->xfer->buffer[data_offset],
				       packet->xfer->actual_length);
		if (!ok) {
			rh_trace(LVL_DBG, "Data send failed\n");
			return false;
		}
	}

	return true;
}

static void free_usb_packet(struct usb_packet *packet)
{
	if (packet->xfer && packet->xfer->buffer) {
		free(packet->xfer->buffer);
		packet->xfer->buffer = NULL;
	}

	if (packet->xfer) {
		libusb_free_transfer(packet->xfer);
		packet->xfer = NULL;
	}

	free(packet);
}

static void *tx_server(void *fwd_dev)
{
	bool ok;
	uint32_t command, usb_direction;
	struct server_usb_device *dev = (struct server_usb_device *)fwd_dev;
	struct usb_packet *packet;

	rh_trace(LVL_DBG, "Fwd TX started\n");

	while (1) {
		pthread_mutex_lock(&dev->fwd.buffer_lock);
		while (!dev->fwd.packets_ready && !dev->fwd.terminate)
			pthread_cond_wait(&dev->fwd.buffer_cond, &dev->fwd.buffer_lock);

		pthread_mutex_unlock(&dev->fwd.buffer_lock);

		if (dev->fwd.terminate)
			goto tx_exit;

		ok = dequeue_ready_packet(&dev->fwd, &packet);
		if (!ok) {
			rh_trace(LVL_ERR, "No packet available\n");
			continue;
		}

		if (packet->unlinked) {
			/* Successful unlink status is -ECONNRESET */
			packet->hdr.base.command = USBIP_RET_UNLINK;
			packet->hdr.u.ret_unlink.status = -ECONNRESET;
			packet->hdr.base.seqnum = packet->unlinked;
		}

		command = packet->hdr.base.command;
		usb_direction = packet->hdr.base.direction;
		usbip_base_header_to_network_endian(&packet->hdr);

		if (command == USBIP_RET_SUBMIT) {
			usbip_ret_submit_header_to_network_endian(&packet->hdr);
		} else if (command == USBIP_RET_UNLINK) {
			usbip_ret_unlink_header_to_network_endian(&packet->hdr);
		} else {
			rh_trace(LVL_DBG, "Unknown command 0x%x\n", command);
			free_usb_packet(packet);
			goto tx_exit;
		}

		ok = network_send_data(dev->fwd.link, (uint8_t *)&packet->hdr,
					sizeof(struct usbip_header));
		if (!ok) {
			free_usb_packet(packet);
			goto tx_exit;
		}

		if (command == USBIP_RET_SUBMIT) {
			rh_trace(LVL_DBG, "Sending submit packet data\n");
			if (packet->xfer->type == USB_ENDPOINT_XFER_ISOC)
				ok = send_iso_xfer_data(packet, usb_direction);
			else
				ok = send_xfer_data(packet, usb_direction);
			if (!ok) {
				free_usb_packet(packet);
				goto tx_exit;
			}
		} else if (command == USBIP_RET_UNLINK) {
			rh_trace(LVL_DBG, "Unlink packet (no data to send)\n");
		}

		free_usb_packet(packet);
	}
tx_exit:
	rh_trace(LVL_DBG, "Fwd TX terminate\n");
	dev->fwd.terminate = true;
	pthread_cond_signal(&dev->fwd.buffer_cond);
	return NULL;
}

static void inform_exported(struct usbip_usb_device dev)
{
	struct rh_event event = {0};

	event.type = EVENT_DEVICE_EXPORTED;
	event.data = &dev;
	event.size = sizeof(dev);
	(void) event_enqueue(&event);
}

static void inform_unexported(struct usbip_usb_device dev)
{
	struct rh_event event = {0};

	event.type = EVENT_DEVICE_UNEXPORTED;
	event.data = &dev;
	event.size = sizeof(dev);
	(void) event_enqueue(&event);
}

static void *monitor(void *f_device)
{
	int ret;
	pthread_t rx_thread, tx_thread;
	struct usb_packet *packet;
	struct server_usb_device *dev = (struct server_usb_device *)f_device;

	inform_exported(dev->info.udev);

	pthread_mutex_init(&dev->fwd.buffer_lock, NULL);
	pthread_cond_init(&dev->fwd.buffer_cond, NULL);

	if (pthread_create(&rx_thread, NULL, rx_server, dev)) {
		rh_trace(LVL_DBG, "RX Create failed\n");
		release_device(dev);
		inform_unexported(dev->info.udev);
		dev->fwd.terminate = true;
		return NULL;
	}

	if (pthread_create(&tx_thread, NULL, tx_server, dev)) {
		rh_trace(LVL_DBG, "TX Create failed\n");
		release_device(dev);
		inform_unexported(dev->info.udev);
		dev->fwd.terminate = true;
		pthread_join(rx_thread, NULL);
		return NULL;
	}

	pthread_join(rx_thread, NULL);
	pthread_join(tx_thread, NULL);

	// Destroy all of the data
	while (dequeue_any_packet(&dev->fwd, &packet)) {
		pthread_mutex_lock(&dev->fwd.buffer_lock);
		if (!packet->ready) {
			ret = libusb_cancel_transfer(packet->xfer);
			if (ret)
				rh_trace(LVL_ERR, "Cancel transfer failed with %d\n", ret);
			while (!packet->ready) {
				pthread_mutex_unlock(&dev->fwd.buffer_lock);
				rh_trace(LVL_DBG, "Waiting for completion\n");
				usleep(10000);
				pthread_mutex_lock(&dev->fwd.buffer_lock);
			}
		}
		pthread_mutex_unlock(&dev->fwd.buffer_lock);
		if (packet->xfer) {
			if (packet->xfer->buffer)
				free(packet->xfer->buffer);
			libusb_free_transfer(packet->xfer);
		}
		free(packet);
	}

	release_device(dev);
	libusb_close(dev->fwd.handle);

	pthread_cond_destroy(&dev->fwd.buffer_cond);
	pthread_mutex_destroy(&dev->fwd.buffer_lock);
	dev->fwd.packets_ready = 0;

	network_close_link(dev->fwd.link);
	free(dev->fwd.link);
	dev->fwd.link = NULL;

	inform_unexported(dev->info.udev);
	rh_trace(LVL_TRC, "Monitor exit\n");

	return NULL;
}

bool forwarding_start(struct server_usb_device *dev)
{
	int ret;
	bool ok;

	if (dev->info.udev.bNumConfigurations != 1) {
		rh_trace(LVL_ERR, "Only single config devices supported!\n");
		return false;
	}

	ret = libusb_open(dev->fwd.libusb_dev, &dev->fwd.handle);
	if (ret < 0) {
		rh_trace(LVL_ERR, "Failed to open device %d, %s - %s\n", ret,
				  libusb_error_name(ret), libusb_strerror(ret));
		return false;
	}

	ok = claim_device(dev);
	if (!ok) {
		rh_trace(LVL_ERR, "Failed to claim device\n");
		return false;
	}

	libusb_reset_device(dev->fwd.handle);

	if (pthread_create(&dev->fwd.forwarding_thread, NULL, monitor, dev)) {
		rh_trace(LVL_ERR, "Monitoring thread creation failed\n");
		dev->fwd.forwarding_thread = 0;
		release_device(dev);
		return false;
	}

	return true;
}
