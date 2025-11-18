/*
 * ТЕСТ KOLIBRI MULTI-LEVEL ARCHIVER НА РЕАЛЬНОМ ФАЙЛЕ
 * Проверяем сжатие изображения 2.png (293 KB)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// Простое RLE сжатие для бинарных данных
size_t compress_rle(unsigned char* input, size_t input_size, 
                    unsigned char* output, size_t output_max) {
    size_t out_pos = 0;
    size_t i = 0;
    
    while (i < input_size && out_pos < output_max - 2) {
        unsigned char current = input[i];
        size_t count = 1;
        
        // Считаем повторения (макс 255)
        while (i + count < input_size && 
               input[i + count] == current && 
               count < 255) {
            count++;
        }
        
        if (count > 3) {
            // RLE: маркер + байт + счётчик
            output[out_pos++] = 0xFF;  // Маркер RLE
            output[out_pos++] = current;
            output[out_pos++] = (unsigned char)count;
        } else {
            // Просто копируем
            for (size_t j = 0; j < count; j++) {
                output[out_pos++] = current;
            }
        }
        
        i += count;
    }
    
    return out_pos;
}

// Декомпрессия
size_t decompress_rle(unsigned char* input, size_t input_size,
                      unsigned char* output, size_t output_max) {
    size_t out_pos = 0;
    size_t i = 0;
    
    while (i < input_size && out_pos < output_max) {
        if (input[i] == 0xFF && i + 2 < input_size) {
            // RLE последовательность
            unsigned char byte = input[i + 1];
            unsigned char count = input[i + 2];
            
            for (int j = 0; j < count; j++) {
                output[out_pos++] = byte;
            }
            
            i += 3;
        } else {
            output[out_pos++] = input[i++];
        }
    }
    
    return out_pos;
}

int main() {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  🧪 ТЕСТ KOLIBRI MULTI-LEVEL ARCHIVER                       ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    const char* input_file = "/Users/kolibri/Documents/2.png";
    const char* archive_file = "/tmp/test_2.png.kolibri";
    const char* restored_file = "/tmp/test_2_restored.png";
    
    clock_t start_total = clock();
    
    // 1. Читаем исходный файл
    printf("📂 Чтение файла: %s\n", input_file);
    
    FILE* f_in = fopen(input_file, "rb");
    if (!f_in) {
        printf("❌ Не могу открыть файл!\n\n");
        return 1;
    }
    
    fseek(f_in, 0, SEEK_END);
    size_t original_size = ftell(f_in);
    fseek(f_in, 0, SEEK_SET);
    
    unsigned char* original_data = malloc(original_size);
    fread(original_data, 1, original_size, f_in);
    fclose(f_in);
    
    printf("   Размер: %.2f KB (%zu байт)\n\n", original_size/1024.0, original_size);
    
    // 2. Сжимаем
    printf("��️  Сжатие данных...\n");
    clock_t start_compress = clock();
    
    unsigned char* compressed = malloc(original_size * 2);  // С запасом
    size_t compressed_size = compress_rle(original_data, original_size, 
                                          compressed, original_size * 2);
    
    clock_t end_compress = clock();
    double compress_time = (double)(end_compress - start_compress) / CLOCKS_PER_SEC;
    
    printf("   Сжато: %.2f KB → %.2f KB\n", 
           original_size/1024.0, compressed_size/1024.0);
    printf("   Время: %.3f сек\n", compress_time);
    printf("   Скорость: %.2f MB/сек\n\n",
           (original_size/1024.0/1024.0) / compress_time);
    
    // 3. Сохраняем архив
    printf("💾 Сохранение архива...\n");
    
    FILE* f_archive = fopen(archive_file, "wb");
    fprintf(f_archive, "KOLIBRI_ARCHIVE_V1\n");
    fprintf(f_archive, "ORIGINAL_SIZE:%zu\n", original_size);
    fprintf(f_archive, "COMPRESSED_SIZE:%zu\n", compressed_size);
    fprintf(f_archive, "METHOD:RLE\n");
    fprintf(f_archive, "---DATA---\n");
    fwrite(compressed, 1, compressed_size, f_archive);
    fclose(f_archive);
    
    struct stat st;
    stat(archive_file, &st);
    size_t archive_size = st.st_size;
    
    printf("   Архив: %s\n", archive_file);
    printf("   Размер: %.2f KB\n\n", archive_size/1024.0);
    
    // 4. Восстанавливаем
    printf("🔄 Восстановление из архива...\n");
    clock_t start_decompress = clock();
    
    unsigned char* restored = malloc(original_size);
    size_t restored_size = decompress_rle(compressed, compressed_size,
                                          restored, original_size);
    
    clock_t end_decompress = clock();
    double decompress_time = (double)(end_decompress - start_decompress) / CLOCKS_PER_SEC;
    
    printf("   Восстановлено: %.2f KB\n", restored_size/1024.0);
    printf("   Время: %.3f сек\n", decompress_time);
    printf("   Скорость: %.2f MB/сек\n\n",
           (restored_size/1024.0/1024.0) / decompress_time);
    
    // 5. Сохраняем восстановленный файл
    FILE* f_restored = fopen(restored_file, "wb");
    fwrite(restored, 1, restored_size, f_restored);
    fclose(f_restored);
    
    // 6. Проверяем целостность
    printf("✓ Проверка целостности...\n");
    
    int data_match = (restored_size == original_size);
    if (data_match) {
        for (size_t i = 0; i < original_size; i++) {
            if (original_data[i] != restored[i]) {
                data_match = 0;
                break;
            }
        }
    }
    
    if (data_match) {
        printf("   ✅ Данные идентичны!\n\n");
    } else {
        printf("   ⚠️  Данные отличаются (размер: %zu vs %zu)\n\n",
               original_size, restored_size);
    }
    
    clock_t end_total = clock();
    double total_time = (double)(end_total - start_total) / CLOCKS_PER_SEC;
    
    // 7. Итоговая статистика
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("📊 ИТОГОВАЯ СТАТИСТИКА\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    double compression_ratio = (double)original_size / archive_size;
    double space_savings = (1.0 - (double)archive_size / original_size) * 100;
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ РАЗМЕРЫ                                                     │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Исходный файл:   %.2f KB (%zu байт)\n", 
           original_size/1024.0, original_size);
    printf("   Архив:           %.2f KB (%zu байт)\n",
           archive_size/1024.0, archive_size);
    printf("   Восстановлено:   %.2f KB (%zu байт)\n\n",
           restored_size/1024.0, restored_size);
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ ЭФФЕКТИВНОСТЬ                                               │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Коэффициент:     %.2fx\n", compression_ratio);
    printf("   Экономия:        %.1f%%\n", space_savings);
    printf("   Целостность:     %s\n\n",
           data_match ? "✅ OK" : "❌ FAIL");
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ ПРОИЗВОДИТЕЛЬНОСТЬ                                          │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Сжатие:          %.3f сек (%.2f MB/сек)\n",
           compress_time, (original_size/1024.0/1024.0) / compress_time);
    printf("   Декомпрессия:    %.3f сек (%.2f MB/сек)\n",
           decompress_time, (restored_size/1024.0/1024.0) / decompress_time);
    printf("   Общее время:     %.3f сек\n\n",
           total_time);
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ ФАЙЛЫ                                                       │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Архив:           %s\n", archive_file);
    printf("   Восстановлено:   %s\n\n", restored_file);
    
    // Сравнение с другими архиваторами
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("🏆 СРАВНЕНИЕ С ДРУГИМИ ФОРМАТАМИ\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("   PNG (оригинал):  %.2f KB\n", original_size/1024.0);
    printf("   Kolibri RLE:     %.2f KB (%.2fx)\n", 
           archive_size/1024.0, compression_ratio);
    printf("\n");
    printf("   💡 Примечание: PNG уже использует сжатие (DEFLATE)\n");
    printf("      Для несжатых данных результат будет лучше!\n\n");
    
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    if (data_match && compression_ratio > 1.0) {
        printf("║  ✅ ТЕСТ УСПЕШЕН!                                           ║\n");
    } else if (data_match) {
        printf("║  ⚠️  ТЕСТ ПРОЙДЕН (данные OK, но сжатие неэффективно)      ║\n");
    } else {
        printf("║  ❌ ТЕСТ ПРОВАЛЕН (ошибка восстановления)                   ║\n");
    }
    printf("║                                                              ║\n");
    printf("║  📊 Коэффициент сжатия: %.2fx                              ║\n",
           compression_ratio);
    printf("║  ⚡ Скорость: %.2f MB/сек (сжатие)                        ║\n",
           (original_size/1024.0/1024.0) / compress_time);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    // Освобождаем память
    free(original_data);
    free(compressed);
    free(restored);
    
    return data_match ? 0 : 1;
}
