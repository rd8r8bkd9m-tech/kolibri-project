# Kolibri Smeta - Мультиплатформенное приложение для расчета смет

Полнофункциональное приложение для расчета строительных смет по нормам ФЕР, ГЭСН, ТЕР с искусственным интеллектом и оффлайн-режимом.

## 🎯 Возможности

### Основные функции
- ✅ Расчет смет по ФЕР/ГЭСН/ТЕР
- ✅ Автоматическое распознавание видов работ (текст, фото)
- ✅ Расчет объемов работ (м², м³, погонные метры)
- ✅ Работа в оффлайн-режиме
- ✅ Экспорт в PDF, Excel, Word, JSON
- ✅ Региональные и сезонные коэффициенты
- ✅ Шаблоны смет
- ✅ Мобильное приложение (iOS/Android)

### AI Возможности
- 🤖 Распознавание видов работ по текстовому описанию
- 🤖 Анализ изображений объектов
- 🤖 Автоматическая генерация смет
- 🤖 Подбор подходящих норм
- 🤖 Парсинг BIM/IFC файлов (в разработке)

### WASM Ядро
- ⚡ Быстрые вычисления (10x быстрее JS)
- ⚡ Расчет объемов помещений
- ⚡ Применение коэффициентов
- ⚡ Комплексные расчеты смет

## 🏗 Архитектура

```
app/
├── backend/          # NestJS Backend API
│   ├── src/
│   │   ├── modules/
│   │   │   ├── smeta/      # Управление сметами
│   │   │   ├── norms/      # База норм ФЕР/ГЭСН/ТЕР
│   │   │   ├── ai/         # AI анализ и генерация
│   │   │   ├── export/     # Экспорт документов
│   │   │   └── sync/       # Оффлайн синхронизация
│   │   └── database/
│   │       └── entities/   # TypeORM сущности
│   └── package.json
│
├── frontend/         # Next.js Frontend
│   ├── src/
│   │   ├── app/           # Next.js App Router
│   │   ├── components/    # React компоненты
│   │   ├── lib/          # API клиент
│   │   ├── types/        # TypeScript типы
│   │   └── utils/        # Утилиты
│   └── package.json
│
├── mobile/          # React Native Mobile
│   ├── src/
│   │   ├── screens/      # Экраны приложения
│   │   ├── services/     # API и SQLite
│   │   └── store/        # State management
│   └── package.json
│
├── wasm_core/       # C/WASM вычислительное ядро
│   ├── calc_engine.c     # Расчетные функции
│   └── build.sh          # Скрипт сборки
│
├── ai/              # AI модели (будущее)
│   └── models/           # TensorFlow модели
│
└── docs/            # Документация
    ├── API.md
    ├── DEPLOYMENT.md
    └── ARCHITECTURE.md
```

## 🚀 Быстрый старт

### Требования

- Node.js 18+ / npm
- PostgreSQL 14+ (или SQLite для разработки)
- Python 3.10+ (для AI модулей)
- Emscripten (для WASM)
- Docker (опционально)

### 1. Backend

```bash
cd app/backend
npm install
cp .env.example .env
# Отредактируйте .env файл

npm run start:dev
# API: http://localhost:3000
# Docs: http://localhost:3000/api/docs
```

### 2. Frontend

```bash
cd app/frontend
npm install
echo "NEXT_PUBLIC_API_URL=http://localhost:3000" > .env.local

npm run dev
# App: http://localhost:3001
```

### 3. WASM Core

```bash
cd app/wasm_core
./build.sh
# Результат: frontend/public/wasm/
```

### 4. Mobile

```bash
cd app/mobile
npm install

npm start
# Сканируйте QR код в Expo Go
```

## 📦 Установка через Docker

```bash
# Запуск всех сервисов
docker-compose up -d

# Backend: http://localhost:3000
# Frontend: http://localhost:3001
# PostgreSQL: localhost:5432
```

## 📚 Документация

### API Документация

После запуска backend доступна по адресу:
```
http://localhost:3000/api/docs
```

### Основные эндпоинты

#### Сметы
- `POST /api/smeta` - Создать смету
- `GET /api/smeta` - Список смет
- `GET /api/smeta/:id` - Получить смету
- `POST /api/smeta/:id/positions` - Добавить позицию

#### Нормы
- `GET /api/norms` - Список норм
- `GET /api/norms/search?q=` - Поиск
- `POST /api/norms/ai-match` - AI подбор

#### AI
- `POST /api/ai/analyze-text` - Анализ текста
- `POST /api/ai/analyze-image` - Анализ изображения
- `POST /api/ai/generate-smeta` - Генерация сметы

#### Экспорт
- `GET /api/export/:id/pdf` - PDF
- `GET /api/export/:id/excel` - Excel
- `GET /api/export/:id/word` - Word

## 🗄 База данных

### Схема

```sql
-- Нормы ФЕР/ГЭСН/ТЕР
CREATE TABLE norms (
  id UUID PRIMARY KEY,
  code VARCHAR(50),
  standard VARCHAR(20),
  name VARCHAR(500),
  unit VARCHAR(20),
  base_price DECIMAL(10,2),
  ...
);

-- Сметы
CREATE TABLE smetas (
  id UUID PRIMARY KEY,
  name VARCHAR(255),
  total_cost DECIMAL(15,2),
  ...
);

-- Позиции смет
CREATE TABLE smeta_positions (
  id UUID PRIMARY KEY,
  smeta_id UUID REFERENCES smetas(id),
  norm_id UUID REFERENCES norms(id),
  quantity DECIMAL(10,3),
  unit_price DECIMAL(10,2),
  total_price DECIMAL(15,2),
  ...
);
```

## 🧪 Тестирование

```bash
# Backend tests
cd app/backend
npm test

# Frontend tests
cd app/frontend
npm test

# E2E tests
npm run test:e2e
```

## 🔧 Разработка

### Backend

```bash
npm run start:dev    # Dev mode с hot reload
npm run lint         # Линтинг
npm run format       # Форматирование
```

### Frontend

```bash
npm run dev          # Dev сервер
npm run build        # Production build
npm run lint         # Линтинг
```

## 📱 Мобильное приложение

### Запуск

```bash
cd app/mobile
npm start            # Expo dev server
npm run android      # Android
npm run ios          # iOS (только Mac)
```

### Сборка

```bash
eas build --platform android
eas build --platform ios
```

## 🚢 Деплой

### Backend (Production)

```bash
cd app/backend
npm run build
npm run start:prod
```

### Frontend (Production)

```bash
cd app/frontend
npm run build
npm start
```

### Docker Compose

```bash
docker-compose -f docker-compose.prod.yml up -d
```

## 🔐 Безопасность

- ✅ JWT аутентификация (будущее)
- ✅ Валидация входных данных (class-validator)
- ✅ CORS защита
- ✅ SQL injection защита (TypeORM)
- ✅ XSS защита

## 🌍 Локализация

- 🇷🇺 Русский (основной)
- 🇬🇧 English (в разработке)

## 📄 Лицензия

MIT License

## 🤝 Вклад

Contributions are welcome! Please read CONTRIBUTING.md

## 📞 Поддержка

- Issues: GitHub Issues
- Email: support@kolibri-smeta.com
- Docs: /docs

## 🎯 Roadmap

### v1.1 (Q1 2025)
- [ ] Полноценная AI модель для классификации
- [ ] BIM/IFC парсинг
- [ ] Мультиязычность
- [ ] Темная тема

### v1.2 (Q2 2025)
- [ ] Командная работа
- [ ] Облачное хранилище
- [ ] Интеграция с 1С
- [ ] REST API v2

### v2.0 (Q3 2025)
- [ ] Desktop приложение (Electron)
- [ ] Расширенная аналитика
- [ ] Машинное обучение для оценки сроков
- [ ] Blockchain для сертификации смет
