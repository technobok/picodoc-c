#include "ffi.h"
#include "errors.h"
#include "eval.h"
#include "lexer.h"
#include "parser.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>

/* Transfer formatted error string out of PdError before freeing.
 * pd_format_error stores the result in err->formatted, which pd_error_free
 * would free — so we steal the pointer first. */
static char *steal_error_msg(PdError *err) {
    pd_format_error(err);
    char *msg = err->formatted;
    err->formatted = NULL;  /* prevent pd_error_free from freeing it */
    pd_error_free(err);
    return msg;
}

char *pd_compile(const char *source, const char *filename,
                 const char *const *env_keys, const char *const *env_vals,
                 int env_count,
                 int *error_kind, char **error_msg) {
    *error_kind = PD_FFI_ERR_NONE;
    *error_msg = NULL;

    PdError err;
    pd_error_init(&err);

    /* Step 1: Tokenize */
    TokenArray tokens;
    if (pd_tokenize(source, filename, &tokens, &err) < 0) {
        *error_kind = PD_FFI_ERR_LEX;
        *error_msg = steal_error_msg(&err);
        return NULL;
    }

    /* Step 2: Parse */
    PdNode *doc = NULL;
    if (pd_parse(&tokens, source, filename, &doc, &err) < 0) {
        *error_kind = PD_FFI_ERR_PARSE;
        *error_msg = steal_error_msg(&err);
        token_array_free(&tokens);
        return NULL;
    }
    token_array_free(&tokens);

    /* Step 3: Evaluate */
    PdNode *expanded = NULL;
    if (pd_evaluate(doc, filename, source,
                    env_keys, env_vals, env_count,
                    NULL, /* no filters */
                    &expanded, &err) < 0) {
        *error_kind = PD_FFI_ERR_EVAL;
        *error_msg = steal_error_msg(&err);
        pd_node_free(doc);
        return NULL;
    }
    pd_node_free(doc);

    /* Step 4: Render */
    char *html = NULL;
    if (pd_render(expanded, source, filename, &html, &err) < 0) {
        *error_kind = PD_FFI_ERR_RENDER;
        *error_msg = steal_error_msg(&err);
        pd_node_free(expanded);
        return NULL;
    }
    pd_node_free(expanded);

    return html;
}

void pd_free_string(char *s) {
    free(s);
}
