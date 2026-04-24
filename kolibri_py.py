"""
Kolibri AI Python Bindings
High-performance compression via C-Core
"""

import ctypes
import ctypes.util
import os
import sys
import platform

class KolibriCore:
    def __init__(self, lib_path=None):
        if lib_path is None:
            # Auto-detect library
            if sys.platform == 'win32':
                lib_path = 'kolibri_core.dll'
            elif sys.platform == 'darwin':
                lib_path = './kolibri_core.dylib'
            else:
                lib_path = './kolibri_core.so'
        
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"Kolibri C-Core library not found at {lib_path}. Please compile first.")
        
        self.lib = ctypes.CDLL(lib_path)
        
        # Setup function signatures
        # int kolibri_compress(unsigned char* input, size_t input_size, unsigned char** output, size_t* output_size)
        self.lib.kolibri_compress.argtypes = [
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
            ctypes.POINTER(ctypes.c_size_t)
        ]
        self.lib.kolibri_compress.restype = ctypes.c_int

        # int kolibri_decompress(unsigned char* input, size_t input_size, unsigned char** output, size_t* output_size)
        self.lib.kolibri_decompress.argtypes = [
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
            ctypes.POINTER(ctypes.c_size_t)
        ]
        self.lib.kolibri_decompress.restype = ctypes.c_int
        
        # void free(void* ptr) - using wasm_free export or standard free if available
        # Note: In the C code we exported wasm_free, but for standard linking we might need libc free
        # For this demo, we assume the library exports a free function or we leak (demo only)
        # Ideally, add EMSCRIPTEN_KEEPALIVE void c_free(void* p) { free(p); } to C code
        if hasattr(self.lib, 'wasm_free'):
            self.lib.wasm_free.argtypes = [ctypes.c_void_p]
            self._free = self.lib.wasm_free
        else:
            # Fallback to libc free if linked dynamically with standard gcc
            libc_name = ctypes.util.find_library('c')
            if libc_name:
                libc = ctypes.CDLL(libc_name)
                self._free = libc.free
                self._free.argtypes = [ctypes.c_void_p]
            else:
                # Ultimate fallback: define free manually from libc
                if platform.system() == 'Linux':
                    libc = ctypes.CDLL('libc.so.6')
                elif platform.system() == 'Darwin':
                    libc = ctypes.CDLL(None) # System library on macOS
                else:
                    raise OSError("Cannot find C library for memory freeing")
                self._free = libc.free
                self._free.argtypes = [ctypes.c_void_p]

    def compress(self, data):
        """Compress bytes data using Kolibri C-Core"""
        if isinstance(data, str):
            data = data.encode('utf-8')
        
        input_arr = (ctypes.c_ubyte * len(data))(*data)
        output_ptr = ctypes.POINTER(ctypes.c_ubyte)()
        output_size = ctypes.c_size_t()
        
        status = self.lib.kolibri_compress(
            input_arr, 
            len(data), 
            ctypes.byref(output_ptr), 
            ctypes.byref(output_size)
        )
        
        if status != 0:
            raise RuntimeError(f"Compression failed with status {status}")
        
        result = bytes(output_ptr[:output_size.value])
        self._free(output_ptr)
        return result

    def decompress(self, data):
        """Decompress bytes data using Kolibri C-Core"""
        if isinstance(data, str):
            raise TypeError("Decompress expects bytes, not string")
            
        input_arr = (ctypes.c_ubyte * len(data))(*data)
        output_ptr = ctypes.POINTER(ctypes.c_ubyte)()
        output_size = ctypes.c_size_t()
        
        status = self.lib.kolibri_decompress(
            input_arr, 
            len(data), 
            ctypes.byref(output_ptr), 
            ctypes.byref(output_size)
        )
        
        if status != 0:
            raise RuntimeError(f"Decompression failed with status {status}")
        
        result = bytes(output_ptr[:output_size.value])
        self._free(output_ptr)
        return result

# CLI Helper
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python kolibri_py.py <compress|decompress> <input_file> [output_file]")
        sys.exit(1)

    mode = sys.argv[1]
    input_file = sys.argv[2]
    output_file = sys.argv[3] if len(sys.argv) > 3 else ("output.kgen" if mode == "compress" else "output.dat")

    try:
        core = KolibriCore()
        with open(input_file, 'rb') as f:
            data = f.read()
        
        if mode == "compress":
            result = core.compress(data)
            print(f"Compressed: {len(data)} -> {len(result)} bytes")
        elif mode == "decompress":
            result = core.decompress(data)
            print(f"Decompressed: {len(data)} -> {len(result)} bytes")
        else:
            print("Invalid mode. Use 'compress' or 'decompress'.")
            sys.exit(1)
            
        with open(output_file, 'wb') as f:
            f.write(result)
            
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)