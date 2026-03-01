#define _POSIX_C_SOURCE 200809L

#include "lsp.h"
#include "ast.h"
#include "builtins.h"
#include "errors.h"
#include "eval.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bstrlib.h"
#include "cJSON.h"
#include "stb_ds.h"

/* -----------------------------------------------------------------------
 * Per-document state
 * ----------------------------------------------------------------------- */

typedef struct {
    char *uri;
    char *source;
    char *filename;
} LspDoc;

static void lsp_doc_clear(LspDoc *doc) {
    free(doc->uri);
    free(doc->source);
    free(doc->filename);
    doc->uri = NULL;
    doc->source = NULL;
    doc->filename = NULL;
}

static void lsp_doc_set(LspDoc *doc, const char *uri, const char *source) {
    lsp_doc_clear(doc);
    doc->uri = strdup(uri);
    doc->source = strdup(source);
    /* Extract filename from URI (last path component). */
    const char *slash = strrchr(uri, '/');
    doc->filename = strdup(slash ? slash + 1 : uri);
}

/* -----------------------------------------------------------------------
 * Transport — JSON-RPC 2.0 over stdio
 * ----------------------------------------------------------------------- */

static char *read_message(void) {
    /* Read headers until blank line. */
    int content_length = -1;
    char line[256];
    for (;;) {
        if (!fgets(line, sizeof(line), stdin))
            return NULL;
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0)
            break;
        if (strncmp(line, "Content-Length:", 15) == 0)
            content_length = atoi(line + 15);
    }
    if (content_length <= 0)
        return NULL;
    char *buf = malloc((size_t)content_length + 1);
    if (!buf)
        return NULL;
    size_t nread = fread(buf, 1, (size_t)content_length, stdin);
    if ((int)nread != content_length) {
        free(buf);
        return NULL;
    }
    buf[content_length] = '\0';
    return buf;
}

static void send_raw(const char *json) {
    int len = (int)strlen(json);
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, json);
    fflush(stdout);
}

static void send_response(cJSON *id, cJSON *result) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddItemToObject(msg, "id", cJSON_Duplicate(id, 1));
    cJSON_AddItemToObject(msg, "result", result ? result : cJSON_CreateNull());
    char *text = cJSON_PrintUnformatted(msg);
    send_raw(text);
    free(text);
    cJSON_Delete(msg);
}

static void send_notification(const char *method, cJSON *params) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddStringToObject(msg, "method", method);
    cJSON_AddItemToObject(msg, "params", params);
    char *text = cJSON_PrintUnformatted(msg);
    send_raw(text);
    free(text);
    cJSON_Delete(msg);
}

/* -----------------------------------------------------------------------
 * Analysis helpers
 * ----------------------------------------------------------------------- */

/* User-definition entry for a stb_ds string hash map. */
typedef struct {
    char *key;          /* stb_ds string key — macro name */
    PdNode *value;      /* NOT owned — pointer into the AST */
} DefMapEntry;

/*
 * Parse source and collect #set definitions.
 * On failure, *ast_out = NULL and *defs_out = NULL.
 */
static void lsp_analyze(const char *source, const char *filename,
                        PdNode **ast_out, DefMapEntry **defs_out) {
    *ast_out = NULL;
    *defs_out = NULL;

    TokenArray tokens;
    token_array_init(&tokens);
    PdError err;
    pd_error_init(&err);

    if (pd_tokenize(source, filename, &tokens, &err) != 0) {
        pd_error_free(&err);
        token_array_free(&tokens);
        return;
    }

    PdNode *ast = NULL;
    if (pd_parse(&tokens, source, filename, &ast, &err) != 0) {
        pd_error_free(&err);
        token_array_free(&tokens);
        return;
    }
    token_array_free(&tokens);

    /* Collect #set definitions (top-level only, matching Python). */
    DefMapEntry *defs = NULL;
    /* stb_ds: must include stb_ds.h for sh_new_strdup.
     * Since STB_DS_IMPLEMENTATION is in eval.c, we just use the header. */
    sh_new_strdup(defs);

    for (int i = 0; i < ast->as.document.count; i++) {
        PdNode *child = ast->as.document.children[i];
        if (child->type != NODE_MACRO_CALL)
            continue;
        const char *resolved = resolve_alias(child->as.macro_call.name);
        if (strcmp(resolved, "set") != 0)
            continue;
        /* Find "name" arg. */
        for (int a = 0; a < child->as.macro_call.arg_count; a++) {
            PdNode *arg = child->as.macro_call.args[a];
            if (arg->type != NODE_NAMED_ARG)
                continue;
            if (strcmp(arg->as.named_arg.name, "name") != 0)
                continue;
            PdNode *val = arg->as.named_arg.value;
            if (val->type == NODE_TEXT || val->type == NODE_RAW_STRING) {
                shput(defs, val->as.text.value, child);
            } else if (val->type == NODE_INTERP_STRING) {
                /* Simple case: all-text parts. */
                bstring s = bfromcstr("");
                for (int p = 0; p < val->as.interp_string.count; p++) {
                    PdNode *part = val->as.interp_string.parts[p];
                    if (part->type == NODE_TEXT)
                        bcatcstr(s, part->as.text.value);
                }
                if (blength(s) > 0)
                    shput(defs, bdata(s), child);
                bdestroy(s);
            }
            break;
        }
    }

    *ast_out = ast;
    *defs_out = defs;
}

/*
 * Find the macro name at cursor position (0-based line/col).
 * Returns heap-allocated string or NULL.
 */
static char *find_macro_at_position(PdNode *ast, int line_0, int col_0);

/* Forward declarations for recursive search. */
static char *check_macro(PdNode *node, int line_1, int col_1);
static char *search_children(PdNode **children, int count, int line_1, int col_1);

static char *search_children(PdNode **children, int count, int line_1, int col_1) {
    for (int i = 0; i < count; i++) {
        PdNode *child = children[i];
        if (child->type == NODE_MACRO_CALL) {
            char *r = check_macro(child, line_1, col_1);
            if (r) return r;
        } else if (child->type == NODE_PARAGRAPH) {
            char *r = search_children(child->as.paragraph.children,
                                      child->as.paragraph.count,
                                      line_1, col_1);
            if (r) return r;
        }
    }
    return NULL;
}

static char *check_macro(PdNode *node, int line_1, int col_1) {
    Position start = node->span.start;
    if (start.line == line_1) {
        int hash_col = node->as.macro_call.bracketed
                           ? start.column + 1
                           : start.column;
        int name_end_col = hash_col + node->as.macro_call.name_len;
        if (col_1 >= hash_col && col_1 <= name_end_col)
            return strdup(node->as.macro_call.name);
    }

    /* Recurse into body. */
    PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        char *r = search_children(body->as.body.children,
                                  body->as.body.count,
                                  line_1, col_1);
        if (r) return r;
    }

    /* Recurse into arg values. */
    for (int a = 0; a < node->as.macro_call.arg_count; a++) {
        PdNode *arg = node->as.macro_call.args[a];
        if (arg->type == NODE_NAMED_ARG && arg->as.named_arg.value) {
            PdNode *val = arg->as.named_arg.value;
            if (val->type == NODE_MACRO_CALL) {
                char *r = check_macro(val, line_1, col_1);
                if (r) return r;
            }
        }
    }
    return NULL;
}

static char *find_macro_at_position(PdNode *ast, int line_0, int col_0) {
    int line_1 = line_0 + 1;
    int col_1 = col_0 + 1;
    return search_children(ast->as.document.children,
                           ast->as.document.count,
                           line_1, col_1);
}

/* -----------------------------------------------------------------------
 * Diagnostics
 * ----------------------------------------------------------------------- */

static cJSON *make_position(int line, int character) {
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", character);
    return pos;
}

static cJSON *make_range(int sl, int sc, int el, int ec) {
    cJSON *range = cJSON_CreateObject();
    cJSON_AddItemToObject(range, "start", make_position(sl, sc));
    cJSON_AddItemToObject(range, "end", make_position(el, ec));
    return range;
}

static void publish_diagnostics(const char *uri, const char *source,
                                const char *filename) {
    cJSON *diags = cJSON_CreateArray();

    TokenArray tokens;
    token_array_init(&tokens);
    PdError err;
    pd_error_init(&err);

    if (pd_tokenize(source, filename, &tokens, &err) != 0) {
        /* Lex error. */
        int line = err.position.line - 1;
        int col = err.position.column - 1;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddItemToObject(d, "range", make_range(line, col, line, col + 1));
        cJSON_AddStringToObject(d, "message", err.message);
        cJSON_AddNumberToObject(d, "severity", 1); /* Error */
        cJSON_AddStringToObject(d, "source", "picodoc");
        cJSON_AddItemToArray(diags, d);
        pd_error_free(&err);
        token_array_free(&tokens);
        goto publish;
    }

    PdNode *ast = NULL;
    if (pd_parse(&tokens, source, filename, &ast, &err) != 0) {
        /* Parse error. */
        int sl = err.span.start.line - 1;
        int sc = err.span.start.column - 1;
        int el = err.span.end.line - 1;
        int ec = err.span.end.column - 1;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddItemToObject(d, "range", make_range(sl, sc, el, ec));
        cJSON_AddStringToObject(d, "message", err.message);
        cJSON_AddNumberToObject(d, "severity", 1); /* Error */
        cJSON_AddStringToObject(d, "source", "picodoc");
        cJSON_AddItemToArray(diags, d);
        pd_error_free(&err);
        token_array_free(&tokens);
        goto publish;
    }
    token_array_free(&tokens);

    /* Try evaluating — eval errors become warnings. */
    {
        PdNode *evaled = NULL;
        pd_error_init(&err);
        if (pd_evaluate(ast, filename, source, NULL, NULL, 0, NULL,
                        &evaled, &err) != 0) {
            int sl = err.span.start.line - 1;
            int sc = err.span.start.column - 1;
            int el = err.span.end.line - 1;
            int ec = err.span.end.column - 1;
            cJSON *d = cJSON_CreateObject();
            cJSON_AddItemToObject(d, "range", make_range(sl, sc, el, ec));
            /* Use detail if available (includes call stack), else message. */
            const char *msg = err.detail ? err.detail : err.message;
            cJSON_AddStringToObject(d, "message", msg);
            cJSON_AddNumberToObject(d, "severity", 2); /* Warning */
            cJSON_AddStringToObject(d, "source", "picodoc");
            cJSON_AddItemToArray(diags, d);
            pd_error_free(&err);
            pd_node_free(ast);
        } else {
            pd_node_free(evaled);
        }
    }

publish:;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri);
    cJSON_AddItemToObject(params, "diagnostics", diags);
    send_notification("textDocument/publishDiagnostics", params);
}

/* -----------------------------------------------------------------------
 * Hover formatting
 * ----------------------------------------------------------------------- */

static char *format_builtin_hover(const char *name) {
    const BuiltinDef *defn = find_builtin(name);
    if (!defn) return NULL;

    bstring s = bfromcstr("");
    bformata(s, "**#%s** (builtin)", defn->name);

    /* Aliases that map to this builtin. */
    bstring aliases = bfromcstr("");
    int alias_n = pd_alias_count();
    for (int i = 0; i < alias_n; i++) {
        const AliasEntry *a = pd_alias_at(i);
        if (strcmp(a->canonical, name) == 0) {
            if (blength(aliases) > 0)
                bcatcstr(aliases, ", ");
            bformata(aliases, "`#%s`", a->alias);
        }
    }
    if (blength(aliases) > 0)
        bformata(s, "\n\nAliases: %s", bdata(aliases));
    bdestroy(aliases);

    /* Parameters. */
    if (defn->param_count > 0) {
        bcatcstr(s, "\n\nParameters:");
        for (int i = 0; i < defn->param_count; i++) {
            const char *req = defn->params[i].required ? "required" : "optional";
            bformata(s, "\n- `%s` (%s)", defn->params[i].name, req);
        }
    }

    if (defn->has_body)
        bcatcstr(s, "\n\nAccepts body content.");

    const char *sd = bdata(s);
    char *result = strdup(sd ? sd : "");
    bdestroy(s);
    return result;
}

static char *format_user_hover(const char *name, PdNode *defn) {
    bstring s = bfromcstr("");
    bformata(s, "**#%s** (user-defined)", name);

    /* Show params (skip name= arg). */
    int has_params = 0;
    for (int i = 0; i < defn->as.macro_call.arg_count; i++) {
        PdNode *arg = defn->as.macro_call.args[i];
        if (arg->type != NODE_NAMED_ARG)
            continue;
        if (strcmp(arg->as.named_arg.name, "name") == 0)
            continue;
        if (!has_params) {
            bcatcstr(s, "\n\nParameters:");
            has_params = 1;
        }
        const char *req = (arg->as.named_arg.value &&
                           arg->as.named_arg.value->type == NODE_REQUIRED_MARKER)
                              ? "required"
                              : "optional";
        bformata(s, "\n- `%s` (%s)", arg->as.named_arg.name, req);
    }

    if (defn->as.macro_call.body)
        bcatcstr(s, "\n\nHas body content.");

    Position loc = defn->span.start;
    bformata(s, "\n\nDefined at line %d, column %d.", loc.line, loc.column);

    const char *sd = bdata(s);
    char *result = strdup(sd ? sd : "");
    bdestroy(s);
    return result;
}

/* -----------------------------------------------------------------------
 * Method handlers
 * ----------------------------------------------------------------------- */

static void handle_initialize(cJSON *id) {
    cJSON *caps = cJSON_CreateObject();

    /* textDocumentSync = Full (1). */
    cJSON_AddNumberToObject(caps, "textDocumentSync", 1);
    cJSON_AddTrueToObject(caps, "hoverProvider");
    cJSON_AddTrueToObject(caps, "definitionProvider");

    /* completionProvider with trigger character. */
    cJSON *comp = cJSON_CreateObject();
    cJSON *triggers = cJSON_CreateArray();
    cJSON_AddItemToArray(triggers, cJSON_CreateString("#"));
    cJSON_AddItemToObject(comp, "triggerCharacters", triggers);
    cJSON_AddItemToObject(caps, "completionProvider", comp);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "capabilities", caps);

    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", "picodoc-lsp");
    cJSON_AddStringToObject(info, "version", "0.1.0");
    cJSON_AddItemToObject(result, "serverInfo", info);

    send_response(id, result);
}

static void handle_did_open(cJSON *params, LspDoc *doc) {
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(td, "text"));
    if (!uri || !text) return;
    lsp_doc_set(doc, uri, text);
    publish_diagnostics(doc->uri, doc->source, doc->filename);
}

static void handle_did_change(cJSON *params, LspDoc *doc) {
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) return;
    cJSON *changes = cJSON_GetObjectItem(params, "contentChanges");
    if (!cJSON_IsArray(changes) || cJSON_GetArraySize(changes) == 0)
        return;
    /* Full sync — take last change. */
    cJSON *last = cJSON_GetArrayItem(changes, cJSON_GetArraySize(changes) - 1);
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(last, "text"));
    if (!text) return;
    lsp_doc_set(doc, uri, text);
    publish_diagnostics(doc->uri, doc->source, doc->filename);
}

static void handle_hover(cJSON *id, cJSON *params, LspDoc *doc) {
    if (!doc->source) {
        send_response(id, NULL);
        return;
    }

    cJSON *pos = cJSON_GetObjectItem(params, "position");
    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int col = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));

    PdNode *ast = NULL;
    DefMapEntry *defs = NULL;
    lsp_analyze(doc->source, doc->filename, &ast, &defs);
    if (!ast) {
        shfree(defs);
        send_response(id, NULL);
        return;
    }

    char *name = find_macro_at_position(ast, line, col);
    if (!name) {
        pd_node_free(ast);
        shfree(defs);
        send_response(id, NULL);
        return;
    }

    const char *resolved = resolve_alias(name);
    char *md = NULL;

    if (find_builtin(resolved)) {
        md = format_builtin_hover(resolved);
    } else {
        int idx = shgeti(defs, resolved);
        if (idx >= 0)
            md = format_user_hover(resolved, defs[idx].value);
    }

    free(name);
    pd_node_free(ast);
    shfree(defs);

    if (!md) {
        send_response(id, NULL);
        return;
    }

    cJSON *contents = cJSON_CreateObject();
    cJSON_AddStringToObject(contents, "kind", "markdown");
    cJSON_AddStringToObject(contents, "value", md);
    free(md);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "contents", contents);
    send_response(id, result);
}

static void handle_definition(cJSON *id, cJSON *params, LspDoc *doc) {
    if (!doc->source) {
        send_response(id, NULL);
        return;
    }

    cJSON *pos = cJSON_GetObjectItem(params, "position");
    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int col = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));

    PdNode *ast = NULL;
    DefMapEntry *defs = NULL;
    lsp_analyze(doc->source, doc->filename, &ast, &defs);
    if (!ast) {
        shfree(defs);
        send_response(id, NULL);
        return;
    }

    char *name = find_macro_at_position(ast, line, col);
    if (!name) {
        pd_node_free(ast);
        shfree(defs);
        send_response(id, NULL);
        return;
    }

    const char *resolved = resolve_alias(name);
    int idx = shgeti(defs, resolved);
    free(name);

    if (idx < 0) {
        pd_node_free(ast);
        shfree(defs);
        send_response(id, NULL);
        return;
    }

    PdNode *defn = defs[idx].value;
    Position start = defn->span.start;
    Position end = defn->span.end;

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "uri", doc->uri);
    cJSON_AddItemToObject(result, "range",
                          make_range(start.line - 1, start.column - 1,
                                     end.line - 1, end.column - 1));

    pd_node_free(ast);
    shfree(defs);
    send_response(id, result);
}

static void handle_completion(cJSON *id, cJSON *params, LspDoc *doc) {
    (void)params;

    cJSON *items = cJSON_CreateArray();

    /* Builtins. */
    int bn = pd_builtin_count();
    for (int i = 0; i < bn; i++) {
        const BuiltinDef *b = pd_builtin_at(i);
        bstring detail = bfromcstr("builtin");
        if (b->param_count > 0) {
            bassigncstr(detail, "builtin(");
            for (int p = 0; p < b->param_count; p++) {
                if (p > 0)
                    bcatcstr(detail, ", ");
                bcatcstr(detail, b->params[p].name);
                if (!b->params[p].required)
                    bcatcstr(detail, "?");
            }
            bcatcstr(detail, ")");
        }
        if (b->has_body)
            bcatcstr(detail, " +body");

        char label[128];
        snprintf(label, sizeof(label), "#%s", b->name);

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "label", label);
        cJSON_AddNumberToObject(item, "kind", 3); /* Function */
        cJSON_AddStringToObject(item, "detail", bdata(detail));
        cJSON_AddItemToArray(items, item);
        bdestroy(detail);
    }

    /* Aliases. */
    int an = pd_alias_count();
    for (int i = 0; i < an; i++) {
        const AliasEntry *a = pd_alias_at(i);
        char label[128];
        snprintf(label, sizeof(label), "#%s", a->alias);
        char det[128];
        snprintf(det, sizeof(det), "alias for #%s", a->canonical);

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "label", label);
        cJSON_AddNumberToObject(item, "kind", 18); /* Reference */
        cJSON_AddStringToObject(item, "detail", det);
        cJSON_AddItemToArray(items, item);
    }

    /* User-defined macros. */
    if (doc->source) {
        PdNode *ast = NULL;
        DefMapEntry *defs = NULL;
        lsp_analyze(doc->source, doc->filename, &ast, &defs);
        if (ast) {
            int n = shlen(defs);
            for (int i = 0; i < n; i++) {
                PdNode *defn = defs[i].value;
                bstring detail = bfromcstr("user");

                /* Build param string. */
                bstring ps = bfromcstr("");
                for (int a = 0; a < defn->as.macro_call.arg_count; a++) {
                    PdNode *arg = defn->as.macro_call.args[a];
                    if (arg->type != NODE_NAMED_ARG)
                        continue;
                    if (strcmp(arg->as.named_arg.name, "name") == 0)
                        continue;
                    if (blength(ps) > 0)
                        bcatcstr(ps, ", ");
                    bcatcstr(ps, arg->as.named_arg.name);
                    if (!(arg->as.named_arg.value &&
                          arg->as.named_arg.value->type == NODE_REQUIRED_MARKER))
                        bcatcstr(ps, "?");
                }
                if (blength(ps) > 0)
                    bformata(detail, "(%s)", bdata(ps));

                /* Rewrite detail from "user" to "user(params)" if needed. */
                if (blength(ps) > 0) {
                    bassigncstr(detail, "user(");
                    bconcat(detail, ps);
                    bcatcstr(detail, ")");
                }
                bdestroy(ps);

                char label[256];
                snprintf(label, sizeof(label), "#%s", defs[i].key);

                cJSON *item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "label", label);
                cJSON_AddNumberToObject(item, "kind", 6); /* Variable */
                cJSON_AddStringToObject(item, "detail", bdata(detail));
                cJSON_AddItemToArray(items, item);
                bdestroy(detail);
            }
            pd_node_free(ast);
            shfree(defs);
        }
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddFalseToObject(result, "isIncomplete");
    cJSON_AddItemToObject(result, "items", items);
    send_response(id, result);
}

/* -----------------------------------------------------------------------
 * Main loop
 * ----------------------------------------------------------------------- */

int pd_lsp_main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    LspDoc doc = {0};
    int shutdown_requested = 0;

    for (;;) {
        char *raw = read_message();
        if (!raw)
            break;

        cJSON *msg = cJSON_Parse(raw);
        free(raw);
        if (!msg)
            continue;

        cJSON *method_j = cJSON_GetObjectItem(msg, "method");
        cJSON *id = cJSON_GetObjectItem(msg, "id");
        cJSON *params = cJSON_GetObjectItem(msg, "params");
        const char *method = cJSON_GetStringValue(method_j);

        if (!method) {
            cJSON_Delete(msg);
            continue;
        }

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(id);
        } else if (strcmp(method, "initialized") == 0) {
            /* No response needed. */
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            handle_did_open(params, &doc);
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            handle_did_change(params, &doc);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            handle_hover(id, params, &doc);
        } else if (strcmp(method, "textDocument/definition") == 0) {
            handle_definition(id, params, &doc);
        } else if (strcmp(method, "textDocument/completion") == 0) {
            handle_completion(id, params, &doc);
        } else if (strcmp(method, "shutdown") == 0) {
            shutdown_requested = 1;
            send_response(id, NULL);
        } else if (strcmp(method, "exit") == 0) {
            cJSON_Delete(msg);
            break;
        }
        /* Ignore unknown methods. */

        cJSON_Delete(msg);
    }

    lsp_doc_clear(&doc);
    return shutdown_requested ? 0 : 1;
}
