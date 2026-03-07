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

/* Alternate form entry: maps a longer name to its canonical short form. */
typedef struct {
    const char *alternate;
    const char *canonical;
} AliasEntry;

/* Iterate builtins and aliases by index. */
int pd_builtin_count(void);
const BuiltinDef *pd_builtin_at(int index);
int pd_alias_count(void);
const AliasEntry *pd_alias_at(int index);

/* Resolve alternate forms to canonical names: "h1" -> "-", "b" -> "**", etc.
 * Returns the canonical name, or the input name if not an alternate. */
const char *resolve_alias(const char *name);

/* Look up a builtin definition by canonical name.
 * Returns NULL if the name is not a builtin. */
const BuiltinDef *find_builtin(const char *name);

/* Returns true if the name is a wrapper/container tag
 * (div, section, span, nav, header, footer, main, article, aside). */
bool is_wrapper_tag(const char *name);

/* Returns true if the name is a block-level macro
 * (- through ------, p, hr, ul, ol, table, div, section, nav, etc.). */
bool is_block_macro(const char *name);

#endif /* PD_BUILTINS_H */
