# WireGuard VPN (Ubuntu, без ключей на первом шаге)

## 1. Подготовка окружения (без ключей)

На Ubuntu-сервере:

```bash
cd /home/ladik/kolibri-project
sudo ./scripts/setup_vpn_wireguard.sh --prepare --interface kolibri
```

Что делает команда:
- проверяет/ставит `wireguard` и `wireguard-tools`;
- создаёт шаблон `/etc/wireguard/kolibri.conf.template`;
- не поднимает туннель.

## 2. Когда будут ключи

Создай готовый конфиг `kolibri.conf` и применяй:

```bash
sudo ./scripts/setup_vpn_wireguard.sh --config /path/to/kolibri.conf --interface kolibri --start
```

## 3. Проверка статуса

```bash
sudo systemctl status wg-quick@kolibri --no-pager
sudo wg show kolibri
```
