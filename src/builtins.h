#ifndef PD_BUILTINS_H
#define PD_BUILTINS_H

#include <stdbool.h>

/* Parameter declaration for a builtin macro. */
typedef struct {
    const char *name;
    bool required;
} ParamDecl;

/* Definition of a builtin macro. */
typedef struct {
    const char *name;
    const ParamDecl *params;
    int param_count;
    bool has_body;
    bool expansion_time;   /* true = cannot be shadowed by user macros */
} BuiltinDef;

/* Resolve heading/formatting aliases: "-" -> "h1", "**" -> "b", etc.
 * Returns the canonical name, or the input name if not an alias. */
const char *resolve_alias(const char *name);

/* Look up a builtin definition by canonical name.
 * Returns NULL if the name is not a builtin. */
const BuiltinDef *find_builtin(const char *name);

/* Returns true if the name is a wrapper/container tag
 * (div, section, span, nav, header, footer, main, article, aside). */
bool is_wrapper_tag(const char *name);

/* Returns true if the name is a block-level macro
 * (h1-h6, p, hr, ul, ol, table, div, section, nav, etc.). */
bool is_block_macro(const char *name);

#endif /* PD_BUILTINS_H */
