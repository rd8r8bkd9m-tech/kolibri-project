/**
 * @file kolibri_kgen.c
 * @brief CLI утилита для работы с файлами .kgen
 * 
 * Компонент C.6: Инструменты CLI
 * 
 * Использование:
 *   kolibri_kgen inspect <file.kgen> [-v]
 *   kolibri_kgen extract <input.kgen> <output>
 *   kolibri_kgen merge <file1.kgen> <file2.kgen> ... -o <output.kgen>
 *   kolibri_kgen search <pattern> <directory>
 */

#include "kolibri/kgen_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program) {
    printf("Kolibri .kgen Utility v0.5\n");
    printf("\nИспользование:\n");
    printf("  %s inspect <file.kgen> [-v]     - Инспекция файла (метаданные)\n", program);
    printf("  %s extract <input.kgen> <out>   - Извлечение содержимого\n", program);
    printf("  %s merge <f1.kgen> ... -o <out> - Слияние файлов\n", program);
    printf("  %s search <pattern> <dir>       - Поиск по директории\n", program);
    printf("\nПримеры:\n");
    printf("  %s inspect model.kgen\n", program);
    printf("  %s inspect data.kgen -v\n", program);
    printf("  %s extract archive.kgen output.txt\n", program);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "inspect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Ошибка: не указан файл\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *filename = argv[2];
        int verbose = (argc >= 4 && strcmp(argv[3], "-v") == 0);
        
        return k_kgen_cli_inspect(filename, verbose);
    }
    
    else if (strcmp(command, "extract") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Ошибка: не указаны файлы\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *input_file = argv[2];
        const char *output_file = argv[3];
        
        return k_kgen_cli_extract(input_file, output_file);
    }
    
    else if (strcmp(command, "merge") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Ошибка: недостаточно файлов для слияния\n");
            print_usage(argv[0]);
            return 1;
        }
        
        /* Поиск флага -o */
        const char *output_file = NULL;
        int num_files = 0;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                output_file = argv[i + 1];
                break;
            }
            num_files++;
        }
        
        if (!output_file) {
            fprintf(stderr, "Ошибка: не указан выходной файл (-o)\n");
            return 1;
        }
        
        /* Создание массива файлов */
        const char **files = malloc(num_files * sizeof(char *));
        if (!files) {
            fprintf(stderr, "Ошибка выделения памяти\n");
            return 1;
        }
        
        int idx = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                i++; /* Пропуск имени выходного файла */
                continue;
            }
            files[idx++] = argv[i];
        }
        
        int result = k_kgen_cli_merge(files, num_files, output_file);
        free(files);
        
        return result;
    }
    
    else if (strcmp(command, "search") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Ошибка: не указан шаблон или директория\n");
            print_usage(argv[0]);
            return 1;
        }
        
        const char *pattern = argv[2];
        const char *directory = argv[3];
        
        return k_kgen_cli_search(pattern, directory);
    }
    
    else if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    else {
        fprintf(stderr, "Неизвестная команда: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
