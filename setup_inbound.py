import pexpect
import json

def run_remote(command):
    child = pexpect.spawn(f'ssh -o StrictHostKeyChecking=no root@217.60.7.164 "{command}"', timeout=60)
    child.expect('password:')
    child.sendline('NB1HLtGC9Kb_C2k8A4')
    child.expect(pexpect.EOF)
    return child.before.decode()

settings = {
  "clients": [
    {
      "id": "ef532324-737c-4fb1-adc5-d48215bd033c",
      "flow": "xtls-rprx-vision",
      "email": "admin@kolibri",
      "limitIp": 0,
      "totalGB": 0,
      "expiryTime": 0,
      "enable": True,
      "tgId": "",
      "subId": ""
    }
  ],
  "decryption": "none",
  "fallbacks": []
}

stream_settings = {
  "network": "tcp",
  "security": "reality",
  "realitySettings": {
    "show": False,
    "dest": "google.com:443",
    "xver": 0,
    "serverNames": [
      "google.com",
      "www.google.com"
    ],
    "privateKey": "OIibJJu6OJN7ndVj_JtQs1ZMjnmWAAZOotu6KYHscHE",
    "minClientVer": "",
    "maxClientVer": "",
    "maxTimeDiff": 0,
    "shortIds": [
      "a1b2c3d4e5f6"
    ]
  },
  "tcpSettings": {
    "header": {
      "type": "none"
    }
  }
}

sniffing = {
  "enabled": True,
  "destOverride": ["http", "tls", "quic"]
}

settings_json = json.dumps(settings)
stream_settings_json = json.dumps(stream_settings)
sniffing_json = json.dumps(sniffing)

sql = f"""
INSERT INTO inbounds (user_id, up, down, total, remark, enable, expiry_time, listen, port, protocol, settings, stream_settings, tag, sniffing, allocate)
VALUES (1, 0, 0, 0, 'VLESS REALITY', 1, 0, '', 443, 'vless', '{settings_json}', '{stream_settings_json}', 'inbound-443', '{sniffing_json}', 'default');
"""

# Escaping single quotes for shell
sql_escaped = sql.replace("'", "'\"'\"'")

print("Inserting inbound...")
result = run_remote(f"sqlite3 /root/3x-ui/db/x-ui.db '{sql_escaped}'")
print(result)

print("Restarting 3x-ui container...")
run_remote("docker restart 3x-ui")
print("Done.")
