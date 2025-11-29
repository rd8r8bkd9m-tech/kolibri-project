# Kolibri Python SDK

Ultra-fast byte-to-decimal encoding for Python.

## Overview

The Kolibri Python SDK provides high-performance byte-to-decimal encoding and decoding. Each byte (0-255) is converted to a 3-digit decimal string (000-255).

**Performance comparison:**

| Implementation | Throughput |
|----------------|------------|
| Pure Python    | ~50 MB/s   |
| C Extension    | ~27,700 MB/s (27.7 GB/s) |
| **Speedup**    | **~550x**  |

## Installation

### From source (recommended for development)

```bash
cd sdk/python

# Install in development mode (builds C extension)
pip install -e .

# Or build the extension in place
python setup.py build_ext --inplace
```

### Requirements

- Python 3.8+
- C compiler (gcc, clang, or MSVC)
- Python development headers

On Ubuntu/Debian:
```bash
sudo apt-get install python3-dev build-essential
```

On macOS (with Homebrew):
```bash
xcode-select --install
```

## Usage

### Basic Usage

```python
from kolibri import encode, decode

# Encode bytes to decimal string
data = b"Hello, World!"
encoded = encode(data)
print(encoded)  # "072101108108111044032087111114108100033"

# Decode back to bytes
decoded = decode(encoded)
print(decoded)  # b"Hello, World!"
```

### Checking C Extension Availability

```python
from kolibri import is_c_extension_available

if is_c_extension_available():
    print("C extension is available - using optimized code")
else:
    print("Falling back to pure Python implementation")
```

### Using Pure Python Explicitly

```python
from kolibri.pure_python import encode_pure, decode_pure

# These always use pure Python, regardless of C extension
encoded = encode_pure(b"test")
decoded = decode_pure(encoded)
```

## API Reference

### `encode(data: bytes) -> str`

Encode bytes to decimal string. Uses C extension if available, otherwise falls back to pure Python.

### `decode(encoded: str) -> bytes`

Decode decimal string back to bytes. The input length must be a multiple of 3.

### `encode_fast(data: bytes) -> str`

Encode using C extension only. Raises `RuntimeError` if C extension is not available.

### `is_c_extension_available() -> bool`

Returns `True` if the C extension is loaded and available.

### Pure Python Module (`kolibri.pure_python`)

- `encode_pure(data: bytes) -> str` - Pure Python encode
- `decode_pure(encoded: str) -> bytes` - Pure Python decode

## Running Benchmarks

```bash
cd sdk/python

# Build the extension first
pip install -e .

# Run benchmark
python benchmark.py
```

Example output:
```
============================================================
KOLIBRI SDK BENCHMARK: Pure Python vs C Extension
============================================================

✓ C Extension: AVAILABLE

============================================================
ENCODE Benchmark: 1 MB (1048576 bytes)
============================================================
  Pure Python          :     52.34 MB/s (avg: 19.11ms, stddev: 0.42ms)
  C Extension          :  27689.45 MB/s (avg: 0.04ms, stddev: 0.00ms)

  Speedup: 529.0x faster!
```

## Running Tests

```bash
cd sdk/python

# With pytest (recommended)
pip install pytest
pytest test_correctness.py -v

# Or without pytest
python test_correctness.py
```

## Building a Portable Binary

By default, the C extension is built with `-march=native` for maximum performance on the build machine. To build a portable version:

```bash
KOLIBRI_PORTABLE=1 pip install .
```

## Encoding Format

The encoding converts each byte to a 3-digit decimal representation:

| Byte (hex) | Byte (decimal) | Encoded |
|------------|----------------|---------|
| `\x00`     | 0              | `000`   |
| `\x48`     | 72             | `072`   |
| `\x65`     | 101            | `101`   |
| `\xff`     | 255            | `255`   |

Example:
- `b"Hi"` → `"072105"` (H=72, i=105)
- `b"\x00\xff"` → `"000255"`

## License

MIT License - see the main project LICENSE file.
