#include "strings.h"
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
