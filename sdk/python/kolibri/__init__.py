"""
Kolibri Python SDK

Ultra-fast byte-to-decimal encoding.

Usage:
    from kolibri import encode, decode

    data = b"Hello, World!"
    encoded = encode(data)  # "072101108108111044032087111114108100033"
    decoded = decode(encoded)  # b"Hello, World!"

The SDK provides three implementations:
    1. Pure Python (slow, ~50 MB/s) - always available
    2. C extension (fast, ~27,700 MB/s) - requires compilation

By default, encode() and decode() use the C extension if available,
falling back to pure Python otherwise.
"""

from kolibri.pure_python import encode_pure, decode_pure

__version__ = "1.0.0"
__all__ = [
    "encode",
    "decode",
    "encode_fast",
    "encode_pure",
    "decode_pure",
    "is_c_extension_available",
]


# Try to import C extension
try:
    from kolibri import _kolibri
    _HAS_C_EXTENSION = True
except ImportError:
    _HAS_C_EXTENSION = False


def encode(data: bytes) -> str:
    """
    Encode bytes to decimal string.

    Uses C extension if available, otherwise falls back to pure Python.

    Args:
        data: Input bytes to encode

    Returns:
        Decimal string representation (3 digits per byte)

    Example:
        >>> encode(b"Hi")
        '072105'
    """
    if _HAS_C_EXTENSION:
        return _kolibri.encode(data)
    return encode_pure(data)


def decode(encoded: str) -> bytes:
    """
    Decode decimal string back to bytes.

    Uses C extension if available, otherwise falls back to pure Python.

    Args:
        encoded: Decimal string (length must be multiple of 3)

    Returns:
        Original bytes

    Example:
        >>> decode('072105')
        b'Hi'
    """
    if _HAS_C_EXTENSION:
        return _kolibri.decode(encoded)
    return decode_pure(encoded)


def encode_fast(data: bytes) -> str:
    """
    Encode bytes using the C extension (raises error if not available).

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


def is_c_extension_available() -> bool:
    """Check if the C extension is available."""
    return _HAS_C_EXTENSION
