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

#include "srv_network.h"
#include "logging.h"

bool network_create_tls_server(struct server_conn *conn)
{
	int ret, one = 1;
	char port_str[20];

	mbedtls_net_init(&conn->tls.listen_fd);
	mbedtls_ssl_config_init(&conn->tls.conf);
	mbedtls_x509_crt_init(&conn->tls.srvcert);
	mbedtls_ctr_drbg_init(&conn->tls.ctr_drbg);
	mbedtls_entropy_init(&conn->tls.entropy);
	mbedtls_pk_init(&conn->tls.pkey);

	ret = mbedtls_x509_crt_parse_file(&conn->tls.srvcert, conn->info.cert_path);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Certificate parsing [%s] failed\n", conn->info.cert_path);
		goto err_exit;
	}

	ret = mbedtls_ctr_drbg_seed(&conn->tls.ctr_drbg, mbedtls_entropy_func,
				    &conn->tls.entropy, (unsigned char *) "remotehub",
				    strlen("remotehub"));
	if (ret != 0) {
		rh_trace(LVL_ERR, "Failed to seed RNG (%d)\n", ret);
		goto err_exit;
	}

	ret = mbedtls_pk_parse_keyfile(&conn->tls.pkey, conn->info.key_path, conn->info.key_pass,
					mbedtls_ctr_drbg_random, &conn->tls.ctr_drbg);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Keyfile parsing [%s] failed\n", conn->info.key_path);
		goto err_exit;
	}

	sprintf(port_str, "%d", conn->info.port);
	ret = mbedtls_net_bind(&conn->tls.listen_fd, NULL, port_str, MBEDTLS_NET_PROTO_TCP);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Failed to bind (%d)\n", ret);
		goto err_exit;
	}

	/* TCP_NODELAY a gives noticeable speed increase when using f.ex mouse */
	setsockopt(conn->tls.listen_fd.MBEDTLS_PRIVATE(fd), IPPROTO_TCP, TCP_NODELAY, &one,
		   sizeof(one));

	// TODO: Implement keepalive for detecting broken connection

	ret = mbedtls_ssl_config_defaults(&conn->tls.conf,
					  MBEDTLS_SSL_IS_SERVER,
					  MBEDTLS_SSL_TRANSPORT_STREAM,
					  MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		rh_trace(LVL_ERR, "TLS config setup failed (%d)\n", ret);
		goto err_exit;
	}

	mbedtls_ssl_conf_rng(&conn->tls.conf, mbedtls_ctr_drbg_random, &conn->tls.ctr_drbg);

	/*
	 * TODO: Implement peer verification
	 * mbedtls_ssl_conf_authmode(&conn->tls.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	 * mbedtls_ssl_conf_ca_chain(&conn->tls.conf, &conn->tls.cacert, NULL);
	 */

	ret = mbedtls_ssl_conf_own_cert(&conn->tls.conf, &conn->tls.srvcert, &conn->tls.pkey);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Failed to set certificates (%d)\n", ret);
		goto err_exit;
	}

	rh_trace(LVL_DBG, "TLS server configured for use\n");

	return true;
err_exit:
	mbedtls_net_free(&conn->tls.listen_fd);
	mbedtls_ssl_config_free(&conn->tls.conf);
	mbedtls_ctr_drbg_free(&conn->tls.ctr_drbg);
	mbedtls_entropy_free(&conn->tls.entropy);
	mbedtls_x509_crt_free(&conn->tls.srvcert);
	mbedtls_pk_free(&conn->tls.pkey);
	return false;
}

bool network_listen_tls(struct server_conn *conn, struct est_conn *link)
{
	int ret;
	char buff[256];

	mbedtls_ssl_init(&link->tls.ssl);
	mbedtls_net_init(&link->tls.socket_fd);

	link->encrypted = true;

	ret = mbedtls_ssl_setup(&link->tls.ssl, &conn->tls.conf);
	if (ret != 0) {
		rh_trace(LVL_ERR, "TLS setup failed (%d)\n", ret);
		goto err_exit;
	}

	mbedtls_ssl_set_bio(&link->tls.ssl, &link->tls.socket_fd,
			    mbedtls_net_send, mbedtls_net_recv, NULL);

	ret = mbedtls_net_accept(&conn->tls.listen_fd, &link->tls.socket_fd, NULL, 0, NULL);
	if (ret != 0) {
		rh_trace(LVL_ERR, "Failed to accept connection (%d)\n", ret);
		goto err_exit;
	}

	while ((ret = mbedtls_ssl_handshake(&link->tls.ssl)) != 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			mbedtls_strerror(ret, buff, sizeof(buff));
			rh_trace(LVL_ERR, "TLS handshake failed %d (%s)\n", ret, buff);
			goto err_exit;
		}
	}

	return true;
err_exit:
	mbedtls_net_free(&link->tls.socket_fd);
	mbedtls_ssl_free(&link->tls.ssl);
	return false;
}

void network_exit_server_tls(struct server_conn *conn)
{
	mbedtls_net_free(&conn->tls.listen_fd);
	mbedtls_x509_crt_free(&conn->tls.srvcert);
	mbedtls_pk_free(&conn->tls.pkey);
	mbedtls_ssl_config_free(&conn->tls.conf);
	mbedtls_ctr_drbg_free(&conn->tls.ctr_drbg);
	mbedtls_entropy_free(&conn->tls.entropy);
}
