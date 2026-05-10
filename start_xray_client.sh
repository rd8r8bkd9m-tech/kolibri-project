#!/bin/bash

# Xray VPN Client Launcher
# Server: 45.39.60.146:443 (VLESS over WebSocket + TLS)

CONFIG_FILE="$(dirname "$0")/xray_client_config.json"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file $CONFIG_FILE not found!"
    exit 1
fi

echo "Starting Xray VPN client..."
echo "SOCKS5 proxy: 127.0.0.1:1080"
echo "HTTP proxy: 127.0.0.1:8080"
echo "Press Ctrl+C to stop"

# Run Xray with the client configuration
xray run -config "$CONFIG_FILE"