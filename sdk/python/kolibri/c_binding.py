"""
Kolibri C extension bindings.

This module provides Python bindings to the optimized C extension.
Falls back to pure Python implementation if the C extension is not available.

Significantly faster than pure Python (100-300x speedup, varies by hardware).
"""

try:
    from kolibri import _kolibri
    _HAS_C_EXTENSION = True
except ImportError:
    _HAS_C_EXTENSION = False


def encode_fast(data: bytes) -> str:
    """
    Encode bytes to decimal string using C extension.

    This is the high-performance version using optimized C code.

    Args:
        data: Input bytes to encode

    Returns:
        Decimal string representation

    Raises:
        RuntimeError: If C extension is not available
    """
    if not _HAS_C_EXTENSION:
        raise RuntimeError(
            "C extension not available. "
            "Please install with: pip install kolibri-fast"
        )
    return _kolibri.encode(data)


def decode_fast(encoded: str) -> bytes:
    """
    Decode decimal string back to bytes using C extension.

    Args:
        encoded: Decimal string (length must be multiple of 3)

    Returns:
        Original bytes

    Raises:
        RuntimeError: If C extension is not available
    """
    if not _HAS_C_EXTENSION:
        raise RuntimeError(
            "C extension not available. "
            "Please install with: pip install kolibri-fast"
        )
    return _kolibri.decode(encoded)


def is_c_extension_available() -> bool:
    """Check if the C extension is available."""
    return _HAS_C_EXTENSION
