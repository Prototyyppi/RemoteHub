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

#ifndef __REMOTEHUB_H__
#define __REMOTEHUB_H__

#include <stdint.h>

#define REMOTEHUB_VERSION_MAJOR			0
#define REMOTEHUB_VERSION_MINOR			0
#define REMOTEHUB_VERSION_PATCH			0

#define RH_SERVER_NAME_MAX_LEN			64
#define RH_IP_NAME_MAX_LEN			64
#define RH_DEVICE_NAME_MAX_LEN			64
#define RH_MAX_USB_INTERFACES			32

#define USBIP_PATH_SIZE				256
#define USBIP_BUSID_SIZE			32

enum rh_error_status {
	RH_OK					= 0,
	RH_FAIL_JSON_CONFIG_READ		= 1,
	RH_FAIL_INIT				= 2,
	RH_FAIL_INIT_TIMER			= 3,
	RH_FAIL_INIT_BEACON			= 4,
	RH_FAIL_INIT_USB			= 5,
	RH_FAIL_INIT_HOST			= 6,
	RH_FAIL_INIT_INTERFACE			= 7,
	RH_FAIL_INIT_MANAGER			= 8,
	RH_FAIL_INIT_HANDLER			= 9,
	RH_FAIL_PERMISSION			= 10,
	RH_FAIL_CERT_PATH_NOT_DEFINED		= 11,
	RH_FAIL_KEY_PATH_NOT_DEFINED		= 12,
	RH_FAIL_CA_PATH_NOT_DEFINED		= 13,
	RH_FAIL_KEY_PASS_NOT_DEFINED		= 14,
	RH_FAIL_VHCI_DRIVER			= 15,
	RH_ERROR_COUNT
};

struct usbip_usb_device {
	// The path variable is used here for USB device manufacturer and product names.
	char path[USBIP_PATH_SIZE];
	char busid[USBIP_BUSID_SIZE];

	uint32_t busnum;
	uint32_t devnum;
	uint32_t speed;

	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;

	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bConfigurationValue;
	uint8_t bNumConfigurations;
	uint8_t bNumInterfaces;
} __attribute__((packed));

struct usbip_usb_interface {
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t padding; /* alignment */
} __attribute__((packed));

struct usb_device_info {
	struct usbip_usb_device	udev;
	struct usbip_usb_interface interface[RH_MAX_USB_INTERFACES];
	char manufacturer_name[RH_DEVICE_NAME_MAX_LEN];
	char product_name[RH_DEVICE_NAME_MAX_LEN];
	uint8_t ep_in_type[16];
	uint8_t	ep_out_type[16];
	uint8_t	exported;
};

const char *rh_err2str(int rh_errno);
void rh_get_version(int *major, int *minor, int *patch);

#endif /* __REMOTEHUB_H__*/
