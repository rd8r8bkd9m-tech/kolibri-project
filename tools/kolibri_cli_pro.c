// ═══════════════════════════════════════════════════════════════
//   KOLIBRI CLI PRO - Professional Terminal Interface
//   Beautiful TUI with real-time progress and statistics
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <math.h>

// ANSI colors
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

// Print header
void print_header(const char* title) {
    printf("\n" CYAN "╔════════════════════════════════════════════════════════════════╗\n");
    printf("║" BOLD WHITE " %-62s" RESET CYAN "║\n", title);
    printf("╚════════════════════════════════════════════════════════════════╝\n" RESET);
}

// Print banner
void print_banner() {
    printf(MAGENTA BOLD);
    printf("\n");
    printf("╦╔═╔═╗╦  ╦╔╗ ╦═╗╦  ╦  ╦  ╦╦  ╦  ╔═╗╦  ╦  ╔═╗╦═╗╔═╗\n");
    printf("╠╩╗║ ║║  ║╠╩╗╠╦╝║  ║  ║  ║╚╗╔╝  ║  ║  ║  ╠═╝╠╦╝║ ║\n");
    printf("╩ ╩╚═╝╩═╝╩╚═╝╩╚═╩  ╩  ╩  ╩ ╚╝   ╚═╝╩═╝╩  ╩  ╩╚═╚═╝\n");
    printf(RESET);
    printf(CYAN "    Professional Compression Tool v13.0 (Generative GPU Engine)\n" RESET);
    printf(YELLOW "    Engine: /Users/kolibri/Documents/os/tools/kolibri-gen-gpu\n" RESET);
}

// Compress file by calling the external GPU tool
int compress_file(const char* input_path, const char* output_path) {
    char command[4096];
    const char* executable_path = "/Users/kolibri/Documents/os/tools/kolibri-gen-gpu";

    snprintf(command, sizeof(command), "\"%s\" compress \"%s\" \"%s\"", executable_path, input_path, output_path);

    printf(WHITE "  Входной файл:  " RESET "%s\n", input_path);
    printf(WHITE "  Выходной файл: " RESET "%s\n", output_path);
    printf("\n" CYAN "--> Запуск Generative GPU Engine..." RESET "\n\n");

    int result = system(command);
    printf("\n");

    if (result == 0) {
        printf(GREEN BOLD "✓ СЖАТИЕ ЗАВЕРШЕНО УСПЕШНО!\n" RESET);
    } else {
        printf(RED BOLD "✗ ПРОИЗОШЛА ОШИБКА ВО ВРЕМЯ СЖАТИЯ.\n" RESET);
    }
    
    return result;
}

// Decompress file by calling the external GPU tool
int decompress_file(const char* input_path, const char* output_path) {
    char command[4096];
    const char* executable_path = "/Users/kolibri/Documents/os/tools/kolibri-gen-gpu";

    snprintf(command, sizeof(command), "\"%s\" decompress \"%s\" \"%s\"", executable_path, input_path, output_path);

    printf(WHITE "  Входной файл:  " RESET "%s\n", input_path);
    printf(WHITE "  Выходной файл: " RESET "%s\n", output_path);
    printf("\n" CYAN "--> Запуск Generative GPU Engine для восстановления..." RESET "\n\n");

    int result = system(command);
    printf("\n");

    if (result == 0) {
        printf(GREEN BOLD "✓ ВОССТАНОВЛЕНИЕ ЗАВЕРШЕНО УСПЕШНО!\n" RESET);
    } else {
        printf(RED BOLD "✗ ПРОИЗОШЛА ОШИБКА ВО ВРЕМЯ ВОССТАНОВЛЕНИЯ.\n" RESET);
    }
    
    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_banner();
        printf("\n" BOLD "ИСПОЛЬЗОВАНИЕ:\n" RESET);
        printf("  %s " YELLOW "compress" RESET " <input> <output>   " CYAN "# Сжать файл\n" RESET, argv[0]);
        printf("  %s " YELLOW "decompress" RESET " <input> <output> " CYAN "# Восстановить файл\n" RESET, argv[0]);
        printf("\n" BOLD "ПРИМЕРЫ:\n" RESET);
        printf("  %s compress data.bin data.kolibri\n", argv[0]);
        printf("  %s decompress data.kolibri data_restored.bin\n", argv[0]);
        printf("\n");
        return 0;
    }
    
    print_banner();
    
    const char* mode = argv[1];
    
    if (strcmp(mode, "compress") == 0) {
        if (argc < 4) {
            printf(RED "✗ Укажите входной и выходной файлы\n" RESET);
            return 1;
        }
        print_header("РЕЖИМ: СЖАТИЕ");
        return compress_file(argv[2], argv[3]);
    }
    else if (strcmp(mode, "decompress") == 0) {
        if (argc < 4) {
            printf(RED "✗ Укажите входной и выходной файлы\n" RESET);
            return 1;
        }
        print_header("РЕЖИМ: ВОССТАНОВЛЕНИЕ");
        return decompress_file(argv[2], argv[3]);
    }
    else {
        printf(RED "✗ Неизвестная команда: %s\n" RESET, mode);
        printf("Используйте: compress или decompress\n");
        return 1;
    }
}
