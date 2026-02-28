#ifndef PD_LEXER_H
#define PD_LEXER_H

#include "tokens.h"
#include "errors.h"

/*
 * Tokenize PicoDoc source text.
 *
 * On success, returns 0 and fills `out` with the token array.
 * The caller must call token_array_free(out) when done.
 *
 * On error, returns -1 and fills `err` with details.
 */
int pd_tokenize(const char *source, const char *filename,
                TokenArray *out, PdError *err);

#endif /* PD_LEXER_H */
