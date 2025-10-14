/*
 * KolibriScript runtime and parser implementation.
 * Supports high-level Russian DSL with variables, control flow and associations.
 */

#include "kolibri/script.h"
#include "kolibri/decimal.h"

#include <ctype.h>
#include <errno.h>
#include <openssl/sha.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KOLIBRI_MAX_LOOP_ITERATIONS 1024
#define KOLIBRI_ARRAY_GROWTH_FACTOR 2U

static void kolibri_digit_text_clear(KolibriDigitText *text) {
    if (!text) {
        return;
    }
    text->length = 0;
}

static void kolibri_hex_encode(const unsigned char *data, size_t len, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    if (!data || len == 0) {
        out[0] = '\0';
        return;
    }
    static const char lookup[] = "0123456789abcdef";
    size_t required = len * 2U + 1U;
    if (out_len < required) {
        if (out_len > 0) {
            out[0] = '\0';
        }
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char byte = data[i];
        out[i * 2U] = lookup[(byte >> 4) & 0x0FU];
        out[i * 2U + 1U] = lookup[byte & 0x0FU];
    }
    out[len * 2U] = '\0';
}

static void kolibri_crystal_init(KolibriCrystalCore *core) {
    if (!core) {
        return;
    }
    kolibri_digit_text_init(&core->stimulus);
    kolibri_digit_text_init(&core->response);
    kolibri_digit_text_init(&core->question);
    kolibri_digit_text_init(&core->answer);
    kolibri_digit_text_init(&core->expected);
    memset(core->teach_hash, 0, sizeof(core->teach_hash));
    memset(core->evaluate_hash, 0, sizeof(core->evaluate_hash));
    core->has_teach = 0;
    core->has_evaluate = 0;
}

static void kolibri_crystal_reset(KolibriCrystalCore *core) {
    if (!core) {
        return;
    }
    kolibri_digit_text_clear(&core->stimulus);
    kolibri_digit_text_clear(&core->response);
    kolibri_digit_text_clear(&core->question);
    kolibri_digit_text_clear(&core->answer);
    kolibri_digit_text_clear(&core->expected);
    memset(core->teach_hash, 0, sizeof(core->teach_hash));
    memset(core->evaluate_hash, 0, sizeof(core->evaluate_hash));
    core->has_teach = 0;
    core->has_evaluate = 0;
}

static void kolibri_crystal_free(KolibriCrystalCore *core) {
    if (!core) {
        return;
    }
    kolibri_digit_text_free(&core->stimulus);
    kolibri_digit_text_free(&core->response);
    kolibri_digit_text_free(&core->question);
    kolibri_digit_text_free(&core->answer);
    kolibri_digit_text_free(&core->expected);
    memset(core->teach_hash, 0, sizeof(core->teach_hash));
    memset(core->evaluate_hash, 0, sizeof(core->evaluate_hash));
    core->has_teach = 0;
    core->has_evaluate = 0;
}

static void kolibri_crystal_compute_hash(const char *stage_tag,
                                         const unsigned char *prev_hash,
                                         size_t prev_len,
                                         const KolibriDigitText *first,
                                         const KolibriDigitText *second,
                                         unsigned char out_hash[SHA256_DIGEST_LENGTH]) {
    if (!out_hash) {
        return;
    }
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    if (stage_tag) {
        SHA256_Update(&ctx, stage_tag, strlen(stage_tag));
    }
    if (prev_hash && prev_len > 0U) {
        SHA256_Update(&ctx, prev_hash, prev_len);
    }
    if (first && first->length > 0U) {
        SHA256_Update(&ctx, first->digits, first->length);
    }
    if (second && second->length > 0U) {
        SHA256_Update(&ctx, second->digits, second->length);
    }
    SHA256_Final(out_hash, &ctx);
}

typedef struct {
    size_t line;
    size_t column;
} KolibriSourceLocation;

typedef struct {
    KolibriSourceLocation start;
    KolibriSourceLocation end;
} KolibriSourceSpan;

typedef enum {
    KOLIBRI_TOKEN_EOF = 0,
    KOLIBRI_TOKEN_NEWLINE,
    KOLIBRI_TOKEN_KEYWORD,
    KOLIBRI_TOKEN_IDENT,
    KOLIBRI_TOKEN_STRING,
    KOLIBRI_TOKEN_NUMBER,
    KOLIBRI_TOKEN_COLON,
    KOLIBRI_TOKEN_DOT,
    KOLIBRI_TOKEN_ASSIGN,
    KOLIBRI_TOKEN_ARROW,
    KOLIBRI_TOKEN_GREATER,
    KOLIBRI_TOKEN_GREATER_EQUAL,
    KOLIBRI_TOKEN_LESS,
    KOLIBRI_TOKEN_LESS_EQUAL,
    KOLIBRI_TOKEN_EQUAL,
    KOLIBRI_TOKEN_NOT_EQUAL,
} KolibriTokenType;

typedef struct {
    KolibriTokenType type;
    char *lexeme;
    KolibriSourceSpan span;
} KolibriToken;

typedef struct {
    KolibriToken *data;
    size_t count;
    size_t capacity;
} KolibriTokenBuffer;

typedef struct {
    char *message;
    KolibriSourceSpan span;
} KolibriDiagnostic;

typedef struct {
    KolibriDiagnostic *data;
    size_t count;
    size_t capacity;
} KolibriDiagnosticBuffer;

static void kolibri_token_buffer_init(KolibriTokenBuffer *buffer) {
    buffer->data = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

static void kolibri_token_buffer_free(KolibriTokenBuffer *buffer) {
    if (!buffer) {
        return;
    }
    for (size_t i = 0; i < buffer->count; ++i) {
        free(buffer->data[i].lexeme);
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

static int kolibri_token_buffer_push(KolibriTokenBuffer *buffer, KolibriToken token) {
    if (buffer->count == buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ? 32U : buffer->capacity * KOLIBRI_ARRAY_GROWTH_FACTOR;
        KolibriToken *new_data = (KolibriToken *)realloc(buffer->data, new_capacity * sizeof(KolibriToken));
        if (!new_data) {
            return -1;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    buffer->data[buffer->count++] = token;
    return 0;
}

static void kolibri_diagnostic_buffer_init(KolibriDiagnosticBuffer *buffer) {
    buffer->data = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

static void kolibri_diagnostic_buffer_free(KolibriDiagnosticBuffer *buffer) {
    if (!buffer) {
        return;
    }
    for (size_t i = 0; i < buffer->count; ++i) {
        free(buffer->data[i].message);
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

static int kolibri_diagnostic_buffer_push(KolibriDiagnosticBuffer *buffer, const char *message, KolibriSourceSpan span) {
    if (buffer->count == buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ? 8U : buffer->capacity * KOLIBRI_ARRAY_GROWTH_FACTOR;
        KolibriDiagnostic *new_data = (KolibriDiagnostic *)realloc(buffer->data, new_capacity * sizeof(KolibriDiagnostic));
        if (!new_data) {
            return -1;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    char *dup = NULL;
    if (message) {
        dup = strdup(message);
        if (!dup) {
            return -1;
        }
    }
    buffer->data[buffer->count].message = dup;
    buffer->data[buffer->count].span = span;
    buffer->count += 1U;
    return 0;
}

static inline KolibriSourceLocation kolibri_make_location(size_t line, size_t column) {
    KolibriSourceLocation loc = { line, column };
    return loc;
}

static inline KolibriSourceSpan kolibri_make_span(KolibriSourceLocation start, KolibriSourceLocation end) {
    KolibriSourceSpan span;
    span.start = start;
    span.end = end;
    return span;
}

/* ===================== Lexer ===================== */

static const char *const KOLIBRI_KEYWORDS[] = {
    "начало",
    "конец",
    "переменная",
    "показать",
    "обучить",
    "связь",
    "создать",
    "формулу",
    "формула",
    "из",
    "оценить",
    "на",
    "задаче",
    "если",
    "тогда",
    "иначе",
    "пока",
    "делать",
    "сохранить",
    "в",
    "геном",
    "отбросить",
    "вызвать",
    "эволюцию",
    "распечатать",
    "канву",
    "рой",
    "отправить",
    "фитнес",
    "итог",
    "режим",
    "верифицировать"
};

static void kolibri_to_lower_ascii(const char *src, char *dst, size_t dst_len);
static void kolibri_script_set_mode(KolibriScript *script, const char *mode);
static void kolibri_apply_mode(KolibriScript *script, char *answer);

static size_t kolibri_keyword_count(void) {
    return sizeof(KOLIBRI_KEYWORDS) / sizeof(KOLIBRI_KEYWORDS[0]);
}

static bool kolibri_is_keyword(const char *lexeme) {
    size_t count = kolibri_keyword_count();
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(lexeme, KOLIBRI_KEYWORDS[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool kolibri_is_word_delimiter(unsigned char ch) {
    switch (ch) {
    case '\0':
    case ' ': case '\t': case '\r': case '\n':
    case ':': case '.': case ',': case '"':
    case '>': case '<': case '=': case '!':
    case '(': case ')':
        return true;
    default:
        return false;
    }
}

static void kolibri_generate_text_from_gene(const KolibriFormula *formula,
                                            const char *question,
                                            int numeric_answer,
                                            char *buffer,
                                            size_t buffer_len) {
    if (!buffer || buffer_len == 0) {
        return;
    }
    buffer[0] = '\0';
    if (!formula || formula->gene.length == 0) {
        return;
    }
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    const size_t alphabet_len = sizeof(alphabet) - 1U;
    if (alphabet_len == 0) {
        return;
    }
    uint64_t seed = numeric_answer >= 0 ? (uint64_t)numeric_answer : (uint64_t)(-(int64_t)numeric_answer);
    if (seed == 0) {
        seed = 1U;
    }
    for (size_t i = 0; i < formula->gene.length; ++i) {
        seed = seed * 1315423911ULL + (uint64_t)(formula->gene.digits[i] + 1U);
    }
    if (question) {
        for (const unsigned char *p = (const unsigned char *)question; *p; ++p) {
            seed ^= (uint64_t)(*p + 17U);
            seed *= 11400714819323198485ULL;
        }
    }
    size_t pos = 0;
    size_t words = 3U + (size_t)(seed % 4U); /* 3..6 слов */
    seed = seed * 2862933555777941757ULL + 3037000493ULL;
    for (size_t w = 0; w < words && pos + 2 < buffer_len; ++w) {
        size_t letters = 3U + (size_t)((seed >> 12) % 4U); /* 3..6 букв в слове */
        if (w > 0) {
            buffer[pos++] = ' ';
        }
        for (size_t i = 0; i < letters && pos + 1 < buffer_len; ++i) {
            seed = seed * 1103515245ULL + 12345ULL;
            size_t idx = (size_t)((seed >> 16) % alphabet_len);
            buffer[pos++] = alphabet[idx];
        }
    }
    if (pos >= buffer_len) {
        pos = buffer_len - 1U;
    }
    buffer[pos] = '\0';
    if (pos > 0) {
        buffer[0] = (char)toupper((unsigned char)buffer[0]);
        if (pos + 1 < buffer_len) {
            buffer[pos++] = '.';
            buffer[pos] = '\0';
        }
    }
}


static char *kolibri_strndup(const char *src, size_t len) {
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

typedef struct {
    const char *source;
    size_t length;
    size_t index;
    size_t line;
    size_t column;
    KolibriTokenBuffer *tokens;
    KolibriDiagnosticBuffer *diagnostics;
} KolibriLexer;

static void kolibri_lexer_init(KolibriLexer *lexer, const char *source, KolibriTokenBuffer *tokens,
                               KolibriDiagnosticBuffer *diagnostics) {
    lexer->source = source ? source : "";
    lexer->length = source ? strlen(source) : 0;
    lexer->index = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->tokens = tokens;
    lexer->diagnostics = diagnostics;
}

static KolibriSourceLocation kolibri_lexer_location(const KolibriLexer *lexer) {
    return kolibri_make_location(lexer->line, lexer->column);
}

static void kolibri_lexer_advance(KolibriLexer *lexer, size_t amount) {
    for (size_t i = 0; i < amount && lexer->index < lexer->length; ++i) {
        unsigned char ch = (unsigned char)lexer->source[lexer->index];
        lexer->index += 1U;
        if (ch == '\n') {
            lexer->line += 1U;
            lexer->column = 1U;
        } else {
            lexer->column += 1U;
        }
    }
}

static unsigned char kolibri_lexer_peek(const KolibriLexer *lexer, size_t offset) {
    size_t pos = lexer->index + offset;
    if (pos >= lexer->length) {
        return '\0';
    }
    return (unsigned char)lexer->source[pos];
}

static int kolibri_lexer_emit_token(KolibriLexer *lexer, KolibriTokenType type,
                                    const char *lexeme_start, size_t lexeme_length,
                                    KolibriSourceLocation start_loc, KolibriSourceLocation end_loc) {
    KolibriToken token;
    token.type = type;
    token.lexeme = lexeme_length > 0 ? kolibri_strndup(lexeme_start, lexeme_length) : NULL;
    if (lexeme_length > 0 && !token.lexeme) {
        return -1;
    }
    token.span = kolibri_make_span(start_loc, end_loc);
    if (kolibri_token_buffer_push(lexer->tokens, token) != 0) {
        free(token.lexeme);
        return -1;
    }
    return 0;
}

static int kolibri_lexer_emit_simple(KolibriLexer *lexer, KolibriTokenType type, size_t length) {
    KolibriSourceLocation start = kolibri_lexer_location(lexer);
    const char *lexeme_start = lexer->source + lexer->index;
    kolibri_lexer_advance(lexer, length);
    KolibriSourceLocation end = kolibri_lexer_location(lexer);
    if (end.column > 0) {
        end.column -= 1U;
    }
    return kolibri_lexer_emit_token(lexer, type, lexeme_start, length, start, end);
}

static void kolibri_lexer_skip_whitespace(KolibriLexer *lexer) {
    while (lexer->index < lexer->length) {
        unsigned char ch = (unsigned char)lexer->source[lexer->index];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            kolibri_lexer_advance(lexer, 1U);
        } else {
            break;
        }
    }
}

static int kolibri_lexer_read_number(KolibriLexer *lexer) {
    KolibriSourceLocation start = kolibri_lexer_location(lexer);
    size_t begin = lexer->index;
    bool saw_dot = false;
    while (lexer->index < lexer->length) {
        unsigned char ch = (unsigned char)lexer->source[lexer->index];
        if (ch == '.') {
            if (saw_dot) {
                break;
            }
            saw_dot = true;
            kolibri_lexer_advance(lexer, 1U);
            continue;
        }
        if (!isdigit(ch)) {
            break;
        }
        kolibri_lexer_advance(lexer, 1U);
    }
    size_t len = lexer->index - begin;
    KolibriSourceLocation end = kolibri_lexer_location(lexer);
    if (end.column > 0) {
        end.column -= 1U;
    }
    return kolibri_lexer_emit_token(lexer, KOLIBRI_TOKEN_NUMBER, lexer->source + begin, len, start, end);
}

static int kolibri_lexer_read_string(KolibriLexer *lexer) {
    KolibriSourceLocation start = kolibri_lexer_location(lexer);
    kolibri_lexer_advance(lexer, 1U); /* skip opening quote */
    size_t begin = lexer->index;
    bool escaped = false;
    while (lexer->index < lexer->length) {
        unsigned char ch = (unsigned char)lexer->source[lexer->index];
        if (ch == '\n' && !escaped) {
            kolibri_diagnostic_buffer_push(lexer->diagnostics,
                                            "Строковый литерал не завершён", kolibri_make_span(start, start));
            return -1;
        }
        if (!escaped && ch == '"') {
            break;
        }
        if (!escaped && ch == '\\') {
            escaped = true;
        } else {
            escaped = false;
        }
        kolibri_lexer_advance(lexer, 1U);
    }
    if (lexer->index >= lexer->length) {
        kolibri_diagnostic_buffer_push(lexer->diagnostics,
                                        "Строковый литерал не завершён", kolibri_make_span(start, start));
        return -1;
    }
    size_t literal_len = lexer->index - begin;
    kolibri_lexer_advance(lexer, 1U); /* closing quote */
    KolibriSourceLocation end = kolibri_lexer_location(lexer);
    if (end.column > 0) {
        end.column -= 1U;
    }
    char *raw = kolibri_strndup(lexer->source + begin, literal_len);
    if (!raw) {
        return -1;
    }
    /* Strip escapes */
    char *decoded = (char *)malloc(literal_len + 1U);
    if (!decoded) {
        free(raw);
        return -1;
    }
    size_t write = 0U;
    bool escape = false;
    for (size_t i = 0; i < literal_len; ++i) {
        char ch = raw[i];
        if (escape) {
            switch (ch) {
            case 'n': decoded[write++] = '\n'; break;
            case 't': decoded[write++] = '\t'; break;
            case '\\': decoded[write++] = '\\'; break;
            case '"': decoded[write++] = '"'; break;
            default: decoded[write++] = ch; break;
            }
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else {
            decoded[write++] = ch;
        }
    }
    decoded[write] = '\0';
    free(raw);
    KolibriToken token;
    token.type = KOLIBRI_TOKEN_STRING;
    token.lexeme = decoded;
    token.span = kolibri_make_span(start, end);
    if (kolibri_token_buffer_push(lexer->tokens, token) != 0) {
        free(decoded);
        return -1;
    }
    return 0;
}

static int kolibri_lexer_read_word(KolibriLexer *lexer) {
    KolibriSourceLocation start = kolibri_lexer_location(lexer);
    size_t begin = lexer->index;
    while (lexer->index < lexer->length) {
        unsigned char ch = (unsigned char)lexer->source[lexer->index];
        if (kolibri_is_word_delimiter(ch)) {
            break;
        }
        if (ch == '-' && kolibri_lexer_peek(lexer, 1U) == '>') {
            break;
        }
        kolibri_lexer_advance(lexer, 1U);
    }
    size_t len = lexer->index - begin;
    KolibriSourceLocation end = kolibri_lexer_location(lexer);
    if (len == 0) {
        return -1;
    }
    if (end.column > 0) {
        end.column -= 1U;
    }
    char *word = kolibri_strndup(lexer->source + begin, len);
    if (!word) {
        return -1;
    }
    KolibriTokenType type = kolibri_is_keyword(word) ? KOLIBRI_TOKEN_KEYWORD : KOLIBRI_TOKEN_IDENT;
    KolibriToken token;
    token.type = type;
    token.lexeme = word;
    token.span = kolibri_make_span(start, end);
    if (kolibri_token_buffer_push(lexer->tokens, token) != 0) {
        free(word);
        return -1;
    }
    return 0;
}

static int kolibri_lexer_run(KolibriLexer *lexer) {
    while (lexer->index < lexer->length) {
        kolibri_lexer_skip_whitespace(lexer);
        if (lexer->index >= lexer->length) {
            break;
        }
        unsigned char ch = (unsigned char)lexer->source[lexer->index];
        if (ch == '\n') {
            if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_NEWLINE, 1U) != 0) {
                return -1;
            }
            continue;
        }
        if (ch == '-' && kolibri_lexer_peek(lexer, 1U) == '>') {
            if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_ARROW, 2U) != 0) {
                return -1;
            }
            continue;
        }
        if (ch == ':' ) {
            if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_COLON, 1U) != 0) {
                return -1;
            }
            continue;
        }
        if (ch == '.') {
            if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_DOT, 1U) != 0) {
                return -1;
            }
            continue;
        }
        if (ch == '>' ) {
            if (kolibri_lexer_peek(lexer, 1U) == '=') {
                if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_GREATER_EQUAL, 2U) != 0) {
                    return -1;
                }
            } else {
                if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_GREATER, 1U) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (ch == '<') {
            if (kolibri_lexer_peek(lexer, 1U) == '=') {
                if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_LESS_EQUAL, 2U) != 0) {
                    return -1;
                }
            } else {
                if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_LESS, 1U) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (ch == '=') {
            if (kolibri_lexer_peek(lexer, 1U) == '=') {
                if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_EQUAL, 2U) != 0) {
                    return -1;
                }
            } else {
                if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_ASSIGN, 1U) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (ch == '!' && kolibri_lexer_peek(lexer, 1U) == '=') {
            if (kolibri_lexer_emit_simple(lexer, KOLIBRI_TOKEN_NOT_EQUAL, 2U) != 0) {
                return -1;
            }
            continue;
        }
        if (ch == '"') {
            if (kolibri_lexer_read_string(lexer) != 0) {
                return -1;
            }
            continue;
        }
        if (isdigit(ch)) {
            if (kolibri_lexer_read_number(lexer) != 0) {
                return -1;
            }
            continue;
        }
        if (kolibri_lexer_read_word(lexer) != 0) {
            kolibri_diagnostic_buffer_push(lexer->diagnostics,
                                            "Неизвестный фрагмент сценария", kolibri_make_span(kolibri_lexer_location(lexer), kolibri_lexer_location(lexer)));
            return -1;
        }
    }
    KolibriSourceLocation loc = kolibri_lexer_location(lexer);
    KolibriToken eof_token;
    eof_token.type = KOLIBRI_TOKEN_EOF;
    eof_token.lexeme = NULL;
    eof_token.span = kolibri_make_span(loc, loc);
    if (kolibri_token_buffer_push(lexer->tokens, eof_token) != 0) {
        return -1;
    }
    return 0;
}

/* ===================== AST ===================== */

typedef struct {
    char *text;
    KolibriSourceSpan span;
} KolibriExpression;

typedef enum {
    KOLIBRI_NODE_SHOW = 0,
    KOLIBRI_NODE_VARIABLE,
    KOLIBRI_NODE_TEACH,
    KOLIBRI_NODE_CREATE_FORMULA,
    KOLIBRI_NODE_EVALUATE_FORMULA,
    KOLIBRI_NODE_SAVE_FORMULA,
    KOLIBRI_NODE_DROP_FORMULA,
    KOLIBRI_NODE_CALL_EVOLUTION,
    KOLIBRI_NODE_PRINT_CANVAS,
    KOLIBRI_NODE_SWARM_SEND,
    KOLIBRI_NODE_IF,
    KOLIBRI_NODE_WHILE,
    KOLIBRI_NODE_MODE,
    KOLIBRI_NODE_VERIFY
} KolibriNodeKind;

typedef struct KolibriStatement KolibriStatement;

typedef struct {
    KolibriStatement **items;
    size_t count;
    size_t capacity;
} KolibriStatementList;

struct KolibriStatement {
    KolibriNodeKind kind;
    KolibriSourceSpan span;
    union {
        struct {
            KolibriExpression value;
        } show;
        struct {
            char *name;
            KolibriExpression value;
        } variable;
        struct {
            KolibriExpression left;
            KolibriExpression right;
        } teach;
        struct {
            char *name;
            KolibriExpression expression;
        } create_formula;
        struct {
            char *name;
            KolibriExpression task;
        } evaluate_formula;
        struct {
            char *name;
        } save_formula;
        struct {
            char *name;
        } drop_formula;
        struct {
            char *name;
        } swarm_send;
        struct {
            KolibriExpression condition;
            KolibriStatementList then_body;
            KolibriStatementList else_body;
        } if_stmt;
        struct {
            KolibriExpression condition;
            KolibriStatementList body;
        } while_stmt;
        struct {
            KolibriExpression value;
        } mode_stmt;
        struct {
            KolibriExpression expected;
        } verify;
    } data;
};

typedef struct {
    KolibriStatementList statements;
} KolibriProgram;

static void kolibri_statement_list_init(KolibriStatementList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void kolibri_free_expression(KolibriExpression *expr) {
    if (!expr) {
        return;
    }
    free(expr->text);
    expr->text = NULL;
}

static void kolibri_free_statement(KolibriStatement *stmt);

static void kolibri_statement_list_free(KolibriStatementList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        kolibri_free_statement(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int kolibri_statement_list_push(KolibriStatementList *list, KolibriStatement *stmt) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 8U : list->capacity * KOLIBRI_ARRAY_GROWTH_FACTOR;
        KolibriStatement **new_items = (KolibriStatement **)realloc(list->items, new_capacity * sizeof(KolibriStatement *));
        if (!new_items) {
            return -1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = stmt;
    return 0;
}

static void kolibri_free_statement(KolibriStatement *stmt) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
    case KOLIBRI_NODE_SHOW:
        kolibri_free_expression(&stmt->data.show.value);
        break;
    case KOLIBRI_NODE_VARIABLE:
        free(stmt->data.variable.name);
        kolibri_free_expression(&stmt->data.variable.value);
        break;
    case KOLIBRI_NODE_TEACH:
        kolibri_free_expression(&stmt->data.teach.left);
        kolibri_free_expression(&stmt->data.teach.right);
        break;
    case KOLIBRI_NODE_CREATE_FORMULA:
        free(stmt->data.create_formula.name);
        kolibri_free_expression(&stmt->data.create_formula.expression);
        break;
    case KOLIBRI_NODE_EVALUATE_FORMULA:
        free(stmt->data.evaluate_formula.name);
        kolibri_free_expression(&stmt->data.evaluate_formula.task);
        break;
    case KOLIBRI_NODE_SAVE_FORMULA:
        free(stmt->data.save_formula.name);
        break;
    case KOLIBRI_NODE_DROP_FORMULA:
        free(stmt->data.drop_formula.name);
        break;
    case KOLIBRI_NODE_SWARM_SEND:
        free(stmt->data.swarm_send.name);
        break;
    case KOLIBRI_NODE_IF:
        kolibri_free_expression(&stmt->data.if_stmt.condition);
        kolibri_statement_list_free(&stmt->data.if_stmt.then_body);
        kolibri_statement_list_free(&stmt->data.if_stmt.else_body);
        break;
    case KOLIBRI_NODE_WHILE:
        kolibri_free_expression(&stmt->data.while_stmt.condition);
        kolibri_statement_list_free(&stmt->data.while_stmt.body);
        break;
    case KOLIBRI_NODE_MODE:
        kolibri_free_expression(&stmt->data.mode_stmt.value);
        break;
    case KOLIBRI_NODE_VERIFY:
        kolibri_free_expression(&stmt->data.verify.expected);
        break;
    case KOLIBRI_NODE_CALL_EVOLUTION:
    case KOLIBRI_NODE_PRINT_CANVAS:
        break;
    }
    free(stmt);
}

static void kolibri_program_free(KolibriProgram *program) {
    if (!program) {
        return;
    }
    kolibri_statement_list_free(&program->statements);
}

/* ===================== Parser ===================== */

typedef struct {
    const KolibriToken *tokens;
    size_t count;
    size_t index;
    KolibriDiagnosticBuffer *diagnostics;
} KolibriParser;

static void kolibri_parser_init(KolibriParser *parser, const KolibriTokenBuffer *buffer,
                                KolibriDiagnosticBuffer *diagnostics) {
    parser->tokens = buffer->data;
    parser->count = buffer->count;
    parser->index = 0;
    parser->diagnostics = diagnostics;
}

static const KolibriToken *kolibri_parser_current(const KolibriParser *parser) {
    if (parser->index >= parser->count) {
        return &parser->tokens[parser->count - 1U];
    }
    return &parser->tokens[parser->index];
}

static const KolibriToken *kolibri_parser_previous(const KolibriParser *parser) {
    if (parser->index == 0) {
        return &parser->tokens[0];
    }
    return &parser->tokens[parser->index - 1U];
}

static const KolibriToken *kolibri_parser_advance(KolibriParser *parser) {
    if (parser->index < parser->count) {
        parser->index += 1U;
    }
    return kolibri_parser_previous(parser);
}

static bool kolibri_parser_check(const KolibriParser *parser, KolibriTokenType type) {
    return kolibri_parser_current(parser)->type == type;
}

static bool kolibri_parser_match_token(KolibriParser *parser, KolibriTokenType type) {
    if (kolibri_parser_check(parser, type)) {
        kolibri_parser_advance(parser);
        return true;
    }
    return false;
}

static bool kolibri_parser_match_keyword(KolibriParser *parser, const char *keyword) {
    const KolibriToken *token = kolibri_parser_current(parser);
    if (token->type == KOLIBRI_TOKEN_KEYWORD && token->lexeme && strcmp(token->lexeme, keyword) == 0) {
        kolibri_parser_advance(parser);
        return true;
    }
    return false;
}

static void kolibri_parser_report(KolibriParser *parser, const char *message, const KolibriToken *token) {
    if (!token) {
        return;
    }
    kolibri_diagnostic_buffer_push(parser->diagnostics, message, token->span);
}

static void kolibri_parser_skip_newlines(KolibriParser *parser) {
    while (kolibri_parser_match_token(parser, KOLIBRI_TOKEN_NEWLINE)) {
    }
}

static char *kolibri_expression_build_string(const KolibriToken *start, const KolibriToken *end) {
    if (!start || !end) {
        return NULL;
    }
    size_t capacity = 64U;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) {
        return NULL;
    }
    buffer[0] = '\0';
    size_t length = 0U;
    const KolibriToken *token = start;
    while (true) {
        const char *fragment = token->lexeme ? token->lexeme : "";
        size_t fragment_len = strlen(fragment);
        if (length + fragment_len + 2U >= capacity) {
            size_t new_capacity = capacity * KOLIBRI_ARRAY_GROWTH_FACTOR + fragment_len + 8U;
            char *new_buffer = (char *)realloc(buffer, new_capacity);
            if (!new_buffer) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }
        if (length > 0) {
            buffer[length++] = ' ';
        }
        memcpy(buffer + length, fragment, fragment_len);
        length += fragment_len;
        buffer[length] = '\0';
        if (token == end) {
            break;
        }
        token += 1;
    }
    return buffer;
}

static void kolibri_trim_spaces(char *text) {
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    size_t start = 0;
    while (start < len && isspace((unsigned char)text[start])) {
        start++;
    }
    size_t end = len;
    while (end > start && isspace((unsigned char)text[end - 1])) {
        end--;
    }
    if (start > 0 || end < len) {
        memmove(text, text + start, end - start);
        text[end - start] = '\0';
    }
    size_t write = 0;
    bool last_space = false;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (isspace(ch)) {
            if (!last_space) {
                text[write++] = ' ';
                last_space = true;
            }
        } else {
            text[write++] = text[i];
            last_space = false;
        }
    }
    text[write] = '\0';
}

static void kolibri_script_set_mode(KolibriScript *script, const char *mode) {
    if (!script) {
        return;
    }
    char temp[sizeof(script->mode)];
    if (!mode) {
        strcpy(temp, "neutral");
    } else {
        strncpy(temp, mode, sizeof(temp) - 1U);
        temp[sizeof(temp) - 1U] = '\0';
        kolibri_trim_spaces(temp);
        kolibri_to_lower_ascii(temp, temp, sizeof(temp));
        if (temp[0] == '\0') {
            strcpy(temp, "neutral");
        }
    }
    strncpy(script->mode, temp, sizeof(script->mode) - 1U);
    script->mode[sizeof(script->mode) - 1U] = '\0';
}

static void kolibri_clean_answer(char *text) {
    if (!text) {
        return;
    }
    kolibri_trim_spaces(text);
    size_t len = strlen(text);
    if (len == 0) {
        return;
    }
    text[0] = (char)toupper((unsigned char)text[0]);
    if (text[len - 1] != '.' && text[len - 1] != '!' && text[len - 1] != '?') {
        if (len + 1 < KOLIBRI_PAYLOAD_SIZE) {
            text[len] = '.';
            text[len + 1] = '\0';
        }
    }
}

static bool kolibri_mode_equals(const KolibriScript *script, const char *expected) {
    if (!script || !expected) {
        return false;
    }
    char lower[sizeof(script->mode)];
    kolibri_to_lower_ascii(script->mode, lower, sizeof(lower));
    kolibri_trim_spaces(lower);
    return strcmp(lower, expected) == 0;
}

static void kolibri_apply_mode(KolibriScript *script, char *answer) {
    if (!script || !answer) {
        return;
    }
    if (kolibri_mode_equals(script, "neutral") || answer[0] == '\0') {
        return;
    }
    char formatted[512];
    if (kolibri_mode_equals(script, "журнал") || kolibri_mode_equals(script, "journal")) {
        snprintf(formatted, sizeof(formatted), "Журнал: %s", answer);
    } else if (kolibri_mode_equals(script, "эмодзи") || kolibri_mode_equals(script, "emoji")) {
        snprintf(formatted, sizeof(formatted), "%s %s", answer, "😊");
    } else if (kolibri_mode_equals(script, "аналитика") || kolibri_mode_equals(script, "analytics")) {
        snprintf(formatted, sizeof(formatted), "• %s", answer);
    } else {
        return;
    }
    strncpy(answer, formatted, 511);
    answer[511] = '\0';
}

static void kolibri_to_lower_ascii(const char *src, char *dst, size_t dst_len) {
    if (!src || !dst || dst_len == 0) {
        return;
    }
    size_t i = 0;
    for (; src[i] && i + 1 < dst_len; ++i) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (char)tolower(ch);
    }
    dst[i] = '\0';
}

static const KolibriAssociation *kolibri_find_partial_association(const KolibriFormulaPool *pool,
                                                                  const char *question) {
    if (!pool || !question) {
        return NULL;
    }
    char lowered_question[256];
    kolibri_to_lower_ascii(question, lowered_question, sizeof(lowered_question));
    size_t best_len = 0U;
    const KolibriAssociation *best = NULL;
    for (size_t i = 0; i < pool->association_count; ++i) {
        const KolibriAssociation *assoc = &pool->associations[i];
        char lowered_assoc[256];
        kolibri_to_lower_ascii(assoc->question, lowered_assoc, sizeof(lowered_assoc));
        if (strstr(lowered_question, lowered_assoc) || strstr(lowered_assoc, lowered_question)) {
            size_t len = strlen(assoc->question);
            if (len > best_len) {
                best_len = len;
                best = assoc;
            }
        }
    }
    return best;
}

static bool kolibri_try_calculate(const char *question, char *buffer, size_t buffer_len) {
    if (!question || !buffer || buffer_len == 0) {
        return false;
    }
    char expr[256];
    kolibri_to_lower_ascii(question, expr, sizeof(expr));
    for (char *p = expr; *p; ++p) {
        if (*p == ',' || *p == '?') {
            *p = ' ';
        }
    }
    double a = 0.0, b = 0.0;
    char op = 0;
    if (sscanf(expr, "%lf %c %lf", &a, &op, &b) == 3) {
        double result = 0.0;
        switch (op) {
        case '+':
            result = a + b;
            break;
        case '-':
            result = a - b;
            break;
        case '*':
        case 'x':
            result = a * b;
            break;
        case '/':
            if (fabs(b) < 1e-12) {
                return false;
            }
            result = a / b;
            break;
        default:
            return false;
        }
        snprintf(buffer, buffer_len, "%.6g", result);
        return true;
    }
    return false;
}

static void kolibri_record_ngrams(KolibriScript *script,
                                  const char *question,
                                  const char *answer,
                                  const char *source,
                                  uint64_t timestamp) {
    if (!script || !script->pool || !question || !answer) {
        return;
    }
    char copy[256];
    strncpy(copy, question, sizeof(copy) - 1U);
    copy[sizeof(copy) - 1U] = '\0';
    kolibri_trim_spaces(copy);
    if (copy[0] == '\0') {
        return;
    }
    char *tokens[32];
    size_t token_count = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(copy, " ", &saveptr);
         tok && token_count < sizeof(tokens) / sizeof(tokens[0]);
         tok = strtok_r(NULL, " ", &saveptr)) {
        tokens[token_count++] = tok;
    }
    if (token_count < 2) {
        return;
    }
    char recorded[64][256];
    size_t recorded_count = 0;
    for (size_t n = 2; n <= 3 && n <= token_count; ++n) {
        for (size_t i = 0; i + n <= token_count; ++i) {
            char ngram[256];
            ngram[0] = '\0';
            for (size_t j = 0; j < n; ++j) {
                if (j > 0) {
                    strncat(ngram, " ", sizeof(ngram) - strlen(ngram) - 1U);
                }
                strncat(ngram, tokens[i + j], sizeof(ngram) - strlen(ngram) - 1U);
            }
            if (ngram[0] == '\0') {
                continue;
            }
            bool seen = false;
            for (size_t r = 0; r < recorded_count; ++r) {
                if (strcmp(recorded[r], ngram) == 0) {
                    seen = true;
                    break;
                }
            }
            if (seen || recorded_count >= sizeof(recorded) / sizeof(recorded[0])) {
                continue;
            }
            strncpy(recorded[recorded_count], ngram, sizeof(recorded[recorded_count]) - 1U);
            recorded[recorded_count][sizeof(recorded[recorded_count]) - 1U] = '\0';
            recorded_count++;
            (void)kf_pool_add_association(script->pool,
                                          &script->symbol_table,
                                          ngram,
                                          answer,
                                          source ? source : "ngram",
                                          timestamp);
        }
    }
}

static bool kolibri_token_is_terminator(const KolibriToken *token, const char *const *keywords, size_t keyword_count,
                                        const KolibriTokenType *types, size_t type_count) {
    for (size_t i = 0; i < type_count; ++i) {
        if (token->type == types[i]) {
            return true;
        }
    }
    if (token->type == KOLIBRI_TOKEN_KEYWORD && token->lexeme) {
        for (size_t i = 0; i < keyword_count; ++i) {
            if (strcmp(token->lexeme, keywords[i]) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool kolibri_parser_parse_expression_until(KolibriParser *parser, KolibriExpression *expr,
                                                  const char *const *keywords, size_t keyword_count,
                                                  const KolibriTokenType *types, size_t type_count) {
    const KolibriToken *start = kolibri_parser_current(parser);
    const KolibriToken *first = NULL;
    const KolibriToken *last = NULL;
    while (true) {
        const KolibriToken *token = kolibri_parser_current(parser);
        if (token->type == KOLIBRI_TOKEN_EOF || token->type == KOLIBRI_TOKEN_NEWLINE) {
            break;
        }
        if (kolibri_token_is_terminator(token, keywords, keyword_count, types, type_count)) {
            break;
        }
        if (!first) {
            first = token;
        }
        last = token;
        kolibri_parser_advance(parser);
    }
    if (!first || !last) {
        kolibri_parser_report(parser, "Ожидалось выражение", start);
        return false;
    }
    char *text = kolibri_expression_build_string(first, last);
    if (!text) {
        return false;
    }
    expr->text = text;
    expr->span = kolibri_make_span(first->span.start, last->span.end);
    return true;
}

static KolibriStatement *kolibri_parser_make_statement(KolibriNodeKind kind, KolibriSourceSpan span) {
    KolibriStatement *stmt = (KolibriStatement *)calloc(1U, sizeof(KolibriStatement));
    if (!stmt) {
        return NULL;
    }
    stmt->kind = kind;
    stmt->span = span;
    if (kind == KOLIBRI_NODE_IF) {
        kolibri_statement_list_init(&stmt->data.if_stmt.then_body);
        kolibri_statement_list_init(&stmt->data.if_stmt.else_body);
    } else if (kind == KOLIBRI_NODE_WHILE) {
        kolibri_statement_list_init(&stmt->data.while_stmt.body);
    }
    return stmt;
}

static bool kolibri_parser_expect_identifier(KolibriParser *parser, const KolibriToken **out_token) {
    const KolibriToken *token = kolibri_parser_current(parser);
    if (token->type == KOLIBRI_TOKEN_IDENT) {
        *out_token = token;
        kolibri_parser_advance(parser);
        return true;
    }
    kolibri_parser_report(parser, "Ожидался идентификатор", token);
    return false;
}

static bool kolibri_parser_expect_keyword(KolibriParser *parser, const char *keyword) {
    if (kolibri_parser_match_keyword(parser, keyword)) {
        return true;
    }
    kolibri_parser_report(parser, "Ожидалось ключевое слово", kolibri_parser_current(parser));
    return false;
}

static KolibriStatementList kolibri_parser_parse_statements(KolibriParser *parser,
                                                            const char *const *terminators,
                                                            size_t terminators_count);

static KolibriStatement *kolibri_parser_parse_statement(KolibriParser *parser);

static KolibriStatement *kolibri_parser_parse_show(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression expr = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &expr, terminator_keywords, 0, terminator_types, 2U)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_SHOW, kolibri_make_span(start->span.start, expr.span.end));
    if (!stmt) {
        kolibri_free_expression(&expr);
        return NULL;
    }
    stmt->data.show.value = expr;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_variable(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const KolibriToken *name_token = NULL;
    if (!kolibri_parser_expect_identifier(parser, &name_token)) {
        return NULL;
    }
    if (!kolibri_parser_match_token(parser, KOLIBRI_TOKEN_ASSIGN)) {
        kolibri_parser_report(parser, "Ожидался символ '='", kolibri_parser_current(parser));
        return NULL;
    }
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression expr = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &expr, terminator_keywords, 0, terminator_types, 2U)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_VARIABLE,
                                                           kolibri_make_span(start->span.start, expr.span.end));
    if (!stmt) {
        kolibri_free_expression(&expr);
        return NULL;
    }
    stmt->data.variable.name = strdup(name_token->lexeme);
    if (!stmt->data.variable.name) {
        kolibri_free_expression(&expr);
        free(stmt);
        return NULL;
    }
    stmt->data.variable.value = expr;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_mode(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression expr = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &expr, terminator_keywords, 0, terminator_types, 2U)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_MODE,
                                                           kolibri_make_span(start->span.start, expr.span.end));
    if (!stmt) {
        kolibri_free_expression(&expr);
        return NULL;
    }
    stmt->data.mode_stmt.value = expr;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_teach(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    if (!kolibri_parser_expect_keyword(parser, "связь")) {
        return NULL;
    }
    const char *arrow_terminators[] = { NULL };
    KolibriTokenType arrow_types[] = { KOLIBRI_TOKEN_ARROW };
    KolibriExpression left = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &left, arrow_terminators, 0, arrow_types, 1U)) {
        return NULL;
    }
    if (!kolibri_parser_match_token(parser, KOLIBRI_TOKEN_ARROW)) {
        kolibri_parser_report(parser, "Ожидался символ '->'", kolibri_parser_current(parser));
        kolibri_free_expression(&left);
        return NULL;
    }
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression right = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &right, terminator_keywords, 0, terminator_types, 2U)) {
        kolibri_free_expression(&left);
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_TEACH,
                                                           kolibri_make_span(start->span.start, right.span.end));
    if (!stmt) {
        kolibri_free_expression(&left);
        kolibri_free_expression(&right);
        return NULL;
    }
    stmt->data.teach.left = left;
    stmt->data.teach.right = right;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_create_formula(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    if (!kolibri_parser_expect_keyword(parser, "формулу") && !kolibri_parser_expect_keyword(parser, "формула")) {
        return NULL;
    }
    const KolibriToken *name_token = NULL;
    if (!kolibri_parser_expect_identifier(parser, &name_token)) {
        return NULL;
    }
    if (!kolibri_parser_expect_keyword(parser, "из")) {
        return NULL;
    }
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression expr = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &expr, terminator_keywords, 0, terminator_types, 2U)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_CREATE_FORMULA,
                                                           kolibri_make_span(start->span.start, expr.span.end));
    if (!stmt) {
        kolibri_free_expression(&expr);
        return NULL;
    }
    stmt->data.create_formula.name = strdup(name_token->lexeme);
    if (!stmt->data.create_formula.name) {
        kolibri_free_expression(&expr);
        free(stmt);
        return NULL;
    }
    stmt->data.create_formula.expression = expr;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_evaluate_formula(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const KolibriToken *name_token = NULL;
    if (!kolibri_parser_expect_identifier(parser, &name_token)) {
        return NULL;
    }
    if (!kolibri_parser_expect_keyword(parser, "на") || !kolibri_parser_expect_keyword(parser, "задаче")) {
        return NULL;
    }
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression expr = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &expr, terminator_keywords, 0, terminator_types, 2U)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_EVALUATE_FORMULA,
                                                           kolibri_make_span(start->span.start, expr.span.end));
    if (!stmt) {
        kolibri_free_expression(&expr);
        return NULL;
    }
    stmt->data.evaluate_formula.name = strdup(name_token->lexeme);
    if (!stmt->data.evaluate_formula.name) {
        kolibri_free_expression(&expr);
        free(stmt);
        return NULL;
    }
    stmt->data.evaluate_formula.task = expr;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_save_formula(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const KolibriToken *name_token = NULL;
    if (!kolibri_parser_expect_identifier(parser, &name_token)) {
        return NULL;
    }
    if (!kolibri_parser_expect_keyword(parser, "в") || !kolibri_parser_expect_keyword(parser, "геном")) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_SAVE_FORMULA,
                                                           kolibri_make_span(start->span.start, name_token->span.end));
    if (!stmt) {
        return NULL;
    }
    stmt->data.save_formula.name = strdup(name_token->lexeme);
    if (!stmt->data.save_formula.name) {
        free(stmt);
        return NULL;
    }
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_drop_formula(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const KolibriToken *name_token = NULL;
    if (!kolibri_parser_expect_identifier(parser, &name_token)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_DROP_FORMULA,
                                                           kolibri_make_span(start->span.start, name_token->span.end));
    if (!stmt) {
        return NULL;
    }
    stmt->data.drop_formula.name = strdup(name_token->lexeme);
    if (!stmt->data.drop_formula.name) {
        free(stmt);
        return NULL;
    }
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_swarm(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    if (!kolibri_parser_expect_keyword(parser, "отправить")) {
        return NULL;
    }
    const KolibriToken *name_token = NULL;
    if (!kolibri_parser_expect_identifier(parser, &name_token)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_SWARM_SEND,
                                                           kolibri_make_span(start->span.start, name_token->span.end));
    if (!stmt) {
        return NULL;
    }
    stmt->data.swarm_send.name = strdup(name_token->lexeme);
    if (!stmt->data.swarm_send.name) {
        free(stmt);
        return NULL;
    }
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_call_evolution(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    if (!kolibri_parser_expect_keyword(parser, "эволюцию")) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_CALL_EVOLUTION, start->span);
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_print_canvas(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    if (!kolibri_parser_expect_keyword(parser, "канву")) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_PRINT_CANVAS, start->span);
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_verify(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const char *terminator_keywords[] = { NULL };
    KolibriTokenType terminator_types[] = { KOLIBRI_TOKEN_NEWLINE, KOLIBRI_TOKEN_EOF };
    KolibriExpression expr = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &expr, terminator_keywords, 0, terminator_types, 2U)) {
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_VERIFY,
                                                           kolibri_make_span(start->span.start, expr.span.end));
    if (!stmt) {
        kolibri_free_expression(&expr);
        return NULL;
    }
    stmt->data.verify.expected = expr;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_if(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const char *terminator_keywords[] = { "тогда" };
    KolibriTokenType terminator_types[] = { };
    KolibriExpression condition = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &condition, terminator_keywords, 1U, terminator_types, 0U)) {
        return NULL;
    }
    if (!kolibri_parser_expect_keyword(parser, "тогда")) {
        kolibri_free_expression(&condition);
        return NULL;
    }
    kolibri_parser_skip_newlines(parser);
    const char *then_terminators[] = { "иначе", "конец" };
    KolibriStatementList then_body = kolibri_parser_parse_statements(parser, then_terminators, 2U);
    const KolibriToken *end_token = kolibri_parser_current(parser);
    KolibriStatementList else_body;
    kolibri_statement_list_init(&else_body);
    bool has_else = false;
    if (kolibri_parser_match_keyword(parser, "иначе")) {
        has_else = true;
        kolibri_parser_skip_newlines(parser);
        const char *else_terminators[] = { "конец" };
        else_body = kolibri_parser_parse_statements(parser, else_terminators, 1U);
        end_token = kolibri_parser_current(parser);
    }
    if (!kolibri_parser_expect_keyword(parser, "конец")) {
        kolibri_free_expression(&condition);
        kolibri_statement_list_free(&then_body);
        kolibri_statement_list_free(&else_body);
        return NULL;
    }
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_IF,
                                                           kolibri_make_span(start->span.start,
                                                                             has_else ? end_token->span.end : kolibri_parser_previous(parser)->span.end));
    if (!stmt) {
        kolibri_free_expression(&condition);
        kolibri_statement_list_free(&then_body);
        kolibri_statement_list_free(&else_body);
        return NULL;
    }
    stmt->data.if_stmt.condition = condition;
    stmt->data.if_stmt.then_body = then_body;
    stmt->data.if_stmt.else_body = else_body;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_while(KolibriParser *parser) {
    const KolibriToken *start = kolibri_parser_advance(parser);
    const char *terminator_keywords[] = { "делать" };
    KolibriTokenType terminator_types[] = { };
    KolibriExpression condition = { 0 };
    if (!kolibri_parser_parse_expression_until(parser, &condition, terminator_keywords, 1U, terminator_types, 0U)) {
        return NULL;
    }
    if (!kolibri_parser_expect_keyword(parser, "делать")) {
        kolibri_free_expression(&condition);
        return NULL;
    }
    kolibri_parser_skip_newlines(parser);
    const char *terminators[] = { "конец" };
    KolibriStatementList body = kolibri_parser_parse_statements(parser, terminators, 1U);
    if (!kolibri_parser_expect_keyword(parser, "конец")) {
        kolibri_free_expression(&condition);
        kolibri_statement_list_free(&body);
        return NULL;
    }
    const KolibriToken *end_token = kolibri_parser_previous(parser);
    KolibriStatement *stmt = kolibri_parser_make_statement(KOLIBRI_NODE_WHILE,
                                                           kolibri_make_span(start->span.start, end_token->span.end));
    if (!stmt) {
        kolibri_free_expression(&condition);
        kolibri_statement_list_free(&body);
        return NULL;
    }
    stmt->data.while_stmt.condition = condition;
    stmt->data.while_stmt.body = body;
    return stmt;
}

static KolibriStatement *kolibri_parser_parse_statement(KolibriParser *parser) {
    const KolibriToken *token = kolibri_parser_current(parser);
    if (token->type != KOLIBRI_TOKEN_KEYWORD || !token->lexeme) {
        kolibri_parser_report(parser, "Неизвестная команда или идентификатор", token);
        return NULL;
    }
    if (strcmp(token->lexeme, "показать") == 0) {
        return kolibri_parser_parse_show(parser);
    }
    if (strcmp(token->lexeme, "переменная") == 0) {
        return kolibri_parser_parse_variable(parser);
    }
    if (strcmp(token->lexeme, "режим") == 0) {
        return kolibri_parser_parse_mode(parser);
    }
    if (strcmp(token->lexeme, "обучить") == 0) {
        return kolibri_parser_parse_teach(parser);
    }
    if (strcmp(token->lexeme, "создать") == 0) {
        return kolibri_parser_parse_create_formula(parser);
    }
    if (strcmp(token->lexeme, "оценить") == 0) {
        return kolibri_parser_parse_evaluate_formula(parser);
    }
    if (strcmp(token->lexeme, "верифицировать") == 0) {
        return kolibri_parser_parse_verify(parser);
    }
    if (strcmp(token->lexeme, "сохранить") == 0) {
        return kolibri_parser_parse_save_formula(parser);
    }
    if (strcmp(token->lexeme, "отбросить") == 0) {
        return kolibri_parser_parse_drop_formula(parser);
    }
    if (strcmp(token->lexeme, "вызвать") == 0) {
        return kolibri_parser_parse_call_evolution(parser);
    }
    if (strcmp(token->lexeme, "распечатать") == 0) {
        return kolibri_parser_parse_print_canvas(parser);
    }
    if (strcmp(token->lexeme, "рой") == 0) {
        return kolibri_parser_parse_swarm(parser);
    }
    if (strcmp(token->lexeme, "если") == 0) {
        return kolibri_parser_parse_if(parser);
    }
    if (strcmp(token->lexeme, "пока") == 0) {
        return kolibri_parser_parse_while(parser);
    }
    kolibri_parser_report(parser, "Неизвестная команда или идентификатор", token);
    kolibri_parser_advance(parser);
    return NULL;
}

static KolibriStatementList kolibri_parser_parse_statements(KolibriParser *parser,
                                                            const char *const *terminators,
                                                            size_t terminators_count) {
    KolibriStatementList list;
    kolibri_statement_list_init(&list);
    while (true) {
        const KolibriToken *token = kolibri_parser_current(parser);
        if (token->type == KOLIBRI_TOKEN_EOF) {
            break;
        }
        if (token->type == KOLIBRI_TOKEN_KEYWORD && token->lexeme) {
            bool is_terminator = false;
            for (size_t i = 0; i < terminators_count; ++i) {
                if (strcmp(token->lexeme, terminators[i]) == 0) {
                    is_terminator = true;
                    break;
                }
            }
            if (is_terminator) {
                break;
            }
        }
        if (token->type == KOLIBRI_TOKEN_NEWLINE) {
            kolibri_parser_advance(parser);
            continue;
        }
        KolibriStatement *stmt = kolibri_parser_parse_statement(parser);
        if (!stmt) {
            while (!kolibri_parser_check(parser, KOLIBRI_TOKEN_NEWLINE) && !kolibri_parser_check(parser, KOLIBRI_TOKEN_EOF)) {
                kolibri_parser_advance(parser);
            }
            kolibri_parser_match_token(parser, KOLIBRI_TOKEN_NEWLINE);
            continue;
        }
        if (kolibri_statement_list_push(&list, stmt) != 0) {
            kolibri_free_statement(stmt);
            break;
        }
        kolibri_parser_match_token(parser, KOLIBRI_TOKEN_NEWLINE);
    }
    return list;
}

static bool kolibri_parser_parse_program(KolibriParser *parser, KolibriProgram *program) {
    kolibri_parser_skip_newlines(parser);
    if (!kolibri_parser_match_keyword(parser, "начало")) {
        kolibri_parser_report(parser, "Программа должна начинаться с 'начало:'", kolibri_parser_current(parser));
        return false;
    }
    if (!kolibri_parser_match_token(parser, KOLIBRI_TOKEN_COLON)) {
        kolibri_parser_report(parser, "Ожидался символ ':'", kolibri_parser_current(parser));
        return false;
    }
    kolibri_parser_skip_newlines(parser);
    KolibriStatementList statements = kolibri_parser_parse_statements(parser, (const char *const[]){ "конец" }, 1U);
    program->statements = statements;
    if (!kolibri_parser_match_keyword(parser, "конец")) {
        kolibri_parser_report(parser, "Отсутствует завершающий 'конец.'", kolibri_parser_current(parser));
        return false;
    }
    kolibri_parser_match_token(parser, KOLIBRI_TOKEN_DOT);
    return true;
}

/* ===================== Runtime Structures ===================== */

typedef enum {
    KOLIBRI_VALUE_NONE = 0,
    KOLIBRI_VALUE_STRING,
    KOLIBRI_VALUE_NUMBER
} KolibriValueType;

typedef struct {
    KolibriValueType type;
    char *string_value;
    double number_value;
} KolibriValue;

typedef struct KolibriScriptVariable {
    char *name;
    KolibriValue value;
} KolibriScriptVariable;

typedef struct KolibriScriptAssociation {
    char *stimulus;
    char *response;
} KolibriScriptAssociation;

typedef struct KolibriScriptFormulaBinding {
    char *name;
    char *expression;
    size_t pool_index;
    double last_fitness;
} KolibriScriptFormulaBinding;

static void kolibri_value_free(KolibriValue *value) {
    if (!value) {
        return;
    }
    if (value->type == KOLIBRI_VALUE_STRING) {
        free(value->string_value);
    }
    value->string_value = NULL;
    value->number_value = 0.0;
    value->type = KOLIBRI_VALUE_NONE;
}

static KolibriValue kolibri_value_from_string(const char *text) {
    KolibriValue value;
    value.type = KOLIBRI_VALUE_STRING;
    value.string_value = text ? strdup(text) : NULL;
    value.number_value = 0.0;
    return value;
}

static KolibriValue kolibri_value_from_number(double number) {
    KolibriValue value;
    value.type = KOLIBRI_VALUE_NUMBER;
    value.number_value = number;
    value.string_value = NULL;
    return value;
}

static KolibriScriptVariable *kolibri_script_find_variable(KolibriScript *script, const char *name) {
    if (!script || !name) {
        return NULL;
    }
    for (size_t i = 0; i < script->variables_count; ++i) {
        if (strcmp(script->variables[i].name, name) == 0) {
            return &script->variables[i];
        }
    }
    return NULL;
}

static int kolibri_script_set_variable(KolibriScript *script, const char *name, KolibriValue value) {
    if (!script || !name) {
        kolibri_value_free(&value);
        return -1;
    }
    KolibriScriptVariable *existing = kolibri_script_find_variable(script, name);
    if (existing) {
        kolibri_value_free(&existing->value);
        existing->value = value;
        return 0;
    }
    if (script->variables_count == script->variables_capacity) {
        size_t new_capacity = script->variables_capacity == 0 ? 8U : script->variables_capacity * KOLIBRI_ARRAY_GROWTH_FACTOR;
        KolibriScriptVariable *new_items = (KolibriScriptVariable *)realloc(script->variables, new_capacity * sizeof(KolibriScriptVariable));
        if (!new_items) {
            kolibri_value_free(&value);
            return -1;
        }
        script->variables = new_items;
        script->variables_capacity = new_capacity;
    }
    KolibriScriptVariable *slot = &script->variables[script->variables_count++];
    slot->name = strdup(name);
    if (!slot->name) {
        --script->variables_count;
        kolibri_value_free(&value);
        return -1;
    }
    slot->value = value;
    return 0;
}

static void kolibri_script_clear_variables(KolibriScript *script) {
    if (!script) {
        return;
    }
    for (size_t i = 0; i < script->variables_count; ++i) {
        free(script->variables[i].name);
        kolibri_value_free(&script->variables[i].value);
    }
    free(script->variables);
    script->variables = NULL;
    script->variables_count = 0;
    script->variables_capacity = 0;
}

static void kolibri_script_clear_associations(KolibriScript *script) {
    if (!script) {
        return;
    }
    for (size_t i = 0; i < script->associations_count; ++i) {
        KolibriScriptAssociation *assoc = &script->associations[i];
        free(assoc->stimulus);
        free(assoc->response);
    }
    free(script->associations);
    script->associations = NULL;
    script->associations_count = 0;
    script->associations_capacity = 0;
}

static void kolibri_script_clear_formulas(KolibriScript *script) {
    if (!script) {
        return;
    }
    for (size_t i = 0; i < script->formulas_count; ++i) {
        KolibriScriptFormulaBinding *binding = &script->formulas[i];
        free(binding->name);
        free(binding->expression);
    }
    free(script->formulas);
    script->formulas = NULL;
    script->formulas_count = 0;
    script->formulas_capacity = 0;
}

static KolibriScriptFormulaBinding *kolibri_script_find_formula(KolibriScript *script, const char *name) {
    if (!script || !name) {
        return NULL;
    }
    for (size_t i = 0; i < script->formulas_count; ++i) {
        if (strcmp(script->formulas[i].name, name) == 0) {
            return &script->formulas[i];
        }
    }
    return NULL;
}

static int kolibri_script_bind_formula(KolibriScript *script, const char *name, const char *expression) {
    if (!script || !name) {
        return -1;
    }
    KolibriScriptFormulaBinding *existing = kolibri_script_find_formula(script, name);
    if (existing) {
        free(existing->expression);
        existing->expression = expression ? strdup(expression) : NULL;
        existing->last_fitness = 0.0;
        return (!existing->expression && expression) ? -1 : 0;
    }
    if (script->formulas_count == script->formulas_capacity) {
        size_t new_capacity = script->formulas_capacity == 0 ? 4U : script->formulas_capacity * KOLIBRI_ARRAY_GROWTH_FACTOR;
        KolibriScriptFormulaBinding *new_items = (KolibriScriptFormulaBinding *)realloc(script->formulas, new_capacity * sizeof(KolibriScriptFormulaBinding));
        if (!new_items) {
            return -1;
        }
        script->formulas = new_items;
        script->formulas_capacity = new_capacity;
    }
    KolibriScriptFormulaBinding *slot = &script->formulas[script->formulas_count++];
    slot->name = strdup(name);
    slot->expression = expression ? strdup(expression) : NULL;
    slot->pool_index = 0;
    slot->last_fitness = 0.0;
    if (!slot->name || (expression && !slot->expression)) {
        free(slot->name);
        free(slot->expression);
        --script->formulas_count;
        return -1;
    }
    return 0;
}

static int kolibri_script_remove_formula(KolibriScript *script, const char *name) {
    if (!script || !name) {
        return -1;
    }
    for (size_t i = 0; i < script->formulas_count; ++i) {
        if (strcmp(script->formulas[i].name, name) == 0) {
            free(script->formulas[i].name);
            free(script->formulas[i].expression);
            if (i + 1U < script->formulas_count) {
                memmove(&script->formulas[i], &script->formulas[i + 1U], (script->formulas_count - i - 1U) * sizeof(KolibriScriptFormulaBinding));
            }
            script->formulas_count -= 1U;
            return 0;
        }
    }
    return -1;
}

/* ===================== Utilities ===================== */

static char *kolibri_trim_copy(const char *text) {
    if (!text) {
        return NULL;
    }
    const char *start = text;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    const char *end = text + strlen(text);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }
    size_t len = (size_t)(end - start);
    return kolibri_strndup(start, len);
}

static bool kolibri_is_string_literal(const char *text) {
    size_t len = strlen(text);
    return len >= 2U && text[0] == '"' && text[len - 1U] == '"';
}

static char *kolibri_strip_quotes(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    if (len < 2U || text[0] != '"' || text[len - 1U] != '"') {
        return strdup(text);
    }
    char *result = (char *)malloc(len - 1U);
    if (!result) {
        return NULL;
    }
    size_t write = 0U;
    bool escape = false;
    for (size_t i = 1U; i < len - 1U; ++i) {
        char ch = text[i];
        if (escape) {
            switch (ch) {
            case 'n': result[write++] = '\n'; break;
            case 't': result[write++] = '\t'; break;
            case '\\': result[write++] = '\\'; break;
            case '"': result[write++] = '"'; break;
            default: result[write++] = ch; break;
            }
            escape = false;
        } else if (ch == '\\') {
            escape = true;
        } else {
            result[write++] = ch;
        }
    }
    result[write] = '\0';
    return result;
}

static double kolibri_parse_number(const char *text, bool *ok) {
    char *endptr = NULL;
    errno = 0;
    double value = strtod(text, &endptr);
    if (ok) {
        *ok = endptr && *endptr == '\0' && errno == 0;
    }
    return value;
}

static int kolibri_value_to_string(const KolibriValue *value, char **out) {
    if (!value || !out) {
        return -1;
    }
    if (value->type == KOLIBRI_VALUE_STRING) {
        *out = value->string_value ? strdup(value->string_value) : strdup("");
        return *out ? 0 : -1;
    }
    if (value->type == KOLIBRI_VALUE_NUMBER) {
        char buffer[64];
        int written = snprintf(buffer, sizeof(buffer), "%.6f", value->number_value);
        if (written < 0) {
            return -1;
        }
        /* Trim trailing zeros */
        for (int i = written - 1; i > 0; --i) {
            if (buffer[i] == '0') {
                buffer[i] = '\0';
            } else if (buffer[i] == '.') {
                buffer[i] = '\0';
                break;
            } else {
                break;
            }
        }
        *out = strdup(buffer);
        return *out ? 0 : -1;
    }
    *out = strdup("");
    return *out ? 0 : -1;
}

static int kolibri_value_to_number(const KolibriValue *value, double *out) {
    if (!value || !out) {
        return -1;
    }
    if (value->type == KOLIBRI_VALUE_NUMBER) {
        *out = value->number_value;
        return 0;
    }
    if (value->type == KOLIBRI_VALUE_STRING && value->string_value) {
        bool ok = false;
        double parsed = kolibri_parse_number(value->string_value, &ok);
        if (ok) {
            *out = parsed;
            return 0;
        }
    }
    return -1;
}

static int kolibri_evaluate_expression(KolibriScript *script, const KolibriExpression *expr, KolibriValue *out_value) {
    char *trimmed = kolibri_trim_copy(expr->text);
    if (!trimmed) {
        return -1;
    }
    if (kolibri_is_string_literal(trimmed)) {
        char *stripped = kolibri_strip_quotes(trimmed);
        free(trimmed);
        if (!stripped) {
            return -1;
        }
        *out_value = kolibri_value_from_string(stripped);
        if (!out_value->string_value && stripped) {
            free(stripped);
            return -1;
        }
        free(stripped);
        return 0;
    }
    KolibriScriptVariable *var = kolibri_script_find_variable(script, trimmed);
    if (var) {
        if (var->value.type == KOLIBRI_VALUE_STRING) {
            *out_value = kolibri_value_from_string(var->value.string_value);
        } else if (var->value.type == KOLIBRI_VALUE_NUMBER) {
            *out_value = kolibri_value_from_number(var->value.number_value);
        } else {
            *out_value = kolibri_value_from_string("");
        }
        free(trimmed);
        return 0;
    }
    if (strncmp(trimmed, "фитнес", strlen("фитнес")) == 0) {
        const char *name_start = trimmed + strlen("фитнес");
        while (*name_start && isspace((unsigned char)*name_start)) {
            ++name_start;
        }
        KolibriScriptFormulaBinding *binding = kolibri_script_find_formula(script, name_start);
        free(trimmed);
        if (binding) {
            *out_value = kolibri_value_from_number(binding->last_fitness);
            return 0;
        }
        *out_value = kolibri_value_from_number(0.0);
        return 0;
    }
    bool ok = false;
    double numeric = kolibri_parse_number(trimmed, &ok);
    if (ok) {
        *out_value = kolibri_value_from_number(numeric);
        free(trimmed);
        return 0;
    }
    *out_value = kolibri_value_from_string(trimmed);
    if (!out_value->string_value) {
        free(trimmed);
        return -1;
    }
    return 0;
}

static bool kolibri_evaluate_condition(KolibriScript *script, const KolibriExpression *expr, bool *result) {
    char *text = kolibri_trim_copy(expr->text);
    if (!text) {
        return false;
    }
    const char *comparators[] = { ">=", "<=", "==", "!=", ">", "<" };
    const char *found = NULL;
    const char *comp = NULL;
    for (size_t i = 0; i < sizeof(comparators) / sizeof(comparators[0]); ++i) {
        comp = comparators[i];
        found = strstr(text, comp);
        if (found) {
            break;
        }
    }
    if (!found) {
        free(text);
        return false;
    }
    size_t left_len = (size_t)(found - text);
    char *left = kolibri_strndup(text, left_len);
    char *right = kolibri_trim_copy(found + strlen(comp));
    free(text);
    if (!left || !right) {
        free(left);
        free(right);
        return false;
    }
    KolibriExpression left_expr = { left, expr->span };
    KolibriExpression right_expr = { right, expr->span };
    KolibriValue left_value;
    KolibriValue right_value;
    if (kolibri_evaluate_expression(script, &left_expr, &left_value) != 0 ||
        kolibri_evaluate_expression(script, &right_expr, &right_value) != 0) {
        free(left);
        free(right);
        return false;
    }
    double left_num = 0.0;
    double right_num = 0.0;
    bool numeric = kolibri_value_to_number(&left_value, &left_num) == 0 && kolibri_value_to_number(&right_value, &right_num) == 0;
    if (!numeric) {
        kolibri_value_free(&left_value);
        kolibri_value_free(&right_value);
        free(left);
        free(right);
        return false;
    }
    if (strcmp(comp, ">=") == 0) {
        *result = left_num >= right_num;
    } else if (strcmp(comp, "<=") == 0) {
        *result = left_num <= right_num;
    } else if (strcmp(comp, "==") == 0) {
        *result = fabs(left_num - right_num) <= 1e-9;
    } else if (strcmp(comp, "!=") == 0) {
        *result = fabs(left_num - right_num) > 1e-9;
    } else if (strcmp(comp, ">") == 0) {
        *result = left_num > right_num;
    } else {
        *result = left_num < right_num;
    }
    kolibri_value_free(&left_value);
    kolibri_value_free(&right_value);
    free(left);
    free(right);
    return true;
}

static void kolibri_script_log(KolibriScript *script, const char *event, const char *message) {
    if (!script || !script->genome) {
        return;
    }
    if (!event) {
        event = "SCRIPT_EVENT";
    }
    if (!message) {
        message = "";
    }
    char digits_payload[KOLIBRI_PAYLOAD_SIZE];
    if (kg_encode_payload(message, digits_payload, sizeof(digits_payload)) != 0) {
        return;
    }
    kg_append(script->genome, event, digits_payload, NULL);
}

/* ===================== Interpreter ===================== */

static int kolibri_execute_show(KolibriScript *script, const KolibriStatement *stmt) {
    KolibriValue value;
    if (kolibri_evaluate_expression(script, &stmt->data.show.value, &value) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить аргумент команды 'показать'");
        return -1;
    }
    char *text = NULL;
    if (kolibri_value_to_string(&value, &text) != 0) {
        kolibri_value_free(&value);
        return -1;
    }
    if (!script->vyvod) {
        script->vyvod = stdout;
    }
    fprintf(script->vyvod, "%s\n", text);
    kolibri_script_log(script, "SCRIPT_SHOW", text);
    free(text);
    kolibri_value_free(&value);
    return 0;
}

static int kolibri_execute_variable(KolibriScript *script, const KolibriStatement *stmt) {
    KolibriValue value;
    if (kolibri_evaluate_expression(script, &stmt->data.variable.value, &value) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить выражение переменной");
        return -1;
    }
    if (kolibri_script_set_variable(script, stmt->data.variable.name, value) != 0) {
        kolibri_value_free(&value);
        return -1;
    }
    return 0;
}

static int kolibri_execute_mode(KolibriScript *script, const KolibriStatement *stmt) {
    KolibriValue value;
    if (kolibri_evaluate_expression(script, &stmt->data.mode_stmt.value, &value) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить режим");
        return -1;
    }
    char *text = NULL;
    if (kolibri_value_to_string(&value, &text) != 0) {
        kolibri_value_free(&value);
        return -1;
    }
    kolibri_script_set_mode(script, text);
    char log_payload[128];
    snprintf(log_payload, sizeof(log_payload), "mode=%s", script->mode);
    kolibri_script_log(script, "SCRIPT_MODE", log_payload);
    if (script->vyvod) {
        fprintf(script->vyvod, "[Колибри] Режим установлен: %s\n", script->mode);
    }
    free(text);
    kolibri_value_free(&value);
    return 0;
}

static int kolibri_execute_verify(KolibriScript *script, const KolibriStatement *stmt) {
    if (!script) {
        return -1;
    }
    KolibriValue expected_value;
    if (kolibri_evaluate_expression(script, &stmt->data.verify.expected, &expected_value) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить ожидаемый ответ для 'верифицировать'");
        return -1;
    }
    char *expected_text = NULL;
    if (kolibri_value_to_string(&expected_value, &expected_text) != 0) {
        kolibri_value_free(&expected_value);
        return -1;
    }
    kolibri_value_free(&expected_value);

    KolibriCrystalCore *crystal = &script->crystal_core;
    if (!crystal->has_evaluate) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Нет вычисленного ответа для верификации");
        free(expected_text);
        return -1;
    }

    bool match = kolibri_digit_text_equals_utf8(&crystal->answer, expected_text);

    if (kolibri_digit_text_assign_utf8(&crystal->expected, expected_text) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Кристалл не смог зафиксировать верификацию");
        free(expected_text);
        return -1;
    }

    unsigned char verify_hash[SHA256_DIGEST_LENGTH];
    const unsigned char *prev = crystal->has_evaluate ? crystal->evaluate_hash : NULL;
    size_t prev_len = crystal->has_evaluate ? sizeof(crystal->evaluate_hash) : 0U;
    kolibri_crystal_compute_hash("CRYSTAL_VERIFY", prev, prev_len, &crystal->answer, &crystal->expected, verify_hash);

    char hash_hex[SHA256_DIGEST_LENGTH * 2U + 1U];
    kolibri_hex_encode(verify_hash, sizeof(verify_hash), hash_hex, sizeof(hash_hex));

    char status_buffer[192];
    snprintf(status_buffer, sizeof(status_buffer), "stage=verify status=%s hash=%s", match ? "ok" : "fail", hash_hex);
    kolibri_script_log(script, "SCRIPT_VERIFY", expected_text);
    kolibri_script_log(script, "CRYSTAL_VERIFY", status_buffer);

    if (script->vyvod) {
        fprintf(script->vyvod, "[Колибри] Верификация: %s\n", match ? "успешна" : "ошибка");
    }

    free(expected_text);

    if (!match) {
        return -1;
    }

    kolibri_crystal_reset(crystal);
    return 0;
}

static int kolibri_execute_teach(KolibriScript *script, const KolibriStatement *stmt) {
    KolibriValue left;
    KolibriValue right;
    if (kolibri_evaluate_expression(script, &stmt->data.teach.left, &left) != 0 ||
        kolibri_evaluate_expression(script, &stmt->data.teach.right, &right) != 0) {
        kolibri_value_free(&left);
        kolibri_value_free(&right);
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось получить аргументы для 'обучить связь'");
        return -1;
    }
    char *left_text = NULL;
    char *right_text = NULL;
    if (kolibri_value_to_string(&left, &left_text) != 0 ||
        kolibri_value_to_string(&right, &right_text) != 0) {
        kolibri_value_free(&left);
        kolibri_value_free(&right);
        free(left_text);
        free(right_text);
        return -1;
    }
    if (script->associations_count == script->associations_capacity) {
        size_t new_capacity = script->associations_capacity == 0 ? 4U : script->associations_capacity * KOLIBRI_ARRAY_GROWTH_FACTOR;
        KolibriScriptAssociation *new_items = (KolibriScriptAssociation *)realloc(script->associations, new_capacity * sizeof(KolibriScriptAssociation));
        if (!new_items) {
            kolibri_value_free(&left);
            kolibri_value_free(&right);
            free(left_text);
            free(right_text);
            return -1;
        }
        script->associations = new_items;
        script->associations_capacity = new_capacity;
    }
    KolibriScriptAssociation *assoc = &script->associations[script->associations_count++];
    assoc->stimulus = left_text;
    assoc->response = right_text;

    kolibri_script_log(script, "SCRIPT_TEACH", assoc->stimulus);

    KolibriCrystalCore *crystal = &script->crystal_core;
    if (kolibri_digit_text_assign_utf8(&crystal->stimulus, assoc->stimulus) == 0 &&
        kolibri_digit_text_assign_utf8(&crystal->response, assoc->response) == 0) {
        kolibri_crystal_compute_hash("CRYSTAL_TEACH", NULL, 0, &crystal->stimulus, &crystal->response, crystal->teach_hash);
        crystal->has_teach = 1;
        crystal->has_evaluate = 0;
        char hash_hex[SHA256_DIGEST_LENGTH * 2U + 1U];
        kolibri_hex_encode(crystal->teach_hash, sizeof(crystal->teach_hash), hash_hex, sizeof(hash_hex));
        char log_message[128];
        snprintf(log_message, sizeof(log_message), "stage=teach hash=%s", hash_hex);
        kolibri_script_log(script, "CRYSTAL_TEACH", log_message);
    } else {
        kolibri_crystal_reset(crystal);
        kolibri_script_log(script, "SCRIPT_ERROR", "Кристалл не смог зафиксировать обучение");
    }

    if (script->pool) {
        uint64_t now = (uint64_t)time(NULL);
        (void)kf_pool_add_association(script->pool, &script->symbol_table, left_text, right_text, "teach", now);
        kolibri_record_ngrams(script, left_text, right_text, "teach", now);
    }
    kolibri_value_free(&left);
    kolibri_value_free(&right);
    return 0;
}

static int kolibri_execute_create_formula(KolibriScript *script, const KolibriStatement *stmt) {
    if (!script->pool) {
        return 0;
    }
    if (kolibri_script_bind_formula(script, stmt->data.create_formula.name, stmt->data.create_formula.expression.text) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось зарегистрировать формулу");
        return -1;
    }
    kolibri_script_log(script, "SCRIPT_FORMULA_CREATE", stmt->data.create_formula.name);
    return 0;
}

static int kolibri_execute_evaluate_formula(KolibriScript *script, const KolibriStatement *stmt) {
    if (!script->pool) {
        return 0;
    }
    KolibriScriptFormulaBinding *binding = kolibri_script_find_formula(script, stmt->data.evaluate_formula.name);
    if (!binding) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Формула не найдена");
        return -1;
    }
    KolibriValue task_value;
    if (kolibri_evaluate_expression(script, &stmt->data.evaluate_formula.task, &task_value) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить задачу для формулы");
        return -1;
    }
    char *task_text = NULL;
    if (kolibri_value_to_string(&task_value, &task_text) != 0) {
        kolibri_value_free(&task_value);
        return -1;
    }
    int task_int = kf_hash_from_text(task_text);
    KolibriFormula *formula = &script->pool->formulas[binding->pool_index % script->pool->count];
    int output = 0;
    if (kf_formula_apply(formula, task_int, &output) != 0) {
        free(task_text);
        kolibri_value_free(&task_value);
        kolibri_script_log(script, "SCRIPT_ERROR", "Формула вернула ошибку");
        return -1;
    }
    char answer_buffer[512];
    bool answer_generated = false;
    if (kf_formula_lookup_answer(formula, task_int, answer_buffer, sizeof(answer_buffer)) == 0) {
        answer_generated = true;
    }
    if (!answer_generated) {
        const KolibriAssociation *partial = kolibri_find_partial_association(script->pool, task_text);
        if (partial) {
            strncpy(answer_buffer, partial->answer, sizeof(answer_buffer) - 1U);
            answer_buffer[sizeof(answer_buffer) - 1U] = '\0';
            answer_generated = true;
        }
    }
    if (!answer_generated) {
        if (kolibri_try_calculate(task_text, answer_buffer, sizeof(answer_buffer))) {
            answer_generated = true;
        }
    }
    if (!answer_generated) {
        char synthesized[256];
        kolibri_generate_text_from_gene(formula, task_text, output, synthesized, sizeof(synthesized));
        if (synthesized[0] != '\0') {
            strncpy(answer_buffer, synthesized, sizeof(answer_buffer) - 1U);
            answer_buffer[sizeof(answer_buffer) - 1U] = '\0';
            answer_generated = true;
            if (script->pool) {
                uint64_t now = (uint64_t)time(NULL);
                (void)kf_pool_add_association(script->pool, &script->symbol_table, task_text, answer_buffer, "auto", now);
                kolibri_record_ngrams(script, task_text, answer_buffer, "auto", now);
            }
        }
    }
    if (!answer_generated) {
        if (snprintf(answer_buffer,
                     sizeof(answer_buffer),
                     "Колибри ещё учится и предлагает числовой ответ: %d",
                     output) < 0) {
            strncpy(answer_buffer, "Колибри ещё учится и не готов ответить.", sizeof(answer_buffer) - 1U);
            answer_buffer[sizeof(answer_buffer) - 1U] = '\0';
        }
    }
    double fitness = formula->fitness;
    binding->last_fitness = fitness;
    kolibri_script_log(script, "SCRIPT_EVALUATE", task_text);
    kolibri_clean_answer(answer_buffer);
    kolibri_apply_mode(script, answer_buffer);

    KolibriCrystalCore *crystal = &script->crystal_core;
    if (kolibri_digit_text_assign_utf8(&crystal->question, task_text) == 0 &&
        kolibri_digit_text_assign_utf8(&crystal->answer, answer_buffer) == 0) {
        const unsigned char *prev = crystal->has_teach ? crystal->teach_hash : NULL;
        size_t prev_len = crystal->has_teach ? sizeof(crystal->teach_hash) : 0U;
        kolibri_crystal_compute_hash("CRYSTAL_EVALUATE", prev, prev_len, &crystal->question, &crystal->answer, crystal->evaluate_hash);
        crystal->has_evaluate = 1;
        char hash_hex[SHA256_DIGEST_LENGTH * 2U + 1U];
        kolibri_hex_encode(crystal->evaluate_hash, sizeof(crystal->evaluate_hash), hash_hex, sizeof(hash_hex));
        char log_message[160];
        snprintf(log_message, sizeof(log_message), "stage=evaluate hash=%s", hash_hex);
        kolibri_script_log(script, "CRYSTAL_EVALUATE", log_message);
    } else {
        crystal->has_evaluate = 0;
        kolibri_script_log(script, "SCRIPT_ERROR", "Кристалл не смог зафиксировать оценку");
    }

    KolibriValue result_value = kolibri_value_from_string(answer_buffer);
    kolibri_script_set_variable(script, "итог", result_value);
    free(task_text);
    kolibri_value_free(&task_value);
    return 0;
}

static int kolibri_execute_save_formula(KolibriScript *script, const KolibriStatement *stmt) {
    if (!script->pool) {
        return 0;
    }
    KolibriScriptFormulaBinding *binding = kolibri_script_find_formula(script, stmt->data.save_formula.name);
    if (!binding) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Формула не найдена для сохранения");
        return -1;
    }
    KolibriFormula *formula = &script->pool->formulas[binding->pool_index % script->pool->count];
    uint8_t digits[128];
    size_t len = kf_formula_digits(formula, digits, sizeof(digits));
    if (len == 0 || len >= sizeof(digits)) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось сериализовать формулу");
        return -1;
    }
    char payload[256];
    for (size_t i = 0; i < len && i < sizeof(payload) - 1U; ++i) {
        payload[i] = (char)('0' + digits[i]);
    }
    payload[len] = '\0';
    kolibri_script_log(script, "SCRIPT_FORMULA_SAVE", payload);
    return 0;
}

static int kolibri_execute_drop_formula(KolibriScript *script, const KolibriStatement *stmt) {
    if (kolibri_script_remove_formula(script, stmt->data.drop_formula.name) != 0) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Формула не найдена для удаления");
        return -1;
    }
    kolibri_script_log(script, "SCRIPT_FORMULA_DROP", stmt->data.drop_formula.name);
    return 0;
}

static int kolibri_execute_call_evolution(KolibriScript *script) {
    if (!script->pool) {
        return 0;
    }
    kf_pool_tick(script->pool, 1U);
    const KolibriFormula *best = kf_pool_best(script->pool);
    if (best) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.6f", best->fitness);
        kolibri_script_log(script, "SCRIPT_TICK", buffer);
    }
    return 0;
}

static int kolibri_execute_print_canvas(KolibriScript *script) {
    if (!script->vyvod) {
        script->vyvod = stdout;
    }
    fprintf(script->vyvod, "[Kolibri] визуализация памяти пока не реализована\n");
    kolibri_script_log(script, "SCRIPT_CANVAS", "недоступно");
    return 0;
}

static int kolibri_execute_swarm(KolibriScript *script, const KolibriStatement *stmt) {
    (void)script;
    (void)stmt;
    kolibri_script_log(script, "SCRIPT_SWARM", "отправка не реализована");
    return 0;
}

static int kolibri_execute_statement(KolibriScript *script, const KolibriStatement *stmt);

static int kolibri_execute_block(KolibriScript *script, const KolibriStatementList *list) {
    for (size_t i = 0; i < list->count; ++i) {
        if (kolibri_execute_statement(script, list->items[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int kolibri_execute_if(KolibriScript *script, const KolibriStatement *stmt) {
    bool condition = false;
    if (!kolibri_evaluate_condition(script, &stmt->data.if_stmt.condition, &condition)) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить условие 'если'");
        return -1;
    }
    if (condition) {
        return kolibri_execute_block(script, &stmt->data.if_stmt.then_body);
    }
    return kolibri_execute_block(script, &stmt->data.if_stmt.else_body);
}

static int kolibri_execute_while(KolibriScript *script, const KolibriStatement *stmt) {
    size_t iterations = 0U;
    while (iterations < KOLIBRI_MAX_LOOP_ITERATIONS) {
        bool condition = false;
        if (!kolibri_evaluate_condition(script, &stmt->data.while_stmt.condition, &condition)) {
            kolibri_script_log(script, "SCRIPT_ERROR", "Не удалось вычислить условие 'пока'");
            return -1;
        }
        if (!condition) {
            break;
        }
        if (kolibri_execute_block(script, &stmt->data.while_stmt.body) != 0) {
            return -1;
        }
        iterations += 1U;
    }
    if (iterations >= KOLIBRI_MAX_LOOP_ITERATIONS) {
        kolibri_script_log(script, "SCRIPT_ERROR", "Превышен лимит итераций цикла");
        return -1;
    }
    return 0;
}

static int kolibri_execute_statement(KolibriScript *script, const KolibriStatement *stmt) {
    switch (stmt->kind) {
    case KOLIBRI_NODE_SHOW:
        return kolibri_execute_show(script, stmt);
    case KOLIBRI_NODE_VARIABLE:
        return kolibri_execute_variable(script, stmt);
    case KOLIBRI_NODE_TEACH:
        return kolibri_execute_teach(script, stmt);
    case KOLIBRI_NODE_CREATE_FORMULA:
        return kolibri_execute_create_formula(script, stmt);
    case KOLIBRI_NODE_EVALUATE_FORMULA:
        return kolibri_execute_evaluate_formula(script, stmt);
    case KOLIBRI_NODE_SAVE_FORMULA:
        return kolibri_execute_save_formula(script, stmt);
    case KOLIBRI_NODE_DROP_FORMULA:
        return kolibri_execute_drop_formula(script, stmt);
    case KOLIBRI_NODE_CALL_EVOLUTION:
        return kolibri_execute_call_evolution(script);
    case KOLIBRI_NODE_PRINT_CANVAS:
        return kolibri_execute_print_canvas(script);
    case KOLIBRI_NODE_SWARM_SEND:
        return kolibri_execute_swarm(script, stmt);
    case KOLIBRI_NODE_IF:
        return kolibri_execute_if(script, stmt);
    case KOLIBRI_NODE_WHILE:
        return kolibri_execute_while(script, stmt);
    case KOLIBRI_NODE_MODE:
        return kolibri_execute_mode(script, stmt);
    case KOLIBRI_NODE_VERIFY:
        return kolibri_execute_verify(script, stmt);
    default:
        return 0;
    }
}

static void kolibri_script_reset(KolibriScript *script) {
    if (!script) {
        return;
    }
    kolibri_script_clear_variables(script);
    kolibri_script_clear_associations(script);
    kolibri_script_clear_formulas(script);
    kolibri_crystal_reset(&script->crystal_core);
    kolibri_script_set_mode(script, "neutral");
}

/* ===================== Public API ===================== */

int ks_init(KolibriScript *skript, KolibriFormulaPool *pool, KolibriGenome *genome) {
    if (!skript) {
        return -1;
    }
    memset(skript, 0, sizeof(*skript));
    skript->pool = pool;
    skript->genome = genome;
    skript->vyvod = stdout;
    kolibri_digit_text_init(&skript->source_stream);
    kolibri_crystal_init(&skript->crystal_core);
    kolibri_symbol_table_init(&skript->symbol_table, genome);
    kolibri_symbol_table_load(&skript->symbol_table);
    kolibri_symbol_table_seed_defaults(&skript->symbol_table);
    kolibri_script_set_mode(skript, "neutral");
    return 0;
}

void ks_free(KolibriScript *skript) {
    if (!skript) {
        return;
    }
    kolibri_script_reset(skript);
    kolibri_digit_text_free(&skript->source_stream);
    kolibri_crystal_free(&skript->crystal_core);
    skript->pool = NULL;
    skript->genome = NULL;
    skript->vyvod = NULL;
}

void ks_set_output(KolibriScript *skript, FILE *vyvod) {
    if (!skript) {
        return;
    }
    skript->vyvod = vyvod ? vyvod : stdout;
}

int ks_load_text(KolibriScript *skript, const char *text) {
    if (!skript || !text) {
        return -1;
    }
    return kolibri_digit_text_assign_utf8(&skript->source_stream, text);
}

int ks_load_file(KolibriScript *skript, const char *path) {
    if (!skript || !path) {
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    char *buffer = (char *)malloc((size_t)size + 1U);
    if (!buffer) {
        fclose(file);
        return -1;
    }
    size_t read_bytes = fread(buffer, 1U, (size_t)size, file);
    fclose(file);
    buffer[read_bytes] = '\0';
    int result = kolibri_digit_text_assign_utf8(&skript->source_stream, buffer);
    free(buffer);
    return result;
}

int ks_execute(KolibriScript *skript) {
    if (!skript) {
        return -1;
    }
    if (skript->source_stream.length == 0U) {
        return -1;
    }
    char *source_utf8 = NULL;
    if (kolibri_digit_text_to_utf8(&skript->source_stream, &source_utf8) != 0) {
        return -1;
    }
    kolibri_script_reset(skript);
    KolibriTokenBuffer tokens;
    kolibri_token_buffer_init(&tokens);
    KolibriDiagnosticBuffer diagnostics;
    kolibri_diagnostic_buffer_init(&diagnostics);

    KolibriLexer lexer;
    kolibri_lexer_init(&lexer, source_utf8, &tokens, &diagnostics);
    if (kolibri_lexer_run(&lexer) != 0) {
        free(source_utf8);
        kolibri_token_buffer_free(&tokens);
        kolibri_diagnostic_buffer_free(&diagnostics);
        kolibri_script_log(skript, "SCRIPT_ERROR", "Лексический анализ завершился с ошибкой");
        return -1;
    }

    KolibriParser parser;
    kolibri_parser_init(&parser, &tokens, &diagnostics);
    KolibriProgram program;
    kolibri_statement_list_init(&program.statements);
    bool parsed = kolibri_parser_parse_program(&parser, &program);
    if (!parsed || diagnostics.count > 0) {
        if (diagnostics.count > 0 && diagnostics.data[0].message) {
            kolibri_script_log(skript, "SCRIPT_ERROR", diagnostics.data[0].message);
        }
        free(source_utf8);
        kolibri_program_free(&program);
        kolibri_token_buffer_free(&tokens);
        kolibri_diagnostic_buffer_free(&diagnostics);
        return -1;
    }

    int status = kolibri_execute_block(skript, &program.statements);

    kolibri_program_free(&program);
    kolibri_token_buffer_free(&tokens);
    kolibri_diagnostic_buffer_free(&diagnostics);
    free(source_utf8);

    return status;
}
#include <ctype.h>
#include <inttypes.h>
