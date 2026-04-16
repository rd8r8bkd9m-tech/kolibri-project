#include "kolibri/symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void kolibri_symbol_table_next_digits(KolibriSymbolTable *table,
                                             uint8_t out_digits[KOLIBRI_SYMBOL_DIGITS]);

static int kolibri_symbol_table_find(const KolibriSymbolTable *table,
                                     uint32_t codepoint) {
    if (!table) {
        return -1;
    }
    for (size_t i = 0; i < table->count; ++i) {
        if (table->entries[i].codepoint == codepoint) {
            return (int)i;
        }
    }
    return -1;
}

static int kolibri_symbol_table_find_digits(const KolibriSymbolTable *table,
                                            const uint8_t digits[KOLIBRI_SYMBOL_DIGITS]) {
    if (!table || !digits) {
        return -1;
    }
    for (size_t i = 0; i < table->count; ++i) {
        if (memcmp(table->entries[i].digits, digits, KOLIBRI_SYMBOL_DIGITS) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint64_t decode_u64_be_symbol(const unsigned char *data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | (uint64_t)data[i];
    }
    return value;
}

static void symbol_deserialize(const unsigned char *bytes, ReasonBlock *block) {
    memset(block, 0, sizeof(*block));
    block->index = decode_u64_be_symbol(bytes);
    block->timestamp = decode_u64_be_symbol(bytes + 8);
    memcpy(block->prev_hash, bytes + 16, KOLIBRI_HASH_SIZE);
    memcpy(block->hmac, bytes + 16 + KOLIBRI_HASH_SIZE, KOLIBRI_HASH_SIZE);
    memcpy(block->event_type, bytes + 16 + KOLIBRI_HASH_SIZE * 2, KOLIBRI_EVENT_TYPE_SIZE);
    memcpy(block->payload, bytes + 16 + KOLIBRI_HASH_SIZE * 2 + KOLIBRI_EVENT_TYPE_SIZE, KOLIBRI_PAYLOAD_SIZE);
}

static void kolibri_symbol_table_log_add(KolibriSymbolTable *table,
                                         uint32_t codepoint,
                                         const uint8_t digits[KOLIBRI_SYMBOL_DIGITS]) {
    if (!table || !table->genome) {
        return;
    }
    char payload[KOLIBRI_PAYLOAD_SIZE];
    memset(payload, 0, sizeof(payload));
    int written = snprintf(payload,
                           sizeof(payload),
                           "%06u|%u%u%u",
                           (unsigned int)codepoint,
                           digits[0],
                           digits[1],
                           digits[2]);
    if (written <= 0 || (size_t)written >= sizeof(payload)) {
        return;
    }
    kg_append(table->genome, "SYMBOL_MAP", payload, NULL);
}

static void kolibri_symbol_table_add_entry(KolibriSymbolTable *table,
                                           uint32_t codepoint,
                                           const uint8_t digits[KOLIBRI_SYMBOL_DIGITS],
                                           int log_event) {
    if (!table || table->count >= KOLIBRI_SYMBOL_MAX) {
        return;
    }
    KolibriSymbolEntry *entry = &table->entries[table->count++];
    entry->codepoint = codepoint;
    memcpy(entry->digits, digits, KOLIBRI_SYMBOL_DIGITS);
    table->version += 1U;
    if (log_event) {
        kolibri_symbol_table_log_add(table, codepoint, digits);
    }
}

static void kolibri_symbol_table_seed_entry(KolibriSymbolTable *table, uint32_t codepoint) {
    if (!table) {
        return;
    }
    if (kolibri_symbol_table_find(table, codepoint) >= 0) {
        return;
    }
    uint8_t digits[KOLIBRI_SYMBOL_DIGITS];
    kolibri_symbol_table_next_digits(table, digits);
    kolibri_symbol_table_add_entry(table, codepoint, digits, 1);
}

void kolibri_symbol_table_init(KolibriSymbolTable *table, KolibriGenome *genome) {
    if (!table) {
        return;
    }
    memset(table, 0, sizeof(*table));
    table->genome = genome;
}

void kolibri_symbol_table_load(KolibriSymbolTable *table) {
    if (!table || !table->genome || !table->genome->file) {
        return;
    }
    KolibriGenome *ctx = table->genome;
    long original_pos = ftell(ctx->file);
    if (original_pos < 0) {
        original_pos = 0;
    }
    if (fseek(ctx->file, 0, SEEK_SET) != 0) {
        return;
    }
    unsigned char bytes[KOLIBRI_BLOCK_SIZE];
    while (fread(bytes, 1, KOLIBRI_BLOCK_SIZE, ctx->file) == KOLIBRI_BLOCK_SIZE) {
        ReasonBlock block;
        symbol_deserialize(bytes, &block);
        if (strncmp(block.event_type, "SYMBOL_MAP", KOLIBRI_EVENT_TYPE_SIZE) != 0) {
            continue;
        }
        char payload[KOLIBRI_PAYLOAD_SIZE + 1];
        memcpy(payload, block.payload, KOLIBRI_PAYLOAD_SIZE);
        payload[KOLIBRI_PAYLOAD_SIZE] = '\0';
        unsigned int d0 = 0U;
        unsigned int d1 = 0U;
        unsigned int d2 = 0U;
        uint32_t codepoint = 0U;
        const char *separator = strchr(payload, '|');
        if (separator) {
            size_t cp_len = (size_t)(separator - payload);
            if (cp_len == 0U || cp_len >= 16U) {
                continue;
            }
            char buffer[16];
            memcpy(buffer, payload, cp_len);
            buffer[cp_len] = '\0';
            char *endptr = NULL;
            unsigned long value = strtoul(buffer, &endptr, 10);
            if (!endptr || *endptr != '\0' || value > 0x10FFFFUL) {
                continue;
            }
            if (strlen(separator + 1) < 3U) {
                continue;
            }
            const char *digits_str = separator + 1;
            if (digits_str[0] < '0' || digits_str[0] > '9' ||
                digits_str[1] < '0' || digits_str[1] > '9' ||
                digits_str[2] < '0' || digits_str[2] > '9') {
                continue;
            }
            d0 = (unsigned int)(digits_str[0] - '0');
            d1 = (unsigned int)(digits_str[1] - '0');
            d2 = (unsigned int)(digits_str[2] - '0');
            codepoint = (uint32_t)value;
        } else {
            unsigned int ascii = 0U;
            if (sscanf(payload, "%03u%1u%1u%1u", &ascii, &d0, &d1, &d2) != 4) {
                continue;
            }
            codepoint = (uint32_t)ascii;
        }
        uint8_t digits[KOLIBRI_SYMBOL_DIGITS] = { (uint8_t)d0, (uint8_t)d1, (uint8_t)d2 };
        if (kolibri_symbol_table_find(table, codepoint) >= 0) {
            continue;
        }
        kolibri_symbol_table_add_entry(table, codepoint, digits, 0);
    }
    fseek(ctx->file, original_pos, SEEK_SET);
}

void kolibri_symbol_table_seed_defaults(KolibriSymbolTable *table) {
    if (!table) {
        return;
    }
    static const uint32_t punctuation[] = {
        0x0020U, 0x002E, 0x002C, 0x003F, 0x0021, 0x003A, 0x003B, 0x002D,
        0x0028, 0x0029, 0x0022, 0x0027
    };
    for (size_t i = 0; i < sizeof(punctuation) / sizeof(punctuation[0]); ++i) {
        kolibri_symbol_table_seed_entry(table, punctuation[i]);
    }

    for (uint32_t digit = 0x0030; digit <= 0x0039; ++digit) {
        kolibri_symbol_table_seed_entry(table, digit);
    }

    for (uint32_t letter = 0x0410; letter <= 0x0415; ++letter) {
        kolibri_symbol_table_seed_entry(table, letter);
    }
    kolibri_symbol_table_seed_entry(table, 0x0401U);
    for (uint32_t letter = 0x0416; letter <= 0x042F; ++letter) {
        kolibri_symbol_table_seed_entry(table, letter);
    }

    for (uint32_t letter = 0x0430; letter <= 0x0435; ++letter) {
        kolibri_symbol_table_seed_entry(table, letter);
    }
    kolibri_symbol_table_seed_entry(table, 0x0451U);
    for (uint32_t letter = 0x0436; letter <= 0x044F; ++letter) {
        kolibri_symbol_table_seed_entry(table, letter);
    }
}

static void kolibri_symbol_table_next_digits(KolibriSymbolTable *table,
                                             uint8_t out_digits[KOLIBRI_SYMBOL_DIGITS]) {
    size_t index = table->count;
    /* простое последовательное распределение */
    out_digits[0] = (uint8_t)((index / 100U) % 10U);
    out_digits[1] = (uint8_t)((index / 10U) % 10U);
    out_digits[2] = (uint8_t)(index % 10U);
}

int kolibri_symbol_encode(KolibriSymbolTable *table,
                          uint32_t codepoint,
                          uint8_t out_digits[KOLIBRI_SYMBOL_DIGITS]) {
    if (!table || !out_digits) {
        return -1;
    }
    int index = kolibri_symbol_table_find(table, codepoint);
    if (index >= 0) {
        memcpy(out_digits, table->entries[index].digits, KOLIBRI_SYMBOL_DIGITS);
        return 0;
    }
    uint8_t digits[KOLIBRI_SYMBOL_DIGITS];
    kolibri_symbol_table_next_digits(table, digits);
    kolibri_symbol_table_add_entry(table, codepoint, digits, 1);
    memcpy(out_digits, digits, KOLIBRI_SYMBOL_DIGITS);
    return 0;
}

int kolibri_symbol_decode(const KolibriSymbolTable *table,
                          const uint8_t digits[KOLIBRI_SYMBOL_DIGITS],
                          uint32_t *out_codepoint) {
    if (!table || !digits || !out_codepoint) {
        return -1;
    }
    int index = kolibri_symbol_table_find_digits(table, digits);
    if (index < 0) {
        return -1;
    }
    *out_codepoint = table->entries[index].codepoint;
    return 0;
}
