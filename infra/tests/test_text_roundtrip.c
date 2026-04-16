/* Text Roundtrip Test - Real world UTF-8 testing */
#include "kolibri/decimal.h"
#include "support.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define MAX_DECIMAL 2097152  /* 2MB for encoded output */
#define GREEN "\033[32m"
#define CYAN "\033[36m"
#define YELLOW "\033[33m"
#define RED "\033[31m"
#define RESET "\033[0m"

static void test_text(const char *name, const char *text) {
    char decimal[MAX_DECIMAL];
    char decoded[MAX_DECIMAL];
    
    printf("\n%s=== %s ===%s\n", CYAN, name, RESET);
    printf("Input (%zu bytes): %s\n", strlen(text), text);
    
    int enc_result = k_encode_text(text, decimal, sizeof(decimal));
    printf("%sEncode result: %d%s\n", YELLOW, enc_result, RESET);
    printf("Decimal (%zu chars): %.100s%s\n", 
           strlen(decimal), decimal, strlen(decimal) > 100 ? "..." : "");
    
    int dec_result = k_decode_text(decimal, decoded, sizeof(decoded));
    printf("%sDecode result: %d%s\n", YELLOW, dec_result, RESET);
    printf("Output (%zu bytes): %s\n", strlen(decoded), decoded);
    
    if (strcmp(text, decoded) == 0) {
        printf("%s✓ PASSED%s\n", GREEN, RESET);
    } else {
        printf("%s✗ FAILED - mismatch!%s\n", RED, RESET);
        printf("  Expected: %s\n", text);
        printf("  Got:      %s\n", decoded);
    }
}

int main(void) {
    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║   TEXT ROUNDTRIP TEST (Real World Data)  ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    
    test_text("ASCII", "Hello, World!");
    test_text("Numbers", "0123456789");
    test_text("Russian", "Привет");
    test_text("Emoji", "🚀");
    
    /* Сложный текст: ~2000 символов - смесь языков, эмодзи, математика, спецсимволы */
    const char *complex_text = 
        "🚀 Kolibri OS - Операционная Система Десятичной Эволюции 🔥\n"
        "════════════════════════════════════════════════════════════\n\n"
        "📋 ОПИСАНИЕ (Russian/English Mix):\n"
        "Kolibri - это революционная ОС, использующая десятичное кодирование данных.\n"
        "The system combines AI evolution, genome logging, and swarm networking!\n\n"
        "🧬 КЛЮЧЕВЫЕ КОМПОНЕНТЫ:\n"
        "• decimal.c → UTF-8 ↔ Decimal encoding (производительность: 2.77×10¹⁰ chars/sec)\n"
        "• formula.c → Evolutionary algorithms with fitness functions 📊\n"
        "• genome.c → HMAC-protected append-only journal 🔐\n"
        "• ai_encoder.c → Optimized encoding for AI genes (283x improvement!) ⚡\n\n"
        "💻 ТЕХНОЛОГИИ:\n"
        "Languages: C (kernel), Python (backend), TypeScript (frontend)\n"
        "Performance: -O3 -march=native compiler optimizations\n"
        "Security: BLAKE2, HMAC-SHA256, KSP protocol 🛡️\n\n"
        "🌐 МАТЕМАТИКА & СИМВОЛЫ:\n"
        "Formula: f(x) = ax² + bx + c, где a∈ℝ, b∈ℝ, c∈ℝ\n"
        "Fitness: ∑(prediction - target)² / n → минимум\n"
        "Encoding: byte → 3 digits (例: 255 → \"255\", 0 → \"000\")\n"
        "Special chars: !@#$%^&*()_+-=[]{}|;':\",./<>?\\`~\n\n"
        "🎯 РЕЗУЛЬТАТЫ ТЕСТИРОВАНИЯ:\n"
        "✓ All 50+ decimal tests: PASSED ✅\n"
        "✓ AI encoder tests (7/7): PASSED 🎉\n"
        "✓ Roundtrip: ASCII, Кириллица, 日本語, العربية, עברית ✓\n"
        "✓ Emoji: 🚀🔥🧬✨💻��🎯📊🛡️⚡ (all preserved!)\n\n"
        "📈 ПРОИЗВОДИТЕЛЬНОСТЬ:\n"
        "Simple division: 2.77×10¹⁰ chars/sec (FASTEST! 🏆)\n"
        "LUT table: 4.82×10⁹ chars/sec (5.7x slower ❌)\n"
        "Loop unrolling: 3.32×10⁹ chars/sec (8x slower ❌)\n\n"
        "🔬 WHY IT WORKS:\n"
        "Clang -O3 transforms division into: multiply + shift = magic! ✨\n"
        "No branches → perfect CPU prediction → pipeline goes brrrr 🚀\n\n"
        "🌍 INTERNATIONAL SUPPORT:\n"
        "English: Hello World!\n"
        "Русский: Привет мир!\n"
        "日本語: こんにちは世界！\n"
        "中文: 你好世界！\n"
        "한국어: 안녕하세요 세계！\n"
        "العربية: مرحبا بالعالم!\n"
        "עברית: שלום עולם!\n"
        "Ελληνικά: Γεια σου κόσμε!\n\n"
        "Made with ❤️ by Kolibri Team © 2025\n"
        "Buffer size increased: 512 → 1MB (2048x larger! 🚀)";
    
    test_text("Complex 2000+ chars", complex_text);
    
    /* Тест на очень большой текст: 10K+ символов */
    printf("\n%s=== STRESS TEST: Generating 10K+ chars text ===%s\n", YELLOW, RESET);
    
    char *huge_text = (char *)malloc(15000);
    if (huge_text) {
        char *ptr = huge_text;
        ptr += sprintf(ptr, "🚀 STRESS TEST: 10,000+ Character UTF-8 Roundtrip 🚀\n");
        ptr += sprintf(ptr, "═══════════════════════════════════════════════════\n\n");
        
        /* Повторяем блок текста 10 раз */
        for (int i = 0; i < 10; i++) {
            ptr += sprintf(ptr, 
                "=== Block %d/10 ===\n"
                "🌍 International: English, Русский, 日本語, 中文, 한국어, العربية, עברית, Ελληνικά\n"
                "📊 Math: ∑∫∂∇±×÷√∞≈≠≤≥∈∉⊂⊃∪∩∧∨¬∀∃\n"
                "🔤 Latin: ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz\n"
                "🔢 Numbers: 0123456789 ⁰¹²³⁴⁵⁶⁷⁸⁹ ₀₁₂₃₄₅₆₇₈₉\n"
                "💻 Code: func main() { println!(\"Hello, 世界\"); } // コメント\n"
                "🎯 Emoji: 😀😃😄😁😆😅🤣😂🙂🙃😉😊😇🥰😍🤩😘😗☺😚😙🥲\n"
                "⚡ Special: !@#$%%^&*()_+-=[]{}|;':\",./<>?\\`~§±¶•ªº«»¿¡\n"
                "🔐 Security: HMAC-SHA256, BLAKE2, AES-256-GCM, RSA-4096\n"
                "📈 Performance: 2.77×10¹⁰ chars/sec (27.7 GB/sec!)\n"
                "🧬 DNA: ACGT ACGT ACGT ACGT ACGT ACGT ACGT ACGT\n"
                "🎵 Music: 𝄞 ♩ ♪ ♫ ♬ 𝅗𝅥 𝅘𝅥 𝅘𝅥𝅮 𝅘𝅥𝅯 𝅘𝅥𝅰 𝅘𝅥𝅱 𝅘𝅥𝅲\n"
                "🌈 Colors: Red🔴 Orange🟠 Yellow🟡 Green🟢 Blue🔵 Purple🟣\n\n", 
                i + 1);
        }
        
        ptr += sprintf(ptr, "✅ END OF STRESS TEST - Total size: ~%ld chars\n", ptr - huge_text);
        
        test_text("HUGE 10K+ chars", huge_text);
        free(huge_text);
    }
    
    printf("\n");
    return 0;
}
