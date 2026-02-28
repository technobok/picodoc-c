#ifndef PD_ERRORS_H
#define PD_ERRORS_H

#include "tokens.h"

typedef enum {
    PD_ERR_NONE = 0,
    PD_ERR_LEX,
    PD_ERR_PARSE,
    PD_ERR_EVAL,
    PD_ERR_RENDER,
} PdErrorKind;

/*
 * Error descriptor — filled in by lexer/parser/eval on failure.
 * Functions return 0 on success, -1 on error with details in PdError.
 *
 * message is a pointer to a static string (not owned).
 * formatted is heap-allocated (caller must free).
 */
typedef struct {
    PdErrorKind kind;
    const char *message;       /* static, not owned */
    Position position;         /* for lex errors */
    Span span;                 /* for parse/eval errors */
    const char *source;        /* not owned — pointer to source buffer */
    const char *filename;      /* not owned */
    char *formatted;           /* heap-allocated formatted string, caller frees */
} PdError;

/* Initialize an error struct to no-error state. */
void pd_error_init(PdError *err);

/* Free any heap memory in the error struct. */
void pd_error_free(PdError *err);

/*
 * Format an error with source context (like rustc diagnostics).
 * Stores result in err->formatted (heap allocated).
 * Returns err->formatted for convenience.
 */
char *pd_format_error(PdError *err);

/*
 * Set a lex error. Returns -1 for convenience in return statements.
 */
int pd_lex_error(PdError *err, const char *message, Position pos,
                 const char *source, const char *filename);

/*
 * Set a parse error. Returns -1 for convenience.
 */
int pd_parse_error(PdError *err, const char *message, Span span,
                   const char *source, const char *filename);

#endif /* PD_ERRORS_H */
