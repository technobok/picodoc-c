#include "unity.h"
#include "ast.h"
#include <string.h>
#include <stdlib.h>

static Span test_span(void) {
    return span_make(pos_make(1, 1, 0), pos_make(1, 5, 4));
}

/* --- Constructor smoke tests --- */

void test_ast_text(void) {
    PdNode *n = pd_node_text("hello", 5, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, n->type);
    TEST_ASSERT_EQUAL_INT(5, n->as.text.value_len);
    TEST_ASSERT_EQUAL_STRING("hello", n->as.text.value);
    pd_node_free(n);
}

void test_ast_escape(void) {
    PdNode *n = pd_node_escape("#", 1, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_ESCAPE, n->type);
    TEST_ASSERT_EQUAL_STRING("#", n->as.text.value);
    pd_node_free(n);
}

void test_ast_raw_string(void) {
    PdNode *n = pd_node_raw_string("raw", 3, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, n->type);
    TEST_ASSERT_EQUAL_STRING("raw", n->as.text.value);
    pd_node_free(n);
}

void test_ast_required_marker(void) {
    PdNode *n = pd_node_required_marker(test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER, n->type);
    pd_node_free(n);
}

void test_ast_code_section(void) {
    PdNode **kids = malloc(sizeof(PdNode *));
    kids[0] = pd_node_text("x", 1, test_span());
    PdNode *n = pd_node_code_section(kids, 1, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_CODE_SECTION, n->type);
    TEST_ASSERT_EQUAL_INT(1, n->as.code_section.count);
    pd_node_free(n);
}

void test_ast_interp_string(void) {
    PdNode **parts = malloc(sizeof(PdNode *));
    parts[0] = pd_node_text("hi", 2, test_span());
    PdNode *n = pd_node_interp_string(parts, 1, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, n->type);
    TEST_ASSERT_EQUAL_INT(1, n->as.interp_string.count);
    pd_node_free(n);
}

void test_ast_named_arg(void) {
    PdNode *val = pd_node_text("v", 1, test_span());
    PdNode *n = pd_node_named_arg("key", 3, test_span(), val, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_NAMED_ARG, n->type);
    TEST_ASSERT_EQUAL_STRING("key", n->as.named_arg.name);
    TEST_ASSERT_EQUAL_INT(3, n->as.named_arg.name_len);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, n->as.named_arg.value->type);
    pd_node_free(n);
}

void test_ast_body(void) {
    PdNode **kids = malloc(sizeof(PdNode *));
    kids[0] = pd_node_text("body", 4, test_span());
    PdNode *n = pd_node_body(kids, 1, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_BODY, n->type);
    TEST_ASSERT_EQUAL_INT(1, n->as.body.count);
    pd_node_free(n);
}

void test_ast_macro_call(void) {
    PdNode *body = pd_node_body(NULL, 0, test_span());
    PdNode *n = pd_node_macro_call("hr", 2, NULL, 0, body, false, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, n->type);
    TEST_ASSERT_EQUAL_STRING("hr", n->as.macro_call.name);
    TEST_ASSERT_FALSE(n->as.macro_call.bracketed);
    TEST_ASSERT_NOT_NULL(n->as.macro_call.body);
    pd_node_free(n);
}

void test_ast_paragraph(void) {
    PdNode **kids = malloc(sizeof(PdNode *));
    kids[0] = pd_node_text("para", 4, test_span());
    PdNode *n = pd_node_paragraph(kids, 1, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, n->type);
    TEST_ASSERT_EQUAL_INT(1, n->as.paragraph.count);
    pd_node_free(n);
}

void test_ast_document(void) {
    PdNode *n = pd_node_document(NULL, 0, test_span());
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(NODE_DOCUMENT, n->type);
    TEST_ASSERT_EQUAL_INT(0, n->as.document.count);
    pd_node_free(n);
}

/* --- Free on a small tree --- */

void test_ast_free_tree(void) {
    /* Build: Document -> Paragraph -> [Text, MacroCall(body=Body(Text))] */
    PdNode *inner_text = pd_node_text("bold", 4, test_span());
    PdNode **body_kids = malloc(sizeof(PdNode *));
    body_kids[0] = inner_text;
    PdNode *body = pd_node_body(body_kids, 1, test_span());

    PdNode *call = pd_node_macro_call("b", 1, NULL, 0, body, false, test_span());
    PdNode *text = pd_node_text("hello ", 6, test_span());

    PdNode **para_kids = malloc(2 * sizeof(PdNode *));
    para_kids[0] = text;
    para_kids[1] = call;
    PdNode *para = pd_node_paragraph(para_kids, 2, test_span());

    PdNode **doc_kids = malloc(sizeof(PdNode *));
    doc_kids[0] = para;
    PdNode *doc = pd_node_document(doc_kids, 1, test_span());

    /* Just verify it doesn't crash */
    pd_node_free(doc);
}

/* --- node_type_name --- */

void test_ast_node_type_names(void) {
    TEST_ASSERT_EQUAL_STRING("Text", node_type_name(NODE_TEXT));
    TEST_ASSERT_EQUAL_STRING("Escape", node_type_name(NODE_ESCAPE));
    TEST_ASSERT_EQUAL_STRING("RawString", node_type_name(NODE_RAW_STRING));
    TEST_ASSERT_EQUAL_STRING("RequiredMarker", node_type_name(NODE_REQUIRED_MARKER));
    TEST_ASSERT_EQUAL_STRING("CodeSection", node_type_name(NODE_CODE_SECTION));
    TEST_ASSERT_EQUAL_STRING("InterpString", node_type_name(NODE_INTERP_STRING));
    TEST_ASSERT_EQUAL_STRING("NamedArg", node_type_name(NODE_NAMED_ARG));
    TEST_ASSERT_EQUAL_STRING("Body", node_type_name(NODE_BODY));
    TEST_ASSERT_EQUAL_STRING("MacroCall", node_type_name(NODE_MACRO_CALL));
    TEST_ASSERT_EQUAL_STRING("Paragraph", node_type_name(NODE_PARAGRAPH));
    TEST_ASSERT_EQUAL_STRING("Document", node_type_name(NODE_DOCUMENT));
}

/* --- coalesce_text tests --- */

void test_coalesce_empty(void) {
    NodeArray arr;
    node_array_init(&arr);
    coalesce_text(&arr);
    TEST_ASSERT_EQUAL_INT(0, arr.count);
    node_array_free_shallow(&arr);
}

void test_coalesce_no_merge(void) {
    /* [Text, Escape, Text] — no adjacent texts */
    NodeArray arr;
    node_array_init(&arr);
    node_array_push(&arr, pd_node_text("a", 1, test_span()));
    node_array_push(&arr, pd_node_escape("#", 1, test_span()));
    node_array_push(&arr, pd_node_text("b", 1, test_span()));
    coalesce_text(&arr);
    TEST_ASSERT_EQUAL_INT(3, arr.count);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, arr.items[0]->type);
    TEST_ASSERT_EQUAL_INT(NODE_ESCAPE, arr.items[1]->type);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, arr.items[2]->type);
    node_array_free_deep(&arr);
}

void test_coalesce_two_adjacent(void) {
    Span s1 = span_make(pos_make(1, 1, 0), pos_make(1, 2, 1));
    Span s2 = span_make(pos_make(1, 2, 1), pos_make(1, 4, 3));
    NodeArray arr;
    node_array_init(&arr);
    node_array_push(&arr, pd_node_text("ab", 2, s1));
    node_array_push(&arr, pd_node_text("cd", 2, s2));
    coalesce_text(&arr);
    TEST_ASSERT_EQUAL_INT(1, arr.count);
    TEST_ASSERT_EQUAL_STRING("abcd", arr.items[0]->as.text.value);
    TEST_ASSERT_EQUAL_INT(4, arr.items[0]->as.text.value_len);
    /* Span covers both */
    TEST_ASSERT_EQUAL_INT(1, arr.items[0]->span.start.column);
    TEST_ASSERT_EQUAL_INT(4, arr.items[0]->span.end.column);
    node_array_free_deep(&arr);
}

void test_coalesce_non_adjacent(void) {
    /* [Text, MacroCall, Text] — texts separated by non-text */
    NodeArray arr;
    node_array_init(&arr);
    node_array_push(&arr, pd_node_text("a", 1, test_span()));
    node_array_push(&arr, pd_node_macro_call("b", 1, NULL, 0, NULL, false, test_span()));
    node_array_push(&arr, pd_node_text("c", 1, test_span()));
    coalesce_text(&arr);
    TEST_ASSERT_EQUAL_INT(3, arr.count);
    TEST_ASSERT_EQUAL_STRING("a", arr.items[0]->as.text.value);
    TEST_ASSERT_EQUAL_STRING("c", arr.items[2]->as.text.value);
    node_array_free_deep(&arr);
}

void test_coalesce_three_adjacent(void) {
    NodeArray arr;
    node_array_init(&arr);
    node_array_push(&arr, pd_node_text("x", 1, test_span()));
    node_array_push(&arr, pd_node_text("y", 1, test_span()));
    node_array_push(&arr, pd_node_text("z", 1, test_span()));
    coalesce_text(&arr);
    TEST_ASSERT_EQUAL_INT(1, arr.count);
    TEST_ASSERT_EQUAL_STRING("xyz", arr.items[0]->as.text.value);
    node_array_free_deep(&arr);
}

/* --- NodeArray push/grow --- */

void test_node_array_push_grow(void) {
    NodeArray arr;
    node_array_init(&arr);
    TEST_ASSERT_EQUAL_INT(0, arr.count);
    TEST_ASSERT_EQUAL_INT(0, arr.capacity);

    /* Push enough to trigger growth */
    for (int i = 0; i < 10; i++) {
        node_array_push(&arr, pd_node_text("x", 1, test_span()));
    }
    TEST_ASSERT_EQUAL_INT(10, arr.count);
    TEST_ASSERT_TRUE(arr.capacity >= 10);

    node_array_free_deep(&arr);
}

void run_test_ast(void) {
    RUN_TEST(test_ast_text);
    RUN_TEST(test_ast_escape);
    RUN_TEST(test_ast_raw_string);
    RUN_TEST(test_ast_required_marker);
    RUN_TEST(test_ast_code_section);
    RUN_TEST(test_ast_interp_string);
    RUN_TEST(test_ast_named_arg);
    RUN_TEST(test_ast_body);
    RUN_TEST(test_ast_macro_call);
    RUN_TEST(test_ast_paragraph);
    RUN_TEST(test_ast_document);
    RUN_TEST(test_ast_free_tree);
    RUN_TEST(test_ast_node_type_names);
    RUN_TEST(test_coalesce_empty);
    RUN_TEST(test_coalesce_no_merge);
    RUN_TEST(test_coalesce_two_adjacent);
    RUN_TEST(test_coalesce_non_adjacent);
    RUN_TEST(test_coalesce_three_adjacent);
    RUN_TEST(test_node_array_push_grow);
}
