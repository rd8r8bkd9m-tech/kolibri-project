# Kolibri OS Archiver Documentation

## Overview

The Kolibri OS Archiver is an advanced compression system that implements multi-layer compression with mathematical analysis for achieving high compression ratios. The system supports all file types and provides integrity verification through CRC32 checksums.

## Architecture

### Compression Methods

The archiver implements three complementary compression methods:

1. **Mathematical Analysis** - Delta encoding for numeric patterns
   - Analyzes data for mathematical sequences
   - Stores differences instead of absolute values
   - Particularly effective for sequential or numeric data

2. **LZ77 Compression** - Dictionary-based compression
   - Sliding window size: 4096 bytes
   - Maximum match length: 255 bytes
   - Replaces repeated sequences with (distance, length) pairs
   - Effective for data with repeated patterns

3. **Run-Length Encoding (RLE)** - Repetition compression
   - Encodes runs of 4 or more identical bytes
   - Format: marker (0xFF) + count + value
   - Escapes literal marker bytes
   - Effective for data with long runs of identical bytes

### Compression Pipeline

Data flows through the compression pipeline in this order:

```
Input Data → Mathematical → LZ77 → RLE → Compressed Output
```

Decompression reverses this order:

```
Compressed Input → RLE → LZ77 → Mathematical → Output Data
```

Only methods that improve compression are applied and recorded in the header.

### File Format

#### Compressed File Header (64 bytes)
```c
struct KolibriCompressHeader {
    uint32_t magic;           // 0x4B4C4252 ("KLBR")
    uint32_t version;         // Format version (1)
    uint32_t methods;         // Bitfield of applied methods
    uint32_t original_size;   // Original data size
    uint32_t compressed_size; // Compressed data size
    uint32_t checksum;        // CRC32 checksum of original data
    uint32_t file_type;       // Detected file type
    uint8_t  reserved[12];    // Reserved for future use
};
```

#### Archive Format
```c
struct KolibriArchiveHeader {
    uint32_t magic;          // 0x4B415243 ("KARC")
    uint32_t version;        // Format version (1)
    uint32_t entry_count;    // Number of files in archive
    uint8_t  reserved[52];   // Reserved for future use
};
```

Each entry contains:
- File name (256 bytes)
- Original and compressed sizes
- CRC32 checksum
- Timestamp
- File type
- Data offset in archive

## Usage

### Command-Line Interface

#### Compress a File
```bash
kolibri_archiver compress input.txt output.klb
```

Output:
```
Compressing 'input.txt' to 'output.klb'...

Compression complete!
Original size:    10000 bytes
Compressed size:  1500 bytes
Compression ratio: 6.67x
File type:        Text
Methods used:     Mathematical+LZ77+RLE
Compression time: 2.50 ms
Checksum:         0x12345678
```

#### Decompress a File
```bash
kolibri_archiver decompress output.klb restored.txt
```

#### Test Compression Ratio
```bash
kolibri_archiver test input.txt
```

Tests compression and verifies data integrity without creating output files.

#### Create an Archive
```bash
kolibri_archiver create myarchive.kar
```

#### Add Files to Archive
```bash
kolibri_archiver add myarchive.kar document.pdf
kolibri_archiver add myarchive.kar image.png
kolibri_archiver add myarchive.kar data.json
```

#### List Archive Contents
```bash
kolibri_archiver list myarchive.kar
```

Output:
```
Listing contents of archive 'myarchive.kar':

Name                                     Original  Compressed    Ratio Type
--------------------------------------------------------------------------------
document.pdf                               245678       98234    2.50x Binary
image.png                                  156789      145234    1.08x Image
data.json                                   12345        3456   3.57x Text
--------------------------------------------------------------------------------
Total files: 3
```

#### Extract from Archive
```bash
kolibri_archiver extract myarchive.kar document.pdf
```

### C API

#### Basic Compression
```c
#include "kolibri/compress.h"

// Create compressor
KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);

// Compress data
uint8_t *compressed = NULL;
size_t compressed_size = 0;
KolibriCompressStats stats;

int ret = kolibri_compress(comp, input_data, input_size,
                            &compressed, &compressed_size, &stats);

if (ret == 0) {
    printf("Compressed %zu -> %zu bytes (%.2fx)\n",
           stats.original_size, stats.compressed_size,
           stats.compression_ratio);
}

// Cleanup
kolibri_compressor_destroy(comp);
free(compressed);
```

#### Decompression
```c
// Decompress data
uint8_t *decompressed = NULL;
size_t decompressed_size = 0;

int ret = kolibri_decompress(compressed, compressed_size,
                              &decompressed, &decompressed_size, NULL);

if (ret == 0) {
    // Use decompressed data
    // ...
}

free(decompressed);
```

#### Archive Operations
```c
// Create archive
KolibriArchive *archive = kolibri_archive_create("myarchive.kar");

// Add files
kolibri_archive_add_file(archive, "file1.txt", file_data, file_size);
kolibri_archive_add_file(archive, "file2.dat", data2, size2);

// Close and save
kolibri_archive_close(archive);

// Open existing archive
archive = kolibri_archive_open("myarchive.kar");

// List contents
KolibriArchiveEntry *entries = NULL;
size_t count = 0;
kolibri_archive_list(archive, &entries, &count);

for (size_t i = 0; i < count; i++) {
    printf("%s: %zu bytes\n", entries[i].name, entries[i].original_size);
}
free(entries);

// Extract file
uint8_t *file_data = NULL;
size_t file_size = 0;
kolibri_archive_extract_file(archive, "file1.txt", &file_data, &file_size);
free(file_data);

kolibri_archive_close(archive);
```

### Python API (Planned)

```python
from kolibri import compress

# Compress file
with open('input.txt', 'rb') as f:
    data = f.read()

compressed, stats = compress.compress(data)
print(f"Compression ratio: {stats.ratio}x")

# Decompress
decompressed = compress.decompress(compressed)

# Archive operations
archive = compress.Archive('myarchive.kar', mode='w')
archive.add_file('document.pdf', pdf_data)
archive.add_file('image.png', image_data)
archive.close()
```

### WebAssembly API (Planned)

```javascript
// Load WASM module
const kolibri = await loadKolibriWasm();

// Compress data
const inputData = new Uint8Array([...]); 
const result = kolibri.compress(inputData);
console.log(`Compressed: ${result.originalSize} -> ${result.compressedSize} bytes`);

// Decompress
const decompressed = kolibri.decompress(result.data);
```

## Performance Characteristics

### Compression Ratios by Data Type

- **Highly repetitive data**: 10-100x
- **Text files**: 2-5x
- **Source code**: 3-6x
- **Binary executables**: 1.5-3x
- **Already compressed data**: ~1x (no improvement)
- **Random data**: ~1x (no improvement)

### Speed

- **Compression**: ~50-200 MB/s (depends on data complexity)
- **Decompression**: ~100-300 MB/s (faster than compression)

### Memory Usage

- Compression requires ~4x input size for temporary buffers
- Decompression requires ~3x compressed size for temporary buffers
- Archive operations use minimal additional memory

## Technical Specifications

### Limitations

- Maximum file size in archive: Limited by available memory
- Maximum archive entries: 1024 files
- Maximum LZ77 distance: 4095 bytes
- Maximum LZ77 match: 255 bytes
- Maximum RLE run: 255 bytes
- Filename length in archives: 256 characters

### Supported File Types

- **Text**: UTF-8 and ASCII text files
- **Binary**: All binary formats
- **Images**: PNG, JPEG, GIF (detected but not specifically optimized)

File type detection is automatic based on file signatures and content analysis.

### Data Integrity

- CRC32 checksums verify data integrity after decompression
- Decompression fails if checksum doesn't match
- Archive entries include individual checksums

## Building

### Prerequisites

- C compiler with C11 support
- CMake 3.16+
- OpenSSL (for CRC32 and other crypto functions)
- SQLite3

### Build Commands

```bash
# Configure
cmake -S . -B build -DKOLIBRI_ENABLE_TESTS=ON

# Build
cmake --build build

# Install
sudo cmake --install build

# Run tests
ctest --test-dir build
```

The archiver executable will be in `build/kolibri_archiver`.

## Examples

### Compress Multiple Files

```bash
# Create archive
kolibri_archiver create backup.kar

# Add files
for file in *.txt; do
    kolibri_archiver add backup.kar "$file"
done

# List contents
kolibri_archiver list backup.kar
```

### Benchmark Compression

```bash
# Generate test file
dd if=/dev/zero of=test.dat bs=1M count=10

# Test compression
kolibri_archiver test test.dat
```

### Verify File Integrity

```bash
# Compress
kolibri_archiver compress original.txt compressed.klb

# Decompress
kolibri_archiver decompress compressed.klb restored.txt

# Compare
diff original.txt restored.txt
# Should produce no output if files are identical
```

## Future Enhancements

### Planned Features

1. **Enhanced Formula-Based Compression**
   - Detect and encode mathematical formulas
   - Support for geometric and algebraic patterns
   - Automatic formula discovery

2. **Huffman Coding**
   - Entropy-based compression layer
   - Adaptive Huffman trees
   - Integration with existing pipeline

3. **Parallel Compression**
   - Multi-threaded compression for large files
   - Block-based parallel processing

4. **Streaming API**
   - Compress/decompress data streams
   - Support for pipes and network streams

5. **Advanced File Type Optimization**
   - Specialized compression for images
   - Audio/video-specific optimizations
   - Structured data (JSON, XML) aware compression

6. **Compression Level Tuning**
   - Fast compression (level 1)
   - Balanced (level 5, default)
   - Maximum compression (level 9)

## License

Copyright (c) 2025 Kolibri OS Project

See LICENSE file for details.

## Contributing

Contributions are welcome! Please see CONTRIBUTING.md for guidelines.

## Support

For bugs and feature requests, please open an issue on GitHub.
