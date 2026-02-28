#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "eval.h"
#include "builtins.h"
#include "lexer.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_ds for hash map and dynamic arrays */
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

/* --- Limits --- */

#define MAX_INCLUDE_DEPTH 16
#define MAX_CALL_DEPTH    64

/* --- Eval context --- */

typedef struct {
    char *key;       /* stb_ds string key */
    PdNode *value;   /* NOT owned — points into original AST */
} DefEntry;

typedef struct {
    const char *filename;
    const char *source;
    char *source_dir;       /* heap-allocated directory of filename */
    DefEntry *definitions;  /* stb_ds string hash map (sh*) */
    char **include_stack;   /* stb_ds dynamic array of heap strings */
    char **call_stack;      /* stb_ds dynamic array of heap strings */
    char **env_keys;        /* stb_ds dynamic array */
    char **env_vals;        /* stb_ds dynamic array */
    int env_count;
    PdError *err;
} EvalContext;

/* --- Forward declarations --- */

static int expand_macro(PdNode *node, EvalContext *ctx, NodeArray *out);
static int recurse_body(PdNode *body, EvalContext *ctx, PdNode **out);
static int expand_body_children(PdNode **children, int count,
                                EvalContext *ctx, NodeArray *out);

/* --- Helper: get source directory from filename --- */

static char *get_source_dir(const char *filename) {
    if (!filename) return strdup(".");
    const char *last_sep = NULL;
    for (const char *p = filename; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (!last_sep) return strdup(".");
    int len = (int)(last_sep - filename);
    if (len == 0) return strdup("/");
    char *dir = malloc((size_t)len + 1);
    if (!dir) return NULL;
    memcpy(dir, filename, (size_t)len);
    dir[len] = '\0';
    return dir;
}

/* --- Helper: context env lookup --- */

static const char *env_get(EvalContext *ctx, const char *key) {
    for (int i = 0; i < ctx->env_count; i++) {
        if (strcmp(ctx->env_keys[i], key) == 0)
            return ctx->env_vals[i];
    }
    return NULL;
}

static void env_set(EvalContext *ctx, const char *key, const char *value) {
    /* Update existing */
    for (int i = 0; i < ctx->env_count; i++) {
        if (strcmp(ctx->env_keys[i], key) == 0) {
            free(ctx->env_vals[i]);
            ctx->env_vals[i] = strdup(value);
            return;
        }
    }
    /* Add new */
    arrput(ctx->env_keys, strdup(key));
    arrput(ctx->env_vals, strdup(value));
    ctx->env_count++;
}

/* --- Helper: set eval error with formatted message --- */

static int eval_err(EvalContext *ctx, const char *message, Span span) {
    return pd_eval_error(ctx->err, message, span, ctx->source, ctx->filename);
}

static int eval_errf(EvalContext *ctx, Span span, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    free(ctx->err->detail);
    ctx->err->detail = strdup(buf);
    return eval_err(ctx, ctx->err->detail, span);
}

/* --- Helper: get named argument value from a MacroCall --- */

static PdNode *get_arg(PdNode *node, const char *name) {
    for (int i = 0; i < node->as.macro_call.arg_count; i++) {
        PdNode *arg = node->as.macro_call.args[i];
        if (arg->type == NODE_NAMED_ARG &&
            strcmp(arg->as.named_arg.name, name) == 0)
            return arg->as.named_arg.value;
    }
    return NULL;
}

/* --- Helper: extract plain text from a #set body --- */

static void extract_def_text(PdNode *macro, char *buf, int bufsize) {
    buf[0] = '\0';
    PdNode *body = macro->as.macro_call.body;
    if (!body) return;

    int pos = 0;
    if (body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count && pos < bufsize - 1; i++) {
            PdNode *c = body->as.body.children[i];
            if (c->type == NODE_TEXT || c->type == NODE_ESCAPE) {
                int len = c->as.text.value_len;
                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                memcpy(buf + pos, c->as.text.value, (size_t)len);
                pos += len;
            }
        }
    } else if (body->type == NODE_INTERP_STRING) {
        for (int i = 0; i < body->as.interp_string.count && pos < bufsize - 1; i++) {
            PdNode *p = body->as.interp_string.parts[i];
            if (p->type == NODE_TEXT) {
                int len = p->as.text.value_len;
                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                memcpy(buf + pos, p->as.text.value, (size_t)len);
                pos += len;
            }
        }
    } else if (body->type == NODE_RAW_STRING) {
        int len = body->as.text.value_len;
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, body->as.text.value, (size_t)len);
        pos = len;
    }
    buf[pos] = '\0';
}

/* --- Helper: resolve an argument value to plain text --- */

static void resolve_value(PdNode *value, EvalContext *ctx,
                          char *buf, int bufsize) {
    buf[0] = '\0';
    if (!value) return;

    int pos = 0;

    if (value->type == NODE_TEXT) {
        int len = value->as.text.value_len;
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, value->as.text.value, (size_t)len);
        buf[len] = '\0';
        return;
    }
    if (value->type == NODE_RAW_STRING) {
        int len = value->as.text.value_len;
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, value->as.text.value, (size_t)len);
        buf[len] = '\0';
        return;
    }
    if (value->type == NODE_INTERP_STRING) {
        for (int i = 0; i < value->as.interp_string.count && pos < bufsize - 1; i++) {
            PdNode *part = value->as.interp_string.parts[i];
            if (part->type == NODE_TEXT) {
                int len = part->as.text.value_len;
                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                memcpy(buf + pos, part->as.text.value, (size_t)len);
                pos += len;
            } else if (part->type == NODE_CODE_SECTION) {
                for (int j = 0; j < part->as.code_section.count && pos < bufsize - 1; j++) {
                    PdNode *child = part->as.code_section.children[j];
                    if (child->type == NODE_TEXT) {
                        int len = child->as.text.value_len;
                        if (pos + len >= bufsize) len = bufsize - 1 - pos;
                        memcpy(buf + pos, child->as.text.value, (size_t)len);
                        pos += len;
                    } else if (child->type == NODE_MACRO_CALL) {
                        const char *ref = resolve_alias(child->as.macro_call.name);
                        if (strncmp(ref, "env.", 4) == 0) {
                            const char *ev = env_get(ctx, ref + 4);
                            if (ev) {
                                int len = (int)strlen(ev);
                                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                                memcpy(buf + pos, ev, (size_t)len);
                                pos += len;
                            }
                        } else {
                            ptrdiff_t idx = shgeti(ctx->definitions, ref);
                            if (idx >= 0) {
                                char tmp[256];
                                extract_def_text(ctx->definitions[idx].value, tmp, sizeof(tmp));
                                int len = (int)strlen(tmp);
                                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                                memcpy(buf + pos, tmp, (size_t)len);
                                pos += len;
                            }
                        }
                    }
                }
            }
        }
        buf[pos] = '\0';
        return;
    }
    if (value->type == NODE_MACRO_CALL) {
        const char *ref = resolve_alias(value->as.macro_call.name);
        if (strncmp(ref, "env.", 4) == 0) {
            const char *ev = env_get(ctx, ref + 4);
            if (ev) {
                int len = (int)strlen(ev);
                if (len >= bufsize) len = bufsize - 1;
                memcpy(buf, ev, (size_t)len);
                buf[len] = '\0';
            }
            return;
        }
        ptrdiff_t idx = shgeti(ctx->definitions, ref);
        if (idx >= 0) {
            extract_def_text(ctx->definitions[idx].value, buf, bufsize);
        }
        return;
    }
}

/* --- Helper: extract plain text from a body node --- */

static void extract_body_text(PdNode *body, char *buf, int bufsize) {
    buf[0] = '\0';
    if (!body) return;

    int pos = 0;
    if (body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count && pos < bufsize - 1; i++) {
            PdNode *c = body->as.body.children[i];
            if (c->type == NODE_TEXT || c->type == NODE_ESCAPE) {
                int len = c->as.text.value_len;
                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                memcpy(buf + pos, c->as.text.value, (size_t)len);
                pos += len;
            }
        }
    } else if (body->type == NODE_INTERP_STRING) {
        for (int i = 0; i < body->as.interp_string.count && pos < bufsize - 1; i++) {
            PdNode *p = body->as.interp_string.parts[i];
            if (p->type == NODE_TEXT) {
                int len = p->as.text.value_len;
                if (pos + len >= bufsize) len = bufsize - 1 - pos;
                memcpy(buf + pos, p->as.text.value, (size_t)len);
                pos += len;
            }
        }
    } else if (body->type == NODE_RAW_STRING) {
        int len = body->as.text.value_len;
        if (len >= bufsize) len = bufsize - 1;
        memcpy(buf, body->as.text.value, (size_t)len);
        pos = len;
    }
    buf[pos] = '\0';
}

/* --- Helper: convert a value node to body children --- */

static void value_to_body_children(PdNode *value, Span span, NodeArray *out) {
    if (!value) return;

    if (value->type == NODE_TEXT || value->type == NODE_ESCAPE) {
        node_array_push(out, pd_node_clone(value));
        return;
    }
    if (value->type == NODE_MACRO_CALL) {
        node_array_push(out, pd_node_clone(value));
        return;
    }
    if (value->type == NODE_INTERP_STRING) {
        /* Flatten InterpString to children */
        for (int i = 0; i < value->as.interp_string.count; i++) {
            PdNode *part = value->as.interp_string.parts[i];
            if (part->type == NODE_TEXT) {
                node_array_push(out, pd_node_clone(part));
            } else if (part->type == NODE_CODE_SECTION) {
                for (int j = 0; j < part->as.code_section.count; j++) {
                    node_array_push(out, pd_node_clone(part->as.code_section.children[j]));
                }
            }
        }
        return;
    }
    if (value->type == NODE_RAW_STRING) {
        node_array_push(out, pd_node_text(value->as.text.value,
                                          value->as.text.value_len, span));
        return;
    }
    (void)span;
}

/* --- Helper: convert a body node to children list --- */

static void body_to_children(PdNode *body, Span span, NodeArray *out) {
    if (!body) return;

    if (body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            node_array_push(out, pd_node_clone(body->as.body.children[i]));
        }
        return;
    }
    if (body->type == NODE_INTERP_STRING) {
        for (int i = 0; i < body->as.interp_string.count; i++) {
            PdNode *part = body->as.interp_string.parts[i];
            if (part->type == NODE_TEXT) {
                node_array_push(out, pd_node_clone(part));
            } else if (part->type == NODE_CODE_SECTION) {
                for (int j = 0; j < part->as.code_section.count; j++) {
                    node_array_push(out, pd_node_clone(part->as.code_section.children[j]));
                }
            }
        }
        return;
    }
    if (body->type == NODE_RAW_STRING) {
        node_array_push(out, pd_node_text(body->as.text.value,
                                          body->as.text.value_len, span));
        return;
    }
}

/* --- Helper: flatten InterpString to body children --- */

static void interp_to_children(PdNode *interp, NodeArray *out) {
    if (!interp || interp->type != NODE_INTERP_STRING) return;

    for (int i = 0; i < interp->as.interp_string.count; i++) {
        PdNode *part = interp->as.interp_string.parts[i];
        if (part->type == NODE_TEXT) {
            node_array_push(out, pd_node_clone(part));
        } else if (part->type == NODE_CODE_SECTION) {
            for (int j = 0; j < part->as.code_section.count; j++) {
                node_array_push(out, pd_node_clone(part->as.code_section.children[j]));
            }
        }
    }
}

/* --- Validation: builtin args --- */

static int validate_builtin_args(PdNode *node, const char *name,
                                 EvalContext *ctx) {
    if (strcmp(name, "set") == 0) return 0;

    const BuiltinDef *builtin = find_builtin(name);
    if (!builtin || node->as.macro_call.arg_count == 0) return 0;

    for (int i = 0; i < node->as.macro_call.arg_count; i++) {
        PdNode *arg = node->as.macro_call.args[i];
        if (arg->type != NODE_NAMED_ARG) continue;

        bool known = false;
        for (int j = 0; j < builtin->param_count; j++) {
            if (strcmp(arg->as.named_arg.name, builtin->params[j].name) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            return eval_errf(ctx, arg->as.named_arg.name_span,
                             "unknown argument '%s' for #%s",
                             arg->as.named_arg.name, name);
        }
    }
    return 0;
}

/* --- Expansion-time builtins --- */

static int expand_set(PdNode *node, EvalContext *ctx, NodeArray *out) {
    (void)out;
    PdNode *name_val = get_arg(node, "name");
    if (!name_val) return 0;

    char name[256];
    resolve_value(name_val, ctx, name, sizeof(name));

    shput(ctx->definitions, name, node);

    if (strncmp(name, "env.", 4) == 0) {
        char text[256];
        extract_def_text(node, text, sizeof(text));
        env_set(ctx, name + 4, text);
    }
    return 0;
}

static int get_condition_body(PdNode *node, EvalContext *ctx, NodeArray *out) {
    PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        return expand_body_children(body->as.body.children,
                                    body->as.body.count, ctx, out);
    }
    return 0;
}

static int expand_ifeq(PdNode *node, EvalContext *ctx, NodeArray *out) {
    PdNode *lhs_val = get_arg(node, "lhs");
    PdNode *rhs_val = get_arg(node, "rhs");
    if (!lhs_val || !rhs_val) return 0;

    char lhs[256], rhs[256];
    resolve_value(lhs_val, ctx, lhs, sizeof(lhs));
    resolve_value(rhs_val, ctx, rhs, sizeof(rhs));

    if (strcmp(lhs, rhs) == 0)
        return get_condition_body(node, ctx, out);
    return 0;
}

static int expand_ifne(PdNode *node, EvalContext *ctx, NodeArray *out) {
    PdNode *lhs_val = get_arg(node, "lhs");
    PdNode *rhs_val = get_arg(node, "rhs");
    if (!lhs_val || !rhs_val) return 0;

    char lhs[256], rhs[256];
    resolve_value(lhs_val, ctx, lhs, sizeof(lhs));
    resolve_value(rhs_val, ctx, rhs, sizeof(rhs));

    if (strcmp(lhs, rhs) != 0)
        return get_condition_body(node, ctx, out);
    return 0;
}

static int expand_ifset(PdNode *node, EvalContext *ctx, NodeArray *out) {
    PdNode *name_val = get_arg(node, "name");
    if (!name_val) return 0;

    char name[256];
    resolve_value(name_val, ctx, name, sizeof(name));

    if (strncmp(name, "env.", 4) == 0) {
        if (env_get(ctx, name + 4) != NULL)
            return get_condition_body(node, ctx, out);
        return 0;
    }
    if (shgeti(ctx->definitions, name) >= 0)
        return get_condition_body(node, ctx, out);
    return 0;
}

/* --- Include --- */

static int expand_include(PdNode *node, EvalContext *ctx, NodeArray *out) {
    PdNode *body = node->as.macro_call.body;
    if (!body) return 0;

    char filename[512];
    extract_body_text(body, filename, sizeof(filename));

    /* Trim whitespace */
    char *start = filename;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
    *end = '\0';

    if (*start == '\0') return 0;

    /* Normalize backslashes */
    for (char *p = start; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* Build full path */
    char filepath[1024];
    if (start[0] == '/') {
        snprintf(filepath, sizeof(filepath), "%s", start);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", ctx->source_dir, start);
    }

    /* Check include depth */
    if (arrlen(ctx->include_stack) >= MAX_INCLUDE_DEPTH) {
        return eval_errf(ctx, node->span,
                         "include depth limit (%d) exceeded",
                         MAX_INCLUDE_DEPTH);
    }

    /* Check circular include */
    /* Use realpath if available, otherwise use the constructed path */
    char *resolved = realpath(filepath, NULL);
    if (!resolved) {
        return eval_errf(ctx, node->span,
                         "included file not found: %s", start);
    }

    for (int i = 0; i < arrlen(ctx->include_stack); i++) {
        if (strcmp(ctx->include_stack[i], resolved) == 0) {
            free(resolved);
            return eval_errf(ctx, node->span,
                             "circular include detected: %s", start);
        }
    }

    /* Check literal mode */
    PdNode *literal_val = get_arg(node, "literal");
    bool literal = false;
    if (literal_val) {
        char lit_text[64];
        resolve_value(literal_val, ctx, lit_text, sizeof(lit_text));
        /* Case-insensitive check for "true" */
        literal = (strcmp(lit_text, "true") == 0 || strcmp(lit_text, "True") == 0 ||
                   strcmp(lit_text, "TRUE") == 0);
    }

    /* Read file */
    FILE *f = fopen(filepath, "r");
    if (!f) {
        free(resolved);
        return eval_errf(ctx, node->span,
                         "included file not found: %s", start);
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        free(resolved);
        return eval_err(ctx, "out of memory reading included file", node->span);
    }
    size_t read_len = fread(content, 1, (size_t)fsize, f);
    content[read_len] = '\0';
    fclose(f);

    if (literal) {
        /* Wrap in #literal MacroCall */
        PdNode *text_node = pd_node_text(content, (int)read_len, node->span);
        PdNode **body_kids = malloc(sizeof(PdNode *));
        body_kids[0] = text_node;
        PdNode *lit_body = pd_node_body(body_kids, 1, node->span);
        PdNode *lit_call = pd_node_macro_call("literal", 7, NULL, 0,
                                              lit_body, false, node->span);
        node_array_push(out, lit_call);
        free(content);
        free(resolved);
        return 0;
    }

    /* Parse included content */
    TokenArray tokens;
    PdError parse_err;
    pd_error_init(&parse_err);

    int rc = pd_tokenize(content, filepath, &tokens, &parse_err);
    if (rc < 0) {
        free(content);
        free(resolved);
        ctx->err->kind = parse_err.kind;
        ctx->err->message = parse_err.message;
        ctx->err->span = node->span;
        ctx->err->source = ctx->source;
        ctx->err->filename = ctx->filename;
        return -1;
    }

    PdNode *inc_doc = NULL;
    rc = pd_parse(&tokens, content, filepath, &inc_doc, &parse_err);
    token_array_free(&tokens);
    if (rc < 0) {
        free(content);
        free(resolved);
        ctx->err->kind = parse_err.kind;
        ctx->err->message = parse_err.message;
        ctx->err->span = node->span;
        ctx->err->source = ctx->source;
        ctx->err->filename = ctx->filename;
        return -1;
    }

    /* Push to include stack */
    arrput(ctx->include_stack, resolved);

    /* Save and update context for included file */
    const char *saved_filename = ctx->filename;
    const char *saved_source = ctx->source;
    char *saved_source_dir = ctx->source_dir;
    ctx->filename = filepath;
    ctx->source = content;
    ctx->source_dir = get_source_dir(filepath);

    /* Collect definitions from included doc */
    for (int i = 0; i < inc_doc->as.document.count; i++) {
        PdNode *child = inc_doc->as.document.children[i];
        if (child->type != NODE_MACRO_CALL) continue;
        const char *cname = resolve_alias(child->as.macro_call.name);
        if (strcmp(cname, "set") != 0) continue;
        PdNode *name_val = get_arg(child, "name");
        if (!name_val) continue;
        char def_name[256];
        resolve_value(name_val, ctx, def_name, sizeof(def_name));
        if (shgeti(ctx->definitions, def_name) >= 0) continue;
        shput(ctx->definitions, def_name, child);
        if (strncmp(def_name, "env.", 4) == 0) {
            char text[256];
            extract_def_text(child, text, sizeof(text));
            env_set(ctx, def_name + 4, text);
        }
    }

    /* Expand top-level nodes */
    rc = 0;
    for (int i = 0; i < inc_doc->as.document.count && rc == 0; i++) {
        PdNode *child = inc_doc->as.document.children[i];
        if (child->type == NODE_PARAGRAPH) {
            /* Wrap paragraph in #p */
            NodeArray para_expanded;
            node_array_init(&para_expanded);
            rc = expand_body_children(child->as.paragraph.children,
                                      child->as.paragraph.count,
                                      ctx, &para_expanded);
            if (rc == 0) {
                PdNode *p_body = pd_node_body(para_expanded.items,
                                              para_expanded.count,
                                              child->span);
                PdNode *p_call = pd_node_macro_call("p", 1, NULL, 0,
                                                    p_body, false, child->span);
                node_array_push(out, p_call);
            } else {
                node_array_free_deep(&para_expanded);
            }
        } else if (child->type == NODE_MACRO_CALL) {
            rc = expand_macro(child, ctx, out);
        }
    }

    /* Restore context */
    free(ctx->source_dir);
    ctx->source_dir = saved_source_dir;
    ctx->source = saved_source;
    ctx->filename = saved_filename;

    /* Pop include stack */
    int stack_len = arrlen(ctx->include_stack);
    free(ctx->include_stack[stack_len - 1]);
    arrpop(ctx->include_stack);

    /* Don't free inc_doc because definitions may reference its nodes.
     * We keep it alive — it will leak, but that's acceptable for now.
     * A proper solution would be an arena allocator. */
    /* Actually, let's track content too. For now we accept the leak. */
    (void)inc_doc;

    return rc;
}

/* --- Table expansion --- */

typedef struct {
    NodeArray *cells;  /* array of NodeArrays */
    int cell_count;
} TableRow;

static bool has_pipes(PdNode *body) {
    if (!body || body->type != NODE_BODY) return false;
    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_TEXT && memchr(c->as.text.value, '|', (size_t)c->as.text.value_len))
            return true;
    }
    return false;
}

/* Split text at newlines and pipes, distributing into rows. */
static void split_text_into_rows(PdNode *text_node,
                                 TableRow **rows, int *row_count) {
    const char *remaining = text_node->as.text.value;
    int rem_len = text_node->as.text.value_len;

    while (rem_len > 0) {
        /* Find first newline or pipe */
        int nl_pos = -1, pipe_pos = -1;
        for (int i = 0; i < rem_len; i++) {
            if (remaining[i] == '\n' && nl_pos < 0) nl_pos = i;
            if (remaining[i] == '|' && pipe_pos < 0) pipe_pos = i;
            if (nl_pos >= 0 && pipe_pos >= 0) break;
        }

        if (nl_pos < 0 && pipe_pos < 0) {
            /* No more delimiters — append remaining text */
            if (rem_len > 0) {
                PdNode *t = pd_node_text(remaining, rem_len, text_node->span);
                TableRow *cur = &(*rows)[*row_count - 1];
                node_array_push(&cur->cells[cur->cell_count - 1], t);
            }
            break;
        } else if (nl_pos >= 0 && (pipe_pos < 0 || nl_pos < pipe_pos)) {
            /* Newline first — finish current row, start new row */
            if (nl_pos > 0) {
                PdNode *t = pd_node_text(remaining, nl_pos, text_node->span);
                TableRow *cur = &(*rows)[*row_count - 1];
                node_array_push(&cur->cells[cur->cell_count - 1], t);
            }
            /* Start new row */
            (*row_count)++;
            *rows = realloc(*rows, (size_t)(*row_count) * sizeof(TableRow));
            TableRow *new_row = &(*rows)[*row_count - 1];
            new_row->cells = malloc(sizeof(NodeArray));
            new_row->cell_count = 1;
            node_array_init(&new_row->cells[0]);

            remaining += nl_pos + 1;
            rem_len -= nl_pos + 1;
        } else {
            /* Pipe first — finish current cell, start new cell */
            if (pipe_pos > 0) {
                PdNode *t = pd_node_text(remaining, pipe_pos, text_node->span);
                TableRow *cur = &(*rows)[*row_count - 1];
                node_array_push(&cur->cells[cur->cell_count - 1], t);
            }
            /* Start new cell in current row */
            TableRow *cur = &(*rows)[*row_count - 1];
            cur->cell_count++;
            cur->cells = realloc(cur->cells, (size_t)cur->cell_count * sizeof(NodeArray));
            node_array_init(&cur->cells[cur->cell_count - 1]);

            remaining += pipe_pos + 1;
            rem_len -= pipe_pos + 1;
        }
    }
}

/* Trim leading/trailing whitespace from Text nodes at cell boundaries. */
static void trim_cell(NodeArray *cell) {
    /* Trim leading */
    while (cell->count > 0 && cell->items[0]->type == NODE_TEXT) {
        char *val = cell->items[0]->as.text.value;
        int len = cell->items[0]->as.text.value_len;
        int start = 0;
        while (start < len && (val[start] == ' ' || val[start] == '\t')) start++;
        if (start == len) {
            pd_node_free(cell->items[0]);
            memmove(cell->items, cell->items + 1,
                    (size_t)(cell->count - 1) * sizeof(PdNode *));
            cell->count--;
        } else if (start > 0) {
            char *trimmed = malloc((size_t)(len - start) + 1);
            memcpy(trimmed, val + start, (size_t)(len - start));
            trimmed[len - start] = '\0';
            free(cell->items[0]->as.text.value);
            cell->items[0]->as.text.value = trimmed;
            cell->items[0]->as.text.value_len = len - start;
            break;
        } else {
            break;
        }
    }
    /* Trim trailing */
    while (cell->count > 0 && cell->items[cell->count - 1]->type == NODE_TEXT) {
        PdNode *last = cell->items[cell->count - 1];
        char *val = last->as.text.value;
        int len = last->as.text.value_len;
        int end = len;
        while (end > 0 && (val[end - 1] == ' ' || val[end - 1] == '\t')) end--;
        if (end == 0) {
            pd_node_free(last);
            cell->count--;
        } else if (end < len) {
            val[end] = '\0';
            last->as.text.value_len = end;
            break;
        } else {
            break;
        }
    }
}

static bool row_is_empty(TableRow *row) {
    for (int i = 0; i < row->cell_count; i++) {
        if (row->cells[i].count > 0) return false;
    }
    return true;
}

static int expand_table(PdNode *node, EvalContext *ctx, NodeArray *out) {
    PdNode *body = node->as.macro_call.body;
    if (!body || body->type != NODE_BODY) {
        /* No pipe body — pass through with recursed body */
        PdNode *new_body = NULL;
        int rc = recurse_body(body, ctx, &new_body);
        if (rc < 0) return rc;
        PdNode **new_args = NULL;
        int new_arg_count = 0;
        if (node->as.macro_call.arg_count > 0) {
            new_args = malloc((size_t)node->as.macro_call.arg_count * sizeof(PdNode *));
            for (int i = 0; i < node->as.macro_call.arg_count; i++)
                new_args[i] = pd_node_clone(node->as.macro_call.args[i]);
            new_arg_count = node->as.macro_call.arg_count;
        }
        node_array_push(out, pd_node_macro_call(
            node->as.macro_call.name, node->as.macro_call.name_len,
            new_args, new_arg_count, new_body,
            node->as.macro_call.bracketed, node->span));
        return 0;
    }

    if (!has_pipes(body)) {
        /* No pipes — recurse into body and pass through */
        PdNode *new_body = NULL;
        int rc = recurse_body(body, ctx, &new_body);
        if (rc < 0) return rc;
        PdNode **new_args = NULL;
        int new_arg_count = 0;
        if (node->as.macro_call.arg_count > 0) {
            new_args = malloc((size_t)node->as.macro_call.arg_count * sizeof(PdNode *));
            for (int i = 0; i < node->as.macro_call.arg_count; i++)
                new_args[i] = pd_node_clone(node->as.macro_call.args[i]);
            new_arg_count = node->as.macro_call.arg_count;
        }
        node_array_push(out, pd_node_macro_call(
            node->as.macro_call.name, node->as.macro_call.name_len,
            new_args, new_arg_count, new_body,
            node->as.macro_call.bracketed, node->span));
        return 0;
    }

    /* Parse pipe-delimited rows */
    int row_count = 1;
    TableRow *rows = malloc(sizeof(TableRow));
    rows[0].cells = malloc(sizeof(NodeArray));
    rows[0].cell_count = 1;
    node_array_init(&rows[0].cells[0]);

    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *child = body->as.body.children[i];
        if (child->type == NODE_TEXT) {
            split_text_into_rows(child, &rows, &row_count);
        } else if (child->type == NODE_MACRO_CALL) {
            /* Expand macro and add to current cell */
            NodeArray expanded;
            node_array_init(&expanded);
            int rc = expand_macro(child, ctx, &expanded);
            if (rc < 0) {
                /* Cleanup */
                node_array_free_deep(&expanded);
                for (int r = 0; r < row_count; r++) {
                    for (int c = 0; c < rows[r].cell_count; c++)
                        node_array_free_deep(&rows[r].cells[c]);
                    free(rows[r].cells);
                }
                free(rows);
                return rc;
            }
            TableRow *cur = &rows[row_count - 1];
            for (int j = 0; j < expanded.count; j++) {
                node_array_push(&cur->cells[cur->cell_count - 1], expanded.items[j]);
            }
            node_array_free_shallow(&expanded);
        } else {
            /* Escape etc. — add to current cell */
            TableRow *cur = &rows[row_count - 1];
            node_array_push(&cur->cells[cur->cell_count - 1], pd_node_clone(child));
        }
    }

    /* Filter empty rows, trim cells */
    for (int r = 0; r < row_count; r++) {
        for (int c = 0; c < rows[r].cell_count; c++) {
            trim_cell(&rows[r].cells[c]);
        }
    }

    /* Build #tr / #th / #td tree */
    NodeArray tr_nodes;
    node_array_init(&tr_nodes);

    int actual_row = 0;
    for (int r = 0; r < row_count; r++) {
        if (row_is_empty(&rows[r])) continue;

        const char *cell_tag = (actual_row == 0) ? "th" : "td";
        int cell_tag_len = (actual_row == 0) ? 2 : 2;

        NodeArray cell_nodes;
        node_array_init(&cell_nodes);

        for (int c = 0; c < rows[r].cell_count; c++) {
            NodeArray *cell = &rows[r].cells[c];
            PdNode *cell_body = pd_node_body(cell->items, cell->count, node->span);
            /* Transfer ownership — don't free cell items */
            cell->items = NULL;
            cell->count = 0;
            cell->capacity = 0;

            PdNode *cell_call = pd_node_macro_call(cell_tag, cell_tag_len,
                                                   NULL, 0, cell_body, true, node->span);
            node_array_push(&cell_nodes, cell_call);
        }

        PdNode *tr_body = pd_node_body(cell_nodes.items, cell_nodes.count,
                                       node->span);
        PdNode *tr_call = pd_node_macro_call("tr", 2, NULL, 0,
                                             tr_body, true, node->span);
        node_array_push(&tr_nodes, tr_call);
        actual_row++;
    }

    /* Build table */
    PdNode *table_body = pd_node_body(tr_nodes.items, tr_nodes.count,
                                      node->span);
    PdNode *table_call = pd_node_macro_call("table", 5, NULL, 0,
                                            table_body,
                                            node->as.macro_call.bracketed,
                                            node->span);
    node_array_push(out, table_call);

    /* Cleanup rows (cells already transferred) */
    for (int r = 0; r < row_count; r++) {
        for (int c = 0; c < rows[r].cell_count; c++) {
            node_array_free_deep(&rows[r].cells[c]);
        }
        free(rows[r].cells);
    }
    free(rows);

    return 0;
}

/* --- User macro expansion --- */

static int expand_user_macro(PdNode *node, const char *name,
                             EvalContext *ctx, NodeArray *out) {
    ptrdiff_t def_idx = shgeti(ctx->definitions, name);
    if (def_idx < 0) return 0;
    PdNode *defn = ctx->definitions[def_idx].value;

    /* Extract param declarations (skip name=) */
    typedef struct {
        char name[64];
        bool required;
        PdNode *default_val; /* not owned */
    } ParamInfo;

    ParamInfo params[32];
    int param_count = 0;

    for (int i = 0; i < defn->as.macro_call.arg_count; i++) {
        PdNode *arg = defn->as.macro_call.args[i];
        if (arg->type != NODE_NAMED_ARG) continue;
        if (strcmp(arg->as.named_arg.name, "name") == 0) continue;
        if (param_count >= 32) break;

        ParamInfo *p = &params[param_count++];
        strncpy(p->name, arg->as.named_arg.name, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';

        if (arg->as.named_arg.value &&
            arg->as.named_arg.value->type == NODE_REQUIRED_MARKER) {
            p->required = true;
            p->default_val = NULL;
        } else {
            p->required = false;
            p->default_val = arg->as.named_arg.value;
        }
    }

    /* Bind call-site args to params */
    NodeArray bindings[32];
    bool bound[32];
    for (int i = 0; i < param_count; i++) {
        node_array_init(&bindings[i]);
        bound[i] = false;
    }

    for (int i = 0; i < node->as.macro_call.arg_count; i++) {
        PdNode *arg = node->as.macro_call.args[i];
        if (arg->type != NODE_NAMED_ARG) continue;

        for (int j = 0; j < param_count; j++) {
            if (strcmp(arg->as.named_arg.name, params[j].name) == 0) {
                value_to_body_children(arg->as.named_arg.value,
                                       node->span, &bindings[j]);
                bound[j] = true;
                break;
            }
        }
    }

    /* Body binding */
    for (int j = 0; j < param_count; j++) {
        if (strcmp(params[j].name, "body") == 0 && !bound[j] &&
            node->as.macro_call.body != NULL) {
            body_to_children(node->as.macro_call.body, node->span,
                             &bindings[j]);
            bound[j] = true;
        }
    }

    /* Defaults for unbound params */
    for (int j = 0; j < param_count; j++) {
        if (!bound[j]) {
            if (params[j].required) {
                /* Cleanup */
                for (int k = 0; k < param_count; k++)
                    node_array_free_deep(&bindings[k]);
                return eval_errf(ctx, node->span,
                                 "missing required argument: %s",
                                 params[j].name);
            }
            if (params[j].default_val) {
                value_to_body_children(params[j].default_val, node->span,
                                       &bindings[j]);
            }
            bound[j] = true;
        }
    }

    /* Recursion check */
    if (arrlen(ctx->call_stack) >= MAX_CALL_DEPTH) {
        for (int j = 0; j < param_count; j++)
            node_array_free_deep(&bindings[j]);
        return eval_errf(ctx, node->span,
                         "macro call depth limit (%d) exceeded",
                         MAX_CALL_DEPTH);
    }

    arrput(ctx->call_stack, strdup(name));

    /* Resolve bindings (expand macro refs before shadowing) */
    NodeArray resolved[32];
    int rc = 0;
    for (int j = 0; j < param_count && rc == 0; j++) {
        node_array_init(&resolved[j]);
        rc = expand_body_children(bindings[j].items, bindings[j].count,
                                  ctx, &resolved[j]);
    }

    if (rc < 0) {
        for (int j = 0; j < param_count; j++) {
            node_array_free_deep(&bindings[j]);
            node_array_free_deep(&resolved[j]);
        }
        int sl = arrlen(ctx->call_stack);
        free(ctx->call_stack[sl - 1]);
        arrpop(ctx->call_stack);
        return rc;
    }

    /* Free raw bindings — resolved replaces them */
    for (int j = 0; j < param_count; j++)
        node_array_free_deep(&bindings[j]);

    /* Scope shadowing: temporarily inject param bindings as definitions */
    PdNode *saved_defs[32];
    bool had_def[32];
    PdNode *synthetic_nodes[32];

    for (int j = 0; j < param_count; j++) {
        saved_defs[j] = NULL;
        had_def[j] = false;
        synthetic_nodes[j] = NULL;

        /* Check for env var shadowing */
        if (strncmp(params[j].name, "env.", 4) == 0) {
            for (int k = 0; k < param_count; k++)
                node_array_free_deep(&resolved[k]);
            int sl = arrlen(ctx->call_stack);
            free(ctx->call_stack[sl - 1]);
            arrpop(ctx->call_stack);
            return eval_errf(ctx, node->span,
                             "cannot shadow environment variable: %s",
                             params[j].name);
        }

        ptrdiff_t existing = shgeti(ctx->definitions, params[j].name);
        if (existing >= 0) {
            saved_defs[j] = ctx->definitions[existing].value;
            had_def[j] = true;
        }

        /* Create synthetic #set node for the binding */
        PdNode *syn_body = pd_node_body(resolved[j].items, resolved[j].count,
                                        node->span);
        /* Transfer ownership of resolved items */
        resolved[j].items = NULL;
        resolved[j].count = 0;
        resolved[j].capacity = 0;

        PdNode *name_text = pd_node_text(params[j].name,
                                         (int)strlen(params[j].name),
                                         node->span);
        PdNode *name_arg = pd_node_named_arg("name", 4, node->span,
                                             name_text, node->span);
        PdNode **args = malloc(sizeof(PdNode *));
        args[0] = name_arg;

        PdNode *synthetic = pd_node_macro_call("set", 3, args, 1,
                                               syn_body, true, node->span);
        synthetic_nodes[j] = synthetic;
        shput(ctx->definitions, params[j].name, synthetic);
    }

    /* Expand definition body */
    PdNode *def_body = defn->as.macro_call.body;
    rc = 0;
    if (def_body && def_body->type == NODE_BODY) {
        rc = expand_body_children(def_body->as.body.children,
                                  def_body->as.body.count, ctx, out);
    } else if (def_body && def_body->type == NODE_INTERP_STRING) {
        PdNode *expanded_interp = NULL;
        rc = recurse_body(def_body, ctx, &expanded_interp);
        if (rc == 0 && expanded_interp) {
            interp_to_children(expanded_interp, out);
            pd_node_free(expanded_interp);
        }
    } else if (def_body && def_body->type == NODE_RAW_STRING) {
        node_array_push(out, pd_node_text(def_body->as.text.value,
                                          def_body->as.text.value_len,
                                          def_body->span));
    }

    /* Restore definitions */
    for (int j = 0; j < param_count; j++) {
        if (had_def[j]) {
            shput(ctx->definitions, params[j].name, saved_defs[j]);
        } else {
            shdel(ctx->definitions, params[j].name);
        }
        pd_node_free(synthetic_nodes[j]);
    }

    /* Free remaining resolved arrays (should be empty — items transferred) */
    for (int j = 0; j < param_count; j++)
        node_array_free_shallow(&resolved[j]);

    /* Pop call stack */
    int sl = arrlen(ctx->call_stack);
    free(ctx->call_stack[sl - 1]);
    arrpop(ctx->call_stack);

    return rc;
}

/* --- Resolve macro args for render-time passthrough --- */

static int resolve_macro_args(PdNode **args, int arg_count,
                              EvalContext *ctx, NodeArray *out) {
    for (int i = 0; i < arg_count; i++) {
        PdNode *arg = args[i];
        if (arg->type != NODE_NAMED_ARG) {
            node_array_push(out, pd_node_clone(arg));
            continue;
        }

        PdNode *value = arg->as.named_arg.value;
        if (value && value->type == NODE_MACRO_CALL) {
            const char *ref = resolve_alias(value->as.macro_call.name);
            if (strncmp(ref, "env.", 4) == 0) {
                const char *ev = env_get(ctx, ref + 4);
                PdNode *new_val = pd_node_text(ev ? ev : "", ev ? (int)strlen(ev) : 0,
                                               value->span);
                PdNode *new_arg = pd_node_named_arg(arg->as.named_arg.name,
                                                    arg->as.named_arg.name_len,
                                                    arg->as.named_arg.name_span,
                                                    new_val, arg->span);
                node_array_push(out, new_arg);
                continue;
            }
            ptrdiff_t idx = shgeti(ctx->definitions, ref);
            if (idx >= 0) {
                char text[256];
                extract_def_text(ctx->definitions[idx].value, text, sizeof(text));
                PdNode *new_val = pd_node_text(text, (int)strlen(text),
                                               value->span);
                PdNode *new_arg = pd_node_named_arg(arg->as.named_arg.name,
                                                    arg->as.named_arg.name_len,
                                                    arg->as.named_arg.name_span,
                                                    new_val, arg->span);
                node_array_push(out, new_arg);
                continue;
            }
        }
        node_array_push(out, pd_node_clone(arg));
    }
    return 0;
}

/* --- Expand InterpString --- */

static int expand_interp_string(PdNode *interp, EvalContext *ctx,
                                PdNode **result) {
    NodeArray new_parts;
    node_array_init(&new_parts);

    for (int i = 0; i < interp->as.interp_string.count; i++) {
        PdNode *part = interp->as.interp_string.parts[i];
        if (part->type == NODE_CODE_SECTION) {
            NodeArray expanded;
            node_array_init(&expanded);
            int rc = expand_body_children(part->as.code_section.children,
                                          part->as.code_section.count,
                                          ctx, &expanded);
            if (rc < 0) {
                node_array_free_deep(&expanded);
                node_array_free_deep(&new_parts);
                return rc;
            }
            PdNode *new_cs = pd_node_code_section(expanded.items,
                                                  expanded.count,
                                                  part->span);
            node_array_push(&new_parts, new_cs);
        } else {
            node_array_push(&new_parts, pd_node_clone(part));
        }
    }

    *result = pd_node_interp_string(new_parts.items, new_parts.count,
                                    interp->span);
    return 0;
}

/* --- Recurse into body --- */

static int recurse_body(PdNode *body, EvalContext *ctx, PdNode **result) {
    if (!body) {
        *result = NULL;
        return 0;
    }

    if (body->type == NODE_BODY) {
        NodeArray expanded;
        node_array_init(&expanded);
        int rc = expand_body_children(body->as.body.children,
                                      body->as.body.count, ctx, &expanded);
        if (rc < 0) {
            node_array_free_deep(&expanded);
            return rc;
        }
        *result = pd_node_body(expanded.items, expanded.count, body->span);
        return 0;
    }

    if (body->type == NODE_INTERP_STRING) {
        return expand_interp_string(body, ctx, result);
    }

    /* RawString: no expansion needed */
    *result = pd_node_clone(body);
    return 0;
}

/* --- Expand body children --- */

static int expand_body_children(PdNode **children, int count,
                                EvalContext *ctx, NodeArray *out) {
    for (int i = 0; i < count; i++) {
        PdNode *child = children[i];
        if (child->type == NODE_MACRO_CALL) {
            int rc = expand_macro(child, ctx, out);
            if (rc < 0) return rc;
        } else {
            node_array_push(out, pd_node_clone(child));
        }
    }
    return 0;
}

/* --- Core macro expansion dispatcher --- */

static int expand_macro(PdNode *node, EvalContext *ctx, NodeArray *out) {
    const char *name = resolve_alias(node->as.macro_call.name);

    int rc = validate_builtin_args(node, name, ctx);
    if (rc < 0) return rc;

    /* Environment variable reference */
    if (strncmp(name, "env.", 4) == 0) {
        const char *env_key = name + 4;
        const char *ev = env_get(ctx, env_key);
        if (ev) {
            node_array_push(out, pd_node_text(ev, (int)strlen(ev), node->span));
        }
        return 0;
    }

    /* Comment — discard */
    if (strcmp(name, "comment") == 0)
        return 0;

    /* Set */
    if (strcmp(name, "set") == 0)
        return expand_set(node, ctx, out);

    /* Conditionals */
    if (strcmp(name, "ifeq") == 0)
        return expand_ifeq(node, ctx, out);
    if (strcmp(name, "ifne") == 0)
        return expand_ifne(node, ctx, out);
    if (strcmp(name, "ifset") == 0)
        return expand_ifset(node, ctx, out);

    /* Include */
    if (strcmp(name, "include") == 0)
        return expand_include(node, ctx, out);

    /* Table */
    if (strcmp(name, "table") == 0)
        return expand_table(node, ctx, out);

    /* User macro expansion */
    {
        ptrdiff_t didx = shgeti(ctx->definitions, name);
        const BuiltinDef *bdef = find_builtin(name);
        if (didx >= 0 && bdef == NULL) {
            return expand_user_macro(node, name, ctx, out);
        }
    }

    /* Trailing dot: #version. -> expand "version" + Text(".") */
    int name_len = (int)strlen(name);
    if (name_len > 1 && name[name_len - 1] == '.') {
        char base[256];
        memcpy(base, name, (size_t)(name_len - 1));
        base[name_len - 1] = '\0';
        if (shgeti(ctx->definitions, base) >= 0 && find_builtin(base) == NULL) {
            rc = expand_user_macro(node, base, ctx, out);
            if (rc < 0) return rc;
            node_array_push(out, pd_node_text(".", 1, node->span));
            return 0;
        }
    }

    /* Render-time macro: resolve args and recurse into body */
    NodeArray new_args;
    node_array_init(&new_args);
    rc = resolve_macro_args(node->as.macro_call.args,
                            node->as.macro_call.arg_count, ctx, &new_args);
    if (rc < 0) {
        node_array_free_deep(&new_args);
        return rc;
    }

    PdNode *new_body = NULL;
    rc = recurse_body(node->as.macro_call.body, ctx, &new_body);
    if (rc < 0) {
        node_array_free_deep(&new_args);
        return rc;
    }

    PdNode *result = pd_node_macro_call(node->as.macro_call.name,
                                        node->as.macro_call.name_len,
                                        new_args.items, new_args.count,
                                        new_body,
                                        node->as.macro_call.bracketed,
                                        node->span);
    node_array_push(out, result);
    return 0;
}

/* --- Pass 1: Definition collection --- */

static int collect_definitions(PdNode *doc, EvalContext *ctx) {
    for (int i = 0; i < doc->as.document.count; i++) {
        PdNode *child = doc->as.document.children[i];
        if (child->type != NODE_MACRO_CALL) continue;

        const char *name = resolve_alias(child->as.macro_call.name);
        if (strcmp(name, "set") != 0) continue;

        PdNode *name_val = get_arg(child, "name");
        if (!name_val) continue;

        char def_name[256];
        resolve_value(name_val, ctx, def_name, sizeof(def_name));

        if (shgeti(ctx->definitions, def_name) >= 0) {
            return eval_errf(ctx, child->span,
                             "duplicate definition: %s", def_name);
        }

        shput(ctx->definitions, def_name, child);

        if (strncmp(def_name, "env.", 4) == 0) {
            char text[256];
            extract_def_text(child, text, sizeof(text));
            env_set(ctx, def_name + 4, text);
        }
    }
    return 0;
}

/* --- Post-expansion validation: doc.content --- */

static int validate_doc_content(PdNode **children, int count,
                                EvalContext *ctx) {
    bool seen = false;
    for (int i = 0; i < count; i++) {
        PdNode *child = children[i];
        if (child->type != NODE_MACRO_CALL) continue;

        const char *name = resolve_alias(child->as.macro_call.name);
        if (strcmp(name, "doc.content") != 0) continue;

        if (seen) {
            return eval_err(ctx, "duplicate #doc.content", child->span);
        }
        seen = true;

        PdNode *type_val = get_arg(child, "type");
        if (type_val) {
            char type_text[64];
            resolve_value(type_val, ctx, type_text, sizeof(type_text));
            /* Simple post-expansion resolve */
            if (type_val->type == NODE_TEXT) {
                memcpy(type_text, type_val->as.text.value,
                       (size_t)type_val->as.text.value_len);
                type_text[type_val->as.text.value_len] = '\0';
            }
            if (!is_wrapper_tag(type_text)) {
                return eval_errf(ctx, child->span,
                    "invalid doc.content type '%s'; must be one of: "
                    "article, aside, div, footer, header, main, nav, section, span",
                    type_text);
            }
        }
    }
    return 0;
}

/* --- Post-expansion validation: nesting --- */

/* child (resolved name) -> set of allowed parent names */
static bool is_in_nesting_rules(const char *name, const char *parent,
                                bool *has_rule) {
    *has_rule = false;

    if (strcmp(name, "tr") == 0) {
        *has_rule = true;
        return parent && strcmp(parent, "table") == 0;
    }
    if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
        *has_rule = true;
        return parent && strcmp(parent, "tr") == 0;
    }
    if (strcmp(name, "*") == 0) {
        *has_rule = true;
        return parent && (strcmp(parent, "ul") == 0 || strcmp(parent, "ol") == 0);
    }
    return true;
}

static bool is_inline_context(const char *name) {
    return name && (
        strcmp(name, "p") == 0 ||
        strcmp(name, "b") == 0 ||
        strcmp(name, "i") == 0 ||
        strcmp(name, "link") == 0 ||
        strcmp(name, "~") == 0 ||
        strcmp(name, "span") == 0 ||
        strcmp(name, "literal") == 0
    );
}

static int validate_node(PdNode *node, const char *parent_name,
                          EvalContext *ctx) {
    const char *name = resolve_alias(node->as.macro_call.name);

    bool has_rule;
    bool allowed = is_in_nesting_rules(name, parent_name, &has_rule);
    if (has_rule && !allowed) {
        if (strcmp(name, "*") == 0) {
            return eval_errf(ctx, node->span,
                             "#%s must appear inside #ol or #ul",
                             node->as.macro_call.name);
        }
        if (strcmp(name, "tr") == 0) {
            return eval_errf(ctx, node->span,
                             "#%s must appear inside #table",
                             node->as.macro_call.name);
        }
        /* td/th */
        return eval_errf(ctx, node->span,
                         "#%s must appear inside #tr",
                         node->as.macro_call.name);
    }

    if (is_block_macro(name) && is_inline_context(parent_name)) {
        return eval_errf(ctx, node->span,
            "block element #%s cannot appear inside inline element #%s",
            node->as.macro_call.name, parent_name);
    }

    /* Recurse into body */
    PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            PdNode *child = body->as.body.children[i];
            if (child->type == NODE_MACRO_CALL) {
                int rc = validate_node(child, name, ctx);
                if (rc < 0) return rc;
            }
        }
    }
    return 0;
}

static int validate_nesting(PdNode **children, int count, EvalContext *ctx) {
    for (int i = 0; i < count; i++) {
        if (children[i]->type == NODE_MACRO_CALL) {
            int rc = validate_node(children[i], NULL, ctx);
            if (rc < 0) return rc;
        }
    }
    return 0;
}

/* --- Top-level expansion --- */

static int expand_top_level(PdNode *doc, EvalContext *ctx, NodeArray *out) {
    for (int i = 0; i < doc->as.document.count; i++) {
        PdNode *child = doc->as.document.children[i];

        if (child->type == NODE_PARAGRAPH) {
            /* Wrap paragraph in #p */
            NodeArray expanded;
            node_array_init(&expanded);
            int rc = expand_body_children(child->as.paragraph.children,
                                          child->as.paragraph.count,
                                          ctx, &expanded);
            if (rc < 0) {
                node_array_free_deep(&expanded);
                return rc;
            }
            PdNode *p_body = pd_node_body(expanded.items, expanded.count,
                                          child->span);
            PdNode *p_call = pd_node_macro_call("p", 1, NULL, 0,
                                                p_body, false, child->span);
            node_array_push(out, p_call);
        } else if (child->type == NODE_MACRO_CALL) {
            int rc = expand_macro(child, ctx, out);
            if (rc < 0) return rc;
        }
    }
    return 0;
}

/* --- Public API --- */

int pd_evaluate(PdNode *doc, const char *filename, const char *source,
                const char *const *env_keys, const char *const *env_vals,
                int env_count,
                PdNode **out, PdError *err) {
    EvalContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.filename = filename ? filename : "input.pdoc";
    ctx.source = source ? source : "";
    ctx.source_dir = get_source_dir(ctx.filename);
    ctx.definitions = NULL;  /* stb_ds string hash map */
    sh_new_strdup(ctx.definitions);  /* enable key duplication */
    ctx.include_stack = NULL; /* stb_ds dynamic array */
    ctx.call_stack = NULL;    /* stb_ds dynamic array */
    ctx.env_keys = NULL;
    ctx.env_vals = NULL;
    ctx.env_count = 0;
    ctx.err = err;

    /* Seed environment */
    for (int i = 0; i < env_count; i++) {
        env_set(&ctx, env_keys[i], env_vals[i]);
    }

    /* Push initial file to include stack */
    {
        char *resolved = realpath(ctx.filename, NULL);
        if (!resolved) resolved = strdup(ctx.filename);
        arrput(ctx.include_stack, resolved);
    }

    /* Pass 1: Collect definitions */
    int rc = collect_definitions(doc, &ctx);
    if (rc < 0) goto cleanup;

    /* Pass 2: Expand */
    {
        NodeArray expanded;
        node_array_init(&expanded);
        rc = expand_top_level(doc, &ctx, &expanded);
        if (rc < 0) {
            node_array_free_deep(&expanded);
            goto cleanup;
        }

        /* Filter to MacroCall nodes only (Text/Escape from conditionals are whitespace) */
        NodeArray filtered;
        node_array_init(&filtered);
        for (int i = 0; i < expanded.count; i++) {
            if (expanded.items[i]->type == NODE_MACRO_CALL) {
                node_array_push(&filtered, expanded.items[i]);
            } else {
                pd_node_free(expanded.items[i]);
            }
        }
        node_array_free_shallow(&expanded);

        /* Validate doc.content */
        rc = validate_doc_content(filtered.items, filtered.count, &ctx);
        if (rc < 0) {
            node_array_free_deep(&filtered);
            goto cleanup;
        }

        /* Build result document */
        *out = pd_node_document(filtered.items, filtered.count, doc->span);

        /* Validate nesting */
        rc = validate_nesting((*out)->as.document.children,
                              (*out)->as.document.count, &ctx);
        if (rc < 0) {
            pd_node_free(*out);
            *out = NULL;
            goto cleanup;
        }
    }

cleanup:
    /* Free include stack */
    for (int i = 0; i < arrlen(ctx.include_stack); i++)
        free(ctx.include_stack[i]);
    arrfree(ctx.include_stack);

    /* Free call stack */
    for (int i = 0; i < arrlen(ctx.call_stack); i++)
        free(ctx.call_stack[i]);
    arrfree(ctx.call_stack);

    /* Free env */
    for (int i = 0; i < ctx.env_count; i++) {
        free(ctx.env_keys[i]);
        free(ctx.env_vals[i]);
    }
    arrfree(ctx.env_keys);
    arrfree(ctx.env_vals);

    /* Free definitions hash map (keys are owned by stb_ds) */
    shfree(ctx.definitions);

    free(ctx.source_dir);

    return rc;
}
