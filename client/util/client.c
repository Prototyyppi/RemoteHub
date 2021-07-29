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

#include <pthread.h>
#include "mbedtls/version.h"
#include "cJSON.h"

#include "remotehub.h"
#include "event.h"
#include "cli_interface.h"
#include "logging.h"
#include "beacon.h"
#include "timer.h"
#include "client.h"
#include "manager.h"

static pthread_t client_thread;

static void *client_event_handler(void *args)
{
	bool success;

	(void) args;
	success = event_handler();
	if (!success)
		rh_trace(LVL_ERR, "Event handling failed\n");

	interface_exit();
	manager_exit();
	beacon_exit();
	timer_exit();
	event_cleanup();

	return NULL;
}

char *rh_get_client_dependency_versions(void)
{
	static char cli_deps[64] = {0};

	snprintf(cli_deps, 64, "MbedTLS: %s\ncJSON: %s", MBEDTLS_VERSION_STRING, cJSON_Version());
	return cli_deps;
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


static int rh_client_init(struct client_info info)
{
	enum rh_error_status ret;
	bool success;

	rh_trace(LVL_INFO, "Start client\n");
	signal(SIGPIPE, SIG_IGN);

	if (geteuid() != 0) {
		rh_trace(LVL_ERR, "Sudo needed to access USB peripherals\n");
		return RH_FAIL_PERMISSION;
	}

	event_init();

	success = timer_task_init();
	if (!success) {
		rh_trace(LVL_ERR, "Timer task init failed\n");
		ret = RH_FAIL_INIT_TIMER;
		goto err_exit;
	}

	success = beacon_recv_init(info.tls_enabled);
	if (!success) {
		rh_trace(LVL_ERR, "Beacon task init failed\n");
		ret = RH_FAIL_INIT_BEACON;
		goto err_exit;
	}

	ret = manager_task_init(info.tls_enabled, info.ca_path);
	if (ret != RH_OK) {
		rh_trace(LVL_ERR, "Manager task init failed\n");
		goto err_exit;
	}

	success = interface_task_init();
	if (!success) {
		rh_trace(LVL_ERR, "Intf task init failed\n");
		ret = RH_FAIL_INIT_INTERFACE;
		goto err_exit;
	}

	if (pthread_create(&client_thread, NULL, client_event_handler, NULL)) {
		rh_trace(LVL_ERR, "Failed to start client event handling\n");
		ret = RH_FAIL_INIT_HANDLER;
		goto err_exit;
	}

	return RH_OK;

err_exit:
	interface_exit();
	manager_exit();
	beacon_exit();
	timer_exit();
	event_cleanup();

	return ret;
}

int rh_client_config_init(char *conf_path)
{
	struct client_info info;
	int conf_version = 1;

	cJSON *config_json;
	cJSON *tls_obj, *version_obj, *ca_cert_obj;

	info.tls_enabled = false;

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

	tls_obj = cJSON_GetObjectItem(config_json, "use_tls");
	if (!tls_obj || cJSON_IsTrue(tls_obj)) {
		rh_trace(LVL_DBG, "TLS enabled\n");
		info.tls_enabled = true;
	}

	ca_cert_obj = cJSON_GetObjectItem(config_json, "ca_path");
	if (!ca_cert_obj || !cJSON_IsString(ca_cert_obj)) {
		rh_trace(LVL_DBG, "Server verification disabled\n");
		info.ca_path[0] = 0;
	} else {
		rh_trace(LVL_DBG, "Verifying server with CA cert\n");
		snprintf((char *)&info.ca_path, PATH_MAX, "%s",
			 cJSON_GetStringValue(ca_cert_obj));
	}

	cJSON_Delete(config_json);
	return rh_client_init(info);
}

void rh_client_exit(void)
{
	struct rh_event event = {0};

	rh_trace(LVL_DBG, "Exit called, stopping\n");
	event.type = EVENT_TERMINATE;
	(void) event_enqueue(&event);
	pthread_join(client_thread, NULL);
}
