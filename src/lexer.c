#define _POSIX_C_SOURCE 200809L

#include "lexer.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    STATE_NORMAL,
    STATE_INTERP_STRING,
    STATE_CODE_MODE,
    STATE_RAW_STRING,
} LexState;

typedef struct {
    LexState state;
    int bracket_depth;
} StateEntry;

typedef struct {
    const char *source;
    int source_len;
    const char *filename;
    int pos;
    int line;
    int col;
    TokenArray tokens;
    StateEntry *state_stack;
    int stack_count;
    int stack_cap;
    LexState state;
    int bracket_depth;
    Position string_start; /* where the current interpreted string opened */
    PdError *err;
} Lexer;

/* Forward declarations */
static int lex_normal(Lexer *L);
static int lex_interp_string(Lexer *L);
static int lex_code_mode(Lexer *L);
static int lex_prose_escape(Lexer *L);
static int lex_string_open(Lexer *L);
static int lex_string_escape(Lexer *L);

static void lexer_init(Lexer *L, const char *source, const char *filename, PdError *err) {
    L->source = source;
    L->source_len = (int)strlen(source);
    L->filename = filename;
    L->pos = 0;
    L->line = 1;
    L->col = 1;
    token_array_init(&L->tokens);
    L->state_stack = NULL;
    L->stack_count = 0;
    L->stack_cap = 0;
    L->state = STATE_NORMAL;
    L->bracket_depth = 0;
    L->string_start = pos_make(0, 0, 0);
    L->err = err;
}

static void lexer_cleanup(Lexer *L) {
    free(L->state_stack);
}

static Position current_pos(Lexer *L) {
    return pos_make(L->line, L->col, L->pos);
}

static char peek(Lexer *L, int offset) {
    int idx = L->pos + offset;
    if (idx < L->source_len) return L->source[idx];
    return '\0';
}

static char advance(Lexer *L) {
    char ch = L->source[L->pos];
    L->pos++;
    if (ch == '\n') {
        L->line++;
        L->col = 1;
    } else {
        L->col++;
    }
    return ch;
}

/*
 * Emit a token where value == raw and both point into the source buffer.
 * No copies needed.
 */
static void emit_simple(Lexer *L, TokenType type, Position start, int len) {
    Token tok;
    tok.type = type;
    tok.span = span_make(start, current_pos(L));
    tok.value = L->source + start.offset;
    tok.value_len = len;
    tok.raw = tok.value;
    tok.raw_len = len;
    tok.value_buf[0] = '\0';
    tok.value_heap = NULL;
    tok.value_is_inline = false;
    token_array_push(&L->tokens, tok);
}

/*
 * Emit a token with a value that needs to be copied (not in source buffer).
 * raw always points into source buffer.
 */
static void emit_copy(Lexer *L, TokenType type, const char *value, int value_len,
                      const char *raw, int raw_len, Position start) {
    Token tok;
    tok.type = type;
    tok.span = span_make(start, current_pos(L));
    tok.raw = raw;
    tok.raw_len = raw_len;
    tok.value_heap = NULL;
    tok.value_is_inline = false;

    if (value_len < (int)sizeof(tok.value_buf)) {
        memcpy(tok.value_buf, value, (size_t)value_len);
        tok.value_buf[value_len] = '\0';
        tok.value = tok.value_buf;
        tok.value_is_inline = true;
    } else {
        tok.value_heap = malloc((size_t)value_len + 1);
        memcpy(tok.value_heap, value, (size_t)value_len);
        tok.value_heap[value_len] = '\0';
        tok.value = tok.value_heap;
        tok.value_buf[0] = '\0';
    }
    tok.value_len = value_len;
    token_array_push(&L->tokens, tok);
}

/*
 * Emit a token where value points directly into the source buffer (no copy).
 * raw also points into source buffer.
 */
static void emit_view(Lexer *L, TokenType type, const char *value, int value_len,
                      const char *raw, int raw_len, Position start) {
    Token tok;
    tok.type = type;
    tok.span = span_make(start, current_pos(L));
    tok.value = value;
    tok.value_len = value_len;
    tok.raw = raw;
    tok.raw_len = raw_len;
    tok.value_buf[0] = '\0';
    tok.value_heap = NULL;
    tok.value_is_inline = false;
    token_array_push(&L->tokens, tok);
}

static int lex_error(Lexer *L, const char *message, Position pos) {
    return pd_lex_error(L->err, message, pos, L->source, L->filename);
}

static void push_state(Lexer *L, LexState state, int bracket_depth) {
    if (L->stack_count >= L->stack_cap) {
        int new_cap = L->stack_cap == 0 ? 8 : L->stack_cap * 2;
        L->state_stack = realloc(L->state_stack, (size_t)new_cap * sizeof(StateEntry));
        L->stack_cap = new_cap;
    }
    L->state_stack[L->stack_count++] = (StateEntry){L->state, L->bracket_depth};
    L->state = state;
    L->bracket_depth = bracket_depth;
}

static void pop_state(Lexer *L) {
    L->stack_count--;
    L->state = L->state_stack[L->stack_count].state;
    L->bracket_depth = L->state_stack[L->stack_count].bracket_depth;
}

/* Encode a Unicode codepoint as UTF-8 into buf. Returns number of bytes written. */
static int encode_utf8(unsigned int cp, char *buf) {
    if (cp <= 0x7F) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static int hex_val(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

/*
 * Read `count` hex digits and produce the decoded UTF-8 value.
 * value_out must be at least 5 bytes. Returns value length, or -1 on error.
 */
static int lex_hex_escape(Lexer *L, int count, Position start,
                          char *value_out, int *value_len_out) {
    unsigned int codepoint = 0;
    for (int i = 0; i < count; i++) {
        if (L->pos >= L->source_len) {
            char msg[80];
            snprintf(msg, sizeof(msg),
                     "incomplete escape: expected %d hex digits, got %d", count, i);
            /* Use a static buffer for the message since pd_lex_error expects
               a string that outlives the call — but we need a dynamic message.
               For now, use a fixed message. */
            return lex_error(L, "incomplete hex escape sequence", start);
        }
        char ch = peek(L, 0);
        if (!is_hex_digit(ch)) {
            return lex_error(L, "invalid hex digit in escape sequence", start);
        }
        codepoint = codepoint * 16 + (unsigned)hex_val(ch);
        advance(L);
    }
    if (codepoint > 0x10FFFF) {
        return lex_error(L, "Unicode codepoint out of range", start);
    }
    *value_len_out = encode_utf8(codepoint, value_out);
    return 0;
}

static int check_adjacent_string(Lexer *L) {
    if (L->pos < L->source_len && peek(L, 0) == '"') {
        return lex_error(L, "adjacent strings are not allowed — insert whitespace between string literals",
                         current_pos(L));
    }
    return 0;
}

/* --- Normal mode --- */

static int lex_ws(Lexer *L) {
    Position start = current_pos(L);
    while (L->pos < L->source_len && (peek(L, 0) == ' ' || peek(L, 0) == '\t')) {
        advance(L);
    }
    emit_simple(L, TOK_WS, start, L->pos - start.offset);
    return 0;
}

static int lex_identifier(Lexer *L) {
    Position start = current_pos(L);
    while (L->pos < L->source_len && is_ident_char(peek(L, 0))) {
        advance(L);
    }
    emit_simple(L, TOK_IDENTIFIER, start, L->pos - start.offset);
    return 0;
}

static int lex_text(Lexer *L) {
    Position start = current_pos(L);
    while (L->pos < L->source_len) {
        char ch = peek(L, 0);
        if (ch == '#' || ch == '[' || ch == ']' || ch == '\\' ||
            ch == ':' || ch == '=' || ch == '?' || ch == '"' ||
            ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
            ch == '\0' || is_ident_char(ch)) {
            break;
        }
        advance(L);
    }
    if (L->pos > start.offset) {
        emit_simple(L, TOK_TEXT, start, L->pos - start.offset);
    }
    return 0;
}

static int lex_prose_escape(Lexer *L) {
    Position start = current_pos(L);
    advance(L); /* consume backslash */

    if (L->pos >= L->source_len) {
        return lex_error(L, "unexpected end of input after '\\'", start);
    }

    char ch = peek(L, 0);

    if (ch == '\\' || ch == '#' || ch == '[' || ch == ']' || ch == '"') {
        advance(L);
        /* value is the resolved char (in source), raw is the full escape */
        const char *raw = L->source + start.offset;
        int raw_len = L->pos - start.offset;
        emit_view(L, TOK_ESCAPE, L->source + L->pos - 1, 1, raw, raw_len, start);
        return 0;
    }

    if (ch == 'x') {
        advance(L);
        char value_buf[8];
        int value_len;
        if (lex_hex_escape(L, 2, start, value_buf, &value_len) < 0) return -1;
        const char *raw = L->source + start.offset;
        int raw_len = L->pos - start.offset;
        emit_copy(L, TOK_ESCAPE, value_buf, value_len, raw, raw_len, start);
        return 0;
    }

    if (ch == 'U') {
        advance(L);
        char value_buf[8];
        int value_len;
        if (lex_hex_escape(L, 8, start, value_buf, &value_len) < 0) return -1;
        const char *raw = L->source + start.offset;
        int raw_len = L->pos - start.offset;
        emit_copy(L, TOK_ESCAPE, value_buf, value_len, raw, raw_len, start);
        return 0;
    }

    return lex_error(L, "invalid escape sequence", start);
}

static int lex_raw_string(Lexer *L, int delimiter_count, Position start) {
    int content_start = L->pos;

    while (L->pos < L->source_len) {
        if (peek(L, 0) == '"') {
            int run_start = L->pos;
            int run_count = 0;
            while (L->pos < L->source_len && peek(L, 0) == '"') {
                advance(L);
                run_count++;
            }
            if (run_count == delimiter_count) {
                /* Found closing delimiter */
                const char *raw_content = L->source + content_start;
                int raw_content_len = run_start - content_start;
                char *stripped = strip_string_whitespace(raw_content, raw_content_len);
                if (!stripped) return lex_error(L, "out of memory", start);

                const char *raw = L->source + start.offset;
                int raw_len = L->pos - start.offset;

                /* For raw strings, value is the stripped content.
                   We need to store it — use value_buf if small enough,
                   otherwise we need a different approach. For now, copy into
                   value_buf (up to 7 chars). For longer strings, we'll
                   allocate separately. */
                int slen = (int)strlen(stripped);
                emit_copy(L, TOK_RAW_STRING, stripped, slen, raw, raw_len, start);
                free(stripped);
                return 0;
            }
            /* Not enough quotes — continue scanning */
        } else {
            advance(L);
        }
    }

    return lex_error(L, "unterminated raw string", start);
}

static int lex_string_open(Lexer *L) {
    Position start = current_pos(L);
    int quote_count = 0;
    while (L->pos < L->source_len && peek(L, 0) == '"') {
        advance(L);
        quote_count++;
    }

    if (quote_count == 1) {
        /* Interpreted string */
        emit_simple(L, TOK_STRING_START, start, 1);
        L->string_start = start;
        push_state(L, STATE_INTERP_STRING, 0);
        return 0;
    }

    if (quote_count == 2) {
        /* Empty string: "" — both quotes are in source at start.offset and start.offset+1 */
        const char *q1 = L->source + start.offset;
        const char *q2 = L->source + start.offset + 1;
        emit_view(L, TOK_STRING_START, q1, 1, q1, 1, start);
        emit_view(L, TOK_STRING_END, q2, 1, q2, 1, start);
        return check_adjacent_string(L);
    }

    /* 3+ quotes → raw string */
    int rc = lex_raw_string(L, quote_count, start);
    if (rc < 0) return rc;
    return check_adjacent_string(L);
}

static int lex_string_escape(Lexer *L) {
    Position start = current_pos(L);
    advance(L); /* consume backslash */

    if (L->pos >= L->source_len) {
        return lex_error(L, "unexpected end of input in string escape", start);
    }

    char ch = peek(L, 0);

    /* \[ enters code mode */
    if (ch == '[') {
        advance(L);
        const char *raw = L->source + start.offset;
        emit_view(L, TOK_CODE_OPEN, raw, 2, raw, 2, start);
        push_state(L, STATE_CODE_MODE, 1);
        return 0;
    }

    /* Simple escapes */
    char resolved = 0;
    if (ch == '\\') resolved = '\\';
    else if (ch == '"') resolved = '"';
    else if (ch == 'n') resolved = '\n';
    else if (ch == 't') resolved = '\t';

    if (resolved) {
        advance(L);
        const char *raw = L->source + start.offset;
        int raw_len = L->pos - start.offset;
        emit_copy(L, TOK_STRING_ESCAPE, &resolved, 1, raw, raw_len, start);
        return 0;
    }

    if (ch == 'x') {
        advance(L);
        char value_buf[8];
        int value_len;
        if (lex_hex_escape(L, 2, start, value_buf, &value_len) < 0) return -1;
        const char *raw = L->source + start.offset;
        int raw_len = L->pos - start.offset;
        emit_copy(L, TOK_STRING_ESCAPE, value_buf, value_len, raw, raw_len, start);
        return 0;
    }

    if (ch == 'U') {
        advance(L);
        char value_buf[8];
        int value_len;
        if (lex_hex_escape(L, 8, start, value_buf, &value_len) < 0) return -1;
        const char *raw = L->source + start.offset;
        int raw_len = L->pos - start.offset;
        emit_copy(L, TOK_STRING_ESCAPE, value_buf, value_len, raw, raw_len, start);
        return 0;
    }

    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "invalid string escape sequence "
                 "(string opened at %d:%d)",
                 L->string_start.line, L->string_start.column);
        L->err->detail = strdup(buf);
        L->err->message = L->err->detail;
        L->err->kind = PD_ERR_LEX;
        L->err->position = start;
        L->err->source = L->source;
        L->err->filename = L->filename;
        return -1;
    }
}

static int lex_normal(Lexer *L) {
    char ch = peek(L, 0);

    if (ch == '\0') {
        return lex_error(L, "NUL character in source", current_pos(L));
    }

    if (ch == '#') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_HASH, start, 1);
        return 0;
    }

    if (ch == '[') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_LBRACKET, start, 1);
        return 0;
    }

    if (ch == ']') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_RBRACKET, start, 1);
        return 0;
    }

    if (ch == ':') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_COLON, start, 1);
        return 0;
    }

    if (ch == '=') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_EQUALS, start, 1);
        return 0;
    }

    if (ch == '?') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_QUESTION, start, 1);
        return 0;
    }

    if (ch == '\\') {
        return lex_prose_escape(L);
    }

    if (ch == '"') {
        /* Only parse as string in value/body-introduction positions.
         * Otherwise treat as literal text in prose. */
        int n = L->tokens.count;
        bool as_string = false;
        if (n > 0) {
            TokenType last = L->tokens.items[n - 1].type;
            /* Directly after EQUALS (argument value) */
            if (last == TOK_EQUALS) {
                as_string = true;
            }
            if (!as_string) {
                /* Scan back past WS/NL for COLON or EQUALS */
                for (int i = n - 1; i >= 0; i--) {
                    TokenType t = L->tokens.items[i].type;
                    if (t == TOK_WS || t == TOK_NEWLINE) continue;
                    if (t == TOK_COLON || t == TOK_EQUALS) as_string = true;
                    break;
                }
            }
            if (!as_string) {
                /* Check if we're in a macro head (after # but before :
                 * or newline) — string is valid as body.
                 * Only break on NEWLINE if we've seen content tokens
                 * (identifiers, etc.) between the newline and the quote,
                 * so that [#p\n"hello"] still works (no content after NL). */
                int depth = 0;
                bool seen_content = false;
                for (int i = n - 1; i >= 0; i--) {
                    TokenType t = L->tokens.items[i].type;
                    /* stop at CODE_OPEN — don't scan past code mode boundary
                     * into the enclosing string's token context */
                    if (t == TOK_CODE_OPEN) break;
                    if (t == TOK_RBRACKET) { depth++; seen_content = true; }
                    else if (t == TOK_LBRACKET) {
                        if (depth > 0) { depth--; seen_content = true; }
                        else {
                            /* Enclosing bracket — check for [# */
                            as_string = (i + 1 < n &&
                                         L->tokens.items[i + 1].type == TOK_HASH);
                            break;
                        }
                    }
                    else if (t == TOK_COLON && depth == 0) { break; }
                    else if (t == TOK_HASH && depth == 0) { as_string = true; break; }
                    else if (t == TOK_NEWLINE && depth == 0) {
                        if (seen_content) break;
                    }
                    else if (t != TOK_WS) { seen_content = true; }
                }
            }
        }
        if (as_string) {
            return lex_string_open(L);
        }
        /* Literal double quote in prose */
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_TEXT, start, 1);
        return 0;
    }

    if (ch == '\n') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_NEWLINE, start, 1);
        return 0;
    }

    if (ch == '\r' && peek(L, 1) == '\n') {
        Position start = current_pos(L);
        advance(L);
        advance(L);
        /* value is \n (normalized), raw is \r\n (source view) */
        emit_copy(L, TOK_NEWLINE, "\n", 1, L->source + start.offset, 2, start);
        return 0;
    }

    if (ch == ' ' || ch == '\t') {
        return lex_ws(L);
    }

    if (is_ident_char(ch)) {
        return lex_identifier(L);
    }

    return lex_text(L);
}

static int lex_interp_string(Lexer *L) {
    char ch = peek(L, 0);

    if (ch == '"') {
        Position start = current_pos(L);
        advance(L);
        emit_simple(L, TOK_STRING_END, start, 1);
        pop_state(L);
        return check_adjacent_string(L);
    }

    if (ch == '\\') {
        return lex_string_escape(L);
    }

    /* Accumulate STRING_TEXT */
    Position start = current_pos(L);
    while (L->pos < L->source_len) {
        char c = peek(L, 0);
        if (c == '"' || c == '\\') break;
        advance(L);
    }
    if (L->pos > start.offset) {
        emit_simple(L, TOK_STRING_TEXT, start, L->pos - start.offset);
    } else if (L->pos >= L->source_len) {
        return lex_error(L, "unterminated interpreted string", start);
    }
    return 0;
}

static int lex_code_mode(Lexer *L) {
    char ch = peek(L, 0);

    if (ch == '[') {
        Position start = current_pos(L);
        advance(L);
        L->bracket_depth++;
        emit_simple(L, TOK_LBRACKET, start, 1);
        return 0;
    }

    if (ch == ']') {
        Position start = current_pos(L);
        advance(L);
        L->bracket_depth--;
        if (L->bracket_depth == 0) {
            emit_simple(L, TOK_CODE_CLOSE, start, 1);
            pop_state(L);
        } else {
            emit_simple(L, TOK_RBRACKET, start, 1);
        }
        return 0;
    }

    /* Everything else dispatches like normal mode */
    return lex_normal(L);
}

int pd_tokenize(const char *source, const char *filename,
                TokenArray *out, PdError *err) {
    Lexer L;
    lexer_init(&L, source, filename, err);

    while (L.pos < L.source_len) {
        int rc;
        switch (L.state) {
        case STATE_NORMAL:
            rc = lex_normal(&L);
            break;
        case STATE_INTERP_STRING:
            rc = lex_interp_string(&L);
            break;
        case STATE_CODE_MODE:
            rc = lex_code_mode(&L);
            break;
        case STATE_RAW_STRING:
            rc = lex_error(&L, "internal error: unexpected RAW_STRING state",
                           current_pos(&L));
            break;
        default:
            rc = lex_error(&L, "internal error: unknown state", current_pos(&L));
            break;
        }
        if (rc < 0) {
            token_array_free(&L.tokens);
            lexer_cleanup(&L);
            return -1;
        }
    }

    /* Check for unclosed states at EOF */
    if (L.state == STATE_INTERP_STRING) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "unterminated interpreted string "
                 "(opened at %d:%d)",
                 L.string_start.line, L.string_start.column);
        token_array_free(&L.tokens);
        err->detail = strdup(buf);
        err->message = err->detail;
        lexer_cleanup(&L);
        err->kind = PD_ERR_LEX;
        err->position = L.string_start;
        err->source = source;
        err->filename = filename;
        return -1;
    }
    if (L.state == STATE_CODE_MODE) {
        token_array_free(&L.tokens);
        lexer_cleanup(&L);
        return pd_lex_error(err, "unterminated code mode in string",
                            current_pos(&L), source, filename);
    }

    /* Emit EOF — source pointer at end, zero-length */
    Position eof_pos = current_pos(&L);
    const char *eof_ptr = L.source + L.source_len;
    emit_view(&L, TOK_EOF, eof_ptr, 0, eof_ptr, 0, eof_pos);

    *out = L.tokens;
    lexer_cleanup(&L);
    return 0;
}
