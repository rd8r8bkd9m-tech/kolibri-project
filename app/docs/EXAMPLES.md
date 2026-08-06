# Примеры использования

Примеры работы с Kolibri Smeta API.

## Создание простой сметы

### 1. Создать смету

```bash
curl -X POST http://localhost:3000/api/smeta \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Ремонт квартиры",
    "objectAddress": "Москва, ул. Ленина, 1, кв. 5",
    "clientName": "Иванов И.И.",
    "region": "Москва"
  }'
```

Ответ:
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "name": "Ремонт квартиры",
  "totalCost": 0,
  "status": "draft",
  ...
}
```

### 2. Добавить позиции

```bash
# Штукатурка стен
curl -X POST http://localhost:3000/api/smeta/550e8400-e29b-41d4-a716-446655440000/positions \
  -H "Content-Type: application/json" \
  -d '{
    "workDescription": "Улучшенная штукатурка стен",
    "unit": "м2",
    "quantity": 48.6,
    "unitPrice": 350,
    "orderNumber": 1
  }'

# Окраска стен
curl -X POST http://localhost:3000/api/smeta/550e8400-e29b-41d4-a716-446655440000/positions \
  -H "Content-Type: application/json" \
  -d '{
    "workDescription": "Окраска водными составами",
    "unit": "м2",
    "quantity": 48.6,
    "unitPrice": 125,
    "orderNumber": 2
  }'
```

### 3. Получить смету с расчетом

```bash
curl http://localhost:3000/api/smeta/550e8400-e29b-41d4-a716-446655440000
```

Ответ:
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "name": "Ремонт квартиры",
  "positions": [
    {
      "orderNumber": 1,
      "workDescription": "Улучшенная штукатурка стен",
      "unit": "м2",
      "quantity": 48.6,
      "unitPrice": 350,
      "totalPrice": 17010
    },
    {
      "orderNumber": 2,
      "workDescription": "Окраска водными составами",
      "unit": "м2",
      "quantity": 48.6,
      "unitPrice": 125,
      "totalPrice": 6075
    }
  ],
  "totalCost": 23085
}
```

## Использование AI для генерации сметы

```bash
curl -X POST http://localhost:3000/api/ai/generate-smeta \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "Комната 5×4 метра, высота 2.7. Нужна штукатурка стен, шпаклевка и окраска"
  }'
```

Ответ:
```json
{
  "name": "Смета: Комната 5×4 метра...",
  "positions": [
    {
      "workDescription": "Улучшенная штукатурка стен",
      "unit": "м2",
      "quantity": 48.6,
      "unitPrice": 350,
      "totalPrice": 17010
    },
    ...
  ],
  "totalCost": 45000,
  "aiGenerated": true
}
```

## Калькулятор объемов

```bash
curl -X POST http://localhost:3000/api/smeta/calculate-volumes \
  -H "Content-Type: application/json" \
  -d '{
    "description": "Комната 5×4, высота 2.7"
  }'
```

Ответ:
```json
{
  "length": 5.0,
  "width": 4.0,
  "height": 2.7,
  "floorArea": 20.0,
  "wallArea": 48.6,
  "volume": 54.0
}
```

## Поиск норм

### Текстовый поиск

```bash
curl "http://localhost:3000/api/norms/search?q=штукатурка"
```

### AI подбор норм

```bash
curl -X POST http://localhost:3000/api/norms/ai-match \
  -H "Content-Type: application/json" \
  -d '{
    "description": "Штукатурка стен цементным раствором"
  }'
```

Ответ:
```json
[
  {
    "id": "...",
    "code": "ФЕР15-01-001-1",
    "name": "Улучшенная штукатурка стен",
    "unit": "м2",
    "basePrice": 350
  },
  ...
]
```

## Применение коэффициентов

```bash
curl -X POST http://localhost:3000/api/norms/550e8400-e29b-41d4-a716-446655440000/apply-coefficients \
  -H "Content-Type: application/json" \
  -d '{
    "regional": 1.15,
    "seasonal": 1.0,
    "complexity": 1.2
  }'
```

Ответ:
```json
{
  "normId": "550e8400-e29b-41d4-a716-446655440000",
  "basePrice": 350,
  "adjustedPrice": 483,
  "coefficients": {
    "regional": 1.15,
    "seasonal": 1.0,
    "complexity": 1.2
  }
}
```

## Экспорт сметы

### PDF

```bash
curl -O http://localhost:3000/api/export/550e8400-e29b-41d4-a716-446655440000/pdf
# Сохранится как smeta-550e8400-e29b-41d4-a716-446655440000.pdf
```

### Excel

```bash
curl -O http://localhost:3000/api/export/550e8400-e29b-41d4-a716-446655440000/excel
# Сохранится как smeta-550e8400-e29b-41d4-a716-446655440000.xlsx
```

## Оффлайн синхронизация

### Получить пакет для оффлайна

```bash
curl http://localhost:3000/api/sync/offline-package > offline-package.json
```

### Синхронизация с устройства

```bash
curl -X POST http://localhost:3000/api/sync/from-device \
  -H "Content-Type: application/json" \
  -d @device-data.json
```

## Использование WASM

```javascript
// Загрузка WASM модуля в браузере
const Module = await import('/wasm/calc_engine.js');

// Расчет площади пола
const floorArea = Module.ccall(
  'calc_floor_area',
  'number',
  ['number', 'number'],
  [5.0, 4.0]
);
console.log('Площадь пола:', floorArea, 'м²'); // 20.0

// Расчет площади стен
const wallArea = Module.ccall(
  'calc_wall_area',
  'number',
  ['number', 'number', 'number'],
  [5.0, 4.0, 2.7]
);
console.log('Площадь стен:', wallArea, 'м²'); // 48.6

// Комплексный расчет сметы
const total = Module.ccall(
  'calc_full_smeta',
  'number',
  ['number', 'number', 'number', 'number', 'number', 'number'],
  [
    1000,  // Базовая стоимость
    1.15,  // Региональный коэф.
    1.0,   // Сезонный коэф.
    0.12,  // Накладные расходы (12%)
    0.08,  // Прибыль (8%)
    0.20   // НДС (20%)
  ]
);
console.log('Итоговая стоимость:', total, 'руб.'); // ~1550
```

## Frontend пример (React)

```tsx
import { useState } from 'react';
import { smetaApi } from '@/lib/api';

function SmetaCreator() {
  const [description, setDescription] = useState('');
  const [smeta, setSmeta] = useState(null);

  const handleGenerate = async () => {
    try {
      const response = await smetaApi.create({
        name: 'Новая смета',
      });
      
      const volumesResponse = await smetaApi.calculateVolumes(description);
      console.log('Объемы:', volumesResponse.data);
      
      setSmeta(response.data);
    } catch (error) {
      console.error('Ошибка:', error);
    }
  };

  return (
    <div>
      <textarea
        value={description}
        onChange={(e) => setDescription(e.target.value)}
        placeholder="Опишите объект..."
      />
      <button onClick={handleGenerate}>
        Создать смету
      </button>
      
      {smeta && (
        <div>
          <h3>{smeta.name}</h3>
          <p>Стоимость: {smeta.totalCost} руб.</p>
        </div>
      )}
    </div>
  );
}
```

## Mobile пример (React Native)

```tsx
import React, { useState } from 'react';
import { View, TextInput, Button, Text } from 'react-native';
import axios from 'axios';

const API_URL = 'http://your-server.com:3000';

function Calculator() {
  const [length, setLength] = useState('');
  const [width, setWidth] = useState('');
  const [result, setResult] = useState(null);

  const calculate = async () => {
    const description = `Комната ${length}×${width}`;
    
    const response = await axios.post(
      `${API_URL}/api/smeta/calculate-volumes`,
      { description }
    );
    
    setResult(response.data);
  };

  return (
    <View>
      <TextInput
        placeholder="Длина"
        value={length}
        onChangeText={setLength}
        keyboardType="numeric"
      />
      <TextInput
        placeholder="Ширина"
        value={width}
        onChangeText={setWidth}
        keyboardType="numeric"
      />
      <Button title="Рассчитать" onPress={calculate} />
      
      {result && (
        <View>
          <Text>Площадь пола: {result.floorArea} м²</Text>
          <Text>Площадь стен: {result.wallArea} м²</Text>
        </View>
      )}
    </View>
  );
}
```

## Импорт норм из CSV

```bash
# Подготовка CSV файла
cat > norms.csv << EOF
code,standard,name,description,unit,basePrice,category
ФЕР15-01-001-1,ФЕР,Улучшенная штукатурка стен,Описание,м2,350.00,Штукатурные работы
ФЕР15-01-002-1,ФЕР,Высококачественная штукатурка,Описание,м2,485.00,Штукатурные работы
EOF

# Импорт
curl -X POST http://localhost:3000/api/norms/import \
  -H "Content-Type: application/json" \
  -d "{\"csvData\": \"$(cat norms.csv)\"}"
```

## Workflow: Полный цикл создания сметы

```bash
#!/bin/bash

# 1. Создать смету
SMETA_ID=$(curl -s -X POST http://localhost:3000/api/smeta \
  -H "Content-Type: application/json" \
  -d '{"name":"Ремонт квартиры","objectAddress":"Москва"}' \
  | jq -r '.id')

echo "Создана смета: $SMETA_ID"

# 2. Рассчитать объемы
VOLUMES=$(curl -s -X POST http://localhost:3000/api/smeta/calculate-volumes \
  -H "Content-Type: application/json" \
  -d '{"description":"Комната 5×4, высота 2.7"}')

WALL_AREA=$(echo $VOLUMES | jq -r '.wallArea')
FLOOR_AREA=$(echo $VOLUMES | jq -r '.floorArea')

echo "Площадь стен: $WALL_AREA м²"
echo "Площадь пола: $FLOOR_AREA м²"

# 3. Добавить позиции
curl -s -X POST http://localhost:3000/api/smeta/$SMETA_ID/positions \
  -H "Content-Type: application/json" \
  -d "{\"workDescription\":\"Штукатурка стен\",\"unit\":\"м2\",\"quantity\":$WALL_AREA,\"unitPrice\":350,\"orderNumber\":1}"

curl -s -X POST http://localhost:3000/api/smeta/$SMETA_ID/positions \
  -H "Content-Type: application/json" \
  -d "{\"workDescription\":\"Стяжка пола\",\"unit\":\"м2\",\"quantity\":$FLOOR_AREA,\"unitPrice\":280,\"orderNumber\":2}"

# 4. Получить итоговую смету
curl -s http://localhost:3000/api/smeta/$SMETA_ID | jq '.'

# 5. Экспортировать в PDF
curl -O http://localhost:3000/api/export/$SMETA_ID/pdf

echo "Смета сохранена в smeta-$SMETA_ID.pdf"
```

## Тестирование API

```bash
# Запуск всех тестов
cd backend
npm test

# Конкретный тест
npm test -- smeta.service.spec.ts
```
