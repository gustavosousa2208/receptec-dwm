#!/bin/sh
# Serve this directory over HTTPS (self-signed) so Web Bluetooth counts the
# page as a secure context: https://localhost:8443/range-dashboard.html
# Accept the browser's certificate warning once (ERR_CERT_AUTHORITY_INVALID).
set -eu

dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
cert=$dir/.serve-cert.pem

if [ ! -f "$cert" ]; then
	openssl req -x509 -newkey rsa:2048 -keyout "$cert" -out "$cert" \
		-days 3650 -nodes -subj "/CN=localhost" >/dev/null 2>&1
	echo "self-signed cert created: $cert"
fi

echo "serving $dir at https://localhost:8443/ (Ctrl-C to stop)"
echo "from another machine on the LAN use https://<this-host-ip>:8443/ (accept both warnings)"
exec python3 -c "
from http.server import HTTPServer, SimpleHTTPRequestHandler
import ssl, os
os.chdir('$dir')
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain('$cert')
srv = HTTPServer(('0.0.0.0', 8443), SimpleHTTPRequestHandler)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
"
