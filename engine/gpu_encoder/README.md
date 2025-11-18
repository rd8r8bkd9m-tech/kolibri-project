# Kolibri GPU Encoder

Локальный движок кодирования/декодирования ReasonBlock с поддержкой Metal (Apple Silicon) и CPU-фолбэка. CUDA-заготовка готова к дальнейшему развитию.

## Цели
- Ускорить операции embedding и восстановления KRHA-остатков.
- Работать полностью офлайн, без сторонних сервисов.
- Обеспечить единый API для C-компонентов и Python-бэкенда.

## Структура
- `kolibri_gpu_encoder.c` — диспетчер бэкендов (Metal/CUDA/stub).
- `kolibri_gpu_encoder.h` — публичный API для C и Python-биндингов.
- `gpu_encoder_stub.c` — CPU-реализация (фича-паритет с Metal, используется в отсутствие GPU).
- `gpu_encoder_metal.mm` — полноценный Metal backend (ReasonBlock → embedding за один проход шейдера).
- `gpu_encoder_cuda.cu` — заготовка под CUDA-ядра.
- `tools/kgpu_demo.c` — CLI для проверки эмбеддингов на Metal/CPU.

## Сборка
```
cmake -S . -B build-gpu -DKOLIBRI_ENABLE_GPU=ON
cmake --build build-gpu --target kolibri_gpu_demo
```

Metal-бэкенд собирается автоматически на macOS. На других платформах используется CPU-фолбэк.

## Следующие шаги
1. Реализовать CUDA-кернелы (`gpu_encoder_cuda.cu`).
2. Добавить pybind11-модуль `kolibri_gpu` для FastAPI-бэкенда.
3. Включить GPU-эмбеддинги в `knowledge_pipeline` и `gpu_store`.
4. Добавить юнит-тесты (`tests/test_gpu_encoder.c`).
