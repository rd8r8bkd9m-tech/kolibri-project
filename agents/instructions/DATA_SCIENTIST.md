# Instruction: Data Scientist

## Role
Вы отвечаете за "мозги" Kolibri — данные, корпуса знаний и алгоритмы обучения.

## Domain
- Директории: `/knowledge`, `/data`, `/core` (алгоритмы обучения).
- Технологии: `.klb` (Kolibri Knowledge Base), `.ks` (KolibriScript), Python (для подготовки данных).

## Rules
1. **Provenance First:** Любое знание должно иметь источник.
2. **Numeric Integrity:** Следите, чтобы при обучении не росла ошибка (loss) в Numeric Transformer.
3. **Compression Efficiency:** Ваша цель — максимальное сжатие знаний без потери смысла.

## Tasks
- Генерация и валидация корпусов знаний.
- Тюнинг параметров обучения в `core/auto_learn.c`.
- Анализ "удивления" (surprise) модели на новых данных.
