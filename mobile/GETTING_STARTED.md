# 📱 Колибри Мобильное Приложение - React Native

**Автор:** Vladislav Evgenievich Kochurov  
**Сайт:** https://kolibriai.ru  
**Версия:** 1.0.0  
**Дата:** 12 ноября 2025 г.

---

## 🚀 БЫСТРЫЙ СТАРТ

### Шаг 1: Требования

```bash
# Проверить версию Node.js (требуется >= 16.0.0)
node --version

# Проверить npm (требуется >= 8.0.0)
npm --version

# Установить Expo CLI
npm install -g expo-cli
```

### Шаг 2: Установка зависимостей

```bash
cd /Users/kolibri/Documents/os/mobile/kolibri-app

# Установить все пакеты
npm install

# или с yarn
yarn install
```

### Шаг 3: Конфигурация

```bash
# Копировать файл примера
cp .env.example .env

# Отредактировать .env с вашими параметрами
# API_URL, ENCRYPTION_KEY, YANDEX_KASSA_ID и т.д.
```

### Шаг 4: Запуск в режиме разработки

```bash
# Запустить Expo
npm start

# Вы увидите меню:
# Press ? to show commands
# 
# i - Run on iOS simulator (требует Xcode)
# a - Run on Android emulator (требует Android Studio)
# w - Open in web browser
# j - Open Debugger
# r - Reload app
# m - Toggle menu
```

---

## 🏗️ СТРУКТУРА ПРОЕКТА

```
kolibri-app/
├── App.tsx                  # Root component
├── app.json                 # Expo config
├── package.json             # Dependencies
├── tsconfig.json            # TypeScript config
├── .env                     # Environment variables (не коммитить!)
├── .env.example             # Template для .env
│
├── src/
│   ├── screens/             # Экраны приложения
│   │   ├── SplashScreen.tsx
│   │   ├── auth/
│   │   │   ├── LoginScreen.tsx
│   │   │   └── RegisterScreen.tsx
│   │   ├── dashboard/
│   │   │   └── DashboardScreen.tsx
│   │   ├── licenses/
│   │   │   ├── LicensesScreen.tsx
│   │   │   └── LicenseDetailScreen.tsx
│   │   ├── payments/
│   │   │   └── PaymentsScreen.tsx
│   │   ├── profile/
│   │   │   └── ProfileScreen.tsx
│   │   └── settings/
│   │       └── SettingsScreen.tsx
│   │
│   ├── components/          # Переиспользуемые компоненты
│   ├── services/            # API, database services
│   ├── store/               # Zustand stores
│   ├── types/               # TypeScript типы
│   ├── utils/               # Утилиты
│   ├── hooks/               # Custom React hooks
│   └── constants/           # Constants, colors, spacing
│
├── assets/                  # Изображения, иконки, логотипы
│   ├── icons/
│   ├── images/
│   ├── logos/
│   └── animations/
│
└── tests/                   # Test files
    ├── unit/
    ├── integration/
    └── e2e/
```

---

## 🎯 ГЛАВНЫЕ ЭКРАНЫ

### 1. Splash Screen
- Заставка с логотипом Kolibri
- Загрузка в течение 2 секунд
- Проверка аутентификации

### 2. Login Screen
- Вход по email/пароль
- Ссылка на восстановление пароля
- Кнопка регистрации нового аккаунта
- Биометрическая аутентификация (Face ID/Touch ID)

### 3. Dashboard (Главная)
- Статистика: активные лицензии, пользователи, хранилище, дни до обновления
- Быстрые действия: Лицензии, Платежи, Поддержка, QR-код
- Текущие лицензии с прогресс-барами
- Новости и обновления

### 4. Licenses (Лицензии)
- Список всех лицензий
- Фильтр по статусу (активна/истекает/истекла)
- Детали по каждой лицензии
- Кнопка "Добавить новую"

### 5. Payments (Платежи)
- Баланс счета
- История платежей
- Методы оплаты (6 способов для России)
- Кнопка "Произвести платёж"

### 6. Profile (Профиль)
- Информация о пользователе
- Статистика (лицензии, пользователи, потрачено)
- Переключатели настроек
- Выход из аккаунта

---

## 🛠️ РАЗРАБОТКА

### Запуск на iOS Simulator

```bash
# Требуется Xcode установлен на macOS
npm run ios

# или просто
npm start
# затем нажать 'i'
```

### Запуск на Android Emulator

```bash
# Требуется Android Studio и Android Emulator запущен
npm run android

# или просто
npm start
# затем нажать 'a'
```

### Запуск на Web

```bash
npm run web

# Откроется в браузере на http://localhost:19006
```

### Отладка

```bash
# Открыть Expo DevTools
# Нажать 'j' в терминале

# React DevTools
# Нажать 'd' и выбрать опцию
```

---

## 📦 BUILD ПРОЦЕСС

### Production Build для iOS

```bash
# Требуется EAS CLI
npm install -g eas-cli

# Логин в Expo
eas login

# Build
npm run build:ios

# Submit to App Store
npm run submit:ios
```

### Production Build для Android

```bash
# Build
npm run build:android

# Submit to Google Play
npm run submit:android
```

---

## 🔌 ИНТЕГРАЦИЯ С BACKEND API

### Конфигурация API клиента

Файл: `src/services/api.ts`

```typescript
import axios from 'axios';
import * as SecureStore from 'expo-secure-store';

const API_URL = process.env.REACT_APP_API_URL;

const client = axios.create({
  baseURL: API_URL,
  timeout: parseInt(process.env.REACT_APP_API_TIMEOUT || '30000'),
});

// Add auth token to requests
client.interceptors.request.use(async (config) => {
  const token = await SecureStore.getItemAsync('auth_token');
  if (token) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

export default client;
```

### Примеры запросов

```typescript
// Login
POST /api/v1/auth/login
Body: { email: "user@example.com", password: "password" }

// Get licenses
GET /api/v1/licenses
Headers: { Authorization: "Bearer token" }

// Get payments
GET /api/v1/payments
Headers: { Authorization: "Bearer token" }

// Create payment
POST /api/v1/payments
Body: { 
  licenseId: "LIC-001",
  amount: 10000,
  method: "yandex_kassa"
}
```

---

## 💾 ЛОКАЛЬНОЕ ХРАНИЛИЩЕ

### SQLite для offline-first

```typescript
import * as SQLite from 'expo-sqlite';

const db = SQLite.openDatabase('kolibri.db');

// Создание таблицы
db.transaction(tx => {
  tx.executeSql(
    'CREATE TABLE IF NOT EXISTS licenses (id TEXT PRIMARY KEY, ...);'
  );
});

// Вставка данных
db.transaction(tx => {
  tx.executeSql(
    'INSERT INTO licenses (id, tier) VALUES (?, ?)',
    ['LIC-001', 'Startup']
  );
});
```

### AsyncStorage для простых данных

```typescript
import AsyncStorage from '@react-native-async-storage/async-storage';

// Сохранить
await AsyncStorage.setItem('user_name', 'Иван Петров');

// Получить
const name = await AsyncStorage.getItem('user_name');

// Удалить
await AsyncStorage.removeItem('user_name');
```

### Secure Store для sensitive data

```typescript
import * as SecureStore from 'expo-secure-store';

// Сохранить токен
await SecureStore.setItemAsync('auth_token', token);

// Получить токен
const token = await SecureStore.getItemAsync('auth_token');

// Удалить
await SecureStore.deleteItemAsync('auth_token');
```

---

## 🔐 БЕЗОПАСНОСТЬ

### Шифрование sensitive data

```typescript
import CryptoJS from 'crypto-js';

const encrypt = (data: string) => {
  return CryptoJS.AES.encrypt(
    data, 
    process.env.ENCRYPTION_KEY
  ).toString();
};

const decrypt = (encrypted: string) => {
  return CryptoJS.AES.decrypt(
    encrypted,
    process.env.ENCRYPTION_KEY
  ).toString(CryptoJS.enc.Utf8);
};
```

### Биометрическая аутентификация

```typescript
import * as LocalAuthentication from 'expo-local-authentication';

const authenticate = async () => {
  const compatible = await LocalAuthentication.hasHardwareAsync();
  if (!compatible) return false;

  return await LocalAuthentication.authenticateAsync({
    disableDeviceFallback: false,
  });
};
```

---

## 📲 PUSH-УВЕДОМЛЕНИЯ

### Регистрация для push-уведомлений

```typescript
import * as Notifications from 'expo-notifications';

// Request permissions
const { status } = await Notifications.requestPermissionsAsync();

// Get push token
const token = (await Notifications.getExpoPushTokenAsync()).data;

// Send to backend
await api.post('/users/push-token', { token });
```

### Обработка уведомлений

```typescript
Notifications.addNotificationResponseReceivedListener(response => {
  const data = response.notification.request.content.data;
  // Handle notification
});
```

---

## 🧪 ТЕСТИРОВАНИЕ

### Запуск тестов

```bash
npm test

# С coverage
npm test -- --coverage
```

### Структура тестов

```typescript
describe('LoginScreen', () => {
  it('should render login form', () => {
    const { getByPlaceholderText } = render(<LoginScreen />);
    expect(getByPlaceholderText('Email')).toBeDefined();
  });

  it('should call login on submit', async () => {
    const { getByText } = render(<LoginScreen />);
    fireEvent.press(getByText('Вход'));
    // Assert
  });
});
```

---

## 📊 PERFORMANCE OPTIMIZATION

### Image optimization

```typescript
import { Image } from 'react-native';

<Image
  source={require('./logo.png')}
  style={{ width: 100, height: 100 }}
  defaultSource={require('./placeholder.png')}
/>
```

### Lazy loading screens

```typescript
const LicensesScreen = React.lazy(
  () => import('./screens/LicensesScreen')
);
```

### Memoization

```typescript
const LicenseCard = React.memo(({ license }) => (
  <View>
    <Text>{license.id}</Text>
  </View>
));
```

---

## 🐛 ОТЛАДКА

### React Native Debugger

```bash
# Установить
brew install react-native-debugger

# Запустить
open "rndebugger://set-debugger-loc?host=localhost&port=8081"
```

### Console logging

```typescript
console.log('Debug message');
console.warn('Warning');
console.error('Error');
```

### Network debugging

```typescript
// Intercept requests
import { XMLHttpRequest as RNXMLHttpRequest } from 'react-native';
// Log all requests
```

---

## 📚 ПОЛЕЗНЫЕ ССЫЛКИ

- **React Native Docs**: https://reactnative.dev
- **Expo Documentation**: https://docs.expo.dev
- **React Navigation**: https://reactnavigation.org
- **TypeScript**: https://www.typescriptlang.org
- **Zustand**: https://github.com/pmndrs/zustand

---

## 🤝 КОНТАКТЫ

```
Поддержка:     support@kolibriai.ru
Разработка:    development@kolibriai.ru
Продажи:       sales@kolibriai.ru

Сайт:          https://kolibriai.ru
Страна:        Россия 🇷🇺
Автор:         Vladislav Evgenievich Kochurov
```

---

## 📋 ЛИЦЕНЗИРОВАНИЕ

Это мобильное приложение распространяется под:
- Community License (бесплатно, для личного использования)
- AGPL-3.0 License (бесплатно, для open-source)
- Commercial License (платно, для коммерческого использования)

Подробнее в файле LICENSE проекта.

---

## 🎯 СЛЕДУЮЩИЕ ШАГИ

1. **Настройка backend API** - убедитесь, что все endpoints работают
2. **Setup payment integration** - интегрировать Яндекс.Касса
3. **Deploy to stores** - отправить в App Store и Google Play
4. **User testing** - тестирование с реальными пользователями
5. **Analytics setup** - настроить отслеживание использования

---

**Готово к разработке! 🚀**

Если у вас возникли вопросы, контактируйте:  
📧 development@kolibriai.ru

© 2025 Vladislav Evgenievich Kochurov - https://kolibriai.ru
