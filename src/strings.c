#include "strings.h"
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static bool is_blank(const char *s, int len) {
    for (int i = 0; i < len; i++) {
        if (s[i] != ' ' && s[i] != '\t') return false;
    }
    return true;
}

static bool starts_with(const char *s, int slen, const char *prefix, int plen) {
    if (slen < plen) return false;
    return memcmp(s, prefix, (size_t)plen) == 0;
}

/*
 * Internal: split content into lines by '\n'.
 * Returns array of (pointer, length) pairs. Count written to *out_count.
 * Caller must free the returned array.
 */
typedef struct {
    const char *data;
    int len;
} StrSlice;

static StrSlice *split_lines(const char *content, int len, int *out_count) {
    /* Count newlines to size the array */
    int n = 1;
    for (int i = 0; i < len; i++) {
        if (content[i] == '\n') n++;
    }

    StrSlice *lines = malloc((size_t)n * sizeof(StrSlice));
    if (!lines) return NULL;

    int count = 0;
    const char *start = content;
    for (int i = 0; i < len; i++) {
        if (content[i] == '\n') {
            lines[count].data = start;
            lines[count].len = (int)(content + i - start);
            count++;
            start = content + i + 1;
        }
    }
    /* Last line (after final newline or if no newline) */
    lines[count].data = start;
    lines[count].len = (int)(content + len - start);
    count++;

    *out_count = count;
    return lines;
}

char *strip_string_whitespace(const char *content, int len) {
    if (len == 0 || !content) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    int line_count = 0;
    StrSlice *lines = split_lines(content, len, &line_count);
    if (!lines) return NULL;

    int first = 0;
    int last = line_count; /* exclusive */

    /* Step 2: discard blank first line */
    if (line_count > 0 && is_blank(lines[0].data, lines[0].len)) {
        first = 1;
    }

    if (first >= last) {
        free(lines);
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    /* Step 3: check last line */
    const char *common_prefix = NULL;
    int prefix_len = 0;

    if (last - first >= 1 && is_blank(lines[last - 1].data, lines[last - 1].len)) {
        common_prefix = lines[last - 1].data;
        prefix_len = lines[last - 1].len;
        last--;
    }

    if (first >= last) {
        free(lines);
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    /* Step 4: strip common prefix from non-empty lines */
    bool can_strip = false;
    if (prefix_len > 0) {
        can_strip = true;
        for (int i = first; i < last; i++) {
            if (!is_blank(lines[i].data, lines[i].len) &&
                !starts_with(lines[i].data, lines[i].len, common_prefix, prefix_len)) {
                can_strip = false;
                break;
            }
        }
    }

    /* Step 5: compute output size and build result */
    int total = 0;
    for (int i = first; i < last; i++) {
        int line_len = lines[i].len;
        if (can_strip && !is_blank(lines[i].data, lines[i].len)) {
            line_len -= prefix_len;
        }
        total += line_len;
        if (i < last - 1) total++; /* newline separator */
    }

    char *result = malloc((size_t)total + 1);
    if (!result) {
        free(lines);
        return NULL;
    }

    int pos = 0;
    for (int i = first; i < last; i++) {
        const char *src = lines[i].data;
        int line_len = lines[i].len;
        if (can_strip && !is_blank(lines[i].data, lines[i].len)) {
            src += prefix_len;
            line_len -= prefix_len;
        }
        memcpy(result + pos, src, (size_t)line_len);
        pos += line_len;
        if (i < last - 1) {
            result[pos++] = '\n';
        }
    }
    result[pos] = '\0';

    free(lines);
    return result;
}

/*
 * Measure leading whitespace (spaces/tabs) at position p within a string
 * of length len. Returns the count of whitespace characters.
 */
static int measure_ws(const char *s, int p, int len) {
    int n = 0;
    while (p + n < len && (s[p + n] == ' ' || s[p + n] == '\t'))
        n++;
    return n;
}

/*
 * Check if a line starting at position p within string s (length len)
 * is blank — contains only whitespace before the next '\n' or end.
 */
static bool line_is_blank(const char *s, int p, int len) {
    while (p < len && s[p] != '\n') {
        if (s[p] != ' ' && s[p] != '\t')
            return false;
        p++;
    }
    return true;
}

int dedent_body_children(PdNode **children, int count) {
    if (count == 0)
        return 0;

    /* --- Pass 1: compute common prefix length --- */

    int prefix_len = -1; /* -1 = not yet set */
    char *prefix_chars = NULL; /* first non-blank line's leading ws */
    bool at_line_start = true;

    for (int ci = 0; ci < count; ci++) {
        PdNode *node = children[ci];
        if (node->type != NODE_TEXT) {
            at_line_start = false;
            continue;
        }
        const char *val = node->as.text.value;
        int vlen = node->as.text.value_len;

        int p = 0;
        while (p < vlen) {
            if (at_line_start) {
                /* Check if this line is blank AND newline-terminated.
                 * Unterminated whitespace means the line continues in
                 * the next non-TEXT node (e.g. code_section) — not blank. */
                bool has_nl = (memchr(val + p, '\n', (size_t)(vlen - p)) != NULL);
                if (line_is_blank(val, p, vlen) && has_nl) {
                    /* Skip to next line — blank lines don't affect prefix */
                    while (p < vlen && val[p] != '\n')
                        p++;
                    p++; /* skip \n */
                    at_line_start = true;
                    continue;
                }

                int ws = measure_ws(val, p, vlen);
                if (prefix_len < 0) {
                    /* First non-blank line — store its prefix */
                    prefix_len = ws;
                    if (ws > 0) {
                        prefix_chars = malloc((size_t)ws);
                        if (!prefix_chars) return -1;
                        memcpy(prefix_chars, val + p, (size_t)ws);
                    }
                } else {
                    /* Subsequent non-blank line — find common prefix */
                    int common = prefix_len < ws ? prefix_len : ws;
                    /* Shrink to matching characters */
                    for (int i = 0; i < common; i++) {
                        if (val[p + i] != prefix_chars[i]) {
                            common = i;
                            break;
                        }
                    }
                    prefix_len = common;
                }
                at_line_start = false;
            }

            /* Scan forward to next newline */
            while (p < vlen && val[p] != '\n')
                p++;
            if (p < vlen) {
                p++; /* skip \n */
                at_line_start = true;
            }
        }
    }

    /* Nothing to strip */
    if (prefix_len <= 0) {
        free(prefix_chars);
        return 0;
    }

    /* --- Pass 2: strip prefix from TEXT nodes --- */

    at_line_start = true;

    for (int ci = 0; ci < count; ci++) {
        PdNode *node = children[ci];
        if (node->type != NODE_TEXT) {
            at_line_start = false;
            continue;
        }
        const char *val = node->as.text.value;
        int vlen = node->as.text.value_len;

        /* Compute output size first */
        int out_len = 0;
        bool als = at_line_start; /* local copy for sizing pass */
        for (int p = 0; p < vlen; ) {
            if (als) {
                int ws = measure_ws(val, p, vlen);
                int skip = ws < prefix_len ? ws : prefix_len;
                /* Only skip from non-blank lines, but also skip from blank
                 * lines up to prefix_len (they just have less ws) */
                out_len += (vlen > p + ws && val[p + ws] != '\n')
                    ? (vlen - p - skip) : (vlen - p);
                /* Actually, let's just do the proper per-character walk */
                /* Reset and break — we'll use a different approach */
                out_len = 0;
                als = at_line_start;
                break;
            }
            out_len++;
            p++;
        }

        /* Build output with a single pass */
        /* Worst case: output is same size as input */
        char *out = malloc((size_t)vlen + 1);
        if (!out) {
            free(prefix_chars);
            return -1;
        }
        int op = 0;
        bool ls = at_line_start;

        for (int p = 0; p < vlen; ) {
            if (ls) {
                bool has_nl = (memchr(val + p, '\n', (size_t)(vlen - p)) != NULL);
                if (line_is_blank(val, p, vlen) && has_nl) {
                    /* Copy blank line as-is (no stripping) */
                    while (p < vlen && val[p] != '\n')
                        out[op++] = val[p++];
                    out[op++] = val[p++]; /* copy \n */
                    ls = true;
                    continue;
                }
                /* Non-blank line: skip prefix_len chars */
                int skip = prefix_len;
                if (skip > vlen - p) skip = vlen - p;
                p += skip;
                ls = false;
                continue;
            }
            if (val[p] == '\n') {
                out[op++] = '\n';
                p++;
                ls = true;
            } else {
                out[op++] = val[p++];
            }
        }
        out[op] = '\0';

        /* Track line-start state for next node */
        at_line_start = ls;

        /* Replace the node's value */
        free(node->as.text.value);
        node->as.text.value = out;
        node->as.text.value_len = op;
    }

    free(prefix_chars);
    return 0;
}
