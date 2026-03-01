#include "tokens.h"
#include <stdlib.h>
#include <string.h>

static const char *TOKEN_NAMES[] = {
    [TOK_HASH]          = "HASH",
    [TOK_LBRACKET]      = "LBRACKET",
    [TOK_RBRACKET]      = "RBRACKET",
    [TOK_COLON]         = "COLON",
    [TOK_EQUALS]        = "EQUALS",
    [TOK_QUESTION]      = "QUESTION",
    [TOK_IDENTIFIER]    = "IDENTIFIER",
    [TOK_TEXT]          = "TEXT",
    [TOK_ESCAPE]        = "ESCAPE",
    [TOK_STRING_START]  = "STRING_START",
    [TOK_STRING_END]    = "STRING_END",
    [TOK_STRING_TEXT]   = "STRING_TEXT",
    [TOK_STRING_ESCAPE] = "STRING_ESCAPE",
    [TOK_CODE_OPEN]     = "CODE_OPEN",
    [TOK_CODE_CLOSE]    = "CODE_CLOSE",
    [TOK_RAW_STRING]    = "RAW_STRING",
    [TOK_WS]            = "WS",
    [TOK_NEWLINE]       = "NEWLINE",
    [TOK_EOF]           = "EOF",
};

const char *token_type_name(TokenType type) {
    if (type >= 0 && type < TOK_COUNT) {
        return TOKEN_NAMES[type];
    }
    return "UNKNOWN";
}

/*
 * Identifier special characters: ! $ % & * + - / < > @ ^ _ ~ |
 * Plus letters, digits, and dot.
 */
static const bool IDENT_TABLE[256] = {
    /* Build from the set: isalpha || isdigit || . || !$%&*+-/<>@^_~| */
    ['!'] = true, ['$'] = true, ['%'] = true, ['&'] = true,
    ['*'] = true, ['+'] = true, ['-'] = true, ['/'] = true,
    ['<'] = true, ['>'] = true, ['@'] = true, ['^'] = true,
    ['_'] = true, ['~'] = true, ['|'] = true, ['.'] = true,
    ['0'] = true, ['1'] = true, ['2'] = true, ['3'] = true,
    ['4'] = true, ['5'] = true, ['6'] = true, ['7'] = true,
    ['8'] = true, ['9'] = true,
    ['A'] = true, ['B'] = true, ['C'] = true, ['D'] = true,
    ['E'] = true, ['F'] = true, ['G'] = true, ['H'] = true,
    ['I'] = true, ['J'] = true, ['K'] = true, ['L'] = true,
    ['M'] = true, ['N'] = true, ['O'] = true, ['P'] = true,
    ['Q'] = true, ['R'] = true, ['S'] = true, ['T'] = true,
    ['U'] = true, ['V'] = true, ['W'] = true, ['X'] = true,
    ['Y'] = true, ['Z'] = true,
    ['a'] = true, ['b'] = true, ['c'] = true, ['d'] = true,
    ['e'] = true, ['f'] = true, ['g'] = true, ['h'] = true,
    ['i'] = true, ['j'] = true, ['k'] = true, ['l'] = true,
    ['m'] = true, ['n'] = true, ['o'] = true, ['p'] = true,
    ['q'] = true, ['r'] = true, ['s'] = true, ['t'] = true,
    ['u'] = true, ['v'] = true, ['w'] = true, ['x'] = true,
    ['y'] = true, ['z'] = true,
};

bool is_ident_char(char ch) {
    return IDENT_TABLE[(unsigned char)ch];
}

static const bool HEX_TABLE[256] = {
    ['0'] = true, ['1'] = true, ['2'] = true, ['3'] = true,
    ['4'] = true, ['5'] = true, ['6'] = true, ['7'] = true,
    ['8'] = true, ['9'] = true,
    ['a'] = true, ['b'] = true, ['c'] = true, ['d'] = true,
    ['e'] = true, ['f'] = true,
    ['A'] = true, ['B'] = true, ['C'] = true, ['D'] = true,
    ['E'] = true, ['F'] = true,
};

bool is_hex_digit(char ch) {
    return HEX_TABLE[(unsigned char)ch];
}

void token_array_init(TokenArray *arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void token_array_push(TokenArray *arr, Token tok) {
    if (arr->count >= arr->capacity) {
        int new_cap = arr->capacity == 0 ? 64 : arr->capacity * 2;
        arr->items = realloc(arr->items, (size_t)new_cap * sizeof(Token));
        arr->capacity = new_cap;
        /* Fix up value pointers for ALL existing tokens after realloc */
        for (int i = 0; i < arr->count; i++) {
            if (arr->items[i].value_is_inline) {
                arr->items[i].value = arr->items[i].value_buf;
            } else if (arr->items[i].value_heap) {
                arr->items[i].value = arr->items[i].value_heap;
            }
        }
    }
    arr->items[arr->count] = tok;
    /* Fix up value pointer for the newly pushed token */
    if (tok.value_is_inline) {
        arr->items[arr->count].value = arr->items[arr->count].value_buf;
    } else if (tok.value_heap) {
        arr->items[arr->count].value = arr->items[arr->count].value_heap;
    }
    arr->count++;
}

void token_array_free(TokenArray *arr) {
    for (int i = 0; i < arr->count; i++) {
        free(arr->items[i].value_heap);
    }
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}
