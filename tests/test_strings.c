#include "unity.h"
#include "strings.h"
#include "ast.h"
#include "tokens.h"
#include <stdlib.h>
#include <string.h>

#define ASSERT_STRIP(input, expected) do { \
    char *result = strip_string_whitespace(input, (int)strlen(input)); \
    TEST_ASSERT_NOT_NULL(result); \
    TEST_ASSERT_EQUAL_STRING(expected, result); \
    free(result); \
} while (0)

void test_strip_empty(void) {
    ASSERT_STRIP("", "");
}

void test_strip_no_newlines(void) {
    ASSERT_STRIP("hello world", "hello world");
}

void test_strip_blank_first_line(void) {
    ASSERT_STRIP("\nhello", "hello");
}

void test_strip_blank_first_line_with_spaces(void) {
    ASSERT_STRIP("  \nhello", "hello");
}

void test_strip_blank_last_line_with_indent(void) {
    /* Content between triple quotes with indentation:
       """
           hello
           world
       """
       After splitting: ["", "    hello", "    world", "    "]
       First blank → discard. Last blank "    " → prefix.
       Strip "    " from lines → "hello\nworld"
    */
    ASSERT_STRIP("\n    hello\n    world\n    ", "hello\nworld");
}

void test_strip_preserves_relative_indent(void) {
    ASSERT_STRIP("\n    hello\n        world\n    ", "hello\n    world");
}

void test_strip_blank_interior_lines_lenient(void) {
    /* Blank interior lines don't prevent stripping */
    ASSERT_STRIP("\n    hello\n\n    world\n    ", "hello\n\nworld");
}

void test_strip_no_common_prefix(void) {
    ASSERT_STRIP("\nhello\nworld\n", "hello\nworld");
}

void test_strip_only_newlines(void) {
    ASSERT_STRIP("\n\n", "");
}

void test_strip_single_newline(void) {
    ASSERT_STRIP("\n", "");
}

void test_strip_tabs_as_indent(void) {
    ASSERT_STRIP("\n\thello\n\tworld\n\t", "hello\nworld");
}

void test_strip_mixed_content(void) {
    /* No blank first/last line → no stripping */
    ASSERT_STRIP("first\n  second\nthird", "first\n  second\nthird");
}

void test_strip_prefix_mismatch(void) {
    /* Last line indent doesn't match all lines → no stripping */
    ASSERT_STRIP("\nhello\n    world\n  ", "hello\n    world");
}

/* ================================================================== */
/* dedent_body_children tests                                          */
/* ================================================================== */

static Span dummy_span(void) {
    return span_make(pos_make(1, 1, 0), pos_make(1, 1, 0));
}

/* Helper: make a body-like array of TEXT nodes, call dedent, check result. */
#define ASSERT_DEDENT_TEXT(input, expected) do { \
    PdNode *n = pd_node_text(input, (int)strlen(input), dummy_span()); \
    TEST_ASSERT_NOT_NULL(n); \
    int rc = dedent_body_children(&n, 1); \
    TEST_ASSERT_EQUAL_INT(0, rc); \
    TEST_ASSERT_EQUAL_STRING(expected, n->as.text.value); \
    pd_node_free(n); \
} while (0)

void test_dedent_basic(void) {
    /* All lines have same indent → stripped to column 0 */
    ASSERT_DEDENT_TEXT("    hello\n    world", "hello\nworld");
}

void test_dedent_relative_indent_preserved(void) {
    /* 4-space and 8-space lines → 0 and 4 after strip */
    ASSERT_DEDENT_TEXT("    hello\n        world", "hello\n    world");
}

void test_dedent_no_common_prefix(void) {
    /* Lines at column 0 → no change */
    ASSERT_DEDENT_TEXT("hello\nworld", "hello\nworld");
}

void test_dedent_blank_interior_lines(void) {
    /* Blank interior line doesn't prevent stripping */
    ASSERT_DEDENT_TEXT("    hello\n\n    world", "hello\n\nworld");
}

void test_dedent_single_line(void) {
    /* Single line body → prefix stripped */
    ASSERT_DEDENT_TEXT("    hello", "hello");
}

void test_dedent_tabs(void) {
    ASSERT_DEDENT_TEXT("\thello\n\tworld", "hello\nworld");
}

void test_dedent_no_text_nodes(void) {
    /* Body with only non-TEXT nodes → no-op */
    PdNode *esc = pd_node_escape("#", 1, dummy_span());
    int rc = dedent_body_children(&esc, 1);
    TEST_ASSERT_EQUAL_INT(0, rc);
    pd_node_free(esc);
}

void test_dedent_mixed_content(void) {
    /* TEXT + ESCAPE + TEXT with indentation across nodes */
    PdNode *nodes[3];
    nodes[0] = pd_node_text("    Hello ", 10, dummy_span());
    nodes[1] = pd_node_escape("#", 1, dummy_span());
    nodes[2] = pd_node_text(" world\n    More text", 20, dummy_span());

    int rc = dedent_body_children(nodes, 3);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("Hello ", nodes[0]->as.text.value);
    TEST_ASSERT_EQUAL_STRING(" world\nMore text", nodes[2]->as.text.value);

    pd_node_free(nodes[0]);
    pd_node_free(nodes[1]);
    pd_node_free(nodes[2]);
}

void test_dedent_empty(void) {
    int rc = dedent_body_children(NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, rc);
}

void test_dedent_whitespace_only_lines(void) {
    /* Lines that are only whitespace → treated as blank, not stripped */
    ASSERT_DEDENT_TEXT("    hello\n  \n    world", "hello\n  \nworld");
}

void run_test_strings(void) {
    RUN_TEST(test_strip_empty);
    RUN_TEST(test_strip_no_newlines);
    RUN_TEST(test_strip_blank_first_line);
    RUN_TEST(test_strip_blank_first_line_with_spaces);
    RUN_TEST(test_strip_blank_last_line_with_indent);
    RUN_TEST(test_strip_preserves_relative_indent);
    RUN_TEST(test_strip_blank_interior_lines_lenient);
    RUN_TEST(test_strip_no_common_prefix);
    RUN_TEST(test_strip_only_newlines);
    RUN_TEST(test_strip_single_newline);
    RUN_TEST(test_strip_tabs_as_indent);
    RUN_TEST(test_strip_mixed_content);
    RUN_TEST(test_strip_prefix_mismatch);

    /* Body dedent */
    RUN_TEST(test_dedent_basic);
    RUN_TEST(test_dedent_relative_indent_preserved);
    RUN_TEST(test_dedent_no_common_prefix);
    RUN_TEST(test_dedent_blank_interior_lines);
    RUN_TEST(test_dedent_single_line);
    RUN_TEST(test_dedent_tabs);
    RUN_TEST(test_dedent_no_text_nodes);
    RUN_TEST(test_dedent_mixed_content);
    RUN_TEST(test_dedent_empty);
    RUN_TEST(test_dedent_whitespace_only_lines);
}
