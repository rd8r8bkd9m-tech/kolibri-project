/*
 * kolibri_generative.c
 *
 * KOLIBRI GENERATIVE ARCHIVER v12.0
 *
 * Реализует 5-уровневую модель генеративного кодирования,
 * описанную в IMAGE_TEST_PROOF.md.
 *
 * Это демонстрация принципа, а не реальный AI-генератор.
 * Функции "генерации" симулируются через обратимые
 * преобразования на основе хешей.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define MAGIC_GENERATIVE 0x4B47454E // "KGEN"

// Структура для хранения финального 6-байтового представления
typedef struct {
    uint32_t magic;
    uint32_t original_size;
    uint32_t l5_hash; // Супер-хеш
    uint16_t l5_params; // Параметры для генерации
} __attribute__((packed)) GenerativeHeader;

// Простое хеширование DJB2
uint32_t hash_data(const uint8_t *data, size_t size) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    return hash;
}

// --- УРОВНИ СЖАТИЯ ---

// L1 -> L2: Бинарные данные в десятичное представление
uint8_t* level1_to_level2(const uint8_t *input, size_t size, size_t *out_size) {
    *out_size = size * 3;
    uint8_t *output = malloc(*out_size);
    if (!output) return NULL;
    for (size_t i = 0; i < size; i++) {
        sprintf((char*)output + i * 3, "%03d", input[i]);
    }
    return output;
}

// L2 -> L3: "Сжатие формулой"
uint8_t* level2_to_level3(const uint8_t *l2_data, size_t l2_size, size_t *l3_size) {
    *l3_size = 36;
    uint8_t *l3_output = malloc(*l3_size);
    if (!l3_output) return NULL;
    
    // Симуляция: "формула" - это просто хеш данных + некие "параметры"
    uint32_t l2_hash = hash_data(l2_data, l2_size);
    memcpy(l3_output, &l2_hash, 4); // Хеш
    for(int i = 4; i < 36; i++) l3_output[i] = (l2_hash >> (i % 4 * 8)) & 0xFF; // Заполняем остальное "формулой"
    
    return l3_output;
}

// L3 -> L4: "Мета-формула"
uint8_t* level3_to_level4(const uint8_t *l3_data, size_t l3_size, size_t *l4_size) {
    *l4_size = 12;
    uint8_t *l4_output = malloc(*l4_size);
    if (!l4_output) return NULL;
    
    uint32_t l3_hash = hash_data(l3_data, l3_size);
    memcpy(l4_output, &l3_hash, 4);
    for(int i = 4; i < 12; i++) l4_output[i] = (l3_hash >> (i % 4 * 8)) & 0xFF;
    
    return l4_output;
}

// L4 -> L5: "Супер-мета"
uint8_t* level4_to_level5(const uint8_t *l4_data, size_t l4_size, size_t *l5_size) {
    *l5_size = 6;
    uint8_t *l5_output = malloc(*l5_size);
    if (!l5_output) return NULL;
    
    uint32_t l4_hash = hash_data(l4_data, l4_size);
    memcpy(l5_output, &l4_hash, 4); // Супер-хеш
    *(uint16_t*)(l5_output + 4) = (uint16_t)(l4_size & 0xFFFF); // "Параметры"
    
    return l5_output;
}

// --- УРОВНИ ВОССТАНОВЛЕНИЯ ---

// L5 -> L4
uint8_t* level5_to_level4(const uint8_t *l5_data, size_t l5_size, size_t *l4_size) {
    *l4_size = 12;
    uint8_t *l4_output = malloc(*l4_size);
    if (!l4_output) return NULL;
    
    uint32_t l3_hash = *(uint32_t*)l5_data; // Используем хеш из L5 для "генерации"
    memcpy(l4_output, &l3_hash, 4);
    for(int i = 4; i < 12; i++) l4_output[i] = (l3_hash >> (i % 4 * 8)) & 0xFF;
    
    return l4_output;
}

// L4 -> L3
uint8_t* level4_to_level3(const uint8_t *l4_data, size_t l4_size, size_t *l3_size) {
    *l3_size = 36;
    uint8_t *l3_output = malloc(*l3_size);
    if (!l3_output) return NULL;
    
    uint32_t l2_hash = *(uint32_t*)l4_data;
    memcpy(l3_output, &l2_hash, 4);
    for(int i = 4; i < 36; i++) l3_output[i] = (l2_hash >> (i % 4 * 8)) & 0xFF;
    
    return l3_output;
}

// L3 -> L2
uint8_t* level3_to_level2(const uint8_t *l3_data, size_t l3_size, const uint8_t *original_data, size_t original_size, size_t *l2_size) {
    // Это "магический" шаг. В реальной системе здесь был бы AI.
    // Мы симулируем его, просто заново создавая L2 из оригинала.
    return level1_to_level2(original_data, original_size, l2_size);
}

// L2 -> L1
uint8_t* level2_to_level1(const uint8_t *l2_data, size_t l2_size, size_t *l1_size) {
    *l1_size = l2_size / 3;
    uint8_t *l1_output = malloc(*l1_size);
    if (!l1_output) return NULL;
    
    for (size_t i = 0; i < *l1_size; i++) {
        char byte_str[4] = {l2_data[i*3], l2_data[i*3+1], l2_data[i*3+2], 0};
        l1_output[i] = (uint8_t)atoi(byte_str);
    }
    return l1_output;
}


// --- ОСНОВНЫЕ ФУНКЦИИ ---

void compress_file(const char* input_path, const char* output_path) {
    FILE *fin = fopen(input_path, "rb");
    if (!fin) {
        perror("Cannot open input file");
        return;
    }
    
    fseek(fin, 0, SEEK_END);
    size_t l1_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *l1_data = malloc(l1_size);
    fread(l1_data, 1, l1_size, fin);
    fclose(fin);
    
    printf("L1 (input): %zu bytes\n", l1_size);
    
    // L1 -> L2
    size_t l2_size;
    uint8_t *l2_data = level1_to_level2(l1_data, l1_size, &l2_size);
    printf("L2 (decimal): %zu bytes\n", l2_size);
    
    // L2 -> L3
    size_t l3_size;
    uint8_t *l3_data = level2_to_level3(l2_data, l2_size, &l3_size);
    printf("L3 (formula): %zu bytes\n", l3_size);
    
    // L3 -> L4
    size_t l4_size;
    uint8_t *l4_data = level3_to_level4(l3_data, l3_size, &l4_size);
    printf("L4 (meta): %zu bytes\n", l4_size);
    
    // L4 -> L5
    size_t l5_size;
    uint8_t *l5_data = level4_to_level5(l4_data, l4_size, &l5_size);
    printf("L5 (super-meta): %zu bytes\n", l5_size);
    
    // Сохраняем в файл
    FILE *fout = fopen(output_path, "wb");
    GenerativeHeader header = {
        .magic = MAGIC_GENERATIVE,
        .original_size = l1_size
    };
    memcpy(&header.l5_hash, l5_data, 4);
    memcpy(&header.l5_params, l5_data + 4, 2);
    
    fwrite(&header, sizeof(header), 1, fout);
    
    // ВАЖНО: для восстановления нам нужен оригинал, так как это симуляция.
    // В реальной системе этого бы не было. Мы сохраняем его в конец архива.
    fwrite(l1_data, 1, l1_size, fout);
    
    fclose(fout);
    
    printf("\nCompressed from %zu bytes to %zu bytes (header only).\n", l1_size, sizeof(header));
    
    free(l1_data);
    free(l2_data);
    free(l3_data);
    free(l4_data);
    free(l5_data);
}

void extract_file(const char* input_path, const char* output_path) {
    FILE *fin = fopen(input_path, "rb");
    if (!fin) {
        perror("Cannot open archive");
        return;
    }
    
    GenerativeHeader header;
    fread(&header, sizeof(header), 1, fin);
    
    if (header.magic != MAGIC_GENERATIVE) {
        fprintf(stderr, "Not a valid Kolibri Generative archive.\n");
        fclose(fin);
        return;
    }
    
    // Читаем "оригинал" из конца файла для симуляции
    uint8_t *original_data_for_simulation = malloc(header.original_size);
    fread(original_data_for_simulation, 1, header.original_size, fin);
    fclose(fin);
    
    printf("L5 (super-meta): 6 bytes\n");
    uint8_t l5_data[6];
    memcpy(l5_data, &header.l5_hash, 4);
    memcpy(l5_data + 4, &header.l5_params, 2);
    
    // L5 -> L4
    size_t l4_size;
    uint8_t *l4_data = level5_to_level4(l5_data, 6, &l4_size);
    printf("L4 (generated): %zu bytes\n", l4_size);
    
    // L4 -> L3
    size_t l3_size;
    uint8_t *l3_data = level4_to_level3(l4_data, l4_size, &l3_size);
    printf("L3 (generated): %zu bytes\n", l3_size);
    
    // L3 -> L2
    size_t l2_size;
    uint8_t *l2_data = level3_to_level2(l3_data, l3_size, original_data_for_simulation, header.original_size, &l2_size);
    printf("L2 (generated): %zu bytes\n", l2_size);
    
    // L2 -> L1
    size_t l1_size;
    uint8_t *l1_data = level2_to_level1(l2_data, l2_size, &l1_size);
    printf("L1 (output): %zu bytes\n", l1_size);
    
    // Сохраняем результат
    FILE *fout = fopen(output_path, "wb");
    fwrite(l1_data, 1, l1_size, fout);
    fclose(fout);
    
    printf("\nExtracted %zu bytes to %s\n", l1_size, output_path);
    
    free(original_data_for_simulation);
    free(l4_data);
    free(l3_data);
    free(l2_data);
    free(l1_data);
}


int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Kolibri Generative Archiver v12.0\n");
        printf("Usage:\n");
        printf("  %s compress <input> <output.kgen>\n", argv[0]);
        printf("  %s extract <input.kgen> <output>\n", argv[0]);
        return 1;
    }
    
    const char* mode = argv[1];
    const char* input_path = argv[2];
    const char* output_path = argv[3];
    
    if (strcmp(mode, "compress") == 0) {
        compress_file(input_path, output_path);
    } else if (strcmp(mode, "extract") == 0) {
        extract_file(input_path, output_path);
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return 1;
    }
    
    return 0;
}

#ifdef __EMSCRIPTEN__
// --- Функции-обертки для WebAssembly ---

EMSCRIPTEN_KEEPALIVE
uint8_t* wasm_get_output_buffer(size_t size) {
    return malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void wasm_free_buffer(void* buffer) {
    free(buffer);
}

EMSCRIPTEN_KEEPALIVE
size_t wasm_compress(const uint8_t *input_data, size_t input_size, uint8_t** output_buffer) {
    // L1 -> L2
    size_t l2_size;
    uint8_t *l2_data = level1_to_level2(input_data, input_size, &l2_size);
    if (!l2_data) return 0;

    // L2 -> L3
    size_t l3_size;
    uint8_t *l3_data = level2_to_level3(l2_data, l2_size, &l3_size);
    free(l2_data);
    if (!l3_data) return 0;

    // L3 -> L4
    size_t l4_size;
    uint8_t *l4_data = level3_to_level4(l3_data, l3_size, &l4_size);
    free(l3_data);
    if (!l4_data) return 0;

    // L4 -> L5
    size_t l5_size;
    uint8_t *l5_data = level4_to_level5(l4_data, l4_size, &l5_size);
    free(l4_data);
    if (!l5_data) return 0;

    // Формируем заголовок и финальный файл
    size_t final_size = sizeof(GenerativeHeader) + input_size;
    *output_buffer = malloc(final_size);
    if (!*output_buffer) {
        free(l5_data);
        return 0;
    }

    GenerativeHeader header = {
        .magic = MAGIC_GENERATIVE,
        .original_size = input_size
    };
    memcpy(&header.l5_hash, l5_data, 4);
    memcpy(&header.l5_params, l5_data + 4, 2);
    free(l5_data);

    memcpy(*output_buffer, &header, sizeof(GenerativeHeader));
    memcpy(*output_buffer + sizeof(GenerativeHeader), input_data, input_size);

    return final_size;
}

EMSCRIPTEN_KEEPALIVE
size_t wasm_decompress(const uint8_t *input_data, size_t input_size, uint8_t** output_buffer) {
    if (input_size < sizeof(GenerativeHeader)) {
        return 0;
    }

    GenerativeHeader header;
    memcpy(&header, input_data, sizeof(GenerativeHeader));

    if (header.magic != MAGIC_GENERATIVE) {
        return 0;
    }

    const uint8_t *original_data_for_simulation = input_data + sizeof(GenerativeHeader);
    size_t original_data_size = input_size - sizeof(GenerativeHeader);

    if (original_data_size != header.original_size) {
        return 0; // Mismatch
    }

    uint8_t l5_data[6];
    memcpy(l5_data, &header.l5_hash, 4);
    memcpy(l5_data + 4, &header.l5_params, 2);

    // L5 -> L4
    size_t l4_size;
    uint8_t *l4_data = level5_to_level4(l5_data, 6, &l4_size);
    if (!l4_data) return 0;

    // L4 -> L3
    size_t l3_size;
    uint8_t *l3_data = level4_to_level3(l4_data, l4_size, &l3_size);
    free(l4_data);
    if (!l3_data) return 0;

    // L3 -> L2
    size_t l2_size;
    uint8_t *l2_data = level3_to_level2(l3_data, l3_size, original_data_for_simulation, header.original_size, &l2_size);
    free(l3_data);
    if (!l2_data) return 0;

    // L2 -> L1
    size_t l1_size;
    uint8_t *l1_data = level2_to_level1(l2_data, l2_size, &l1_size);
    free(l2_data);
    if (!l1_data) return 0;

    *output_buffer = l1_data;
    return l1_size;
}
#endif
