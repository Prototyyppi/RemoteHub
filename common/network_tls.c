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
#include <stdlib.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "network.h"
#include "logging.h"

void network_close_tls(struct est_conn *link)
{
	mbedtls_net_close(&link->tls.socket_fd);
	mbedtls_ssl_free(&link->tls.ssl);
	mbedtls_ssl_config_free(&link->tls.conf);
	mbedtls_ctr_drbg_free(&link->tls.ctr_drbg);
	mbedtls_entropy_free(&link->tls.entropy);
	mbedtls_x509_crt_free(&link->tls.cacert);
}

void network_shut_tls(struct est_conn *link)
{
	mbedtls_ssl_close_notify(&link->tls.ssl);
	shutdown(link->tls.socket_fd.MBEDTLS_PRIVATE(fd), SHUT_RDWR);
}

int network_tls_send(struct est_conn *link, uint8_t *data, uint32_t len)
{
	return mbedtls_ssl_write(&link->tls.ssl, data, len);
}

int network_tls_recv(struct est_conn *link, uint8_t *data, uint32_t len)
{
	return mbedtls_ssl_read(&link->tls.ssl, data, len);
}
