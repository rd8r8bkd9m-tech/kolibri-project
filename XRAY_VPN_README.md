# Xray VPN Setup

## Server Configuration
- **Server IP**: 45.39.60.146
- **Port**: 443
- **Protocol**: VLESS
- **Transport**: TCP
- **Security**: None (plain connection)
- **UUID**: a3418fb5-9a18-4f1d-b5bc-6efdc13b85aa

## Client Setup

### 1. Start VPN Client
```bash
./start_xray_client.sh
```

### 2. Configure System Proxy
After starting the client, configure your applications to use the proxy:

- **SOCKS5 Proxy**: `127.0.0.1:1080`
- **HTTP Proxy**: `127.0.0.1:8080`

### 3. Browser Configuration
For Firefox/Chrome, you can use extensions like:
- FoxyProxy
- Proxy SwitchyOmega

Or set system-wide proxy in macOS:
```bash
# SOCKS5
networksetup -setsocksfirewallproxy Wi-Fi 127.0.0.1 1080

# HTTP
networksetup -setwebproxy Wi-Fi 127.0.0.1 8080
networksetup -setsecurewebproxy Wi-Fi 127.0.0.1 8080
```

### 4. Test Connection
Visit https://www.whatismyipaddress.com/ to verify your IP has changed.

## Files
- `xray_client_config.json` - Client configuration
- `start_xray_client.sh` - Startup script

## Troubleshooting
- Check if Xray is running: `ps aux | grep xray`
- View logs: `tail -f /var/log/xray/error.log` (on server)
- Test connectivity: `curl --socks5 127.0.0.1:1080 https://httpbin.org/ip`

## Security Note
This setup uses plain TCP without TLS. For production use, consider adding TLS encryption.