# Kolibri Compression Format Specification

**Version:** 1.0  
**Date:** 2025-02-11  
**Author:** Кочуров Владислав Евгеньевич  

## Overview

Kolibri supports two wire formats for compressed data and one streaming framing protocol.
All multi-byte integers are **little-endian**. Byte order is fixed regardless of platform.

---

## 1. LZCM Compact Format (single-block, ≤ 16 MB)

Used for small inputs compressed via the LZCM unified encoder (LZ+CM).
This is the preferred format for embedded and archive use.

### Header (5 bytes)

| Offset | Size | Field            | Value / Description                      |
|--------|------|------------------|------------------------------------------|
| 0      | 2    | `magic`          | `0x4D4B` ("KM", little-endian)           |
| 2      | 3    | `original_size`  | Original uncompressed size (LE, 24-bit)  |

Maximum representable original size: **16 777 215** bytes (≈ 16 MB).

### Payload

Immediately after the header: one LZCM compressed block.

Block format flag byte:

| Flag   | Meaning                                               |
|--------|-------------------------------------------------------|
| `0x00` | Compressed single block (LZCM v66 bit-level CM data) |
| `0x01` | Raw single block (uncompressed copy)                  |
| `0x02` | Multi-block (see below)                               |

#### Multi-block sub-format (flag `0x02`)

| Offset    | Size      | Field                           |
|-----------|-----------|---------------------------------|
| 0         | 1         | Flag `0x02`                     |
| 1         | 2         | `nblocks` (uint16 LE)           |
| 3         | 4×N       | `block_csizes[]` (uint32 LE ea.)|
| 3+4×N     | variable  | Concatenated block payloads     |

Each block payload is either LZCM-compressed data (if `block_csize > 0`)
or raw data of the original block size (if `block_csize == 0`).

Block size: **2 097 152** bytes (2 MB, defined as `KF62_BLOCK_SIZE`).
Last block may be smaller.

---

## 2. Traditional Format (legacy, ≤ 4 GB)

Used when LZCM doesn't win, or for older codec paths (Formula, Token, LZ77, etc.).

### Header (16 bytes)

| Offset | Size | Field            | Value / Description                          |
|--------|------|------------------|----------------------------------------------|
| 0      | 4    | `magic`          | `0x4B4C4252` ("KLBR")                        |
| 4      | 2    | `version`        | Format version (currently `66`)              |
| 6      | 2    | `methods`        | Bitmask of methods used (see below)          |
| 8      | 4    | `original_size`  | Original uncompressed size (uint32 LE)       |
| 12     | 4    | `checksum`       | CRC-32 of original data                      |

### Methods bitmask

| Bit  | Value    | Method                          |
|------|----------|---------------------------------|
| 0    | `0x0001` | LZ77                            |
| 1    | `0x0002` | RLE                             |
| 2    | `0x0004` | Huffman                         |
| 3    | `0x0008` | Formula                         |
| 4    | `0x0010` | Math                            |
| 5    | `0x0020` | LZMA                            |
| 6    | `0x0040` | Zstandard                       |
| 7    | `0x0080` | Adaptive dictionary             |
| 8    | `0x0100` | Token-level text stream (v52+)  |
| 9    | `0x0200` | LZCM unified (v66+)             |

### Payload

After the 16-byte header, compressed data follows.
Decompression order is determined by `methods` and `version`:

1. Token dictionary (if `KOLIBRI_COMPRESS_TOKEN` set and version ≥ 52)
2. Formula decompress (if `KOLIBRI_COMPRESS_FORMULA` set)
3. LZCM decompress (if `KOLIBRI_COMPRESS_LZCM` set)
4. LZ77 decompress (if `KOLIBRI_COMPRESS_LZ77` set)

---

## 3. Streaming Format

Wire framing for incremental compress/decompress via `kolibri_stream_*()` API.
Designed for pipes, sockets, and large-file processing.

### Stream Header (7 bytes)

| Offset | Size | Field         | Value / Description                   |
|--------|------|---------------|---------------------------------------|
| 0      | 2    | `magic`       | `0x4B53` ("KS", little-endian)        |
| 2      | 1    | `version`     | Stream protocol version (currently 1) |
| 3      | 4    | `block_size`  | Max uncompressed block size (uint32 LE), default 2 097 152 |

### Block Frames

Repeated sequence of block frames:

| Offset | Size | Field            | Description                               |
|--------|------|------------------|-------------------------------------------|
| 0      | 4    | `compressed_size`| Compressed payload size (uint32 LE)       |
| 4      | 4    | `original_size`  | Uncompressed size of this block (uint32 LE) |
| 8      | var  | `payload`        | Compressed (or raw) block data            |

- If `compressed_size == original_size`, the payload is stored raw (uncompressed).
- Otherwise, the payload is a single LZCM v66 compressed block.

### End-of-Stream Marker (8 bytes)

| Offset | Size | Field            | Value                                     |
|--------|------|------------------|-------------------------------------------|
| 0      | 4    | `compressed_size`| `0xFFFFFFFF` (sentinel)                   |
| 4      | 4    | `original_size`  | `0x00000000`                              |

After the end marker, the stream is complete. No more data should follow.

---

## 4. Integrity

- **CRC-32**: Used in the traditional format header. Polynomial: standard CRC-32 (ISO 3309).
- **Roundtrip guarantee**: For all formats, `decompress(compress(data)) == data` must hold exactly.
- **No encryption**: The formats carry plaintext. Encryption (if needed) is applied at a higher layer.

---

## 5. API Summary

### One-shot API

```c
KolibriCompressor *kolibri_compressor_create(uint32_t methods);
void kolibri_compressor_destroy(KolibriCompressor *comp);

int kolibri_compress(KolibriCompressor *comp,
                     const uint8_t *input, size_t input_size,
                     uint8_t **output, size_t *output_size,
                     KolibriCompressStats *stats);

int kolibri_decompress(const uint8_t *input, size_t input_size,
                       uint8_t **output, size_t *output_size,
                       KolibriCompressStats *stats);
```

### Streaming API

```c
KolibriStream *kolibri_stream_create(KolibriStreamMode mode,
                                      uint32_t methods,
                                      KolibriStreamWriteFn write_fn,
                                      void *user_data);

KolibriStreamStatus kolibri_stream_write(KolibriStream *stream,
                                          const uint8_t *data, size_t size);

KolibriStreamStatus kolibri_stream_finish(KolibriStream *stream);

void kolibri_stream_stats(const KolibriStream *stream,
                           KolibriCompressStats *stats);

void kolibri_stream_destroy(KolibriStream *stream);
```

### Streaming usage example (compress to file)

```c
static int file_writer(void *ctx, const uint8_t *data, size_t size) {
    return fwrite(data, 1, size, (FILE *)ctx) == size ? 0 : -1;
}

FILE *f = fopen("out.kstream", "wb");
KolibriStream *s = kolibri_stream_create(
    KOLIBRI_STREAM_COMPRESS, KOLIBRI_COMPRESS_LZCM, file_writer, f);

while (/* more input */) {
    kolibri_stream_write(s, chunk, chunk_size);
}
kolibri_stream_finish(s);
kolibri_stream_destroy(s);
fclose(f);
```

---

## 6. Versioning & Compatibility

- The `version` field in both traditional and streaming headers allows forward-compatible evolution.
- Decompressors MUST reject unknown magic values and unsupported versions.
- Block size is communicated in the streaming header, so compressor and decompressor agree automatically.
- Bumping the format version requires updating `KOLIBRI_COMPRESS_VERSION` in `compress.h`.
