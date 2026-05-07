import ctypes
import os
import sys

# Load the shared library
lib_path = os.path.join(os.path.dirname(__file__), "..", "build", "libkolibri_compress.dylib")
if not os.path.exists(lib_path):
    lib_path = os.path.join(os.path.dirname(__file__), "..", "build", "libkolibri_compress.so")

if not os.path.exists(lib_path):
    raise FileNotFoundError(f"Kolibri core library not found at {lib_path}")

lib = ctypes.CDLL(lib_path)

# Define structures
class KolibriCompressStats(ctypes.Structure):
    _fields_ = [
        ("original_size", ctypes.c_size_t),
        ("compressed_size", ctypes.c_size_t),
        ("compression_ratio", ctypes.c_double),
        ("checksum", ctypes.c_uint32),
        ("file_type", ctypes.c_int),
        ("methods_used", ctypes.c_uint32),
        ("compression_time_ms", ctypes.c_double),
        ("decompression_time_ms", ctypes.c_double),
    ]

# Define function prototypes
lib.kolibri_compressor_create.argtypes = [ctypes.c_uint32]
lib.kolibri_compressor_create.restype = ctypes.c_void_p

lib.kolibri_compressor_destroy.argtypes = [ctypes.c_void_p]
lib.kolibri_compressor_destroy.restype = None

lib.kolibri_compress.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(KolibriCompressStats)
]
lib.kolibri_compress.restype = ctypes.c_int

lib.kolibri_decompress.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(KolibriCompressStats)
]
lib.kolibri_decompress.restype = ctypes.c_int

# Constants from compress.h
KOLIBRI_COMPRESS_ALL = 0x7FF

class KolibriNative:
    def __init__(self, methods=KOLIBRI_COMPRESS_ALL):
        self.compressor = lib.kolibri_compressor_create(methods)
        if not self.compressor:
            raise RuntimeError("Failed to create Kolibri compressor")

    def __del__(self):
        if hasattr(self, 'compressor') and self.compressor:
            lib.kolibri_compressor_destroy(self.compressor)

    def compress(self, data):
        if isinstance(data, str):
            data = data.encode('utf-8')
        
        input_ptr = (ctypes.c_uint8 * len(data))(*data)
        output_ptr = ctypes.POINTER(ctypes.c_uint8)()
        output_size = ctypes.c_size_t()
        stats = KolibriCompressStats()

        res = lib.kolibri_compress(
            self.compressor,
            input_ptr,
            len(data),
            ctypes.byref(output_ptr),
            ctypes.byref(output_size),
            ctypes.byref(stats)
        )

        if res != 0:
            raise RuntimeError(f"Compression failed with error {res}")

        result = bytes(ctypes.string_at(output_ptr, output_size.value))
        # Note: We should ideally free the C-allocated buffer here if the C code doesn't manage it.
        # Looking at the C code, it usually uses malloc. We need a free function in C.
        return result, stats

    def decompress(self, compressed_data):
        input_ptr = (ctypes.c_uint8 * len(compressed_data))(*compressed_data)
        output_ptr = ctypes.POINTER(ctypes.c_uint8)()
        output_size = ctypes.c_size_t()
        stats = KolibriCompressStats()

        res = lib.kolibri_decompress(
            input_ptr,
            len(compressed_data),
            ctypes.byref(output_ptr),
            ctypes.byref(output_size),
            ctypes.byref(stats)
        )

        if res != 0:
            raise RuntimeError(f"Decompression failed with error {res}")

        result = bytes(ctypes.string_at(output_ptr, output_size.value))
        return result, stats
