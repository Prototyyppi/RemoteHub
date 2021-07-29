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

#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#include "cJSON.h"
#include "mbedtls/version.h"

#include <libusb-1.0/libusb.h>

#include "remotehub.h"
#include "network.h"
#include "event.h"
#include "srv_interface.h"
#include "server.h"
#include "logging.h"
#include "beacon.h"
#include "timer.h"
#include "host.h"
#include "usb.h"

static pthread_t server_thread;

static void *server_event_handler(void *args)
{
	bool success;

	(void)args;

	success = event_handler();
	if (!success)
		rh_trace(LVL_ERR, "Event handling failed\n");

	interface_exit();
	host_exit();
	usb_exit();
	beacon_exit();
	timer_exit();
	event_cleanup();

	return NULL;
}

char *rh_get_server_dependency_versions(void)
{
	static char srv_deps[64] = {0};
	const struct libusb_version *libusb_v = libusb_get_version();

	snprintf(srv_deps, 64, "libusb: %d.%d.%d.%d\nMbedTLS: %s\ncJSON: %s",
		libusb_v->major, libusb_v->minor, libusb_v->micro, libusb_v->nano,
		MBEDTLS_VERSION_STRING, cJSON_Version());

	return srv_deps;
}

static int rh_server_init(struct server_info info)
{
	int ret;
	bool success;

	// TODO: Disable own vhci-hcd buses if running both server and client

	rh_trace(LVL_TRC, "Start server\n");

	signal(SIGPIPE, SIG_IGN);
	event_init();

	success = timer_task_init();
	if (!success) {
		rh_trace(LVL_ERR, "Timer task init failed\n");
		ret = RH_FAIL_INIT_TIMER;
		goto err_exit;
	}

	success = beacon_send_init(info.server_name, info.bcast_enabled,
				   info.tls_enabled, info.port);
	if (!success) {
		rh_trace(LVL_ERR, "Beacon task init failed\n");
		ret = RH_FAIL_INIT_BEACON;
		goto err_exit;
	}

	success = usb_task_init();
	if (!success) {
		rh_trace(LVL_ERR, "USB task init failed\n");
		ret = RH_FAIL_INIT_USB;
		goto err_exit;
	}

	success = host_task_init(info);
	if (!success) {
		rh_trace(LVL_ERR, "Host task init failed\n");
		ret = RH_FAIL_INIT_HOST;
		goto err_exit;
	}

	success = interface_task_init();
	if (!success) {
		rh_trace(LVL_ERR, "Interface task init failed\n");
		ret = RH_FAIL_INIT_INTERFACE;
		goto err_exit;
	}

	if (pthread_create(&server_thread, NULL, server_event_handler, NULL)) {
		rh_trace(LVL_ERR, "Failed to start server event handling\n");
		ret = RH_FAIL_INIT_HANDLER;
		goto err_exit;
	}

	return 0;

err_exit:
	interface_exit();
	host_exit();
	usb_exit();
	beacon_exit();
	timer_exit();
	event_cleanup();

	return ret;
}

static cJSON *read_config(char *conf_path)
{
	FILE *f = NULL;
	long len = 0;
	char *json_string = NULL;
	cJSON *json_object = NULL;

	if (!conf_path)
		return NULL;

	f = fopen(conf_path, "rb");
	if (!f) {
		rh_trace(LVL_ERR, "Fopen\n");
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);

	json_string = calloc(1, len + 1);
	if (!json_string) {
		rh_trace(LVL_ERR, "Calloc\n");
		fclose(f);
		return NULL;
	}

	fread(json_string, 1, len, f);
	fclose(f);

	json_object = cJSON_Parse(json_string);
	if (!json_object)
		rh_trace(LVL_ERR, "cJSON_Parse [%s]\n", cJSON_GetErrorPtr());

	free(json_string);

	return json_object;
}

int rh_server_config_init(char *conf_path)
{
	cJSON *config_json, *version_obj;
	cJSON *tls_obj, *bcast_obj, *cert_obj, *name_obj, *key_obj;
	cJSON *keypass_obj, *port_obj;
	cJSON *buses = NULL, *bus = NULL, *busnum = NULL;

	int conf_version = 1;
	struct server_info info = {0};

	info.tls_enabled = false;
	info.port = DEFAULT_PORT;

	if (geteuid() != 0) {
		rh_trace(LVL_ERR, "Sudo needed to access USB peripherals\n");
		return RH_FAIL_PERMISSION;
	}

	config_json = read_config(conf_path);
	if (!config_json) {
		rh_trace(LVL_ERR, "Failed to read config %s\n", conf_path);
		return RH_FAIL_JSON_CONFIG_READ;
	}

	version_obj = cJSON_GetObjectItem(config_json, "config_version");
	if (!version_obj || !cJSON_IsNumber(version_obj))
		rh_trace(LVL_ERR, "Config version not defined\n");
	else
		conf_version = cJSON_GetNumberValue(version_obj);


	if (conf_version != 1) {
		rh_trace(LVL_ERR, "Config version %d not supported\n", conf_version);
		cJSON_Delete(config_json);
		return RH_FAIL_JSON_CONFIG_READ;
	}

	name_obj = cJSON_GetObjectItem(config_json, "server_name");
	if (!name_obj || !cJSON_IsString(name_obj)) {
		snprintf((char *)&info.server_name, RH_SERVER_NAME_MAX_LEN,
			 "RemoteHub");
	} else {
		snprintf((char *)&info.server_name, RH_SERVER_NAME_MAX_LEN,
			 "%s", cJSON_GetStringValue(name_obj));
	}

	rh_trace(LVL_DBG, "Server name: %s\n", &info.server_name);

	bcast_obj = cJSON_GetObjectItem(config_json, "bcast_enabled");
	if (!bcast_obj || cJSON_IsTrue(bcast_obj)) {
		rh_trace(LVL_DBG, "Presence broadcast enabled\n");
		info.bcast_enabled = true;
	}

	tls_obj = cJSON_GetObjectItem(config_json, "use_tls");
	if (!tls_obj || cJSON_IsTrue(tls_obj)) {
		rh_trace(LVL_DBG, "TLS enabled\n");
		info.tls_enabled = true;
	}

	port_obj = cJSON_GetObjectItem(config_json, "port");
	if (port_obj && cJSON_IsNumber(port_obj)) {
		info.port = (int)cJSON_GetNumberValue(port_obj);
		rh_trace(LVL_DBG, "Using port %d\n",
				  (int)cJSON_GetNumberValue(port_obj));
	}

	if (info.tls_enabled) {
		cert_obj = cJSON_GetObjectItem(config_json, "cert_path");
		if (!cert_obj || !cJSON_IsString(cert_obj)) {
			cJSON_Delete(config_json);
			return RH_FAIL_CERT_PATH_NOT_DEFINED;
		}

		snprintf((char *)&info.cert_path, PATH_MAX, "%s",
			 cJSON_GetStringValue(cert_obj));

		key_obj = cJSON_GetObjectItem(config_json, "key_path");
		if (!key_obj || !cJSON_IsString(key_obj)) {
			cJSON_Delete(config_json);
			return RH_FAIL_KEY_PATH_NOT_DEFINED;
		}

		snprintf((char *)&info.key_path, PATH_MAX, "%s",
			 cJSON_GetStringValue(key_obj));

		keypass_obj = cJSON_GetObjectItem(config_json, "key_pass");
		if (!keypass_obj || !cJSON_IsString(keypass_obj)) {
			cJSON_Delete(config_json);
			return RH_FAIL_KEY_PASS_NOT_DEFINED;
		}

		snprintf((char *)&info.key_pass, KEY_PASSWORD_MAX_LEN, "%s",
				cJSON_GetStringValue(keypass_obj));
	}

	// TODO: Add support for disabling specific ports
	buses = cJSON_GetObjectItemCaseSensitive(config_json, "disable_array");
	cJSON_ArrayForEach(bus, buses) {
		busnum = cJSON_GetObjectItemCaseSensitive(bus, "bus");
		if (!cJSON_IsNumber(busnum)) {
			rh_trace(LVL_ERR, "Invalid bus %d\n",
					  (int)cJSON_GetNumberValue(busnum));
			continue;
		}
		rh_trace(LVL_DBG, "Disabling bus %d\n",
				  (int)cJSON_GetNumberValue(busnum));
		rh_disable_usb_bus((int)cJSON_GetNumberValue(busnum));
	}

	cJSON_Delete(config_json);
	return rh_server_init(info);
}

void rh_server_exit(void)
{
	struct rh_event event = {0};

	rh_trace(LVL_TRC, "Exit called\n");
	event.type = EVENT_TERMINATE;
	(void) event_enqueue(&event);
	pthread_join(server_thread, NULL);
}
