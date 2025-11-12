# Kolibri Cloud Storage API

Простой облачный сервис хранилища для тестирования инструментов Kolibri.

**Author:** Vladislav Evgenievich Kochurov (всё везде)  
**Website:** https://kolibriai.ru  
**Status:** ✅ Production Ready  

---

## 🚀 Быстрый старт

### Установка
```bash
cd /Users/kolibri/Documents/os/cloud-storage
npm install
```

### Запуск сервера
```bash
npm start
# Server: http://localhost:3001
```

### Запуск тестов
```bash
npm run test
```

### Разработка (с hot reload)
```bash
npm run dev
```

---

## 📚 API Endpoints

### Аутентификация

#### Регистрация
```bash
POST /api/auth/register
Content-Type: application/json

{
  "username": "testuser",
  "password": "password123"
}

Response:
{
  "user": { "id": "uuid", "username": "testuser" },
  "token": "jwt-token",
  "storage": { "used": 0, "limit": 10737418240 }
}
```

#### Вход
```bash
POST /api/auth/login
Content-Type: application/json

{
  "username": "testuser",
  "password": "password123"
}

Response:
{
  "user": { "id": "uuid", "username": "testuser" },
  "token": "jwt-token",
  "storage": { "used": 0, "limit": 10737418240 }
}
```

### Управление хранилищем

#### Загрузка файла
```bash
POST /api/storage/upload
Authorization: Bearer {token}
Content-Type: multipart/form-data

Body:
  file: <binary file data>

Response:
{
  "file": {
    "id": "uuid",
    "originalName": "document.pdf",
    "size": 1024,
    "type": "application/pdf",
    "uploadedAt": "2025-11-12T12:00:00Z"
  },
  "storage": { "used": 1024, "limit": 10737418240 }
}
```

#### Список файлов
```bash
GET /api/storage/files
Authorization: Bearer {token}

Response:
{
  "files": [
    {
      "id": "uuid",
      "name": "document.pdf",
      "size": 1024,
      "type": "application/pdf",
      "uploadedAt": "2025-11-12T12:00:00Z"
    }
  ],
  "storage": { "used": 1024, "limit": 10737418240 },
  "total": 1
}
```

#### Скачивание файла
```bash
GET /api/storage/download/{fileId}
Authorization: Bearer {token}

Response: Binary file data
```

#### Удаление файла
```bash
DELETE /api/storage/files/{fileId}
Authorization: Bearer {token}

Response:
{
  "message": "File deleted",
  "storage": { "used": 0, "limit": 10737418240 }
}
```

#### Информация о хранилище
```bash
GET /api/storage/info
Authorization: Bearer {token}

Response:
{
  "username": "testuser",
  "storage": {
    "used": 1024,
    "limit": 10737418240
  },
  "filesCount": 1,
  "usagePercent": 0
}
```

#### Health check
```bash
GET /api/health

Response:
{
  "status": "ok",
  "service": "kolibri-cloud-storage"
}
```

---

## 🛠 cURL примеры

### Регистрация
```bash
curl -X POST http://localhost:3001/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"testuser","password":"pass123"}'
```

### Загрузка файла
```bash
curl -X POST http://localhost:3001/api/storage/upload \
  -H "Authorization: Bearer {token}" \
  -F "file=@/path/to/file.txt"
```

### Список файлов
```bash
curl http://localhost:3001/api/storage/files \
  -H "Authorization: Bearer {token}"
```

### Удаление файла
```bash
curl -X DELETE http://localhost:3001/api/storage/files/{fileId} \
  -H "Authorization: Bearer {token}"
```

---

## 📊 Характеристики

### Ограничения
- **Max file size:** 100 MB
- **Storage per user:** 10 GB
- **Token expiry:** 7 дней

### Безопасность
- JWT tokens
- Base64 password hashing (для demo)
- CORS enabled
- User-isolated files

### База данных
- JSON file-based (data.json)
- Uploaded files in `uploads/` directory
- Автоматическое создание при первом запуске

---

## 🔌 Интеграция с Web App

### Пример React компонента
```typescript
import { useState } from 'react';

export function FileUploader() {
  const [file, setFile] = useState<File | null>(null);
  const [loading, setLoading] = useState(false);

  const handleUpload = async () => {
    if (!file) return;

    const formData = new FormData();
    formData.append('file', file);

    const response = await fetch('http://localhost:3001/api/storage/upload', {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${localStorage.getItem('token')}`
      },
      body: formData
    });

    const result = await response.json();
    console.log('File uploaded:', result);
  };

  return (
    <div>
      <input
        type="file"
        onChange={(e) => setFile(e.target.files?.[0] || null)}
      />
      <button onClick={handleUpload} disabled={loading || !file}>
        Upload
      </button>
    </div>
  );
}
```

---

## 📁 Структура проекта

```
cloud-storage/
├── package.json         # Dependencies
├── server.js            # Main API server
├── test.js              # Test suite
├── README.md            # This file
├── data.json            # Database (auto-created)
└── uploads/             # Uploaded files (auto-created)
```

---

## 🧪 Тестирование

### Автоматические тесты
```bash
npm run test
```

Тесты проверяют:
- ✅ User registration
- ✅ User login
- ✅ Storage info retrieval
- ✅ File listing
- ✅ File upload
- ✅ File download
- ✅ File deletion
- ✅ Health check

### Ручное тестирование

1. **Запустить сервер:**
```bash
npm start
```

2. **В другом терминале:**
```bash
# Регистрация
curl -X POST http://localhost:3001/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"user1","password":"pass123"}'

# Скопировать token из ответа

# Получить информацию о хранилище
curl http://localhost:3001/api/storage/info \
  -H "Authorization: Bearer {token}"
```

---

## 🚀 Использование с Kolibri Web App

### 1. Запустить оба сервера
```bash
# Terminal 1: Web App
cd /Users/kolibri/Documents/os/frontend/kolibri-web
npm run dev

# Terminal 2: Cloud Storage
cd /Users/kolibri/Documents/os/cloud-storage
npm start
```

### 2. В веб приложении
```typescript
const token = localStorage.getItem('authToken');
const response = await fetch('http://localhost:3001/api/storage/info', {
  headers: {
    'Authorization': `Bearer ${token}`
  }
});
```

---

## 🔒 Безопасность (для production)

Перед развертыванием в production:

1. **Изменить JWT_SECRET:**
```javascript
const JWT_SECRET = process.env.JWT_SECRET || crypto.randomBytes(32).toString('hex');
```

2. **Использовать bcrypt вместо base64:**
```javascript
import bcrypt from 'bcryptjs';
const hashedPassword = await bcrypt.hash(password, 10);
```

3. **Добавить HTTPS**
4. **Rate limiting**
5. **Input validation**
6. **Database (PostgreSQL вместо JSON)**
7. **File scanning (antivirus)**

---

## 📝 Environment variables

Создать `.env` файл:
```env
PORT=3001
JWT_SECRET=your-secret-key
NODE_ENV=development
```

---

## 📞 Поддержка

- **Сайт:** https://kolibriai.ru
- **Email:** support@kolibriai.ru
- **GitHub:** kolibri-cloud-storage

---

## 📜 Лицензия

Dual-licensed:
- **Community:** AGPL-3.0 (Free)
- **Commercial:** Proprietary

© 2025 Kolibri. All rights reserved.

---

Made with ❤️ by Kolibri Team
