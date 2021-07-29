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

#include <libusb-1.0/libusb.h>

#include "network.h"
#include "logging.h"
#include "event.h"
#include "task.h"
#include "srv_event.h"
#include "server.h"
#include "usbip.h"
#include "usb.h"

struct usb_bus_info {
	int bus;
	struct usb_bus_info *next;
};

static pthread_t libusb_thread, usb_thread;
static pthread_mutex_t usb_conf_lock;
static bool libusb_running = true;

static libusb_context *usb_context;
static struct server_usb_device *usb_head;
static struct usb_bus_info *usb_bus_info_head;

static struct rh_task usb;

#define for_each_device(dev) \
	struct server_usb_device *next = NULL; \
	for (dev = usb_head; next = dev != NULL ? dev->next : NULL, \
	     dev != NULL; dev = next)

static bool device_already_exists(char *id)
{
	struct server_usb_device *tmp;

	for_each_device(tmp) {
		if (!strcmp(tmp->info.udev.busid, id))
			return true;
	}

	return false;
}

static void insert_device(struct server_usb_device *device)
{
	if (usb_head == NULL) {
		usb_head = device;
		return;
	}
	device->next = usb_head;
	usb_head = device;
}

static void delete_device(struct server_usb_device *device)
{
	struct server_usb_device *tmp;

	if (usb_head == NULL) {
		rh_trace(LVL_ERR, "Usb head was null\n");
		return;
	}

	if (!strcmp(usb_head->info.udev.busid, device->info.udev.busid)) {
		usb_head = usb_head->next;
		rh_trace(LVL_DBG, "Deleting device at head %s\n", device->info.product_name);
		free(device);
		return;
	}

	tmp = usb_head;
	while (tmp->next) {
		if (!strcmp(tmp->next->info.udev.busid, device->info.udev.busid)) {
			tmp->next = device->next;
			rh_trace(LVL_DBG, "Deleting %s\n", device->info.product_name);
			free(device);
			return;
		}
		tmp = tmp->next;
	}
}

static bool bus_is_disabled(int busnum)
{
	struct usb_bus_info *tmp;

	pthread_mutex_lock(&usb_conf_lock);

	if (usb_bus_info_head == NULL) {
		pthread_mutex_unlock(&usb_conf_lock);
		return false;
	}

	tmp = usb_bus_info_head;
	if (tmp->bus == busnum) {
		pthread_mutex_unlock(&usb_conf_lock);
		return true;
	}

	tmp = usb_bus_info_head;
	while (tmp->next != NULL) {
		tmp = tmp->next;
		if (tmp->bus == busnum) {
			pthread_mutex_unlock(&usb_conf_lock);
			return true;
		}
	}

	pthread_mutex_unlock(&usb_conf_lock);
	return false;
}

static bool port_is_disabled(int busnum, int portnum)
{
	// TODO: Port disabling
	(void) portnum;
	return bus_is_disabled(busnum);
}

bool rh_disable_usb_bus(int busnum)
{
	struct usb_bus_info *info, *tmp;

	if (bus_is_disabled(busnum))
		return true;

	info = calloc(1, sizeof(struct usb_bus_info));
	if (!info)
		return false;

	info->bus = busnum;

	pthread_mutex_lock(&usb_conf_lock);
	if (usb_bus_info_head == NULL) {
		usb_bus_info_head = info;
		pthread_mutex_unlock(&usb_conf_lock);
		return true;
	}

	tmp = usb_bus_info_head;
	while (tmp->next != NULL)
		tmp = tmp->next;

	tmp->next = info;
	pthread_mutex_unlock(&usb_conf_lock);

	return true;
}

static void delete_bus_info(void)
{
	struct usb_bus_info *info, *tmp;

	pthread_mutex_lock(&usb_conf_lock);

	tmp = usb_bus_info_head;
	while (tmp != NULL) {
		info = tmp;
		tmp = tmp->next;
		free(info);
	}

	pthread_mutex_unlock(&usb_conf_lock);
}

static void inform_attached(struct usbip_usb_device dev)
{
	struct rh_event event = {0};

	event.type = EVENT_DEVICE_ATTACHED;
	event.data = &dev;
	event.size = sizeof(dev);
	(void) event_enqueue(&event);
}

static void inform_detached(struct usbip_usb_device dev)
{
	struct rh_event event = {0};

	event.type = EVENT_DEVICE_DETACHED;
	event.data = &dev;
	event.size = sizeof(dev);
	(void) event_enqueue(&event);
}

static bool get_busid(struct libusb_device *dev, char *dest, int max_len)
{
	char port_numbers[32] = {0}, tmp[32] = {0};
	int len, ret;

	memset(dest, 0, max_len);

	len = sprintf(tmp, "%d-", libusb_get_bus_number(dev));

	strcat(dest, tmp);
	ret = libusb_get_port_numbers(dev, (uint8_t *)&port_numbers, 32);
	if (ret == LIBUSB_ERROR_OVERFLOW) {
		rh_trace(LVL_ERR, "Busid read failed %d\n", ret);
		return false;
	}

	for (int i = 0; i < ret - 1; i++) {
		len += sprintf(tmp, "%d.", port_numbers[i]);
		if (len >= max_len)
			return false;
		strcat(dest, tmp);
	}

	len += sprintf(tmp, "%d", port_numbers[ret - 1]);
	if (len >= max_len)
		return false;
	strcat(dest, tmp);

	return true;
}

static void parse_endpoints(struct usb_device_info *info, struct libusb_interface_descriptor intf)
{
	for (int i = 0; i < intf.bNumEndpoints; i++) {
		uint8_t addr = intf.endpoint[i].bEndpointAddress;
		uint8_t epnum = intf.endpoint[i].bEndpointAddress &
						 USB_ENDPOINT_NUMBER_MASK;
		if (addr & USB_DIR_IN) {
			info->ep_in_type[epnum] = intf.endpoint[i].bmAttributes &
							USB_ENDPOINT_XFERTYPE_MASK;
			rh_trace(LVL_DBG, "USB_DIR_IN - ep %d -> type %d\n", epnum,
					  info->ep_in_type[epnum]);
		} else {
			info->ep_out_type[epnum] = intf.endpoint[i].bmAttributes &
							USB_ENDPOINT_XFERTYPE_MASK;
			rh_trace(LVL_DBG, "USB_DIR_OUT - ep %d -> %d\n", epnum,
					  info->ep_out_type[epnum]);
		}
	}
}

static bool get_basic_device_info(struct server_usb_device *device, struct libusb_device *dev)
{
	int ret;
	bool ok;
	libusb_device_handle *temp_handle;
	struct libusb_config_descriptor *cfg;
	struct libusb_device_descriptor dev_desc;
	struct usb_device_info info = {0};

	info.udev.busnum = libusb_get_bus_number(dev);
	info.udev.devnum = libusb_get_port_number(dev);
	info.udev.speed = libusb_get_device_speed(dev);

	/* Skip wireless speed to be in line with kernel definitions */
	if (info.udev.speed >= LIBUSB_SPEED_SUPER)
		info.udev.speed++;

	ret = libusb_get_device_descriptor(dev, &dev_desc);
	if (ret != 0)
		return false;

	info.udev.idProduct = dev_desc.idProduct;
	info.udev.idVendor = dev_desc.idVendor;
	info.udev.bcdDevice = dev_desc.bcdDevice;
	info.udev.bConfigurationValue = 0;
	info.udev.bDeviceClass = dev_desc.bDeviceClass;
	info.udev.bDeviceSubClass = dev_desc.bDeviceSubClass;
	info.udev.bDeviceProtocol = dev_desc.bDeviceProtocol;
	info.udev.bNumConfigurations = dev_desc.bNumConfigurations;

	rh_trace(LVL_DBG, "Initializing device 0x%04x:0x%04x\n",
			   info.udev.idProduct, info.udev.idVendor);

	ok = get_busid(dev, info.udev.busid, MAX_BUSID_LEN);
	if (!ok)
		return false;

	/* Devices with one config are only supported */
	ret = libusb_get_config_descriptor(dev, 0, &cfg);
	if (ret != 0)
		return false;

	info.udev.bNumInterfaces = cfg->bNumInterfaces;

	for (int i = 0; i < cfg->bNumInterfaces; i++) {
		if (i >= RH_MAX_USB_INTERFACES) {
			rh_trace(LVL_ERR, "Too many interfaces\n");
			libusb_free_config_descriptor(cfg);
			return false;
		}
		info.interface[i].bInterfaceClass =
			cfg->interface[i].altsetting[0].bInterfaceClass;
		info.interface[i].bInterfaceSubClass =
			cfg->interface[i].altsetting[0].bInterfaceSubClass;
		info.interface[i].bInterfaceProtocol =
			cfg->interface[i].altsetting[0].bInterfaceProtocol;
	}

	/* Parse all interfaces to find all endpoints. If an endpoint
	 * had a different transfer type in another interface, use the one from
	 * the last.
	 */
	for (int i = 0; i < cfg->bNumInterfaces; i++) {
		rh_trace(LVL_DBG, "Interface %d:\n", i);
		for (int j = 0; j < cfg->interface[i].num_altsetting; j++)
			parse_endpoints(&info, cfg->interface[i].altsetting[j]);
	}

	libusb_free_config_descriptor(cfg);

	ret = libusb_open(dev, &temp_handle);
	if (ret < 0) {
		rh_trace(LVL_DBG, "Failed to open device %d, %s - %s\n",
			 ret,
			 libusb_error_name(ret),
			 libusb_strerror(ret));
		return false;
	}

	ret = libusb_get_string_descriptor_ascii(temp_handle, dev_desc.iManufacturer,
						 (uint8_t *)info.manufacturer_name,
						 RH_DEVICE_NAME_MAX_LEN);
	if (ret < 0) {
		rh_trace(LVL_DBG, "Device string 1 query failed: %d, %s - %s\n",
			 ret, libusb_error_name(ret), libusb_strerror(ret));
		memset(info.manufacturer_name, 0x00, RH_DEVICE_NAME_MAX_LEN);
	}

	ret = libusb_get_string_descriptor_ascii(temp_handle, dev_desc.iProduct,
						 (uint8_t *)info.product_name,
						 RH_DEVICE_NAME_MAX_LEN);
	if (ret < 0) {
		rh_trace(LVL_DBG, "Device string 2 query failed: %d, %s - %s\n",
			 ret, libusb_error_name(ret), libusb_strerror(ret));
		memset(info.product_name, 0x00, RH_DEVICE_NAME_MAX_LEN);
	}

	libusb_close(temp_handle);

	/* Put device name into path variable instead of the path itself */
	snprintf(info.udev.path, 256, "%s - %s", info.manufacturer_name, info.product_name);

	device->info = info;

	return true;
}

static bool add_new_devices(libusb_device **devs)
{
	int i = 0;
	bool ok;
	char busid[MAX_BUSID_LEN];
	struct libusb_device *dev;
	struct libusb_device_descriptor desc;
	struct server_usb_device *device_entry;

	while (dev = devs[i++], dev != NULL) {

		if (libusb_get_device_descriptor(dev, &desc))
			continue;

		/* We are not interested in usb hubs now */
		if (desc.bDeviceClass == 0x09)
			continue;

		ok = get_busid(dev, busid, MAX_BUSID_LEN);
		if (!ok)
			continue;

		if (device_already_exists(busid)) {
			rh_trace(LVL_TRC, "Device %s already exists\n", busid);
			continue;
		}

		// TODO: Disable individual ports

		if (bus_is_disabled(libusb_get_bus_number(dev)))
			continue;

		device_entry = calloc(1, sizeof(struct server_usb_device));
		if (!device_entry)
			return false;

		if (!get_basic_device_info(device_entry, dev)) {
			free(device_entry);
			continue;
		}

		libusb_ref_device(dev);
		device_entry->fwd.libusb_dev = dev;

		rh_trace(LVL_DBG, "Inserting new device %s\n", device_entry->info.product_name);
		insert_device(device_entry);
		inform_attached(device_entry->info.udev);
	}

	return true;
}

static void terminate_forward(struct server_usb_device *device)
{
	if (device->fwd.link)
		network_shut_link(device->fwd.link);

	if (device->fwd.forwarding_thread)
		pthread_join(device->fwd.forwarding_thread, NULL);
	device->fwd.forwarding_thread = 0;
}

static bool remove_detached_devices(libusb_device **devs)
{
	int i = 0;
	char busid[MAX_BUSID_LEN] = {0};
	struct libusb_device *dev;
	struct libusb_device_descriptor desc;
	struct server_usb_device *device;
	bool device_found, ok;

	for_each_device(device) {
		i = 0;
		device_found = false;

		if (device->fwd.terminate) {
			if (device->fwd.forwarding_thread)
				pthread_join(device->fwd.forwarding_thread, NULL);
			device->fwd.forwarding_thread = 0;
			device->fwd.terminate = false;
		}

		if (device->fwd.forwarding_thread)
			device->info.exported = true;
		else
			device->info.exported = false;

		while (dev = devs[i++], dev != NULL) {

			if (libusb_get_device_descriptor(dev, &desc))
				continue;

			/* We are not interested in usb hubs now */
			if (desc.bDeviceClass == 0x09)
				continue;

			ok = get_busid(dev, busid, MAX_BUSID_LEN);
			if (!ok) {
				rh_trace(LVL_CRIT, "Can not remove device (%s)\n", busid);
				continue;
			}

			if (!strcmp(device->info.udev.busid, busid)) {
				device_found = true;
				break;
			}
		}

		if (!device_found) {
			rh_trace(LVL_DBG, "Deleting %s\n", device->info.manufacturer_name);
			terminate_forward(device);
			libusb_unref_device(device->fwd.libusb_dev);
			inform_detached(device->info.udev);
			delete_device(device);
			rh_trace(LVL_DBG, "Deleted\n");
			continue;
		}
	}

	return true;
}

static bool update_local_usb_devices(void)
{
	bool ret = true;
	ssize_t devlist_result;
	libusb_device **devs = NULL;

	devlist_result = libusb_get_device_list(usb_context, &devs);
	if (devlist_result < 0) {
		rh_trace(LVL_ERR, "Failed to get libusb devlist: %d, %s - %s\n",
			 ret,
			 libusb_error_name(ret),
			 libusb_strerror(ret));
		return false;
	}

	if (!remove_detached_devices(devs)) {
		rh_trace(LVL_ERR, "Removing detached devices failed\n");
		ret = false;
	}

	if (!add_new_devices(devs))
		ret = false;

	libusb_free_device_list(devs, 1);

	return ret;
}

static bool get_devicelist(struct usb_device_info **info, int *dev_count, bool get_exported)
{
	struct server_usb_device *tmp;
	struct usb_device_info *tmp_info = NULL;
	int available = 0, count = 0;
	*info = NULL;
	*dev_count = 0;

	for_each_device(tmp) {
		if (get_exported || !tmp->info.exported)
			available++;
	}

	rh_trace(LVL_DBG, "%d devices available\n", available);

	if (available > 0) {
		tmp_info = calloc(available, sizeof(struct usb_device_info));
		if (!tmp_info) {
			rh_trace(LVL_ERR, "Alloc failed\n");
			return false;
		}

		for_each_device(tmp) {
			if (get_exported || !tmp->info.exported) {
				memcpy(&tmp_info[count], &tmp->info,
				sizeof(struct usb_device_info));
				count++;
			}
			if (count >= available)
				break;
		}
	}

	*dev_count = count;
	*info = tmp_info;

	return true;
}

static void handle_usbip_req_devicelist(struct rh_event *ev)
{
	struct usb_device_info *list = NULL;
	struct usbip_op_devlist_reply rep_hdr = {0};
	struct usbip_usb_device usbip_dev;
	struct usbip_op_common hdr = {0};
	int dev_count = 0;
	bool ok;

	if (!ev->link) {
		rh_trace(LVL_ERR, "Nowhere to send the data!\n");
		return;
	}

	hdr.version = USBIP_DEFAULT_PROTOCOL_VERSION;
	hdr.code = USBIP_OP_REP_DEVLIST;
	hdr.status = USBIP_ST_OK;

	ok = get_devicelist(&list, &dev_count, false);
	if (!ok) {
		hdr.status = USBIP_ST_ERROR;
		if (!usbip_net_send_usbip_header(ev->link, &hdr))
			rh_trace(LVL_ERR, "Failed to send USBIP header\n");

		return;
	}

	if (!usbip_net_send_usbip_header(ev->link, &hdr)) {
		rh_trace(LVL_ERR, "Failed to send USBIP header\n");
		free(list);
		return;
	}

	rep_hdr.ndev = dev_count;

	usbip_net_devlist_reply_to_network_order(&rep_hdr);
	if (!network_send_data(ev->link, (uint8_t *)&rep_hdr, sizeof(rep_hdr))) {
		rh_trace(LVL_ERR, "Failed to send data\n");
		free(list);
		return;
	}

	for (int i = 0; i < dev_count; i++) {
		usbip_dev = list[i].udev;
		usbip_net_dev_to_network_order(&usbip_dev);
		if (!network_send_data(ev->link, (uint8_t *)&usbip_dev, sizeof(usbip_dev))) {
			rh_trace(LVL_ERR, "Failed to send data\n");
			free(list);
			return;
		}
		for (int j = 0; j < usbip_dev.bNumInterfaces; j++) {
			if (!network_send_data(ev->link, (uint8_t *)&list[i].interface[j],
					       sizeof(list[i].interface[j]))) {
				rh_trace(LVL_ERR, "Failed to send data\n");
				free(list);
				return;
			}
		}
	}
	free(list);
}

static bool handle_usbip_req_import(struct rh_event *ev)
{
	struct server_usb_device *dev;
	struct usbip_op_import_request import_req = {0};
	struct usbip_op_import_reply import_rep = {0};
	struct usbip_op_common hdr = {0};
	bool ok, found = false;

	if (!network_recv_data(ev->link, (uint8_t *)&import_req, sizeof(import_req))) {
		rh_trace(LVL_ERR, "Failed to receive data\n");
		return false;
	}

	hdr.version = USBIP_DEFAULT_PROTOCOL_VERSION;
	hdr.code = USBIP_OP_REP_IMPORT;
	hdr.status = USBIP_ST_OK;

	for_each_device(dev) {
		rh_trace(LVL_TRC, "Checking %s and %s\n", dev->info.udev.busid, import_req.busid);
		if (!strcmp(dev->info.udev.busid, import_req.busid)) {
			rh_trace(LVL_DBG, "Device %s found\n", dev->info.product_name);
			found = true;
			break;
		}
	}

	if (!found) {
		hdr.status = USBIP_ST_NODEV;
		if (!usbip_net_send_usbip_header(ev->link, &hdr))
			rh_trace(LVL_ERR, "Failed to send USBIP header\n");

		rh_trace(LVL_ERR, "Device was not found\n");
		return false;
	}

	if (port_is_disabled(dev->info.udev.busnum, dev->info.udev.devnum)) {
		rh_trace(LVL_ERR, "Port was disabled\n");
		hdr.status = USBIP_ST_DEV_BUSY;
		if (!usbip_net_send_usbip_header(ev->link, &hdr))
			rh_trace(LVL_ERR, "Failed to send USBIP header\n");
		return false;
	}

	if (dev->fwd.forwarding_thread) {
		rh_trace(LVL_ERR, "Already exported\n");
		hdr.status = USBIP_ST_DEV_BUSY;
		if (!usbip_net_send_usbip_header(ev->link, &hdr))
			rh_trace(LVL_ERR, "Failed to send USBIP header\n");
		return false;
	}

	if (!usbip_net_send_usbip_header(ev->link, &hdr)) {
		rh_trace(LVL_ERR, "Failed to send USBIP header\n");
		return false;
	}

	import_rep.udev = dev->info.udev;

	usbip_net_import_reply_to_network_order(&import_rep);
	if (!network_send_data(ev->link, (uint8_t *)&import_rep, sizeof(import_rep))) {
		rh_trace(LVL_ERR, "Failed to receive data\n");
		return false;
	}

	dev->fwd.link = ev->link;
	ok = forwarding_start(dev);
	if (!ok) {
		rh_trace(LVL_ERR, "Device [%s] fwd failed\n", dev->info.manufacturer_name);
		dev->fwd.link = NULL;
		return false;
	}

	rh_trace(LVL_TRC, "Device [%s] forwarding\n", dev->info.manufacturer_name);

	return true;
}

static void generate_devicelist(void)
{
	struct rh_event event = {0};
	struct usb_device_info *info = NULL;
	int dev_count;

	(void) get_devicelist(&info, &dev_count, true);

	event.type = EVENT_LOCAL_DEVICELIST;
	event.data = info;
	event.size = dev_count * sizeof(struct usb_device_info);
	(void) event_enqueue(&event);
	if (info)
		free(info);
}

static void handle_event(struct rh_event *ev)
{
	bool keep_link;

	switch (ev->type) {
	case EVENT_TIMER_1S:
		rh_trace(LVL_DBG, "Updating local USB devices\n");
		update_local_usb_devices();
		generate_devicelist();
		break;
	case EVENT_REQ_DEVICELIST:
		handle_usbip_req_devicelist(ev);
		network_close_link(ev->link);
		free(ev->link);
		break;
	case EVENT_REQ_IMPORT:
		keep_link = handle_usbip_req_import(ev);
		if (!keep_link) {
			network_close_link(ev->link);
			free(ev->link);
		}
		break;
	default:
		return;
	}
}

static void *usb_loop(void *args)
{
	struct rh_event *event = {0};
	bool ok;

	(void)args;
	rh_trace(LVL_TRC, "USB task starting\n");

	while (usb.running) {
		ok = event_dequeue(&usb, &event);
		if (!ok) {
			rh_trace(LVL_TRC, "USB task stopping\n");
			break;
		}
		handle_event(event);
		free(event);
	}

	rh_trace(LVL_TRC, "USB task exit\n");

	return NULL;
}

static void *libusb_loop(void *args)
{
	(void) args;

	while (libusb_running)
		libusb_handle_events(usb_context);

	rh_trace(LVL_TRC, "Libusb stopped\n");
	return NULL;
}

void usb_exit(void)
{
	struct server_usb_device *device;

	rh_trace(LVL_TRC, "USB terminate\n");
	usb.running = false;
	pthread_cond_signal(&usb.event_cond);
	if (usb_thread)
		pthread_join(usb_thread, NULL);

	rh_trace(LVL_TRC, "Running cleanup\n");

	for_each_device(device) {
		terminate_forward(device);
		libusb_unref_device(device->fwd.libusb_dev);
		delete_device(device);
	}

	delete_bus_info();

	libusb_running = false;

	if (libusb_thread) {
		libusb_interrupt_event_handler(usb_context);
		pthread_join(libusb_thread, NULL);
		libusb_exit(usb_context);
		rh_trace(LVL_TRC, "LibUSB terminated\n");
	}
	rh_trace(LVL_TRC, "USB terminated\n");
}

bool usb_task_init(void)
{
	int ret;

	rh_trace(LVL_TRC, "USB init\n");

	ret = libusb_init(&usb_context);
	if (ret < 0) {
		rh_trace(LVL_ERR, "Libusb init failed %d, %s - %s\n", ret,
			 libusb_error_name(ret),
			 libusb_strerror(ret));
		return false;
	}

	pthread_mutex_init(&usb_conf_lock, NULL);

	//libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_DEBUG);

	usb.running = true;
	if (pthread_create(&libusb_thread, NULL, libusb_loop, NULL)) {
		rh_trace(LVL_ERR, "Failed to start libUSB device handling\n");
		return false;
	}

	usb.event_mask = EVENT_TIMER_1S | EVENT_REQ_DEVICELIST | EVENT_REQ_IMPORT;
	strcpy(usb.task_name, "USB task");
	event_task_register(&usb);

	if (pthread_create(&usb_thread, NULL, usb_loop, NULL)) {
		rh_trace(LVL_ERR, "Failed to start USB device handling\n");
		return false;
	}

	return true;
}
