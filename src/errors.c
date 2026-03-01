#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pd_error_init(PdError *err) {
    err->kind = PD_ERR_NONE;
    err->message = NULL;
    err->position = (Position){0, 0, 0};
    err->span = (Span){{0, 0, 0}, {0, 0, 0}};
    err->source = NULL;
    err->filename = NULL;
    err->formatted = NULL;
    err->detail = NULL;
}

void pd_error_free(PdError *err) {
    free(err->formatted);
    err->formatted = NULL;
    free(err->detail);
    err->detail = NULL;
}

/*
 * Find the start of line `line_idx` (0-based) in source.
 * Returns pointer to start, sets *line_len to length (excluding newline).
 */
static const char *find_source_line(const char *source, int line_idx, int *line_len) {
    const char *p = source;
    int cur = 0;
    while (cur < line_idx && *p) {
        if (*p == '\n') cur++;
        p++;
    }
    /* p now points to start of the desired line */
    const char *end = p;
    while (*end && *end != '\n' && *end != '\r') end++;
    *line_len = (int)(end - p);
    return p;
}

static int int_max(int a, int b) { return a > b ? a : b; }
static int int_min(int a, int b) { return a < b ? a : b; }

static int count_digits(int n) {
    if (n <= 0) return 1;
    int d = 0;
    while (n > 0) { d++; n /= 10; }
    return d;
}

/*
 * Format error with source context.
 * Output format (mirrors rustc/Python picodoc):
 *
 * error: <message>
 *   --> <filename>:<line>:<col>
 *    |
 *  N | <source line>
 *    |   ^^
 */
char *pd_format_error(PdError *err) {
    if (!err->source) {
        /* No source context — just format the message */
        int len = (int)strlen(err->message) + 32;
        err->formatted = malloc((size_t)len);
        if (err->formatted) {
            snprintf(err->formatted, (size_t)len, "error: %s", err->message);
        }
        return err->formatted;
    }

    const char *filename = err->filename ? err->filename : "input.pdoc";
    int line, col, underline_len;

    if (err->kind == PD_ERR_LEX) {
        line = err->position.line;
        col = err->position.column;
        int line_len;
        find_source_line(err->source, line - 1, &line_len);
        underline_len = int_max(1, int_min(2, line_len - col + 1));
    } else {
        line = err->span.start.line;
        col = err->span.start.column;
        if (err->span.end.line == err->span.start.line) {
            underline_len = int_max(1, err->span.end.column - col);
        } else {
            int line_len;
            find_source_line(err->source, line - 1, &line_len);
            underline_len = int_max(1, line_len - col + 1);
        }
    }

    int source_line_len;
    const char *source_line = find_source_line(err->source, line - 1, &source_line_len);

    int gutter_width = count_digits(line) + 1;

    /* Build formatted output */
    /* Generous allocation */
    int alloc = (int)strlen(err->message) + (int)strlen(filename) +
                source_line_len + gutter_width * 5 + col + underline_len + 128;
    err->formatted = malloc((size_t)alloc);
    if (!err->formatted) return NULL;

    char *p = err->formatted;

    /* Line 1: error: <message> */
    p += sprintf(p, "error: %s\n", err->message);

    /* Line 2:   --> filename:line:col */
    for (int i = 0; i < gutter_width; i++) *p++ = ' ';
    p += sprintf(p, "--> %s:%d:%d\n", filename, line, col);

    /* Line 3: blank gutter | */
    for (int i = 0; i < gutter_width; i++) *p++ = ' ';
    *p++ = '|'; *p++ = '\n';

    /* Line 4: N | source line */
    p += sprintf(p, "%*d |", gutter_width - 1, line);
    *p++ = ' ';
    memcpy(p, source_line, (size_t)source_line_len);
    p += source_line_len;
    *p++ = '\n';

    /* Line 5: blank gutter | pad carets */
    for (int i = 0; i < gutter_width; i++) *p++ = ' ';
    *p++ = '|'; *p++ = ' ';
    for (int i = 0; i < col - 1; i++) *p++ = ' ';
    for (int i = 0; i < underline_len; i++) *p++ = '^';
    *p = '\0';

    return err->formatted;
}

int pd_lex_error(PdError *err, const char *message, Position pos,
                 const char *source, const char *filename) {
    err->kind = PD_ERR_LEX;
    err->message = message;
    err->position = pos;
    err->source = source;
    err->filename = filename;
    return -1;
}

int pd_parse_error(PdError *err, const char *message, Span span,
                   const char *source, const char *filename) {
    err->kind = PD_ERR_PARSE;
    err->message = message;
    err->span = span;
    err->source = source;
    err->filename = filename;
    return -1;
}

int pd_eval_error(PdError *err, const char *message, Span span,
                  const char *source, const char *filename) {
    err->kind = PD_ERR_EVAL;
    err->message = message;
    err->span = span;
    err->source = source;
    err->filename = filename;
    return -1;
}

int pd_render_error(PdError *err, const char *message, Span span,
                    const char *source, const char *filename) {
    err->kind = PD_ERR_RENDER;
    err->message = message;
    err->span = span;
    err->source = source;
    err->filename = filename;
    return -1;
}
