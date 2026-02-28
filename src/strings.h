#ifndef PD_STRINGS_H
#define PD_STRINGS_H

/*
 * Apply PicoDoc whitespace stripping rules to string content.
 *
 * Algorithm:
 * 1. Split into lines.
 * 2. If the first line is blank (whitespace-only), discard it.
 * 3. If the last line is blank, record its whitespace as common_prefix, discard it.
 * 4. If common_prefix appears at the start of every non-empty interior line, strip it.
 * 5. Rejoin with newline.
 *
 * Returns a newly allocated string (caller must free).
 * Returns NULL on allocation failure.
 */
char *strip_string_whitespace(const char *content, int len);

#endif /* PD_STRINGS_H */
