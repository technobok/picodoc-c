#include "ast.h"
#include <stdlib.h>
#include <string.h>

/* --- NodeArray --- */

void node_array_init(NodeArray *arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void node_array_push(NodeArray *arr, PdNode *node) {
    if (arr->count >= arr->capacity) {
        int new_cap = arr->capacity == 0 ? 4 : arr->capacity * 2;
        PdNode **new_items = realloc(arr->items, (size_t)new_cap * sizeof(PdNode *));
        if (!new_items) return; /* allocation failure — silent */
        arr->items = new_items;
        arr->capacity = new_cap;
    }
    arr->items[arr->count++] = node;
}

void node_array_free_shallow(NodeArray *arr) {
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void node_array_free_deep(NodeArray *arr) {
    for (int i = 0; i < arr->count; i++) {
        pd_node_free(arr->items[i]);
    }
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

/* --- Internal helpers --- */

static PdNode *alloc_node(NodeType type, Span span) {
    PdNode *n = calloc(1, sizeof(PdNode));
    if (!n) return NULL;
    n->type = type;
    n->span = span;
    return n;
}

static char *copy_str(const char *src, int len) {
    char *dst = malloc((size_t)len + 1);
    if (!dst) return NULL;
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    return dst;
}

/* --- Constructors --- */

PdNode *pd_node_text(const char *value, int len, Span span) {
    PdNode *n = alloc_node(NODE_TEXT, span);
    if (!n) return NULL;
    n->as.text.value = copy_str(value, len);
    n->as.text.value_len = len;
    if (!n->as.text.value) { free(n); return NULL; }
    return n;
}

PdNode *pd_node_escape(const char *value, int len, Span span) {
    PdNode *n = alloc_node(NODE_ESCAPE, span);
    if (!n) return NULL;
    n->as.text.value = copy_str(value, len);
    n->as.text.value_len = len;
    if (!n->as.text.value) { free(n); return NULL; }
    return n;
}

PdNode *pd_node_raw_string(const char *value, int len, Span span) {
    PdNode *n = alloc_node(NODE_RAW_STRING, span);
    if (!n) return NULL;
    n->as.text.value = copy_str(value, len);
    n->as.text.value_len = len;
    if (!n->as.text.value) { free(n); return NULL; }
    return n;
}

PdNode *pd_node_required_marker(Span span) {
    return alloc_node(NODE_REQUIRED_MARKER, span);
}

PdNode *pd_node_code_section(PdNode **children, int count, Span span) {
    PdNode *n = alloc_node(NODE_CODE_SECTION, span);
    if (!n) return NULL;
    n->as.code_section.children = children;
    n->as.code_section.count = count;
    return n;
}

PdNode *pd_node_interp_string(PdNode **parts, int count, Span span) {
    PdNode *n = alloc_node(NODE_INTERP_STRING, span);
    if (!n) return NULL;
    n->as.interp_string.parts = parts;
    n->as.interp_string.count = count;
    return n;
}

PdNode *pd_node_named_arg(const char *name, int name_len, Span name_span,
                           PdNode *value, Span span) {
    PdNode *n = alloc_node(NODE_NAMED_ARG, span);
    if (!n) return NULL;
    n->as.named_arg.name = copy_str(name, name_len);
    n->as.named_arg.name_len = name_len;
    n->as.named_arg.name_span = name_span;
    n->as.named_arg.value = value;
    if (!n->as.named_arg.name) { free(n); return NULL; }
    return n;
}

PdNode *pd_node_body(PdNode **children, int count, Span span) {
    PdNode *n = alloc_node(NODE_BODY, span);
    if (!n) return NULL;
    n->as.body.children = children;
    n->as.body.count = count;
    return n;
}

PdNode *pd_node_macro_call(const char *name, int name_len,
                            PdNode **args, int arg_count,
                            PdNode *body, bool bracketed, Span span) {
    PdNode *n = alloc_node(NODE_MACRO_CALL, span);
    if (!n) return NULL;
    n->as.macro_call.name = copy_str(name, name_len);
    n->as.macro_call.name_len = name_len;
    n->as.macro_call.args = args;
    n->as.macro_call.arg_count = arg_count;
    n->as.macro_call.body = body;
    n->as.macro_call.bracketed = bracketed;
    if (!n->as.macro_call.name) { free(n); return NULL; }
    return n;
}

PdNode *pd_node_paragraph(PdNode **children, int count, Span span) {
    PdNode *n = alloc_node(NODE_PARAGRAPH, span);
    if (!n) return NULL;
    n->as.paragraph.children = children;
    n->as.paragraph.count = count;
    return n;
}

PdNode *pd_node_document(PdNode **children, int count, Span span) {
    PdNode *n = alloc_node(NODE_DOCUMENT, span);
    if (!n) return NULL;
    n->as.document.children = children;
    n->as.document.count = count;
    return n;
}

/* --- Destructor --- */

static void free_child_array(PdNode **children, int count) {
    if (!children) return;
    for (int i = 0; i < count; i++) {
        pd_node_free(children[i]);
    }
    free(children);
}

void pd_node_free(PdNode *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_TEXT:
    case NODE_ESCAPE:
    case NODE_RAW_STRING:
        free(node->as.text.value);
        break;

    case NODE_REQUIRED_MARKER:
        break;

    case NODE_CODE_SECTION:
        free_child_array(node->as.code_section.children,
                         node->as.code_section.count);
        break;

    case NODE_INTERP_STRING:
        free_child_array(node->as.interp_string.parts,
                         node->as.interp_string.count);
        break;

    case NODE_NAMED_ARG:
        free(node->as.named_arg.name);
        pd_node_free(node->as.named_arg.value);
        break;

    case NODE_BODY:
        free_child_array(node->as.body.children, node->as.body.count);
        break;

    case NODE_MACRO_CALL:
        free(node->as.macro_call.name);
        free_child_array(node->as.macro_call.args,
                         node->as.macro_call.arg_count);
        pd_node_free(node->as.macro_call.body);
        break;

    case NODE_PARAGRAPH:
        free_child_array(node->as.paragraph.children,
                         node->as.paragraph.count);
        break;

    case NODE_DOCUMENT:
        free_child_array(node->as.document.children,
                         node->as.document.count);
        break;
    }

    free(node);
}

/* --- coalesce_text --- */

void coalesce_text(NodeArray *arr) {
    if (arr->count <= 1) return;

    int write = 0;
    for (int read = 0; read < arr->count; read++) {
        PdNode *cur = arr->items[read];
        if (cur->type == NODE_TEXT && write > 0 &&
            arr->items[write - 1]->type == NODE_TEXT) {
            /* Merge cur into prev */
            PdNode *prev = arr->items[write - 1];
            int new_len = prev->as.text.value_len + cur->as.text.value_len;
            char *merged = malloc((size_t)new_len + 1);
            if (merged) {
                memcpy(merged, prev->as.text.value,
                       (size_t)prev->as.text.value_len);
                memcpy(merged + prev->as.text.value_len,
                       cur->as.text.value, (size_t)cur->as.text.value_len);
                merged[new_len] = '\0';
                free(prev->as.text.value);
                prev->as.text.value = merged;
                prev->as.text.value_len = new_len;
                prev->span.end = cur->span.end;
            }
            pd_node_free(cur);
        } else {
            arr->items[write++] = cur;
        }
    }
    arr->count = write;
}

/* --- node_type_name --- */

const char *node_type_name(NodeType type) {
    switch (type) {
    case NODE_TEXT:            return "Text";
    case NODE_ESCAPE:         return "Escape";
    case NODE_RAW_STRING:     return "RawString";
    case NODE_REQUIRED_MARKER:return "RequiredMarker";
    case NODE_CODE_SECTION:   return "CodeSection";
    case NODE_INTERP_STRING:  return "InterpString";
    case NODE_NAMED_ARG:      return "NamedArg";
    case NODE_BODY:           return "Body";
    case NODE_MACRO_CALL:     return "MacroCall";
    case NODE_PARAGRAPH:      return "Paragraph";
    case NODE_DOCUMENT:       return "Document";
    }
    return "Unknown";
}
