#!/bin/bash

# Build script for WASM core

echo "Building Kolibri Smeta WASM Core..."

# Check if emscripten is installed
if ! command -v emcc &> /dev/null; then
    echo "Error: Emscripten not found. Please install it first."
    echo "Visit: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

# Create output directory
mkdir -p ../frontend/public/wasm

# Compile to WASM
emcc calc_engine.c \
    -o ../frontend/public/wasm/calc_engine.js \
    -s WASM=1 \
    -s EXPORTED_RUNTIME_METHODS='["cwrap","ccall"]' \
    -s EXPORTED_FUNCTIONS='["_calc_position","_calc_room_volume","_calc_floor_area","_calc_wall_area","_calc_ceiling_area","_calc_perimeter","_apply_regional_coefficient","_apply_seasonal_coefficient","_calc_labor_hours","_calc_materials_cost","_calc_machine_hours","_sum_positions","_calc_vat","_calc_total_with_vat","_calc_overhead","_calc_profit","_calc_full_smeta"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -O3

if [ $? -eq 0 ]; then
    echo "✓ WASM build successful!"
    echo "Output files:"
    echo "  - ../frontend/public/wasm/calc_engine.js"
    echo "  - ../frontend/public/wasm/calc_engine.wasm"
else
    echo "✗ WASM build failed!"
    exit 1
fi

# Build native binary for testing
echo ""
echo "Building native test binary..."
gcc calc_engine.c -o calc_engine_test -lm

if [ $? -eq 0 ]; then
    echo "✓ Native build successful!"
    echo "Run: ./calc_engine_test"
else
    echo "✗ Native build failed!"
fi
