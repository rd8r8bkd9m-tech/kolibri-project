"""
Kolibri pure Python implementation.

A slow but portable reference implementation for byte-to-decimal encoding.
Used as a baseline for benchmarking against the C extension.

Typical throughput: ~5 MB/s (varies by hardware)
"""


def encode_pure(data: bytes) -> str:
    """
    Encode bytes to decimal string (3 digits per byte).

    Args:
        data: Input bytes to encode

    Returns:
        Decimal string representation (e.g., b"\\x48\\x65" -> "072101")
    """
    return ''.join(f'{b:03d}' for b in data)


def decode_pure(encoded: str) -> bytes:
    """
    Decode decimal string back to bytes.

    Args:
        encoded: Decimal string (length must be multiple of 3)

    Returns:
        Original bytes

    Raises:
        ValueError: If encoded string length is not multiple of 3
    """
    if len(encoded) % 3 != 0:
        raise ValueError("Encoded string length must be a multiple of 3")
    return bytes(int(encoded[i:i+3]) for i in range(0, len(encoded), 3))
