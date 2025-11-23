# Kolibri Smeta Mobile

Мобильное приложение для расчета смет ФЕР/ГЭСН/ТЕР.

## Технологии

- **Framework**: React Native + Expo
- **Navigation**: React Navigation
- **UI**: React Native Paper
- **State**: Zustand + React Query
- **Database**: SQLite (Expo SQLite)
- **API**: Axios
- **Camera**: Expo Camera, Image Picker

## Возможности

- ✅ Создание и управление сметами
- ✅ Калькулятор объемов работ
- ✅ Оффлайн режим с SQLite
- ✅ AI распознавание по фото
- ✅ Синхронизация с сервером
- ✅ Экспорт смет
- ✅ Работает на iOS и Android

## Установка

### Требования

- Node.js 16+
- Expo CLI
- Android Studio (для Android)
- Xcode (для iOS, только Mac)

### Установка зависимостей

```bash
npm install
```

### Запуск в режиме разработки

```bash
# Запуск Expo Dev Server
npm start

# Запуск на Android
npm run android

# Запуск на iOS (только Mac)
npm run ios

# Запуск в браузере
npm run web
```

## Структура проекта

```
src/
├── screens/              # Экраны приложения
│   ├── HomeScreen.tsx
│   ├── SmetaListScreen.tsx
│   ├── CalculatorScreen.tsx
│   └── SettingsScreen.tsx
├── components/           # Переиспользуемые компоненты
├── navigation/           # Навигация
├── services/            # API сервисы
│   ├── api.ts
│   └── sqlite.ts
├── store/               # Zustand store
├── types/               # TypeScript типы
└── utils/               # Утилиты
```

## Сборка для продакшн

### Android

```bash
# Сборка APK
eas build --platform android --profile preview

# Сборка AAB для Google Play
eas build --platform android --profile production
```

### iOS

```bash
# Сборка для TestFlight
eas build --platform ios --profile preview

# Сборка для App Store
eas build --platform ios --profile production
```

## Оффлайн режим

Приложение использует SQLite для локального хранения:

- Сметы
- Нормы (кэш)
- Настройки

Синхронизация происходит автоматически при подключении к интернету.

## API Integration

Приложение подключается к Backend API:

```typescript
const API_URL = 'http://your-server.com:3000';

// Получение смет
const smetas = await api.get('/api/smeta');

// Создание сметы
const newSmeta = await api.post('/api/smeta', data);
```

## Камера и распознавание

```typescript
import { Camera } from 'expo-camera';
import * as ImagePicker from 'expo-image-picker';

// Сделать фото
const photo = await ImagePicker.launchCameraAsync();

// AI анализ
const analysis = await api.post('/api/ai/analyze-image', photo);
```

## Настройка окружения

Создайте файл `.env`:

```
API_URL=http://localhost:3000
```

## Разработка

```bash
# Проверка типов
npx tsc --noEmit

# Форматирование
npx prettier --write .
```

## Публикация

### Google Play

1. Зарегистрируйтесь в Google Play Console
2. Создайте приложение
3. Загрузите AAB файл

### App Store

1. Зарегистрируйтесь в Apple Developer Program
2. Создайте App ID
3. Используйте EAS для сборки
4. Загрузите через Xcode или Transporter
