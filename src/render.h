#ifndef PICODOC_RENDER_H
#define PICODOC_RENDER_H

#include "ast.h"
#include "errors.h"

/*
 * Render an expanded AST to a complete HTML5 document string.
 *
 * doc must be a fully evaluated Document (output of pd_evaluate).
 * On success, *out is a heap-allocated HTML string (caller frees with free()).
 * On error, returns -1 and fills err.
 */
int pd_render(const PdNode *doc, const char *source, const char *filename,
              char **out, PdError *err);

#endif
