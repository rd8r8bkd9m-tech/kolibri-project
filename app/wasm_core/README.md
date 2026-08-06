# Kolibri Smeta WASM Core

WASM-ядро для быстрых вычислений смет.

## Возможности

- ✅ Расчет объемов помещений (пол, стены, потолок)
- ✅ Расчет позиций смет
- ✅ Применение коэффициентов (региональные, сезонные)
- ✅ Расчет трудозатрат
- ✅ Расчет стоимости материалов
- ✅ Расчет НДС
- ✅ Расчет накладных расходов и прибыли
- ✅ Комплексный расчет с всеми коэффициентами

## Сборка

### Требования

- Emscripten SDK (для WASM)
- GCC (для нативной сборки)

### Установка Emscripten

```bash
# Linux/Mac
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### Сборка WASM

```bash
./build.sh
```

Результат:
- `../frontend/public/wasm/calc_engine.js`
- `../frontend/public/wasm/calc_engine.wasm`

### Тестирование нативной версии

```bash
gcc calc_engine.c -o calc_engine_test -lm
./calc_engine_test
```

## Использование в JavaScript

```javascript
// Загрузка WASM модуля
const Module = await import('/wasm/calc_engine.js');

// Расчет площади пола
const floorArea = Module.ccall(
  'calc_floor_area',
  'number',
  ['number', 'number'],
  [5.0, 4.0]
);

// Расчет площади стен
const wallArea = Module.ccall(
  'calc_wall_area',
  'number',
  ['number', 'number', 'number'],
  [5.0, 4.0, 2.7]
);

// Комплексный расчет сметы
const total = Module.ccall(
  'calc_full_smeta',
  'number',
  ['number', 'number', 'number', 'number', 'number', 'number'],
  [1000, 1.15, 1.0, 0.12, 0.08, 0.20]
);
```

## API Функции

### Расчет объемов

- `calc_room_volume(length, width, height)` - объем помещения (м³)
- `calc_floor_area(length, width)` - площадь пола (м²)
- `calc_wall_area(length, width, height)` - площадь стен (м²)
- `calc_ceiling_area(length, width)` - площадь потолка (м²)
- `calc_perimeter(length, width)` - периметр (м)

### Расчет позиций

- `calc_position(quantity, unit_price, coefficient)` - стоимость позиции
- `calc_labor_hours(quantity, norm_per_unit)` - трудозатраты (чел-час)
- `calc_materials_cost(quantity, material_rate, material_price)` - стоимость материалов
- `calc_machine_hours(quantity, norm_per_unit)` - машино-часы

### Коэффициенты

- `apply_regional_coefficient(base_price, coefficient)` - региональный коэф.
- `apply_seasonal_coefficient(base_price, coefficient)` - сезонный коэф.

### Финансовые расчеты

- `calc_vat(base_price, vat_rate)` - НДС
- `calc_total_with_vat(base_price, vat_rate)` - сумма с НДС
- `calc_overhead(direct_costs, overhead_rate)` - накладные расходы
- `calc_profit(direct_costs, profit_rate)` - сметная прибыль

### Комплексный расчет

- `calc_full_smeta(base_cost, regional_coef, seasonal_coef, overhead_rate, profit_rate, vat_rate)` - 
  полный расчет с учетом всех коэффициентов

## Производительность

WASM обеспечивает:
- ~10x ускорение по сравнению с чистым JS
- Поддержка SIMD инструкций
- Оптимизация компилятора (-O3)
- Работа в любом браузере с поддержкой WASM

## Будущие улучшения

- [ ] SIMD оптимизации для массовых расчетов
- [ ] Загрузка бинарных норм из файлов
- [ ] Кэширование часто используемых норм
- [ ] Параллельные вычисления (Workers)
