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
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include <getopt.h>

#include "remotehub.h"
#include "logging.h"
#include "server.h"

bool info_available, running = true;
char info_str[64];

void sig_handler(int sig)
{
	(void) sig;

	running = false;
}

void device_state_changed(enum usb_dev_state state, struct usbip_usb_device dev)
{
	switch (state) {
	case ATTACHED:
		sprintf(info_str, "%s [0x%04x:0x%04x] ATTACHED",
			dev.busid, dev.idVendor, dev.idProduct);
		break;
	case DETACHED:
		sprintf(info_str, "%s [0x%04x:0x%04x] DETACHED",
			dev.busid, dev.idVendor, dev.idProduct);
		break;
	case EXPORTED:
		sprintf(info_str, "%s [0x%04x:0x%04x] EXPORTED",
			dev.busid, dev.idVendor, dev.idProduct);
		break;
	case UNEXPORTED:
		sprintf(info_str, "%s [0x%04x:0x%04x] UNEXPORTED",
			dev.busid, dev.idVendor, dev.idProduct);
		break;
	default:
		break;
	}
}

void devlist_handler(struct usb_device_info *devlist, int count)
{
	printf("\033[1;1H\033[2J");
	printf("|%*sBusid%*s|%*sManufacturer%*s|%*sProduct%*s| Exported |\n",
		8, " ", 8, " ", 5, " ", 5, " ", 7, " ", 8, " ");
	for (int i = 0; i < count; i++) {
		printf("|%-21.21s|%-22.22s|%-22.22s|%-10.10s|\n",
			devlist[i].udev.busid, devlist[i].manufacturer_name,
			devlist[i].product_name,
			devlist[i].exported ? "True" : "False");
	}
	if (info_str[0]) {
		printf("%s\n", info_str);
		memset(info_str, 0, sizeof(info_str));
	}

	rh_free_server_devlist(devlist);
}

static void print_rh_version(void)
{
	int major, minor, patch;

	rh_get_version(&major, &minor, &patch);
	printf("Remotehub library: %d.%d.%d\n", major, minor, patch);
	printf("Dependencies:\n%s\n", rh_get_server_dependency_versions());
}

static void print_help(void)
{
	printf("Usage: rh_server -c <json_config>\n");
	printf("Options:\n");
	printf(" -c, --config  - Path to server configuration file\n");
	printf(" -v, --version - Print version information\n");
}

int main(int argc, char *argv[])
{
	int ret, opt;
	char *conf_path = NULL;

	static const struct option opts[] = {
		{"config", required_argument, NULL, 'c'},
		{"version", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	signal(SIGINT, sig_handler);

	while (1) {
		opt = getopt_long(argc, argv, "c:vh", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'v':
			print_rh_version();
			return 0;
		case 'c':
			conf_path = optarg;
			break;
		case 'h':
			print_help();
			return 0;
		default:
			printf("Invalid parameters\n");
			print_help();
			return 1;
		}
	}

	if (!conf_path) {
		printf("Configuration file path needed\n");
		print_help();
		return 1;
	}

	rh_set_debug_level(LVL_CRIT);

	ret = rh_server_config_init(conf_path);
	if (ret) {
		printf("Server init failed [%s]\n", rh_err2str(ret));
		return 1;
	}

	rh_devicelist_subscribe(devlist_handler);
	rh_attached_subscribe(device_state_changed);
	rh_detached_subscribe(device_state_changed);
	rh_exported_subscribe(device_state_changed);
	rh_unexported_subscribe(device_state_changed);

	while (running)
		usleep(100000);

	rh_server_exit();
	return 0;
}
