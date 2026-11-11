#!/usr/bin/env bash
# Serve the app for local testing.
#   ./serve.sh          → http://localhost:8787      (desktop, or phone via `adb reverse tcp:8787 tcp:8787`)
#   ./serve.sh https    → https://<this-mac-ip>:8443 (phone on the same Wi-Fi; needs mkcert: brew install mkcert)
set -euo pipefail
cd "$(dirname "$0")"
if [ "${1:-}" = "https" ]; then
  command -v mkcert >/dev/null || { echo "brew install mkcert && mkcert -install"; exit 1; }
  IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1)
  mkdir -p .certs
  [ -f .certs/cert.pem ] || (cd .certs && mkcert -cert-file cert.pem -key-file key.pem localhost "$IP")
  echo "Open https://$IP:8443 on the phone (accept the certificate, or install the mkcert root CA on the phone)."
  python3 - <<EOF
import http.server, ssl
srv = http.server.ThreadingHTTPServer(('0.0.0.0', 8443), http.server.SimpleHTTPRequestHandler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain('.certs/cert.pem', '.certs/key.pem')
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
EOF
else
  echo "http://localhost:8787  (phone: adb reverse tcp:8787 tcp:8787, then open http://localhost:8787)"
  python3 -m http.server 8787 --bind 0.0.0.0
fi
