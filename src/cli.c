/*
 * picodoc CLI — command-line interface for the picodoc document processor.
 *
 * Port of Python cli.py + inject.py + debug.py.
 * Skipped: --config (TOML), --watch.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "ast.h"
#include "errors.h"
#include "eval.h"
#include "filters.h"
#include "lexer.h"
#include "parser.h"
#include "render.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* CLI options                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *input_file;
    const char *output_file;    /* NULL → stdout */
    const char **env_keys;      /* dynamic array (realloc) */
    const char **env_vals;      /* dynamic array (realloc) */
    int env_count;
    const char **css_files;     /* dynamic array (realloc) */
    int css_count;
    const char **js_files;      /* dynamic array (realloc) */
    int js_count;
    const char **meta_names;    /* dynamic array (realloc) */
    const char **meta_values;   /* dynamic array (realloc) */
    int meta_count;
    const char **filter_paths;  /* dynamic array (realloc) */
    int filter_path_count;
    float filter_timeout;       /* seconds, 0 = default (5.0) */
    bool debug;
} CliOptions;

static void cli_options_init(CliOptions *opts) {
    memset(opts, 0, sizeof(*opts));
}

static void cli_options_free(CliOptions *opts) {
    free(opts->env_keys);
    free(opts->env_vals);
    free(opts->css_files);
    free(opts->js_files);
    free(opts->meta_names);
    free(opts->meta_values);
    free(opts->filter_paths);
}

/* Append to a growable const char* array. */
static void arr_push(const char ***arr, int *count, const char *val) {
    *arr = realloc(*arr, (size_t)(*count + 1) * sizeof(const char *));
    (*arr)[*count] = val;
    (*count)++;
}

/* ------------------------------------------------------------------ */
/* Usage                                                               */
/* ------------------------------------------------------------------ */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <input.pdoc>\n"
        "\n"
        "Options:\n"
        "  -o FILE, --output FILE    Output file (default: stdout)\n"
        "  -e NAME=VALUE, --env NAME=VALUE\n"
        "                            Set environment variable (repeatable)\n"
        "  --css FILE                CSS stylesheet to include (repeatable)\n"
        "  --js FILE                 JavaScript file to include (repeatable)\n"
        "  --meta NAME=VALUE         Meta tag to include (repeatable)\n"
        "  --filter-path DIR         Extra filter search directory (repeatable)\n"
        "  --filter-timeout SECS     Filter execution timeout (default: 5.0)\n"
        "  --debug                   Dump evaluated AST to stderr\n"
        "  -h, --help                Print this help and exit\n",
        prog);
}

/* ------------------------------------------------------------------ */
/* Argument parsing                                                    */
/* ------------------------------------------------------------------ */

/* Split "NAME=VALUE" at the first '='. Returns 0 on success, -1 if no '='. */
static int split_kv(const char *s, const char **key, const char **val) {
    const char *eq = strchr(s, '=');
    if (!eq) return -1;
    /* key is a heap-allocated substring */
    size_t klen = (size_t)(eq - s);
    char *k = malloc(klen + 1);
    memcpy(k, s, klen);
    k[klen] = '\0';
    *key = k;
    *val = eq + 1;  /* points into argv, stable for program lifetime */
    return 0;
}

static int parse_args(int argc, char **argv, CliOptions *opts) {
    const char *prog = argc > 0 ? argv[0] : "picodoc";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(prog);
            exit(0);
        } else if (strcmp(arg, "--debug") == 0) {
            opts->debug = true;
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: %s requires an argument\n", prog, arg);
                return -1;
            }
            opts->output_file = argv[i];
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--env") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: %s requires an argument\n", prog, arg);
                return -1;
            }
            const char *key, *val;
            if (split_kv(argv[i], &key, &val) < 0) {
                fprintf(stderr, "%s: invalid env format '%s' (expected NAME=VALUE)\n",
                        prog, argv[i]);
                return -1;
            }
            arr_push(&opts->env_keys, &opts->env_count, key);
            /* env_vals shares the same count, but we track it via env_count
             * only on env_keys — push to vals separately. */
            opts->env_vals = realloc(opts->env_vals,
                                     (size_t)opts->env_count * sizeof(const char *));
            opts->env_vals[opts->env_count - 1] = val;
        } else if (strcmp(arg, "--css") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: --css requires an argument\n", prog);
                return -1;
            }
            arr_push(&opts->css_files, &opts->css_count, argv[i]);
        } else if (strcmp(arg, "--js") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: --js requires an argument\n", prog);
                return -1;
            }
            arr_push(&opts->js_files, &opts->js_count, argv[i]);
        } else if (strcmp(arg, "--meta") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: --meta requires an argument\n", prog);
                return -1;
            }
            const char *name, *content;
            if (split_kv(argv[i], &name, &content) < 0) {
                fprintf(stderr, "%s: invalid meta format '%s' (expected NAME=VALUE)\n",
                        prog, argv[i]);
                return -1;
            }
            arr_push(&opts->meta_names, &opts->meta_count, name);
            opts->meta_values = realloc(opts->meta_values,
                                        (size_t)opts->meta_count * sizeof(const char *));
            opts->meta_values[opts->meta_count - 1] = content;
        } else if (strcmp(arg, "--filter-path") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: --filter-path requires an argument\n", prog);
                return -1;
            }
            arr_push(&opts->filter_paths, &opts->filter_path_count, argv[i]);
        } else if (strcmp(arg, "--filter-timeout") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s: --filter-timeout requires an argument\n", prog);
                return -1;
            }
            opts->filter_timeout = (float)atof(argv[i]);
        } else if (arg[0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", prog, arg);
            return -1;
        } else {
            /* Positional: input file */
            if (opts->input_file) {
                fprintf(stderr, "%s: unexpected argument '%s'\n", prog, arg);
                return -1;
            }
            opts->input_file = arg;
        }
    }

    if (!opts->input_file) {
        print_usage(prog);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* File I/O                                                            */
/* ------------------------------------------------------------------ */

static int read_file(const char *path, char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 0) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        fclose(f);
        return -1;
    }

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fprintf(stderr, "error: out of memory reading '%s'\n", path);
        fclose(f);
        return -1;
    }

    size_t nread = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[nread] = '\0';

    *out = buf;
    if (out_len) *out_len = nread;
    return 0;
}

/* ------------------------------------------------------------------ */
/* AST dump (port of debug.py)                                        */
/* ------------------------------------------------------------------ */

static void dump_child(const PdNode *node, int depth);
static void dump_macro(const PdNode *node, int depth);

static void indent(int depth) {
    for (int i = 0; i < depth; i++)
        fputs("  ", stderr);
}

static void dump_value_inline(const PdNode *node) {
    switch (node->type) {
    case NODE_TEXT:
        fprintf(stderr, "Text(\"%.*s\")", node->as.text.value_len, node->as.text.value);
        break;
    case NODE_RAW_STRING:
        fprintf(stderr, "RawString(\"%.*s\")", node->as.text.value_len, node->as.text.value);
        break;
    case NODE_INTERP_STRING:
        fputs("InterpString(", stderr);
        for (int i = 0; i < node->as.interp_string.count; i++) {
            const PdNode *part = node->as.interp_string.parts[i];
            if (part->type == NODE_TEXT) {
                fprintf(stderr, "Text(\"%.*s\")",
                        part->as.text.value_len, part->as.text.value);
            } else if (part->type == NODE_CODE_SECTION) {
                fputs("Code[...]", stderr);
            }
        }
        fputs(")", stderr);
        break;
    case NODE_MACRO_CALL:
        fprintf(stderr, "MacroCall(#%.*s)",
                node->as.macro_call.name_len, node->as.macro_call.name);
        break;
    case NODE_REQUIRED_MARKER:
        fputs("?", stderr);
        break;
    default:
        fprintf(stderr, "<%s>", node_type_name(node->type));
        break;
    }
}

static void dump_arg(const PdNode *node, int depth) {
    indent(depth);
    fprintf(stderr, "Arg %.*s=",
            node->as.named_arg.name_len, node->as.named_arg.name);
    dump_value_inline(node->as.named_arg.value);
    fputs("\n", stderr);
}

static void dump_body(const PdNode *node, int depth) {
    switch (node->type) {
    case NODE_BODY:
        indent(depth);
        fputs("Body\n", stderr);
        for (int i = 0; i < node->as.body.count; i++)
            dump_child(node->as.body.children[i], depth + 1);
        break;
    case NODE_INTERP_STRING:
        indent(depth);
        fputs("InterpString\n", stderr);
        for (int i = 0; i < node->as.interp_string.count; i++) {
            const PdNode *part = node->as.interp_string.parts[i];
            if (part->type == NODE_TEXT) {
                indent(depth + 1);
                fprintf(stderr, "Text(\"%.*s\")\n",
                        part->as.text.value_len, part->as.text.value);
            } else if (part->type == NODE_CODE_SECTION) {
                indent(depth + 1);
                fputs("CodeSection\n", stderr);
                for (int j = 0; j < part->as.code_section.count; j++)
                    dump_child(part->as.code_section.children[j], depth + 2);
            }
        }
        break;
    case NODE_RAW_STRING:
        indent(depth);
        fprintf(stderr, "RawString(\"%.*s\")\n",
                node->as.text.value_len, node->as.text.value);
        break;
    default:
        break;
    }
}

static void dump_macro(const PdNode *node, int depth) {
    const char *bracket = node->as.macro_call.bracketed ? "[#...]" : "#";
    indent(depth);
    fprintf(stderr, "MacroCall %s%.*s\n",
            bracket, node->as.macro_call.name_len, node->as.macro_call.name);
    for (int i = 0; i < node->as.macro_call.arg_count; i++)
        dump_arg(node->as.macro_call.args[i], depth + 1);
    if (node->as.macro_call.body)
        dump_body(node->as.macro_call.body, depth + 1);
}

static void dump_child(const PdNode *node, int depth) {
    switch (node->type) {
    case NODE_TEXT:
        indent(depth);
        fprintf(stderr, "Text(\"%.*s\")\n",
                node->as.text.value_len, node->as.text.value);
        break;
    case NODE_ESCAPE:
        indent(depth);
        fprintf(stderr, "Escape(\"%.*s\")\n",
                node->as.text.value_len, node->as.text.value);
        break;
    case NODE_MACRO_CALL:
        dump_macro(node, depth);
        break;
    default:
        indent(depth);
        fprintf(stderr, "<%s>\n", node_type_name(node->type));
        break;
    }
}

static void dump_paragraph(const PdNode *node, int depth) {
    indent(depth);
    fputs("Paragraph\n", stderr);
    for (int i = 0; i < node->as.paragraph.count; i++)
        dump_child(node->as.paragraph.children[i], depth + 1);
}

static void dump_ast(const PdNode *doc) {
    indent(0);
    fputs("Document\n", stderr);
    for (int i = 0; i < doc->as.document.count; i++) {
        const PdNode *child = doc->as.document.children[i];
        if (child->type == NODE_MACRO_CALL)
            dump_macro(child, 1);
        else if (child->type == NODE_PARAGRAPH)
            dump_paragraph(child, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Head-item injection (port of inject.py)                            */
/* ------------------------------------------------------------------ */

static const Span CLI_SPAN = {
    .start = {.line = 0, .column = 0, .offset = 0},
    .end   = {.line = 0, .column = 0, .offset = 0},
};

/*
 * Build a NamedArg node: name=Text(value).
 * All strings are copied by the node constructors.
 */
static PdNode *make_named_arg(const char *name, const char *value) {
    PdNode *text = pd_node_text(value, (int)strlen(value), CLI_SPAN);
    return pd_node_named_arg(name, (int)strlen(name), CLI_SPAN,
                              text, CLI_SPAN);
}

/*
 * Prepend synthesized #doc.link, #doc.script, #doc.meta nodes to doc.
 * Modifies the document in place by rebuilding its children array.
 */
static void inject_head_items(PdNode *doc, const CliOptions *opts) {
    int inject_count = opts->css_count + opts->js_count + opts->meta_count;
    if (inject_count == 0) return;

    /* Build array of injected nodes */
    NodeArray items;
    node_array_init(&items);

    /* CSS: #doc.link rel=stylesheet href=<path> */
    for (int i = 0; i < opts->css_count; i++) {
        PdNode **args = malloc(2 * sizeof(PdNode *));
        args[0] = make_named_arg("rel", "stylesheet");
        args[1] = make_named_arg("href", opts->css_files[i]);
        PdNode *call = pd_node_macro_call("doc.link", 8, args, 2,
                                           NULL, true, CLI_SPAN);
        node_array_push(&items, call);
    }

    /* JS: #doc.script src=<path> */
    for (int i = 0; i < opts->js_count; i++) {
        PdNode **args = malloc(1 * sizeof(PdNode *));
        args[0] = make_named_arg("src", opts->js_files[i]);
        PdNode *call = pd_node_macro_call("doc.script", 10, args, 1,
                                           NULL, true, CLI_SPAN);
        node_array_push(&items, call);
    }

    /* Meta: #doc.meta name=<n> content=<v> */
    for (int i = 0; i < opts->meta_count; i++) {
        PdNode **args = malloc(2 * sizeof(PdNode *));
        args[0] = make_named_arg("name", opts->meta_names[i]);
        args[1] = make_named_arg("content", opts->meta_values[i]);
        PdNode *call = pd_node_macro_call("doc.meta", 8, args, 2,
                                           NULL, true, CLI_SPAN);
        node_array_push(&items, call);
    }

    /* Build new children: injected items + original children */
    int old_count = doc->as.document.count;
    int new_count = items.count + old_count;
    PdNode **new_children = malloc((size_t)new_count * sizeof(PdNode *));

    for (int i = 0; i < items.count; i++)
        new_children[i] = items.items[i];
    for (int i = 0; i < old_count; i++)
        new_children[items.count + i] = doc->as.document.children[i];

    free(doc->as.document.children);
    doc->as.document.children = new_children;
    doc->as.document.count = new_count;

    node_array_free_shallow(&items);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    CliOptions opts;
    cli_options_init(&opts);

    if (parse_args(argc, argv, &opts) < 0) {
        cli_options_free(&opts);
        return 2;
    }

    /* Read input file */
    char *source = NULL;
    size_t source_len = 0;
    if (read_file(opts.input_file, &source, &source_len) < 0) {
        cli_options_free(&opts);
        return 2;
    }

    PdError err;
    pd_error_init(&err);

    /* Step 1: Tokenize */
    TokenArray tokens;
    int rc = pd_tokenize(source, opts.input_file, &tokens, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        fprintf(stderr, "%s\n", fmt ? fmt : err.message);
        pd_error_free(&err);
        free(source);
        cli_options_free(&opts);
        return 1;
    }

    /* Step 2: Parse */
    PdNode *doc = NULL;
    rc = pd_parse(&tokens, source, opts.input_file, &doc, &err);
    token_array_free(&tokens);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        fprintf(stderr, "%s\n", fmt ? fmt : err.message);
        pd_error_free(&err);
        free(source);
        cli_options_free(&opts);
        return 1;
    }

    /* Step 3: Set up filter registry */
    PdFilterRegistry filter_reg;
    PdFilterRegistry *filters = NULL;
    {
        /* Get source directory from input file */
        const char *last_sep = strrchr(opts.input_file, '/');
        char *src_dir;
        if (last_sep) {
            size_t dlen = (size_t)(last_sep - opts.input_file);
            src_dir = malloc(dlen + 1);
            memcpy(src_dir, opts.input_file, dlen);
            src_dir[dlen] = '\0';
        } else {
            src_dir = strdup(".");
        }
        pd_filter_init(&filter_reg, src_dir);
        free(src_dir);
        if (opts.filter_timeout > 0)
            filter_reg.timeout = opts.filter_timeout;
        for (int i = 0; i < opts.filter_path_count; i++)
            pd_filter_add_path(&filter_reg, opts.filter_paths[i]);
        filters = &filter_reg;
    }

    /* Step 4: Evaluate */
    PdNode *expanded = NULL;
    rc = pd_evaluate(doc, opts.input_file, source,
                     (const char *const *)opts.env_keys,
                     (const char *const *)opts.env_vals,
                     opts.env_count,
                     filters,
                     &expanded, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        fprintf(stderr, "%s\n", fmt ? fmt : err.message);
        pd_node_free(doc);
        pd_filter_free(&filter_reg);
        pd_error_free(&err);
        free(source);
        cli_options_free(&opts);
        return 2;
    }
    pd_node_free(doc);

    /* Step 5: Debug dump (before injection, matching Python behavior) */
    if (opts.debug)
        dump_ast(expanded);

    /* Step 6: Inject head items (CSS, JS, meta) */
    inject_head_items(expanded, &opts);

    /* Step 7: Render */
    char *html = NULL;
    rc = pd_render(expanded, source, opts.input_file, &html, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        fprintf(stderr, "%s\n", fmt ? fmt : err.message);
        pd_node_free(expanded);
        pd_error_free(&err);
        free(source);
        cli_options_free(&opts);
        return 2;
    }
    pd_node_free(expanded);

    /* Step 8: Write output */
    if (opts.output_file) {
        FILE *f = fopen(opts.output_file, "w");
        if (!f) {
            fprintf(stderr, "error: cannot write '%s': %s\n",
                    opts.output_file, strerror(errno));
            free(html);
            free(source);
            cli_options_free(&opts);
            return 2;
        }
        fputs(html, f);
        fclose(f);
    } else {
        fputs(html, stdout);
    }

    /* Cleanup */
    pd_filter_free(&filter_reg);
    free(html);
    free(source);
    /* Free heap-allocated env keys and meta names */
    for (int i = 0; i < opts.env_count; i++)
        free((void *)opts.env_keys[i]);
    for (int i = 0; i < opts.meta_count; i++)
        free((void *)opts.meta_names[i]);
    cli_options_free(&opts);
    return 0;
}
