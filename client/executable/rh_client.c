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

#include <getopt.h>

#include "remotehub.h"
#include "client.h"

#define ARRAYSIZE(list) (sizeof(list) / sizeof(list[0]))

struct disabled_device {
	uint16_t idProduct;
	uint16_t idVendor;
};

static pthread_mutex_t devlist_lock;
static bool running = true;

static struct disabled_device disable_list[] = {
	{.idProduct = 0xEC00, .idVendor = 0x0424},
	{.idProduct = 0x0083, .idVendor = 0x21B4},
	{.idProduct = 0x3012, .idVendor = 0x413C},
};

void sig_handler(int sig)
{
	(void) sig;
	printf("Stopping client\n");
	running = false;
}

bool device_disabled(struct usbip_usb_device dev)
{
	// TODO: Add port disabling support to server and read these from client configuration JSON
	for (uint32_t i = 0; i < ARRAYSIZE(disable_list); i++) {
		if ((dev.idProduct == disable_list[i].idProduct) &&
		    (dev.idVendor == disable_list[i].idVendor)) {
			return true;
		}
	}
	return false;
}

void usbip_devlist_callback(bool success, char *server, uint16_t port,
			    struct usbip_usb_device *devlist, uint32_t count)
{
	if (!success)
		printf("Failed to get devicelist from %s:%d\n", server, port);

	for (uint32_t i = 0; i < count; i++) {
		if (!device_disabled(devlist[i]))
			rh_attach_device(server, port, devlist[i]);
	}
	rh_free_client_devlist(devlist);
}

void attach_callback(bool success, char *server, uint16_t port, struct usbip_usb_device dev)
{
	if (success)
		printf("Attached %s from %s:%d\n", dev.path, server, port);
}

void detach_callback(bool success, char *server, uint16_t port, struct usbip_usb_device dev)
{
	if (success)
		printf("Detached %s from %s:%d\n", dev.path, server, port);
}

void on_server_discovered(char *server_ip, uint16_t port, char *name)
{
	(void) name;
	rh_get_devicelist(server_ip, port);
}

static void print_rh_version(void)
{
	int major, minor, patch;

	rh_get_version(&major, &minor, &patch);
	printf("Remotehub library: %d.%d.%d\n", major, minor, patch);
	printf("Dependencies:\n%s\n", rh_get_client_dependency_versions());
}

static void print_help(void)
{
	printf("Usage: rh_client [-t <CA_cert>] [-i <server_ip> -p <port>]\n");
	printf("This client attempts to attach all USB devices from server\n");
	printf("Options:\n");
	printf(" -c, --config  - Path to client configuration file\n");
	printf(" -i, --ip      - Client tries to use only server at this ip\n");
	printf(" -p, --port    - Port to use with targeted ip address\n");
	printf(" -v, --version - Print version information\n");
}

int main(int argc, char *argv[])
{
	int ret, opt, port = 3240;
	bool is_target_ip = false;
	char *conf_path = NULL, *target_ip = NULL, *endptr;

	static const struct option opts[] = {
		{"config", required_argument, NULL, 'c'},
		{"ip", required_argument, NULL, 'i'},
		{"port", required_argument, NULL, 'p'},
		{"version", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	signal(SIGINT, sig_handler);

	while (1) {
		opt = getopt_long(argc, argv, "c:i:p:vh", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'v':
			print_rh_version();
			return 0;
		case 'c':
			conf_path = optarg;
			break;
		case 'i':
			is_target_ip = true;
			target_ip = optarg;
			break;
		case 'p':
			port = strtol(optarg, &endptr, 10);
			if ((port == 0) || (endptr == optarg)) {
				fprintf(stderr, "Failed to read port value\n");
				return 1;
			}
			break;
		case 'h':
			print_help();
			return 0;
		default:
			fprintf(stderr, "Invalid parameters\n");
			print_help();
			return 1;
		}
	}

	if (!is_target_ip && port != 3240) {
		fprintf(stderr, "IP address must be given with port parameter\n");
		return 1;
	}

	rh_set_debug_level(LVL_CRIT);

	ret = rh_client_config_init(conf_path);
	if (ret) {
		fprintf(stderr, "Client init failed [%s]\n", rh_err2str(ret));
		return 1;
	}

	pthread_mutex_init(&devlist_lock, NULL);

	rh_usbip_devicelist_subscribe(usbip_devlist_callback);
	rh_attach_subscribe(attach_callback);
	rh_detach_subscribe(detach_callback);
	if (!is_target_ip)
		rh_server_discovery_subscribe(on_server_discovered);

	printf("Client started\n");

	while (running) {
		if (is_target_ip)
			rh_get_devicelist(target_ip, port);

		usleep(5000000);
	}

	rh_client_exit();

	pthread_mutex_destroy(&devlist_lock);
	return 0;
}
