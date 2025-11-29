# Kolibri Smeta Backend

Backend API для мультиплатформенного приложения расчета смет по ФЕР/ГЭСН/ТЕР.

## Технологии

- **Framework**: NestJS
- **Database**: PostgreSQL / SQLite
- **ORM**: TypeORM
- **AI/ML**: TensorFlow.js
- **Export**: pdf-lib, ExcelJS, docxtemplater
- **Documentation**: Swagger/OpenAPI

## Возможности

- ✅ CRUD операции со сметами
- ✅ Управление нормами (ФЕР/ГЭСН/ТЕР)
- ✅ AI анализ описаний работ
- ✅ AI распознавание изображений
- ✅ Автоматическая генерация смет
- ✅ Расчет объемов работ
- ✅ Применение коэффициентов
- ✅ Экспорт в PDF/Excel/Word/JSON
- ✅ Оффлайн синхронизация
- ✅ Импорт норм из CSV
- ✅ Swagger документация

## Установка

```bash
# Установка зависимостей
npm install

# Настройка окружения
cp .env.example .env
# Отредактируйте .env файл

# Запуск в режиме разработки
npm run start:dev

# Сборка для production
npm run build

# Запуск production
npm run start:prod
```

## API Эндпоинты

### Сметы (`/api/smeta`)
- `POST /api/smeta` - Создать смету
- `GET /api/smeta` - Список смет
- `GET /api/smeta/:id` - Получить смету
- `PATCH /api/smeta/:id` - Обновить смету
- `DELETE /api/smeta/:id` - Удалить смету
- `POST /api/smeta/:id/positions` - Добавить позицию
- `DELETE /api/smeta/positions/:id` - Удалить позицию
- `POST /api/smeta/calculate-volumes` - Рассчитать объемы

### Нормы (`/api/norms`)
- `GET /api/norms` - Список норм
- `GET /api/norms/search?q=` - Поиск норм
- `GET /api/norms/code/:code` - Норма по коду
- `POST /api/norms` - Создать норму
- `POST /api/norms/import` - Импорт из CSV
- `POST /api/norms/ai-match` - AI подбор норм
- `POST /api/norms/:id/apply-coefficients` - Применить коэффициенты

### AI (`/api/ai`)
- `POST /api/ai/analyze-text` - Анализ текста
- `POST /api/ai/analyze-image` - Анализ изображения
- `POST /api/ai/generate-smeta` - Генерация сметы
- `POST /api/ai/parse-bim` - Парсинг BIM/IFC

### Экспорт (`/api/export`)
- `GET /api/export/:id/pdf` - Экспорт в PDF
- `GET /api/export/:id/excel` - Экспорт в Excel
- `GET /api/export/:id/word` - Экспорт в Word
- `GET /api/export/:id/json` - Экспорт в JSON

### Синхронизация (`/api/sync`)
- `POST /api/sync/from-device` - Синхронизация с устройства
- `GET /api/sync/to-device` - Синхронизация на устройство
- `GET /api/sync/offline-package` - Пакет для оффлайна
- `POST /api/sync/resolve-conflicts` - Разрешение конфликтов

## Документация API

После запуска сервера документация доступна по адресу:
```
http://localhost:3000/api/docs
```

## Структура базы данных

### Таблица `norms`
- Хранение норм ФЕР/ГЭСН/ТЕР
- Материалы, трудозатраты, оборудование
- Коэффициенты и региональные множители

### Таблица `smetas`
- Основная информация о смете
- Связь с пользователем
- Общая стоимость

### Таблица `smeta_positions`
- Позиции сметы
- Связь с нормами
- Расчеты и коэффициенты

## Тестирование

```bash
# Unit тесты
npm run test

# E2E тесты
npm run test:e2e

# Покрытие кода
npm run test:cov
```

## Разработка

```bash
# Форматирование кода
npm run format

# Линтинг
npm run lint
```

## Docker

```bash
# Сборка образа
docker build -t kolibri-smeta-backend .

# Запуск контейнера
docker run -p 3000:3000 kolibri-smeta-backend
```
