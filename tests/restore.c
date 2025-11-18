#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Читаем закодированные цифры
    FILE *f = fopen("image_encoded.txt", "r");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *encoded = malloc(size + 1);
    fread(encoded, 1, size, f);
    encoded[size] = '\0';
    fclose(f);
    
    printf("📥 Загружено %zu цифр из image_encoded.txt\n", size);
    
    // Декодируем: каждые 3 цифры → 1 байт
    size_t output_size = size / 3;
    unsigned char *decoded = malloc(output_size);
    
    for (size_t i = 0; i < output_size; i++) {
        char triplet[4];
        memcpy(triplet, encoded + i*3, 3);
        triplet[3] = '\0';
        decoded[i] = atoi(triplet);
    }
    
    printf("🔢 Декодировано %zu bytes\n", output_size);
    
    // Сохраняем
    FILE *out = fopen("test_image_RECOVERED.png", "wb");
    fwrite(decoded, 1, output_size, out);
    fclose(out);
    
    printf("✅ ВОССТАНОВЛЕНО: test_image_RECOVERED.png\n");
    
    free(encoded);
    free(decoded);
    return 0;
}
