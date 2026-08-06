# API Documentation

## Base URL

```
http://localhost:3000/api
```

## Authentication

В будущих версиях будет использоваться JWT токен:

```http
Authorization: Bearer <token>
```

## Endpoints

### Сметы (Smeta)

#### Создать смету

```http
POST /api/smeta
Content-Type: application/json

{
  "name": "Смета №1 - Штукатурка",
  "description": "Описание объекта",
  "objectAddress": "Москва, ул. Ленина, 1",
  "clientName": "ООО Строитель",
  "region": "Москва"
}

Response: 201 Created
{
  "id": "uuid",
  "name": "Смета №1 - Штукатурка",
  "totalCost": 0,
  "status": "draft",
  ...
}
```

#### Получить список смет

```http
GET /api/smeta?userId=uuid

Response: 200 OK
[
  {
    "id": "uuid",
    "name": "Смета №1",
    "totalCost": 45000,
    ...
  }
]
```

#### Получить смету по ID

```http
GET /api/smeta/:id

Response: 200 OK
{
  "id": "uuid",
  "name": "Смета №1",
  "positions": [
    {
      "id": "uuid",
      "workDescription": "Штукатурка стен",
      "quantity": 20.5,
      "unitPrice": 350,
      "totalPrice": 7175
    }
  ],
  "totalCost": 7175,
  ...
}
```

#### Добавить позицию в смету

```http
POST /api/smeta/:id/positions
Content-Type: application/json

{
  "normId": "uuid",
  "workDescription": "Штукатурка стен",
  "unit": "м2",
  "quantity": 20.5,
  "unitPrice": 350,
  "orderNumber": 1
}

Response: 201 Created
```

#### Рассчитать объемы

```http
POST /api/smeta/calculate-volumes
Content-Type: application/json

{
  "description": "Комната 5×4, высота 2.7. Штукатурка стен"
}

Response: 200 OK
{
  "length": 5.0,
  "width": 4.0,
  "height": 2.7,
  "floorArea": 20.0,
  "wallArea": 48.6,
  "volume": 54.0
}
```

### Нормы (Norms)

#### Получить список норм

```http
GET /api/norms?standard=ФЕР&category=Штукатурные работы

Response: 200 OK
[
  {
    "id": "uuid",
    "code": "ФЕР15-01-001-1",
    "name": "Улучшенная штукатурка стен",
    "unit": "м2",
    "basePrice": 350,
    ...
  }
]
```

#### Поиск норм

```http
GET /api/norms/search?q=штукатурка

Response: 200 OK
[...]
```

#### Получить норму по коду

```http
GET /api/norms/code/ФЕР15-01-001-1

Response: 200 OK
{
  "id": "uuid",
  "code": "ФЕР15-01-001-1",
  "name": "Улучшенная штукатурка стен",
  ...
}
```

#### AI подбор норм

```http
POST /api/norms/ai-match
Content-Type: application/json

{
  "description": "Штукатурка стен цементным раствором"
}

Response: 200 OK
[
  {
    "id": "uuid",
    "code": "ФЕР15-01-001-1",
    "name": "Улучшенная штукатурка стен",
    ...
  }
]
```

#### Применить коэффициенты

```http
POST /api/norms/:id/apply-coefficients
Content-Type: application/json

{
  "regional": 1.15,
  "seasonal": 1.0,
  "complexity": 1.2
}

Response: 200 OK
{
  "normId": "uuid",
  "basePrice": 350,
  "adjustedPrice": 483,
  "coefficients": {
    "regional": 1.15,
    "seasonal": 1.0,
    "complexity": 1.2
  }
}
```

### AI

#### Анализ текста

```http
POST /api/ai/analyze-text
Content-Type: application/json

{
  "description": "Комната 5×4, высота 2.7. Штукатурка стен и откосов"
}

Response: 200 OK
{
  "description": "...",
  "workTypes": ["Штукатурные работы"],
  "suggestedNorms": [...],
  "volumes": {
    "floorArea": 20.0,
    "wallArea": 48.6,
    ...
  },
  "confidence": 0.85
}
```

#### Анализ изображения

```http
POST /api/ai/analyze-image
Content-Type: multipart/form-data

image: <file>

Response: 200 OK
{
  "detectedObjects": [
    { "type": "wall", "confidence": 0.92 }
  ],
  "estimatedDimensions": {
    "width": 5.0,
    "length": 4.0,
    "height": 2.7
  },
  "suggestedWorks": ["Штукатурка стен", ...]
}
```

#### Генерация сметы

```http
POST /api/ai/generate-smeta
Content-Type: application/json

{
  "prompt": "Комната 5×4, высота 2.7. Нужна штукатурка, шпаклевка и покраска"
}

Response: 200 OK
{
  "name": "Смета: Комната 5×4, высота 2.7...",
  "positions": [...],
  "totalCost": 45000,
  "aiGenerated": true
}
```

### Экспорт (Export)

#### Экспорт в PDF

```http
GET /api/export/:id/pdf

Response: 200 OK
Content-Type: application/pdf
Content-Disposition: attachment; filename=smeta-{id}.pdf
```

#### Экспорт в Excel

```http
GET /api/export/:id/excel

Response: 200 OK
Content-Type: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet
Content-Disposition: attachment; filename=smeta-{id}.xlsx
```

#### Экспорт в Word

```http
GET /api/export/:id/word

Response: 200 OK
Content-Type: application/vnd.openxmlformats-officedocument.wordprocessingml.document
Content-Disposition: attachment; filename=smeta-{id}.docx
```

#### Экспорт в JSON

```http
GET /api/export/:id/json

Response: 200 OK
Content-Type: application/json
{
  "id": "uuid",
  "name": "Смета №1",
  ...
}
```

### Синхронизация (Sync)

#### Синхронизация с устройства

```http
POST /api/sync/from-device
Content-Type: application/json

{
  "userId": "uuid",
  "smetas": [
    {
      "id": "uuid",
      "name": "Смета №1",
      ...
    }
  ]
}

Response: 200 OK
{
  "synced": {
    "smetas": [...],
    "errors": []
  }
}
```

#### Синхронизация на устройство

```http
GET /api/sync/to-device?userId=uuid&lastSync=2024-11-20T10:00:00Z

Response: 200 OK
{
  "timestamp": "2024-11-23T10:00:00Z",
  "smetas": [...],
  "norms": [...]
}
```

#### Получить оффлайн пакет

```http
GET /api/sync/offline-package

Response: 200 OK
{
  "version": "1.0.0",
  "norms": [...],
  "coefficients": {...},
  "templates": [...]
}
```

## Error Responses

### 400 Bad Request

```json
{
  "statusCode": 400,
  "message": ["field must be a string"],
  "error": "Bad Request"
}
```

### 404 Not Found

```json
{
  "statusCode": 404,
  "message": "Смета с ID xxx не найдена",
  "error": "Not Found"
}
```

### 500 Internal Server Error

```json
{
  "statusCode": 500,
  "message": "Internal server error"
}
```

## Rate Limiting

В production версии будет ограничение:
- 100 запросов в минуту для неавторизованных пользователей
- 1000 запросов в минуту для авторизованных

## Swagger UI

Интерактивная документация доступна по адресу:
```
http://localhost:3000/api/docs
```
