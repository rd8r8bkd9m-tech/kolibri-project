"""
Kolibri OS Archiver - Python Bindings

Provides a Python interface to the Kolibri compression and archiving system.
"""

import ctypes
import os
from pathlib import Path
from typing import Optional, Tuple, List
from enum import IntEnum

# Try to find the library
def _find_library():
    """Find the Kolibri core library"""
    possible_paths = [
        # Build directory
        Path(__file__).parent.parent.parent / "build" / "libkolibri_core.so",
        Path(__file__).parent.parent.parent / "build" / "libkolibri_core.dylib",
        # Installed location
        "/usr/local/lib/libkolibri_core.so",
        "/usr/local/lib/libkolibri_core.dylib",
        # System location
        "/usr/lib/libkolibri_core.so",
    ]
    
    for path in possible_paths:
        if path.exists():
            return str(path)
    
    raise RuntimeError("Could not find libkolibri_core library. Please build the project first.")

# Load the library
_lib = ctypes.CDLL(_find_library())

# File types
class FileType(IntEnum):
    """File type enumeration"""
    BINARY = 0
    TEXT = 1
    IMAGE = 2
    UNKNOWN = 3

# Compression methods
class CompressionMethod(IntEnum):
    """Compression method flags"""
    LZ77 = 0x01
    RLE = 0x02
    HUFFMAN = 0x04
    FORMULA = 0x08
    MATH = 0x10
    ALL = 0x1F

# C structures
class _CompressStats(ctypes.Structure):
    """Compression statistics structure"""
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

class CompressionStats:
    """Python wrapper for compression statistics"""
    def __init__(self, c_stats: _CompressStats):
        self.original_size = c_stats.original_size
        self.compressed_size = c_stats.compressed_size
        self.compression_ratio = c_stats.compression_ratio
        self.checksum = c_stats.checksum
        self.file_type = FileType(c_stats.file_type)
        self.methods_used = c_stats.methods_used
        self.compression_time_ms = c_stats.compression_time_ms
        self.decompression_time_ms = c_stats.decompression_time_ms
    
    def __repr__(self):
        return (f"CompressionStats(ratio={self.compression_ratio:.2f}x, "
                f"original={self.original_size}, compressed={self.compressed_size})")

# Function signatures
_lib.kolibri_compressor_create.argtypes = [ctypes.c_uint32]
_lib.kolibri_compressor_create.restype = ctypes.c_void_p

_lib.kolibri_compressor_destroy.argtypes = [ctypes.c_void_p]
_lib.kolibri_compressor_destroy.restype = None

_lib.kolibri_compress.argtypes = [
    ctypes.c_void_p,  # compressor
    ctypes.POINTER(ctypes.c_uint8),  # input
    ctypes.c_size_t,  # input_size
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),  # output
    ctypes.POINTER(ctypes.c_size_t),  # output_size
    ctypes.POINTER(_CompressStats),  # stats
]
_lib.kolibri_compress.restype = ctypes.c_int

_lib.kolibri_decompress.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),  # input
    ctypes.c_size_t,  # input_size
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),  # output
    ctypes.POINTER(ctypes.c_size_t),  # output_size
    ctypes.POINTER(_CompressStats),  # stats
]
_lib.kolibri_decompress.restype = ctypes.c_int

_lib.kolibri_checksum.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
_lib.kolibri_checksum.restype = ctypes.c_uint32

_lib.kolibri_detect_file_type.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
_lib.kolibri_detect_file_type.restype = ctypes.c_int

# High-level Python API
class Compressor:
    """Kolibri compression engine"""
    
    def __init__(self, methods: int = CompressionMethod.ALL):
        """
        Create a new compressor
        
        Args:
            methods: Compression methods to use (bitfield)
        """
        self._handle = _lib.kolibri_compressor_create(methods)
        if not self._handle:
            raise MemoryError("Failed to create compressor")
    
    def __del__(self):
        if hasattr(self, '_handle') and self._handle:
            _lib.kolibri_compressor_destroy(self._handle)
    
    def compress(self, data: bytes) -> Tuple[bytes, CompressionStats]:
        """
        Compress data
        
        Args:
            data: Input data to compress
            
        Returns:
            Tuple of (compressed_data, statistics)
            
        Raises:
            ValueError: If compression fails
        """
        input_array = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        output_ptr = ctypes.POINTER(ctypes.c_uint8)()
        output_size = ctypes.c_size_t()
        stats = _CompressStats()
        
        ret = _lib.kolibri_compress(
            self._handle,
            input_array,
            len(data),
            ctypes.byref(output_ptr),
            ctypes.byref(output_size),
            ctypes.byref(stats)
        )
        
        if ret != 0:
            raise ValueError("Compression failed")
        
        # Copy data and free C memory
        result = bytes(output_ptr[:output_size.value])
        ctypes.pythonapi.free(output_ptr)
        
        return result, CompressionStats(stats)

def compress(data: bytes, methods: int = CompressionMethod.ALL) -> Tuple[bytes, CompressionStats]:
    """
    Compress data (convenience function)
    
    Args:
        data: Input data to compress
        methods: Compression methods to use
        
    Returns:
        Tuple of (compressed_data, statistics)
    """
    comp = Compressor(methods)
    return comp.compress(data)

def decompress(data: bytes) -> Tuple[bytes, CompressionStats]:
    """
    Decompress data
    
    Args:
        data: Compressed data
        
    Returns:
        Tuple of (decompressed_data, statistics)
        
    Raises:
        ValueError: If decompression fails or checksum doesn't match
    """
    input_array = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
    output_ptr = ctypes.POINTER(ctypes.c_uint8)()
    output_size = ctypes.c_size_t()
    stats = _CompressStats()
    
    ret = _lib.kolibri_decompress(
        input_array,
        len(data),
        ctypes.byref(output_ptr),
        ctypes.byref(output_size),
        ctypes.byref(stats)
    )
    
    if ret != 0:
        raise ValueError("Decompression failed (possibly corrupt data or checksum mismatch)")
    
    # Copy data and free C memory
    result = bytes(output_ptr[:output_size.value])
    ctypes.pythonapi.free(output_ptr)
    
    return result, CompressionStats(stats)

def checksum(data: bytes) -> int:
    """
    Calculate CRC32 checksum
    
    Args:
        data: Input data
        
    Returns:
        CRC32 checksum (32-bit unsigned integer)
    """
    input_array = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
    return _lib.kolibri_checksum(input_array, len(data))

def detect_file_type(data: bytes) -> FileType:
    """
    Detect file type from data
    
    Args:
        data: Input data (at least first few bytes)
        
    Returns:
        Detected file type
    """
    input_array = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
    return FileType(_lib.kolibri_detect_file_type(input_array, len(data)))

# File operations
def compress_file(input_path: str, output_path: str, 
                  methods: int = CompressionMethod.ALL) -> CompressionStats:
    """
    Compress a file
    
    Args:
        input_path: Path to input file
        output_path: Path to output compressed file
        methods: Compression methods to use
        
    Returns:
        Compression statistics
    """
    with open(input_path, 'rb') as f:
        data = f.read()
    
    compressed, stats = compress(data, methods)
    
    with open(output_path, 'wb') as f:
        f.write(compressed)
    
    return stats

def decompress_file(input_path: str, output_path: str) -> CompressionStats:
    """
    Decompress a file
    
    Args:
        input_path: Path to compressed file
        output_path: Path to output decompressed file
        
    Returns:
        Decompression statistics
    """
    with open(input_path, 'rb') as f:
        data = f.read()
    
    decompressed, stats = decompress(data)
    
    with open(output_path, 'wb') as f:
        f.write(decompressed)
    
    return stats

# Example usage
if __name__ == "__main__":
    # Test compression
    test_data = b"Hello, Kolibri OS! " * 100
    
    print(f"Original size: {len(test_data)} bytes")
    
    # Compress
    compressed, stats = compress(test_data)
    print(f"Compressed size: {len(compressed)} bytes")
    print(f"Compression ratio: {stats.compression_ratio:.2f}x")
    print(f"File type: {stats.file_type.name}")
    print(f"Checksum: 0x{stats.checksum:08X}")
    
    # Decompress
    decompressed, _ = decompress(compressed)
    print(f"Decompressed size: {len(decompressed)} bytes")
    
    # Verify
    if test_data == decompressed:
        print("✓ Data integrity verified!")
    else:
        print("✗ Data mismatch!")
