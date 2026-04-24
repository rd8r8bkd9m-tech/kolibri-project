# Kolibri AI C-Core Build System

## Prerequisites
- GCC or Clang
- Emscripten (for WASM)
- Python 3.x (for bindings)

## 1. Compile Native Binary (Linux/Mac/Windows)
```bash
gcc -O3 -o kolibri_core kolibri_core.c -lm
```

## 2. Compile to WebAssembly (WASM)
```bash
# Ensure Emscripten is installed and sourced
emcc kolibri_core.c -O3 -s WASM=1 -s EXPORTED_FUNCTIONS='["_wasm_compress", "_wasm_decompress", "_wasm_free", "_malloc", "_free"]' -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' -o kolibri_core.js
```

## 3. Build Python Bindings (using ctypes)
Create `kolibri_py.py`:
```python
import ctypes
import os

lib = ctypes.CDLL('./kolibri_core.so') # Or .dll on Windows

# Define argument types
lib.kolibri_compress.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t, ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)), ctypes.POINTER(ctypes.c_size_t)]
lib.kolibri_compress.restype = ctypes.c_int

def compress(data):
    input_arr = (ctypes.c_ubyte * len(data))(*data)
    output_ptr = ctypes.POINTER(ctypes.c_ubyte)()
    output_size = ctypes.c_size_t()
    
    lib.kolibri_compress(input_arr, len(data), ctypes.byref(output_ptr), ctypes.byref(output_size))
    
    result = bytes(output_ptr[:output_size.value])
    lib.wasm_free(output_ptr) # Use free from C
    return result
```

## Usage Examples

### Native CLI
```bash
./kolibri_core compress input.txt output.kgen
./kolibri_core decompress output.kgen restored.txt
```

### In Browser (via WASM)
See updated `app.js` for integration.

### In Python
```python
from kolibri_py import compress
data = b"Hello Kolibri AI!"
compressed = compress(data)
print(f"Compressed size: {len(compressed)}")
```