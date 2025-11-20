/*
 * СЖАТИЕ ДИРЕКТОРИИ KOLIBRI ARCHIVER
 * Рекурсивное сжатие всех файлов в директории
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define MAX_PATH 4096

typedef struct {
    size_t total_files;
    size_t total_size;
    size_t compressed_size;
    double total_time;
} Stats;

// Простое RLE сжатие
size_t compress_rle(unsigned char* input, size_t input_size, 
                    unsigned char* output, size_t output_max) {
    size_t out_pos = 0;
    size_t i = 0;
    
    while (i < input_size && out_pos < output_max - 2) {
        unsigned char current = input[i];
        size_t count = 1;
        
        while (i + count < input_size && 
               input[i + count] == current && 
               count < 255) {
            count++;
        }
        
        if (count > 3) {
            output[out_pos++] = 0xFF;
            output[out_pos++] = current;
            output[out_pos++] = (unsigned char)count;
        } else {
            for (size_t j = 0; j < count; j++) {
                output[out_pos++] = current;
            }
        }
        
        i += count;
    }
    
    return out_pos;
}

int is_regular_file(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

int is_directory(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

void process_file(const char* filepath, FILE* archive, Stats* stats) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return;
    
    // Получаем размер
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size == 0) {
        fclose(f);
        return;
    }
    
    // Читаем данные
    unsigned char* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);
    
    // Сжимаем
    unsigned char* compressed = malloc(size * 2);
    size_t compressed_size = compress_rle(data, size, compressed, size * 2);
    
    // Записываем в архив
    fprintf(archive, "FILE:%s\n", filepath);
    fprintf(archive, "ORIGINAL:%zu\n", size);
    fprintf(archive, "COMPRESSED:%zu\n", compressed_size);
    fprintf(archive, "---DATA---\n");
    fwrite(compressed, 1, compressed_size, archive);
    fprintf(archive, "\n---END---\n");
    
    // Обновляем статистику
    stats->total_files++;
    stats->total_size += size;
    stats->compressed_size += compressed_size + 100; // +заголовок
    
    printf(".");
    fflush(stdout);
    
    free(data);
    free(compressed);
}

void scan_directory(const char* dirpath, FILE* archive, Stats* stats, const char* base_path) {
    DIR* dir = opendir(dirpath);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0 ||
            strncmp(entry->d_name, ".", 1) == 0) {
            continue;
        }
        
        char fullpath[MAX_PATH];
        snprintf(fullpath, MAX_PATH, "%s/%s", dirpath, entry->d_name);
        
        if (is_directory(fullpath)) {
            // Пропускаем build директории и бинарники
            if (strcmp(entry->d_name, "build") == 0 ||
                strcmp(entry->d_name, "bin") == 0 ||
                strcmp(entry->d_name, ".git") == 0 ||
                strcmp(entry->d_name, "node_modules") == 0) {
                continue;
            }
            scan_directory(fullpath, archive, stats, base_path);
        } else if (is_regular_file(fullpath)) {
            process_file(fullpath, archive, stats);
        }
    }
    
    closedir(dir);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Использование: %s <директория>\n", argv[0]);
        printf("Пример: %s /Users/kolibri/Documents/pilot\n", argv[0]);
        return 1;
    }
    
    const char* input_dir = argv[1];
    const char* output_file = "/tmp/pilot_archive.kolibri";
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  🗜️  СЖАТИЕ ДИРЕКТОРИИ KOLIBRI ARCHIVER                    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("📂 Директория:  %s\n", input_dir);
    printf("💾 Архив:       %s\n\n", output_file);
    
    // Проверяем директорию
    if (!is_directory(input_dir)) {
        printf("❌ Директория не найдена!\n\n");
        return 1;
    }
    
    clock_t start = clock();
    
    // Создаём архив
    FILE* archive = fopen(output_file, "w");
    fprintf(archive, "KOLIBRI_DIRECTORY_ARCHIVE_V1\n");
    fprintf(archive, "SOURCE:%s\n", input_dir);
    fprintf(archive, "METHOD:RLE\n");
    fprintf(archive, "---FILES---\n");
    
    Stats stats = {0, 0, 0, 0.0};
    
    printf("🔄 Сканирование и сжатие");
    fflush(stdout);
    
    scan_directory(input_dir, archive, &stats, input_dir);
    
    fprintf(archive, "---END_ARCHIVE---\n");
    fclose(archive);
    
    clock_t end = clock();
    stats.total_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n\n");
    
    // Получаем размер архива
    struct stat st;
    stat(output_file, &st);
    size_t archive_size = st.st_size;
    
    // Итоговая статистика
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("📊 РЕЗУЛЬТАТЫ АРХИВАЦИИ\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    double compression_ratio = (double)stats.total_size / archive_size;
    double space_savings = (1.0 - (double)archive_size / stats.total_size) * 100;
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ РАЗМЕРЫ                                                     │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Файлов обработано: %zu\n", stats.total_files);
    printf("   Исходный размер:   %.2f MB (%zu байт)\n", 
           stats.total_size/1024.0/1024.0, stats.total_size);
    printf("   Размер архива:     %.2f MB (%zu байт)\n\n",
           archive_size/1024.0/1024.0, archive_size);
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ ЭФФЕКТИВНОСТЬ                                               │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Коэффициент:       %.2fx\n", compression_ratio);
    printf("   Экономия места:    %.1f%%\n\n", space_savings);
    
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│ ПРОИЗВОДИТЕЛЬНОСТЬ                                          │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n");
    printf("   Время:             %.2f сек\n", stats.total_time);
    printf("   Скорость:          %.2f MB/сек\n\n",
           (stats.total_size/1024.0/1024.0) / stats.total_time);
    
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    if (compression_ratio > 1.0) {
        printf("║  ✅ АРХИВАЦИЯ УСПЕШНА!                                      ║\n");
    } else {
        printf("║  ⚠️  АРХИВАЦИЯ ЗАВЕРШЕНА (сжатие неэффективно)              ║\n");
    }
    printf("║                                                              ║\n");
    printf("║  📦 Архив: %s                ║\n", output_file);
    printf("║  📊 Сжатие: %.2fx                                          ║\n",
           compression_ratio);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
