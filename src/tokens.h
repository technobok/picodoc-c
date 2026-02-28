#ifndef PD_TOKENS_H
#define PD_TOKENS_H

#include <stdbool.h>

/* Token types — mirrors Python TokenType enum exactly. */
typedef enum {
    /* Structural (single-character) */
    TOK_HASH,         /* # */
    TOK_LBRACKET,     /* [ */
    TOK_RBRACKET,     /* ] */
    TOK_COLON,        /* : */
    TOK_EQUALS,       /* = */
    TOK_QUESTION,     /* ? */

    /* Content */
    TOK_IDENTIFIER,   /* ident_char+ */
    TOK_TEXT,         /* non-ident, non-special char runs */
    TOK_ESCAPE,       /* prose escape — value is resolved character */

    /* Interpreted string sub-tokens */
    TOK_STRING_START, /* opening " */
    TOK_STRING_END,   /* closing " */
    TOK_STRING_TEXT,  /* literal text segment within string */
    TOK_STRING_ESCAPE,/* string escape — value is resolved character */
    TOK_CODE_OPEN,    /* \[ entering code mode */
    TOK_CODE_CLOSE,   /* ] exiting code mode */

    /* Raw string (single token, content after whitespace stripping) */
    TOK_RAW_STRING,

    /* Whitespace */
    TOK_WS,           /* horizontal whitespace (spaces/tabs) */
    TOK_NEWLINE,      /* \n or \r\n */

    TOK_EOF,

    TOK_COUNT         /* sentinel for array sizing */
} TokenType;

/* Return a human-readable name for a token type. */
const char *token_type_name(TokenType type);

/* Source position: 1-based line and column, 0-based byte offset. */
typedef struct {
    int line;
    int column;
    int offset;
} Position;

/* Source range from start to end position. */
typedef struct {
    Position start;
    Position end;
} Span;

/*
 * A single lexer token.
 *
 * value: resolved content (e.g. the actual character for escapes)
 * raw:   original source text (view into source buffer)
 *
 * For most tokens, value and raw point directly into the source buffer.
 * For escapes and raw strings, value points to value_buf (inline, up to
 * 7 bytes) or to a heap-allocated buffer (value_heap, for longer values).
 */
typedef struct {
    TokenType type;
    const char *value;    /* resolved content */
    int value_len;
    const char *raw;      /* original source text */
    int raw_len;
    Span span;
    char value_buf[8];    /* inline storage for short decoded values */
    char *value_heap;     /* heap storage for long decoded values (or NULL) */
    bool value_is_inline; /* true if value should point to value_buf */
} Token;

/* Dynamic token array. */
typedef struct {
    Token *items;
    int count;
    int capacity;
} TokenArray;

void token_array_init(TokenArray *arr);
void token_array_push(TokenArray *arr, Token tok);
void token_array_free(TokenArray *arr);

/* Character classification. */
bool is_ident_char(char ch);
bool is_hex_digit(char ch);

/* Convenience: create a Position. */
static inline Position pos_make(int line, int col, int offset) {
    return (Position){.line = line, .column = col, .offset = offset};
}

/* Convenience: create a Span. */
static inline Span span_make(Position start, Position end) {
    return (Span){.start = start, .end = end};
}

#endif /* PD_TOKENS_H */
