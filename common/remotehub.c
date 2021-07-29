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

#include "remotehub.h"

#include <stddef.h>
#include <stdlib.h>

void rh_get_version(int *major, int *minor, int *patch)
{
	*major = REMOTEHUB_VERSION_MAJOR;
	*minor = REMOTEHUB_VERSION_MINOR;
	*patch = REMOTEHUB_VERSION_PATCH;
}

const char *rh_error_explanation[RH_ERROR_COUNT] = {
	[RH_OK] = "OK",
	[RH_FAIL_JSON_CONFIG_READ] = "JSON config read failed",
	[RH_FAIL_INIT] = "Server init failed",
	[RH_FAIL_INIT_TIMER] = "Failed to start timer task",
	[RH_FAIL_INIT_BEACON] = "Failed to start beacon task",
	[RH_FAIL_INIT_USB] = "Failed to start USB task",
	[RH_FAIL_INIT_HOST] = "Failed to start host network task",
	[RH_FAIL_INIT_INTERFACE] = "Failed to start interface task",
	[RH_FAIL_INIT_MANAGER] = "Failed to start manager task",
	[RH_FAIL_INIT_HANDLER] = "Failed to start event handling",
	[RH_FAIL_PERMISSION] = "Root permission required",
	[RH_FAIL_CERT_PATH_NOT_DEFINED] = "Certificate path for TLS communication needed",
	[RH_FAIL_KEY_PATH_NOT_DEFINED] = "Private key path for TLS communication needed",
	[RH_FAIL_CA_PATH_NOT_DEFINED] = "CA certificate path for TLS communication needed",
	[RH_FAIL_KEY_PASS_NOT_DEFINED] = "Private key password for TLS communication needed",
	[RH_FAIL_VHCI_DRIVER] = "Load VHCI driver with 'modprobe vhci-hcd'",
};

const char *rh_err2str(int rh_errno)
{
	if (rh_errno >= 0 && rh_errno < RH_ERROR_COUNT)
		return rh_error_explanation[rh_errno];
	return NULL;
}
