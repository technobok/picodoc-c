#include "unity.h"
#include "strings.h"
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
}
