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

#ifndef __REMOTEHUB_NETWORK_H__
#define __REMOTEHUB_NETWORK_H__

#include <stdbool.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#define DEFAULT_PORT		3240

struct tls {
	mbedtls_net_context socket_fd;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cacert;

	/* Server only */
	mbedtls_net_context listen_fd;
	mbedtls_x509_crt srvcert;
	mbedtls_pk_context pkey;
};

struct est_conn {
	bool encrypted;
	struct tls tls;
	int socket;
};

int network_send(struct est_conn *link, uint8_t *data, uint32_t len);
int network_recv(struct est_conn *link, uint8_t *data, uint32_t len);

void network_close_link(struct est_conn *link);
void network_shut_link(struct est_conn *link);
void network_close_tcp(struct est_conn *link);
void network_shut_tcp(struct est_conn *link);
bool network_recv_data(struct est_conn *link, uint8_t *data, uint32_t len);
bool network_send_data(struct est_conn *link, uint8_t *data, uint32_t len);
void network_close_tls(struct est_conn *link);
void network_shut_tls(struct est_conn *link);
int network_tls_send(struct est_conn *link, uint8_t *data, uint32_t len);
int network_tls_recv(struct est_conn *link, uint8_t *data, uint32_t len);

void network_send_timeout_seconds_set(int socket, uint32_t seconds);
void network_recv_timeout_seconds_set(int socket, uint32_t seconds);

#endif //__REMOTEHUB_NETWORK_H__//
