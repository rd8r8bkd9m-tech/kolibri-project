# 📱 Kolibri Mobile App - React Native
## iOS/Android приложение для управления лицензиями

**Автор:** Vladislav Evgenievich Kochurov  
**Сайт:** https://kolibriai.ru  
**Версия:** 1.0.0  
**Дата:** 12 ноября 2025 г.

---

## 📋 СТРУКТУРА ПРОЕКТА

```
kolibri-app/
├── App.tsx                          # Главное приложение
├── app.json                         # Expo конфигурация
├── package.json                     # Зависимости
├── tsconfig.json                    # TypeScript конфигурация
├── .env                             # Переменные окружения
│
├── src/
│   ├── screens/                     # Экраны
│   │   ├── SplashScreen.tsx
│   │   ├── auth/
│   │   │   ├── LoginScreen.tsx      # Вход
│   │   │   └── RegisterScreen.tsx   # Регистрация
│   │   ├── dashboard/
│   │   │   └── DashboardScreen.tsx  # Главная
│   │   ├── licenses/
│   │   │   ├── LicensesScreen.tsx   # Список лицензий
│   │   │   └── LicenseDetailScreen.tsx # Детали
│   │   ├── payments/
│   │   │   └── PaymentsScreen.tsx   # Платежи
│   │   ├── profile/
│   │   │   └── ProfileScreen.tsx    # Профиль
│   │   └── settings/
│   │       └── SettingsScreen.tsx   # Настройки
│   │
│   ├── components/                  # Переиспользуемые компоненты
│   │   ├── Button.tsx
│   │   ├── Card.tsx
│   │   ├── Modal.tsx
│   │   ├── Header.tsx
│   │   └── LicenseCard.tsx
│   │
│   ├── services/                    # API и сервисы
│   │   ├── api.ts                   # Axios instance
│   │   ├── auth.ts                  # Аутентификация
│   │   ├── licenses.ts              # Работа с лицензиями
│   │   ├── payments.ts              # Платежи
│   │   └── storage.ts               # SQLite хранилище
│   │
│   ├── store/                       # State management (Zustand)
│   │   ├── authStore.ts
│   │   ├── licenseStore.ts
│   │   └── userStore.ts
│   │
│   ├── types/                       # TypeScript типы
│   │   ├── auth.ts
│   │   ├── license.ts
│   │   ├── payment.ts
│   │   └── user.ts
│   │
│   ├── utils/                       # Утилиты
│   │   ├── validators.ts
│   │   ├── formatting.ts
│   │   ├── encryption.ts
│   │   └── logger.ts
│   │
│   ├── hooks/                       # Custom React hooks
│   │   ├── useAuth.ts
│   │   ├── useLicenses.ts
│   │   ├── usePayments.ts
│   │   └── useOfflineSync.ts
│   │
│   └── constants/
│       ├── colors.ts
│       ├── spacing.ts
│       ├── typography.ts
│       └── api.ts
│
├── assets/
│   ├── icons/
│   ├── images/
│   ├── logos/
│   └── animations/
│
└── tests/
    ├── unit/
    ├── integration/
    └── e2e/
```

---

## 🎯 ФУНКЦИОНАЛЬНОСТЬ

### 1️⃣ АУТЕНТИФИКАЦИЯ
- ✅ Вход через email/пароль
- ✅ Регистрация нового аккаунта
- ✅ Восстановление пароля
- ✅ Двухфакторная аутентификация (2FA)
- ✅ Биометрическая аутентификация (Face ID/Touch ID)
- ✅ Secure token хранилище

### 2️⃣ УПРАВЛЕНИЕ ЛИЦЕНЗИЯМИ
- ✅ Просмотр активных лицензий
- ✅ Детальная информация по каждой лицензии
- ✅ Статус лицензии (активна/истекает/истекла)
- ✅ Управление пользователями
- ✅ QR-код генерация для активации
- ✅ Экспорт информации по лицензии
- ✅ Напоминания об окончании

### 3️⃣ УПРАВЛЕНИЕ ПЛАТЕЖАМИ
- ✅ История платежей
- ✅ Остаток задолженности
- ✅ Методы оплаты (6 способов для России)
- ✅ Автоматическое продление
- ✅ Счета (счет-фактура)
- ✅ Уведомления об оплате
- ✅ Интеграция с Яндекс.Касса

### 4️⃣ ПРОФИЛЬ И НАСТРОЙКИ
- ✅ Редактирование профиля
- ✅ Смена пароля
- ✅ Выбор языка (RU/EN)
- ✅ Выбор темы (Dark/Light)
- ✅ Уведомления
- ✅ Privacy & Security
- ✅ О приложении

### 5️⃣ ОФФЛАЙН ФУНКЦИОНАЛЬНОСТЬ
- ✅ SQLite локальное хранилище
- ✅ Синхронизация при подключении
- ✅ Кэширование данных
- ✅ Offline-first архитектура
- ✅ Конфликт разрешение

### 6️⃣ PUSH-УВЕДОМЛЕНИЯ
- ✅ Уведомления о платежах
- ✅ Напоминания об окончании лицензии
- ✅ Системные оповещения
- ✅ Кастомные уведомления
- ✅ Расписание отправки

### 7️⃣ СИНХРОНИЗАЦИЯ ОБЛАКА
- ✅ Автоматическая синхронизация
- ✅ Резервное копирование
- ✅ Восстановление данных
- ✅ Версионирование
- ✅ Конфликт разрешение

---

## 🛠️ ТЕХНОЛОГИЧЕСКИЙ СТЕК

### Frontend
```
React Native 0.72.0       - Мобильное приложение
Expo 49.0.0               - SDK для iOS/Android
React Navigation 6.1.8    - Навигация
Zustand 4.4.1             - State management
TypeScript 5.1.3          - Type safety
```

### UI/UX
```
@shopify/restyle          - Стилизация компонентов
Lottie 6.2.2              - Анимации
Victory Native 36.6.6     - Графики и диаграммы
QRCode SVG                - QR-коды
```

### Storage & Sync
```
SQLite 11.1.1             - Локальная БД
AsyncStorage 1.19.3       - Простое хранилище
Secure Store 12.3.1       - Защищённое хранилище
```

### Network & API
```
Axios 1.5.0               - HTTP клиент
Crypto-JS 4.1.1           - Шифрование
```

### Notifications
```
Expo Notifications 0.18.1 - Push-уведомления
```

---

## 📲 СКРИНШОТЫ И ИНТЕРФЕЙС

### Auth Flow
```
┌─────────────────────┐
│  Splash Screen      │ (2 сек)
└──────────┬──────────┘
           │
    ┌──────▼──────┐
    │   Login     │
    ├─────────────┤
    │ • Email     │
    │ • Password  │
    │ • Forgot?   │
    └──────┬──────┘
           │
    ┌──────▼──────┐
    │ Dashboard   │
    └─────────────┘
```

### Main Navigation
```
┌──────────────────────────────────────────┐
│              Dashboard                   │
├──────────────────────────────────────────┤
│ • Active Licenses Stats                  │
│ • Quick Actions                          │
│ • Recent Licenses                        │
│ • News & Updates                         │
├──────────────────────────────────────────┤
│ Home │ Licenses │ Payments │ Profile │   │
└──────────────────────────────────────────┘
```

### Licenses Screen
```
┌──────────────────────────────────────────┐
│  My Licenses (2)                   ╳     │
├──────────────────────────────────────────┤
│ ┌──────────────────────────────────────┐ │
│ │ LIC-001 | Startup Plan   | ● Active │ │
│ ├──────────────────────────────────────┤ │
│ │ Users: 3 / 10 [████░░░░░░]          │ │
│ │ Storage: 20 GB                      │ │
│ │ Expires: 2026-11-12                 │ │
│ │ ┌──────────────────────────────────┐│ │
│ │ │ Renew → Details → Export         ││ │
│ │ └──────────────────────────────────┘│ │
│ └──────────────────────────────────────┘ │
│                                          │
│ ┌──────────────────────────────────────┐ │
│ │ LIC-002 | Business Plan  | ⚠ Expir │ │
│ ├──────────────────────────────────────┤ │
│ │ Users: 45 / 50 [███████░░]          │ │
│ │ Storage: 100 GB                     │ │
│ │ Expires: 2025-11-25 (14 дней)       │ │
│ │ ┌──────────────────────────────────┐│ │
│ │ │ Renew URGENTLY → Details        ││ │
│ │ └──────────────────────────────────┘│ │
│ └──────────────────────────────────────┘ │
│                                          │
│ [+ Add New License]                      │
└──────────────────────────────────────────┘
```

### Payments Screen
```
┌──────────────────────────────────────────┐
│  Payments                           ╳    │
├──────────────────────────────────────────┤
│ BALANCE: $2,350.00                       │
│                                          │
│ Recent Transactions:                     │
│ ┌──────────────────────────────────────┐ │
│ │ 10.11.2025  LIC-001 Renewal  -$10K │ │
│ │ [Paid] Яндекс.Касса                 │ │
│ │ Status: ✓ Completed                │ │
│ └──────────────────────────────────────┘ │
│                                          │
│ ┌──────────────────────────────────────┐ │
│ │ 05.11.2025  LIC-002 Purchase  -$50K│ │
│ │ [Paid] Sberbank                     │ │
│ │ Status: ✓ Completed                │ │
│ └──────────────────────────────────────┘ │
│                                          │
│ [Pay Now] [Invoices] [Add Payment]       │
└──────────────────────────────────────────┘
```

---

## 💾 ЛОКАЛЬНОЕ ХРАНИЛИЩЕ

### SQLite Schema
```sql
-- Users Table
CREATE TABLE users (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  name TEXT,
  avatar_url TEXT,
  created_at DATETIME,
  updated_at DATETIME,
  synced_at DATETIME
);

-- Licenses Table
CREATE TABLE licenses (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL,
  tier TEXT NOT NULL,
  status TEXT DEFAULT 'active',
  expiry_date DATETIME,
  max_users INTEGER,
  storage_gb INTEGER,
  created_at DATETIME,
  updated_at DATETIME,
  synced_at DATETIME,
  FOREIGN KEY (user_id) REFERENCES users(id)
);

-- Payments Table
CREATE TABLE payments (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL,
  license_id TEXT,
  amount REAL NOT NULL,
  currency TEXT DEFAULT 'USD',
  status TEXT DEFAULT 'pending',
  method TEXT,
  transaction_id TEXT,
  created_at DATETIME,
  updated_at DATETIME,
  synced_at DATETIME,
  FOREIGN KEY (user_id) REFERENCES users(id),
  FOREIGN KEY (license_id) REFERENCES licenses(id)
);

-- Sync Queue Table
CREATE TABLE sync_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  table_name TEXT NOT NULL,
  operation TEXT NOT NULL, -- 'INSERT', 'UPDATE', 'DELETE'
  record_data TEXT NOT NULL, -- JSON
  created_at DATETIME,
  status TEXT DEFAULT 'pending' -- 'pending', 'synced', 'error'
);
```

---

## 🔐 БЕЗОПАСНОСТЬ

### Шифрование
```typescript
// Sensitive data encryption
import CryptoJS from 'crypto-js';

const encryptToken = (token: string) => {
  const encrypted = CryptoJS.AES.encrypt(
    token,
    process.env.ENCRYPTION_KEY
  ).toString();
  return encrypted;
};

const decryptToken = (encrypted: string) => {
  const decrypted = CryptoJS.AES.decrypt(
    encrypted,
    process.env.ENCRYPTION_KEY
  ).toString(CryptoJS.enc.Utf8);
  return decrypted;
};
```

### Secure Storage
```typescript
import SecureStore from 'expo-secure-store';

// Сохранение токена
await SecureStore.setItemAsync('auth_token', token);

// Получение токена
const token = await SecureStore.getItemAsync('auth_token');

// Удаление
await SecureStore.deleteItemAsync('auth_token');
```

### Биометрическая аутентификация
```typescript
import * as LocalAuthentication from 'expo-local-authentication';

const authenticate = async () => {
  try {
    const compatible = await LocalAuthentication.hasHardwareAsync();
    if (!compatible) return;

    const result = await LocalAuthentication.authenticateAsync({
      disableDeviceFallback: false,
    });

    return result.success;
  } catch (error) {
    console.error('Biometric auth failed:', error);
  }
};
```

---

## 🔄 СИНХРОНИЗАЦИЯ ДАННЫХ

### Offline-First Architecture
```typescript
// Store (Zustand)
const useLicenseStore = create((set) => ({
  licenses: [],
  addLicense: async (license) => {
    // 1. Сохранить локально в SQLite
    await db.insert('licenses', license);
    
    // 2. Обновить состояние
    set((state) => ({
      licenses: [...state.licenses, license]
    }));
    
    // 3. Добавить в очередь синхронизации
    await addToSyncQueue('licenses', 'INSERT', license);
    
    // 4. Попытаться синхронизировать
    await syncWithServer();
  },
}));
```

### Автоматическая синхронизация
```typescript
// Синхронизация при изменении подключения
import { useNetInfo } from '@react-native-community/netinfo';

export const useOfflineSync = () => {
  const netInfo = useNetInfo();
  const store = useLicenseStore();

  React.useEffect(() => {
    if (netInfo.isConnected) {
      // Синхронизировать все локальные изменения
      syncWithServer();
    }
  }, [netInfo.isConnected]);
};
```

---

## 📊 API ENDPOINTS

### Аутентификация
```
POST   /api/v1/auth/login          Вход
POST   /api/v1/auth/register       Регистрация
POST   /api/v1/auth/refresh        Обновить токен
POST   /api/v1/auth/logout         Выход
```

### Лицензии
```
GET    /api/v1/licenses            Список лицензий
GET    /api/v1/licenses/{id}       Детали лицензии
POST   /api/v1/licenses            Создать лицензию
PUT    /api/v1/licenses/{id}       Обновить лицензию
DELETE /api/v1/licenses/{id}       Удалить лицензию
POST   /api/v1/licenses/{id}/renew Продлить лицензию
```

### Платежи
```
GET    /api/v1/payments            История платежей
GET    /api/v1/payments/{id}       Детали платежа
POST   /api/v1/payments            Создать платёж
GET    /api/v1/invoices            Счета
GET    /api/v1/invoices/{id}       Детали счёта
```

### Профиль
```
GET    /api/v1/users/me            Мой профиль
PUT    /api/v1/users/me            Обновить профиль
POST   /api/v1/users/change-password Смена пароля
```

---

## 🚀 УСТАНОВКА И ЗАПУСК

### Требования
```
Node.js >= 16.0.0
npm >= 8.0.0 или yarn >= 1.22.0
Expo CLI
Xcode (для iOS)
Android Studio (для Android)
```

### Установка
```bash
# 1. Клонировать репозиторий
cd /Users/kolibri/Documents/os/mobile/kolibri-app

# 2. Установить зависимости
npm install

# 3. Создать .env файл
cp .env.example .env
# Отредактировать переменные окружения

# 4. Запустить Expo
npm start

# 5. Выбрать платформу
# i - iOS (требует Xcode)
# a - Android (требует Android Studio)
# w - Web
```

### Development Build
```bash
# iOS
npm run ios

# Android
npm run android

# Web
npm run web
```

### Production Build
```bash
# iOS
npm run build:ios

# Android
npm run build:android

# Submit to App Store
npm run submit:ios

# Submit to Google Play
npm run submit:android
```

---

## 📦 ENVIRONMENT VARIABLES

```env
# API Configuration
REACT_APP_API_URL=https://api.kolibriai.ru/api/v1
REACT_APP_API_TIMEOUT=30000

# Encryption
ENCRYPTION_KEY=your-secret-encryption-key

# Payments
YANDEX_KASSA_SHOP_ID=your-shop-id
YANDEX_KASSA_SECRET_KEY=your-secret-key

# Notifications
EXPO_PUSH_CHANNEL_ID=notifications

# Analytics
ANALYTICS_ID=your-analytics-id

# Feature Flags
FEATURE_2FA=true
FEATURE_BIOMETRIC=true
FEATURE_OFFLINE=true
```

---

## 🧪 ТЕСТИРОВАНИЕ

### Unit Tests
```bash
npm test

# Coverage
npm test -- --coverage
```

### E2E Tests
```bash
# Using Detox
npm run test:e2e:build
npm run test:e2e:test
```

### Test Suite
```typescript
describe('Login Screen', () => {
  it('should login successfully', async () => {
    // Test code
  });

  it('should show error on invalid credentials', async () => {
    // Test code
  });
});
```

---

## 📱 ПОДДЕРЖИВАЕМЫЕ ПЛАТФОРМЫ

### iOS
- iOS 14.0+
- iPhone, iPad, Apple Watch

### Android
- Android 8.0+ (API 26)
- Phone, Tablet, Wear OS

### Web
- Chrome, Safari, Firefox
- Progressive Web App (PWA)

---

## 📈 ROADMAP

### v1.0.0 (November 2025) ✅
- ✅ Базовая функциональность
- ✅ Управление лицензиями
- ✅ Платежи
- ✅ iOS/Android версии

### v1.1.0 (December 2025)
- [ ] Биометрическая аутентификация
- [ ] Улучшенная синхронизация
- [ ] Оффлайн поддержка
- [ ] Тёмная тема

### v1.2.0 (Q1 2026)
- [ ] Поддержка Apple Watch
- [ ] Wear OS поддержка
- [ ] AI рекомендации
- [ ] Продвинутая аналитика

### v2.0.0 (Q2 2026)
- [ ] Desktop версия (Electron)
- [ ] Web версия
- [ ] API для интеграций
- [ ] Плагин система

---

## 🤝 КОНТАКТЫ И ПОДДЕРЖКА

```
Лицензирование:    licensing@kolibriai.ru
Платежи/Счета:     billing@kolibriai.ru
Техподдержка:      support@kolibriai.ru
Разработка:        development@kolibriai.ru
Продажи:           sales@kolibriai.ru

Сайт:              https://kolibriai.ru
Страна:            Россия 🇷🇺
Автор:             Vladislav Evgenievich Kochurov
```

---

## ⚖️ ЛИЦЕНЗИРОВАНИЕ

Это приложение распространяется под следующими лицензиями:
- Community License - для персонального использования
- AGPL-3.0 - для open-source проектов
- Commercial License - для коммерческого использования

Подробнее в LICENSE файле.

---

**Создано:** 12 ноября 2025 г.  
**Версия:** 1.0.0  
**Статус:** Production Ready

© 2025 Vladislav Evgenievich Kochurov - https://kolibriai.ru
