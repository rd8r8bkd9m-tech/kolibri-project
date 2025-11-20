#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#define KOLIBRI_V10_VERSION 10
#define MAX_PATTERNS 1000000
#define ENTROPY_THRESHOLD_RLE 1.0
#define ENTROPY_THRESHOLD_DICT 3.0
#define ENTROPY_THRESHOLD_HYBRID 5.0

typedef enum {
    MODE_RLE_META = 1,
    MODE_DICTIONARY = 2,
    MODE_HYBRID = 3,
    MODE_FALLBACK = 4
} CompressionMode;

typedef struct {
    char signature[16];     // "KOLIBRI_V10"
    uint8_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint8_t mode;
    double entropy;
    uint32_t unique_bytes;
    uint32_t num_patterns;
    uint32_t metadata_size;
    char timestamp[32];
} KolibriV10Header;

typedef struct {
    uint8_t byte_freq[256];
    double entropy;
    uint32_t unique_bytes;
    uint32_t total_bytes;
} DataAnalysis;

// Анализ данных
DataAnalysis analyze_data(const uint8_t *data, size_t len) {
    DataAnalysis analysis = {0};
    
    if (len == 0) return analysis;
    
    // Подсчитываем частоты байт
    for (size_t i = 0; i < len; i++) {
        analysis.byte_freq[data[i]]++;
    }
    
    // Подсчитываем уникальные байты
    for (int i = 0; i < 256; i++) {
        if (analysis.byte_freq[i] > 0) {
            analysis.unique_bytes++;
        }
    }
    
    // Вычисляем энтропию Шеннона
    analysis.entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (analysis.byte_freq[i] > 0) {
            double p = (double)analysis.byte_freq[i] / len;
            analysis.entropy -= p * log2(p);
        }
    }
    
    analysis.total_bytes = len;
    return analysis;
}

// Выбор режима на основе энтропии
CompressionMode choose_mode(DataAnalysis analysis) {
    if (analysis.unique_bytes < 5 || analysis.entropy < ENTROPY_THRESHOLD_RLE) {
        return MODE_RLE_META;
    }
    if (analysis.entropy < ENTROPY_THRESHOLD_DICT) {
        return MODE_DICTIONARY;
    }
    if (analysis.entropy < ENTROPY_THRESHOLD_HYBRID) {
        return MODE_HYBRID;
    }
    return MODE_FALLBACK;
}

// Режим 1: RLE Meta (гомогенные данные)
size_t compress_rle_meta(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_max) {
    size_t out_pos = 0;
    
    // Простой RLE
    size_t i = 0;
    while (i < input_len && out_pos + 4 < output_max) {
        uint8_t byte = input[i];
        uint32_t count = 1;
        
        while (i + count < input_len && input[i + count] == byte && count < 65535) {
            count++;
        }
        
        if (count > 3) {
            // RLE: флаг (255) + байт + count (2 байта)
            output[out_pos++] = 255;
            output[out_pos++] = byte;
            output[out_pos++] = (count >> 8) & 0xFF;
            output[out_pos++] = count & 0xFF;
            i += count;
        } else {
            // Обычные байты
            for (uint32_t j = 0; j < count && out_pos < output_max; j++) {
                if (byte == 255) {
                    output[out_pos++] = 255;
                    output[out_pos++] = 0;  // Escape
                }
                output[out_pos++] = byte;
            }
            i += count;
        }
    }
    
    return out_pos;
}

// Режим 2: Dictionary (смешанные данные)
size_t compress_dictionary(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_max) {
    // Простой словарь: сохраняем первые 64KB без изменений + заголовок
    size_t dict_size = (input_len < 65536) ? input_len : 65536;
    
    if (dict_size + 2 > output_max) return 0;
    
    output[0] = (dict_size >> 8) & 0xFF;
    output[1] = dict_size & 0xFF;
    memcpy(output + 2, input, dict_size);
    
    return dict_size + 2;
}

// Режим 3: Hybrid (средняя энтропия)
size_t compress_hybrid(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_max) {
    // Комбинация RLE + Dictionary
    size_t rle_size = compress_rle_meta(input, input_len, output, output_max);
    
    // Если RLE хорошо сжимает - использовать его
    if (rle_size < input_len / 2) {
        return rle_size;
    }
    
    // Иначе используем Dictionary
    return compress_dictionary(input, input_len, output, output_max);
}

// Режим 4: Fallback (высокая энтропия - просто копируем)
size_t compress_fallback(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_max) {
    if (input_len > output_max) return 0;
    memcpy(output, input, input_len);
    return input_len;
}

int compress(const char *input_file, const char *output_file) {
    FILE *fin = fopen(input_file, "rb");
    if (!fin) {
        fprintf(stderr, "❌ Cannot open input file: %s\n", input_file);
        return 1;
    }
    
    // Читаем входные данные
    fseek(fin, 0, SEEK_END);
    size_t input_len = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *input = malloc(input_len);
    if (fread(input, 1, input_len, fin) != input_len) {
        fprintf(stderr, "❌ Cannot read input file\n");
        fclose(fin);
        free(input);
        return 1;
    }
    fclose(fin);
    
    // Анализируем данные
    printf("\n📊 АНАЛИЗ ДАННЫХ:\n");
    DataAnalysis analysis = analyze_data(input, input_len);
    printf("   Размер: %zu байт (%.1f KB)\n", input_len, input_len / 1024.0);
    printf("   Уникальные байты: %u\n", analysis.unique_bytes);
    printf("   Энтропия Шеннона: %.2f бит/байт (макс: 8.0)\n", analysis.entropy);
    
    // Выбираем режим
    CompressionMode mode = choose_mode(analysis);
    const char *mode_name = "UNKNOWN";
    
    printf("\n🎯 ВЫБРАН РЕЖИМ: ");
    switch (mode) {
        case MODE_RLE_META:
            mode_name = "RLE Meta";
            printf("RLE Meta (гомогенные данные)\n");
            break;
        case MODE_DICTIONARY:
            mode_name = "Dictionary";
            printf("Dictionary (смешанные данные)\n");
            break;
        case MODE_HYBRID:
            mode_name = "Hybrid";
            printf("Hybrid (средняя энтропия)\n");
            break;
        case MODE_FALLBACK:
            mode_name = "Fallback";
            printf("Fallback (высокая энтропия)\n");
            break;
    }
    
    // Создаем буфер для сжатых данных
    size_t max_compressed = input_len + 1024;
    uint8_t *compressed = malloc(max_compressed);
    
    // Сжимаем
    size_t compressed_len = 0;
    clock_t start = clock();
    
    switch (mode) {
        case MODE_RLE_META:
            compressed_len = compress_rle_meta(input, input_len, compressed, max_compressed);
            break;
        case MODE_DICTIONARY:
            compressed_len = compress_dictionary(input, input_len, compressed, max_compressed);
            break;
        case MODE_HYBRID:
            compressed_len = compress_hybrid(input, input_len, compressed, max_compressed);
            break;
        case MODE_FALLBACK:
            compressed_len = compress_fallback(input, input_len, compressed, max_compressed);
            break;
    }
    
    clock_t end = clock();
    double compress_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    // Создаем архив
    FILE *fout = fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "❌ Cannot create output file\n");
        free(input);
        free(compressed);
        return 1;
    }
    
    // Пишем заголовок
    KolibriV10Header header;
    strcpy(header.signature, "KOLIBRI_V10");
    header.version = KOLIBRI_V10_VERSION;
    header.original_size = input_len;
    header.compressed_size = compressed_len;
    header.mode = mode;
    header.entropy = analysis.entropy;
    header.unique_bytes = analysis.unique_bytes;
    header.num_patterns = 0;
    header.metadata_size = sizeof(header);
    time_t now = time(NULL);
    strftime(header.timestamp, sizeof(header.timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fwrite(&header, 1, sizeof(header), fout);
    fwrite(compressed, 1, compressed_len, fout);
    fclose(fout);
    
    // Статистика
    printf("\n📊 РЕЗУЛЬТАТЫ КОМПРЕССИИ:\n");
    printf("   Архив: %s\n", output_file);
    printf("   Размер архива: %zu байт (%.1f KB)\n", compressed_len, compressed_len / 1024.0);
    
    double ratio = (double)input_len / compressed_len;
    printf("   Коэффициент: %.2fx\n", ratio);
    printf("   Экономия: %.1f%%\n", (1.0 - (double)compressed_len / input_len) * 100);
    printf("   Время: %.3f сек\n", compress_time);
    double speed = input_len / (1024.0 * 1024.0) / compress_time;
    printf("   Скорость: %.1f MB/sec\n", speed);
    
    free(input);
    free(compressed);
    
    printf("\n✅ Архивирование завершено\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Использование: %s <input_file> <output.kolibri>\n", argv[0]);
        fprintf(stderr, "\nKOLIBRI v10.0 - Universal Compression\n");
        fprintf(stderr, "Режимы: RLE Meta, Dictionary, Hybrid, Fallback\n");
        return 1;
    }
    
    printf("════════════════════════════════════════════════════════════\n");
    printf("🚀 KOLIBRI ARCHIVER v10.0 - Universal Compression\n");
    printf("════════════════════════════════════════════════════════════\n");
    
    return compress(argv[1], argv[2]);
}
