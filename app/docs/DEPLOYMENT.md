# Deployment Guide

Руководство по развертыванию Kolibri Smeta в production.

## Требования

### Серверные требования
- Linux сервер (Ubuntu 22.04 LTS рекомендуется)
- 2+ CPU cores
- 4+ GB RAM
- 20+ GB диск
- Node.js 18+
- PostgreSQL 14+
- Nginx (для reverse proxy)
- SSL сертификат

### Опционально
- Docker & Docker Compose
- Redis (для кэширования)
- Cloudflare (CDN)

## Вариант 1: Docker Compose (Рекомендуется)

### 1. Подготовка сервера

```bash
# Обновление системы
sudo apt update && sudo apt upgrade -y

# Установка Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Установка Docker Compose
sudo apt install docker-compose -y

# Создание пользователя для приложения
sudo useradd -m -s /bin/bash kolibri
sudo usermod -aG docker kolibri
```

### 2. Клонирование репозитория

```bash
sudo su - kolibri
git clone https://github.com/your-org/kolibri-smeta.git
cd kolibri-smeta/app
```

### 3. Конфигурация

Создайте `.env` файлы:

**Backend (.env)**
```bash
NODE_ENV=production
PORT=3000
DB_TYPE=postgres
DB_HOST=postgres
DB_PORT=5432
DB_USERNAME=kolibri
DB_PASSWORD=CHANGE_ME_STRONG_PASSWORD
DB_NAME=kolibri_smeta
JWT_SECRET=CHANGE_ME_SECRET_KEY
```

**Frontend (.env.local)**
```bash
NEXT_PUBLIC_API_URL=https://api.your-domain.com
```

### 4. Production Docker Compose

Создайте `docker-compose.prod.yml`:

```yaml
version: '3.8'

services:
  postgres:
    image: postgres:14-alpine
    restart: always
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_PASSWORD: ${DB_PASSWORD}
      POSTGRES_DB: ${DB_NAME}
    volumes:
      - postgres_data:/var/lib/postgresql/data
    networks:
      - kolibri-network

  backend:
    build:
      context: ./backend
      dockerfile: Dockerfile
    restart: always
    environment:
      NODE_ENV: production
      DB_HOST: postgres
      DB_PORT: 5432
      DB_USERNAME: ${DB_USERNAME}
      DB_PASSWORD: ${DB_PASSWORD}
      DB_NAME: ${DB_NAME}
      PORT: 3000
    depends_on:
      - postgres
    networks:
      - kolibri-network

  frontend:
    build:
      context: ./frontend
      dockerfile: Dockerfile
    restart: always
    environment:
      NEXT_PUBLIC_API_URL: https://api.your-domain.com
    networks:
      - kolibri-network

  nginx:
    image: nginx:alpine
    restart: always
    ports:
      - "80:80"
      - "443:443"
    volumes:
      - ./nginx/nginx.conf:/etc/nginx/nginx.conf
      - ./nginx/ssl:/etc/nginx/ssl
    depends_on:
      - backend
      - frontend
    networks:
      - kolibri-network

volumes:
  postgres_data:

networks:
  kolibri-network:
    driver: bridge
```

### 5. Nginx конфигурация

Создайте `nginx/nginx.conf`:

```nginx
events {
    worker_connections 1024;
}

http {
    upstream backend {
        server backend:3000;
    }

    upstream frontend {
        server frontend:3000;
    }

    # Redirect HTTP to HTTPS
    server {
        listen 80;
        server_name your-domain.com api.your-domain.com;
        return 301 https://$server_name$request_uri;
    }

    # Frontend
    server {
        listen 443 ssl http2;
        server_name your-domain.com;

        ssl_certificate /etc/nginx/ssl/fullchain.pem;
        ssl_certificate_key /etc/nginx/ssl/privkey.pem;

        location / {
            proxy_pass http://frontend;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection 'upgrade';
            proxy_set_header Host $host;
            proxy_cache_bypass $http_upgrade;
        }
    }

    # Backend API
    server {
        listen 443 ssl http2;
        server_name api.your-domain.com;

        ssl_certificate /etc/nginx/ssl/fullchain.pem;
        ssl_certificate_key /etc/nginx/ssl/privkey.pem;

        location / {
            proxy_pass http://backend;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection 'upgrade';
            proxy_set_header Host $host;
            proxy_cache_bypass $http_upgrade;
        }
    }
}
```

### 6. SSL сертификаты (Let's Encrypt)

```bash
# Установка Certbot
sudo apt install certbot -y

# Получение сертификата
sudo certbot certonly --standalone -d your-domain.com -d api.your-domain.com

# Копирование сертификатов
sudo mkdir -p nginx/ssl
sudo cp /etc/letsencrypt/live/your-domain.com/fullchain.pem nginx/ssl/
sudo cp /etc/letsencrypt/live/your-domain.com/privkey.pem nginx/ssl/
```

### 7. Запуск

```bash
docker-compose -f docker-compose.prod.yml up -d
```

### 8. Проверка

```bash
# Проверка контейнеров
docker-compose -f docker-compose.prod.yml ps

# Логи
docker-compose -f docker-compose.prod.yml logs -f

# Проверка API
curl https://api.your-domain.com/api/smeta

# Проверка Frontend
curl https://your-domain.com
```

## Вариант 2: Ручная установка

### Backend

```bash
cd backend
npm ci --production
npm run build
pm2 start dist/main.js --name kolibri-backend
```

### Frontend

```bash
cd frontend
npm ci --production
npm run build
pm2 start npm --name kolibri-frontend -- start
```

## Миграции базы данных

```bash
# Запуск миграций
cd backend
npm run migration:run

# Откат миграций
npm run migration:revert
```

## Мониторинг

### PM2 Dashboard

```bash
pm2 monit
```

### Логи

```bash
# Backend logs
docker-compose -f docker-compose.prod.yml logs -f backend

# Frontend logs
docker-compose -f docker-compose.prod.yml logs -f frontend

# Nginx logs
docker-compose -f docker-compose.prod.yml logs -f nginx
```

## Бэкапы

### База данных

```bash
# Создание бэкапа
docker exec postgres pg_dump -U kolibri kolibri_smeta > backup_$(date +%Y%m%d).sql

# Восстановление
docker exec -i postgres psql -U kolibri kolibri_smeta < backup_20241123.sql
```

### Автоматические бэкапы

Добавьте в crontab:

```bash
0 2 * * * /home/kolibri/scripts/backup.sh
```

backup.sh:
```bash
#!/bin/bash
BACKUP_DIR=/home/kolibri/backups
DATE=$(date +%Y%m%d_%H%M%S)

# Бэкап базы данных
docker exec postgres pg_dump -U kolibri kolibri_smeta | gzip > $BACKUP_DIR/db_$DATE.sql.gz

# Удалить старые бэкапы (старше 30 дней)
find $BACKUP_DIR -name "db_*.sql.gz" -mtime +30 -delete
```

## Обновление

```bash
# Остановка сервисов
docker-compose -f docker-compose.prod.yml down

# Обновление кода
git pull origin main

# Пересборка
docker-compose -f docker-compose.prod.yml build

# Запуск
docker-compose -f docker-compose.prod.yml up -d
```

## Масштабирование

### Горизонтальное масштабирование Backend

```yaml
backend:
  build: ./backend
  deploy:
    replicas: 3
```

### Load Balancer

Используйте Nginx или HAProxy для распределения нагрузки.

## Безопасность

### Firewall

```bash
sudo ufw allow 22/tcp
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw enable
```

### Ограничение доступа к базе

```yaml
postgres:
  environment:
    POSTGRES_HOST_AUTH_METHOD: md5
  networks:
    - kolibri-network  # Только внутренняя сеть
```

### Регулярные обновления

```bash
sudo apt update && sudo apt upgrade -y
docker-compose pull
```

## Мониторинг и алерты

### Настройка Prometheus + Grafana (опционально)

См. отдельное руководство в `docs/MONITORING.md`

## Troubleshooting

### Backend не запускается

```bash
docker-compose logs backend
# Проверьте переменные окружения
# Проверьте подключение к БД
```

### База данных недоступна

```bash
docker-compose exec postgres psql -U kolibri -d kolibri_smeta
# Проверьте доступность
# Проверьте логи PostgreSQL
```

### Frontend 502 ошибка

```bash
# Проверьте статус контейнера
docker-compose ps frontend

# Проверьте логи
docker-compose logs frontend
```

## Поддержка

- GitHub Issues
- Email: support@kolibri-smeta.com
- Documentation: https://docs.kolibri-smeta.com
