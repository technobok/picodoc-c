#ifndef PD_PARSER_H
#define PD_PARSER_H

#include "ast.h"
#include "errors.h"
#include "tokens.h"

/*
 * Parse a PicoDoc token stream into an AST.
 *
 * Returns 0 on success with *out set to the Document node.
 * Returns -1 on error with details in *err.
 * Caller must call pd_node_free(*out) when done.
 */
int pd_parse(const TokenArray *tokens, const char *source,
             const char *filename, PdNode **out, PdError *err);

#endif /* PD_PARSER_H */
