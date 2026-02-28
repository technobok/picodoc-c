#ifndef PD_EVAL_H
#define PD_EVAL_H

#include "ast.h"
#include "errors.h"

/*
 * Evaluate a parsed document — expand macros, collect definitions,
 * wrap paragraphs, validate nesting.
 *
 * The input `doc` is consumed: the caller should NOT free it after a
 * successful call (ownership transfers to the evaluator).
 * On success, *out is the expanded Document.
 * On error, returns -1 with details in *err; caller must pd_node_free(doc).
 *
 * env is an optional array of "key=value" strings (NULL-terminated values).
 * env_count is the number of entries in env.
 */
int pd_evaluate(PdNode *doc, const char *filename, const char *source,
                const char *const *env_keys, const char *const *env_vals,
                int env_count,
                PdNode **out, PdError *err);

#endif /* PD_EVAL_H */
