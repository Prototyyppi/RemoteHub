#!/bin/bash

# See generation guide in:
# https://gist.github.com/fntlnz/cf14feb5a46b2eda428e000157447309

openssl genrsa -des3 -out rootCA.key 4096

openssl req -x509 -new -nodes -key rootCA.key -sha256 -days 1024 -out rootCA.crt

openssl genrsa -aes256 -out RemoteHub.key 2048

openssl req -new -sha256 -key RemoteHub.key -out RemoteHub.csr

openssl x509 -req -in RemoteHub.csr -CA rootCA.crt -CAkey rootCA.key -CAcreateserial -out RemoteHub.crt -days 500 -sha256

echo "Created: RemoteHub.crt, rootCA.crt RemoteHub.key"
echo "Use RemoteHub.crt and RemoteHub.key on server and give rootCA.crt to client"

exit 0
