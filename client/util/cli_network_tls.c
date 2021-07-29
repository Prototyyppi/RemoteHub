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

#include "cli_network.h"
#include "logging.h"

bool network_connect_tls(struct client_conn conn, struct est_conn *link)
{
	int ret;
	char buff[256];

	mbedtls_net_init(&link->tls.socket_fd);
	mbedtls_ssl_init(&link->tls.ssl);
	mbedtls_ssl_config_init(&link->tls.conf);
	mbedtls_ctr_drbg_init(&link->tls.ctr_drbg);
	mbedtls_entropy_init(&link->tls.entropy);
	mbedtls_x509_crt_init(&link->tls.cacert);

	link->encrypted = true;

	if (mbedtls_ctr_drbg_seed(&link->tls.ctr_drbg, mbedtls_entropy_func,
				  &link->tls.entropy, (unsigned char *) "remotehub",
				  strlen("remotehub")) != 0) {
		rh_trace(LVL_ERR, "Failed to initialize RNG\n");
		ret = -1;
		goto exit;
	}

	ret = mbedtls_x509_crt_parse_file(&link->tls.cacert, conn.ca_path);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Failed to parse CA cert\n");
		goto exit;
	}

#if 0
	rh_trace(LVL_TRC, "mbedtls_net_connect\n");
	snprintf(port, 9, "%d", conn.port);
	ret = mbedtls_net_connect(&link->tls.socket_fd, inet_ntoa(conn.ip),
				  port, MBEDTLS_NET_PROTO_TCP);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Failed to connect: %s:%s\n",
				  inet_ntoa(conn.ip), port);
		ret = -1;
		goto exit;
	}
#else
	/* Connect with timeout support */
	struct sockaddr_in addr;


	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;

	addr.sin_port = htons(conn.port);
	addr.sin_addr = conn.ip;

	link->tls.socket_fd.MBEDTLS_PRIVATE(fd) = socket(AF_INET, SOCK_STREAM, 0);
	if (link->tls.socket_fd.MBEDTLS_PRIVATE(fd) < 0) {
		rh_trace(LVL_ERR, "Socket creation failed\n");
		ret = -1;
		goto exit;
	}

	network_send_timeout_seconds_set(link->tls.socket_fd.MBEDTLS_PRIVATE(fd), 2);
	network_recv_timeout_seconds_set(link->tls.socket_fd.MBEDTLS_PRIVATE(fd), 2);

	ret = connect(link->tls.socket_fd.MBEDTLS_PRIVATE(fd), (const struct sockaddr *)&addr,
		      sizeof(addr));
	if (ret < 0) {
		rh_trace(LVL_ERR, "Failed to connect: %s:%d\n",
				   inet_ntoa(conn.ip), conn.port);
		ret = -1;
		goto exit;
	}

#endif

	if (mbedtls_ssl_config_defaults(&link->tls.conf, MBEDTLS_SSL_IS_CLIENT,
					MBEDTLS_SSL_TRANSPORT_STREAM,
					MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
		rh_trace(LVL_ERR, "TLS config setup failed\n");
		ret = -1;
		goto exit;
	}

	mbedtls_ssl_conf_authmode(&link->tls.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	mbedtls_ssl_conf_ca_chain(&link->tls.conf, &link->tls.cacert, NULL);
	mbedtls_ssl_conf_rng(&link->tls.conf, mbedtls_ctr_drbg_random, &link->tls.ctr_drbg);

	/* Different ciphers can be tested for performance improvement
	 * int ciphers[1] = {MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA};
	 * mbedtls_ssl_conf_ciphersuites(&link->tls.conf, ciphers);
	 */

	if (mbedtls_ssl_setup(&link->tls.ssl, &link->tls.conf) != 0) {
		rh_trace(LVL_ERR, "TLS setup failed\n");
		ret = -1;
		goto exit;
	}

	mbedtls_ssl_set_bio(&link->tls.ssl, &link->tls.socket_fd,
			    mbedtls_net_send, mbedtls_net_recv, NULL);

	while ((ret = mbedtls_ssl_handshake(&link->tls.ssl)) != 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			mbedtls_strerror(ret, buff, sizeof(buff));
			rh_trace(LVL_DBG, "TLS handshake failed %s\n", buff);
			ret = -1;
			goto exit;
		}
	}

	// TODO: Client verification for server

	return true;
exit:
	network_close_tls(link);
	return false;
}
