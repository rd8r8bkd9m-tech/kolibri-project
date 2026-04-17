#!/bin/bash
set -e

echo "--- Использование системного Emscripten (Homebrew) ---"
emcc --version

# Директория для сборки
BUILD_DIR="build_wasm"
echo "--- Очистка старой директории сборки ---"
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR

# Конфигурация CMake для Emscripten
echo "--- Конфигурация CMake для WASM ---"
emcmake cmake -B $BUILD_DIR -S . -DCMAKE_BUILD_TYPE=Release -DKOLIBRI_ENABLE_TESTS=OFF

# Сборка
echo "--- Сборка WASM модуля (kolibri_wasm) ---"
cmake --build $BUILD_DIR --target kolibri_wasm -j

# Копирование результата в публичную директорию фронтенда
WEB_PUBLIC_DIR="web/public"
COMPILED_WASM_PATH="$BUILD_DIR/kolibri.wasm"

echo "--- Копирование $COMPILED_WASM_PATH в $WEB_PUBLIC_DIR/ ---"
if [ -f "$COMPILED_WASM_PATH" ]; then
    cp "$COMPILED_WASM_PATH" "$WEB_PUBLIC_DIR/"
    echo "Сборка WASM успешно завершена. Файл kolibri.wasm скопирован в $WEB_PUBLIC_DIR/"
else
    echo "Ошибка: Скомпилированный файл kolibri.wasm не найден после сборки!"
    exit 1
fi
