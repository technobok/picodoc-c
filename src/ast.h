#ifndef PD_AST_H
#define PD_AST_H

#include "tokens.h"
#include <stdbool.h>

/* AST node types — mirrors Python ast.py exactly. */
typedef enum {
    NODE_TEXT,
    NODE_ESCAPE,
    NODE_RAW_STRING,
    NODE_REQUIRED_MARKER,
    NODE_CODE_SECTION,
    NODE_INTERP_STRING,
    NODE_NAMED_ARG,
    NODE_BODY,
    NODE_MACRO_CALL,
    NODE_PARAGRAPH,
    NODE_DOCUMENT,
} NodeType;

typedef struct PdNode PdNode;

/*
 * Single tagged-union AST node.
 *
 * Strings are always owned copies (not views into the source buffer).
 * Child arrays are owned — pd_node_free() frees them recursively.
 */
struct PdNode {
    NodeType type;
    Span span;
    union {
        /* NODE_TEXT, NODE_ESCAPE, NODE_RAW_STRING */
        struct { char *value; int value_len; } text;

        /* NODE_REQUIRED_MARKER — no extra fields */

        /* NODE_CODE_SECTION */
        struct { PdNode **children; int count; } code_section;

        /* NODE_INTERP_STRING */
        struct { PdNode **parts; int count; } interp_string;

        /* NODE_NAMED_ARG */
        struct { char *name; int name_len; Span name_span; PdNode *value; } named_arg;

        /* NODE_BODY */
        struct { PdNode **children; int count; } body;

        /* NODE_MACRO_CALL */
        struct {
            char *name; int name_len;
            PdNode **args; int arg_count;
            PdNode *body;
            bool bracketed;
        } macro_call;

        /* NODE_PARAGRAPH */
        struct { PdNode **children; int count; } paragraph;

        /* NODE_DOCUMENT */
        struct { PdNode **children; int count; } document;
    } as;
};

/* Growable array of PdNode pointers — for building child arrays. */
typedef struct {
    PdNode **items;
    int count;
    int capacity;
} NodeArray;

void node_array_init(NodeArray *arr);
void node_array_push(NodeArray *arr, PdNode *node);
void node_array_free_shallow(NodeArray *arr);  /* frees array only, not nodes */
void node_array_free_deep(NodeArray *arr);     /* frees array and all nodes */

/* Constructors — always copy strings, take ownership of child arrays.
 * All return NULL on allocation failure. */
PdNode *pd_node_text(const char *value, int len, Span span);
PdNode *pd_node_escape(const char *value, int len, Span span);
PdNode *pd_node_raw_string(const char *value, int len, Span span);
PdNode *pd_node_required_marker(Span span);
PdNode *pd_node_code_section(PdNode **children, int count, Span span);
PdNode *pd_node_interp_string(PdNode **parts, int count, Span span);
PdNode *pd_node_named_arg(const char *name, int name_len, Span name_span,
                           PdNode *value, Span span);
PdNode *pd_node_body(PdNode **children, int count, Span span);
PdNode *pd_node_macro_call(const char *name, int name_len,
                            PdNode **args, int arg_count,
                            PdNode *body, bool bracketed, Span span);
PdNode *pd_node_paragraph(PdNode **children, int count, Span span);
PdNode *pd_node_document(PdNode **children, int count, Span span);

/* Recursive destructor — frees node and all children. */
void pd_node_free(PdNode *node);

/* Coalesce adjacent NODE_TEXT nodes in a NodeArray in-place. */
void coalesce_text(NodeArray *arr);

/* Human-readable name for a node type. */
const char *node_type_name(NodeType type);

#endif /* PD_AST_H */
