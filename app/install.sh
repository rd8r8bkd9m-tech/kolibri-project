#!/bin/bash

# Kolibri Smeta Installation Script
# Автоматическая установка и настройка приложения

set -e

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo "=================================="
echo "Kolibri Smeta - Installation"
echo "=================================="
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_command() {
    if ! command -v $1 &> /dev/null; then
        log_error "$1 не установлен"
        return 1
    else
        log_info "$1 установлен"
        return 0
    fi
}

# Check prerequisites
log_info "Проверка зависимостей..."

if ! check_command node; then
    log_error "Node.js не найден. Установите Node.js 18+ https://nodejs.org/"
    exit 1
fi

NODE_VERSION=$(node -v | cut -d'v' -f2 | cut -d'.' -f1)
if [ "$NODE_VERSION" -lt 18 ]; then
    log_error "Требуется Node.js 18+. Текущая версия: $(node -v)"
    exit 1
fi

if ! check_command npm; then
    log_error "npm не найден"
    exit 1
fi

if ! check_command docker; then
    log_warn "Docker не найден. Будет использована локальная установка"
    USE_DOCKER=false
else
    USE_DOCKER=true
fi

# Check for docker-compose (v1) or docker compose (v2)
DOCKER_COMPOSE_CMD=""
if command -v docker-compose &> /dev/null; then
    DOCKER_COMPOSE_CMD="docker-compose"
elif docker compose version &> /dev/null; then
    DOCKER_COMPOSE_CMD="docker compose"
fi

if [ -z "$DOCKER_COMPOSE_CMD" ] && [ "$USE_DOCKER" = true ]; then
    log_warn "Docker Compose не найден"
    USE_DOCKER=false
fi

# Select installation mode
echo ""
echo "Выберите режим установки:"
echo "1) Полная установка с Docker (рекомендуется)"
echo "2) Локальная установка (без Docker)"
echo "3) Только Backend"
echo "4) Только Frontend"
echo ""
read -p "Выбор [1-4]: " INSTALL_MODE

case $INSTALL_MODE in
    1)
        if [ "$USE_DOCKER" = false ]; then
            log_error "Docker не установлен"
            exit 1
        fi
        log_info "Установка с Docker..."
        ;;
    2)
        log_info "Локальная установка..."
        ;;
    3)
        log_info "Установка Backend..."
        ;;
    4)
        log_info "Установка Frontend..."
        ;;
    *)
        log_error "Неверный выбор"
        exit 1
        ;;
esac

# Backend installation
install_backend() {
    log_info "Установка Backend..."
    
    cd "$SCRIPT_DIR/backend"
    
    # Install dependencies
    log_info "Установка зависимостей..."
    npm ci
    
    # Setup environment
    if [ ! -f .env ]; then
        log_info "Создание .env файла..."
        cp .env.example .env
        log_warn "Отредактируйте файл .env перед запуском"
    fi
    
    # Build
    log_info "Сборка приложения..."
    npm run build
    
    log_info "Backend установлен"
    cd "$SCRIPT_DIR"
}

# Frontend installation
install_frontend() {
    log_info "Установка Frontend..."
    
    cd "$SCRIPT_DIR/frontend"
    
    # Install dependencies
    log_info "Установка зависимостей..."
    npm ci
    
    # Setup environment
    if [ ! -f .env.local ]; then
        log_info "Создание .env.local файла..."
        echo "NEXT_PUBLIC_API_URL=http://localhost:3000" > .env.local
    fi
    
    # Build
    log_info "Сборка приложения..."
    npm run build
    
    log_info "Frontend установлен"
    cd "$SCRIPT_DIR"
}

# WASM installation
install_wasm() {
    log_info "Сборка WASM ядра..."
    
    cd "$SCRIPT_DIR/wasm_core"
    
    if ! check_command emcc; then
        log_warn "Emscripten не установлен. WASM не будет собран"
        log_info "Для сборки WASM установите Emscripten: https://emscripten.org/"
    else
        ./build.sh
        log_info "WASM ядро собрано"
    fi
    
    cd "$SCRIPT_DIR"
}

# Docker installation
install_docker() {
    log_info "Установка с Docker..."
    
    cd "$SCRIPT_DIR"
    
    # Check docker-compose file
    if [ ! -f docker-compose.yml ]; then
        log_error "docker-compose.yml не найден"
        exit 1
    fi
    
    # Setup environment
    if [ ! -f backend/.env ]; then
        log_info "Создание .env файлов..."
        cp backend/.env.example backend/.env
    fi
    
    # Build images
    log_info "Сборка Docker образов..."
    $DOCKER_COMPOSE_CMD build
    
    # Start services
    log_info "Запуск сервисов..."
    $DOCKER_COMPOSE_CMD up -d
    
    log_info "Docker контейнеры запущены"
    
    # Import sample data
    log_info "Ожидание запуска базы данных..."
    sleep 10
    
    log_info "Импорт примеров норм..."
    # TODO: Add data import
    
    echo ""
    log_info "Установка завершена!"
    echo ""
    echo "Сервисы доступны по адресам:"
    echo "  Backend API: http://localhost:3000"
    echo "  API Docs: http://localhost:3000/api/docs"
    echo "  Frontend: http://localhost:3001"
    echo ""
    echo "Управление:"
    echo "  Просмотр логов: $DOCKER_COMPOSE_CMD logs -f"
    echo "  Остановка: $DOCKER_COMPOSE_CMD stop"
    echo "  Перезапуск: $DOCKER_COMPOSE_CMD restart"
}

# Execute installation
case $INSTALL_MODE in
    1)
        install_docker
        ;;
    2)
        install_backend
        install_frontend
        install_wasm
        
        echo ""
        log_info "Установка завершена!"
        echo ""
        echo "Запуск приложения:"
        echo ""
        echo "1. Backend:"
        echo "   cd backend"
        echo "   npm run start:dev"
        echo ""
        echo "2. Frontend (в другом терминале):"
        echo "   cd frontend"
        echo "   npm run dev"
        echo ""
        echo "Сервисы будут доступны:"
        echo "  Backend API: http://localhost:3000"
        echo "  Frontend: http://localhost:3001"
        ;;
    3)
        install_backend
        
        echo ""
        log_info "Backend установлен"
        echo ""
        echo "Запуск:"
        echo "  cd backend"
        echo "  npm run start:dev"
        ;;
    4)
        install_frontend
        
        echo ""
        log_info "Frontend установлен"
        echo ""
        echo "Запуск:"
        echo "  cd frontend"
        echo "  npm run dev"
        ;;
esac

echo ""
log_info "Документация: README.md"
log_info "API документация: docs/API.md"
log_info "Деплой: docs/DEPLOYMENT.md"
echo ""
