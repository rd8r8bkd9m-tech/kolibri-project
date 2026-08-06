# Kolibri Smeta Frontend

Frontend приложение для расчета смет ФЕР/ГЭСН/ТЕР.

## Технологии

- **Framework**: Next.js 14 (App Router)
- **UI**: React 18, Tailwind CSS
- **State**: Zustand
- **API**: React Query, Axios
- **Forms**: React Hook Form + Zod
- **Icons**: Lucide React
- **Offline**: IndexedDB (idb)

## Возможности

- ✅ Создание и управление сметами
- ✅ Калькулятор объемов работ
- ✅ База норм ФЕР/ГЭСН/ТЕР
- ✅ AI генерация смет
- ✅ Экспорт в PDF/Excel/Word
- ✅ PWA с оффлайн-режимом
- ✅ Адаптивный дизайн

## Установка

```bash
# Установка зависимостей
npm install

# Настройка окружения
echo "NEXT_PUBLIC_API_URL=http://localhost:3000" > .env.local

# Запуск в режиме разработки
npm run dev

# Сборка для production
npm run build

# Запуск production сервера
npm start
```

## Структура проекта

```
src/
├── app/                    # Next.js App Router
│   ├── page.tsx           # Главная страница
│   ├── calculator/        # Калькулятор объемов
│   ├── smeta/            # Страницы смет
│   ├── norms/            # База норм
│   └── settings/         # Настройки
├── components/
│   ├── ui/               # UI компоненты
│   ├── smeta/            # Компоненты смет
│   ├── norms/            # Компоненты норм
│   └── calculator/       # Компоненты калькулятора
├── lib/
│   ├── api.ts            # API клиент
│   └── offline.ts        # Оффлайн функции
├── hooks/                # Custom hooks
├── store/                # Zustand store
├── types/                # TypeScript types
└── utils/                # Утилиты
```

## API Integration

Приложение взаимодействует с бэкендом через REST API:

- `GET /api/smeta` - Список смет
- `POST /api/smeta` - Создать смету
- `GET /api/norms` - Список норм
- `POST /api/ai/generate-smeta` - AI генерация
- `GET /api/export/:id/pdf` - Экспорт в PDF

## Оффлайн режим

Приложение поддерживает работу без интернета:

1. Кэширование данных в IndexedDB
2. Service Worker для кэширования ресурсов
3. Синхронизация при подключении к сети

## PWA

Для установки как PWA добавьте `manifest.json` и настройте Service Worker.

## Разработка

```bash
# Проверка типов
npm run type-check

# Линтинг
npm run lint
```

## Сборка

```bash
# Production build
npm run build

# Статический export
npm run build && npm run export
```

## Docker

```bash
# Сборка образа
docker build -t kolibri-smeta-frontend .

# Запуск контейнера
docker run -p 3001:3000 kolibri-smeta-frontend
```
