#include "unity.h"
#include "builtins.h"
#include <string.h>

/* --- Alternate form resolution --- */

void test_alternate_h1_to_dash(void) {
    TEST_ASSERT_EQUAL_STRING("-", resolve_alias("h1"));
}

void test_alternate_headings(void) {
    TEST_ASSERT_EQUAL_STRING("--", resolve_alias("h2"));
    TEST_ASSERT_EQUAL_STRING("---", resolve_alias("h3"));
    TEST_ASSERT_EQUAL_STRING("----", resolve_alias("h4"));
    TEST_ASSERT_EQUAL_STRING("-----", resolve_alias("h5"));
    TEST_ASSERT_EQUAL_STRING("------", resolve_alias("h6"));
}

void test_alternate_formatting(void) {
    TEST_ASSERT_EQUAL_STRING("**", resolve_alias("b"));
    TEST_ASSERT_EQUAL_STRING("__", resolve_alias("i"));
}

void test_alternate_misc(void) {
    TEST_ASSERT_EQUAL_STRING("//", resolve_alias("comment"));
    TEST_ASSERT_EQUAL_STRING(">", resolve_alias("link"));
    TEST_ASSERT_EQUAL_STRING("*", resolve_alias("li"));
}

void test_alternate_unknown_passthrough(void) {
    /* Non-alternate names should be returned unchanged (same pointer). */
    const char *name = "p";
    TEST_ASSERT_EQUAL_PTR(name, resolve_alias(name));
}

/* --- find_builtin --- */

void test_find_builtin_known(void) {
    const BuiltinDef *b = find_builtin("p");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_STRING("p", b->name);
    TEST_ASSERT_TRUE(b->has_body);
    TEST_ASSERT_EQUAL_INT(0, b->param_count);
}

void test_find_builtin_unknown(void) {
    TEST_ASSERT_NULL(find_builtin("nonexistent"));
}

void test_find_builtin_link_params(void) {
    const BuiltinDef *b = find_builtin(">");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(1, b->param_count);
    TEST_ASSERT_EQUAL_STRING("to", b->params[0].name);
    TEST_ASSERT_FALSE(b->params[0].required);
    TEST_ASSERT_TRUE(b->has_body);
}

void test_find_builtin_set_params(void) {
    const BuiltinDef *b = find_builtin("set");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(1, b->param_count);
    TEST_ASSERT_EQUAL_STRING("name", b->params[0].name);
    TEST_ASSERT_TRUE(b->params[0].required);
}

void test_find_builtin_div_params(void) {
    const BuiltinDef *b = find_builtin("div");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(2, b->param_count);
    TEST_ASSERT_EQUAL_STRING("class", b->params[0].name);
    TEST_ASSERT_FALSE(b->params[0].required);
    TEST_ASSERT_EQUAL_STRING("id", b->params[1].name);
    TEST_ASSERT_FALSE(b->params[1].required);
}

void test_find_builtin_ifeq_params(void) {
    const BuiltinDef *b = find_builtin("ifeq");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(2, b->param_count);
    TEST_ASSERT_EQUAL_STRING("lhs", b->params[0].name);
    TEST_ASSERT_TRUE(b->params[0].required);
    TEST_ASSERT_EQUAL_STRING("rhs", b->params[1].name);
    TEST_ASSERT_TRUE(b->params[1].required);
}

void test_find_builtin_bold_italic(void) {
    const BuiltinDef *bi = find_builtin("*_");
    TEST_ASSERT_NOT_NULL(bi);
    TEST_ASSERT_TRUE(bi->has_body);
    TEST_ASSERT_EQUAL_INT(0, bi->param_count);

    const BuiltinDef *ib = find_builtin("_*");
    TEST_ASSERT_NOT_NULL(ib);
    TEST_ASSERT_TRUE(ib->has_body);
    TEST_ASSERT_EQUAL_INT(0, ib->param_count);
}

/* --- is_wrapper_tag --- */

void test_is_wrapper_tag_positive(void) {
    TEST_ASSERT_TRUE(is_wrapper_tag("div"));
    TEST_ASSERT_TRUE(is_wrapper_tag("section"));
    TEST_ASSERT_TRUE(is_wrapper_tag("span"));
    TEST_ASSERT_TRUE(is_wrapper_tag("nav"));
    TEST_ASSERT_TRUE(is_wrapper_tag("header"));
    TEST_ASSERT_TRUE(is_wrapper_tag("footer"));
    TEST_ASSERT_TRUE(is_wrapper_tag("main"));
    TEST_ASSERT_TRUE(is_wrapper_tag("article"));
    TEST_ASSERT_TRUE(is_wrapper_tag("aside"));
}

void test_is_wrapper_tag_negative(void) {
    TEST_ASSERT_FALSE(is_wrapper_tag("p"));
    TEST_ASSERT_FALSE(is_wrapper_tag("-"));
    TEST_ASSERT_FALSE(is_wrapper_tag("table"));
    TEST_ASSERT_FALSE(is_wrapper_tag("**"));
    TEST_ASSERT_FALSE(is_wrapper_tag("nonexistent"));
}

/* --- is_block_macro --- */

void test_is_block_macro_positive(void) {
    TEST_ASSERT_TRUE(is_block_macro("-"));
    TEST_ASSERT_TRUE(is_block_macro("------"));
    TEST_ASSERT_TRUE(is_block_macro("p"));
    TEST_ASSERT_TRUE(is_block_macro("hr"));
    TEST_ASSERT_TRUE(is_block_macro("ul"));
    TEST_ASSERT_TRUE(is_block_macro("ol"));
    TEST_ASSERT_TRUE(is_block_macro("table"));
    TEST_ASSERT_TRUE(is_block_macro("div"));
    TEST_ASSERT_TRUE(is_block_macro("code"));
}

void test_is_block_macro_negative(void) {
    TEST_ASSERT_FALSE(is_block_macro("**"));
    TEST_ASSERT_FALSE(is_block_macro("__"));
    TEST_ASSERT_FALSE(is_block_macro(">"));
    TEST_ASSERT_FALSE(is_block_macro("~"));
    TEST_ASSERT_FALSE(is_block_macro("*"));
    TEST_ASSERT_FALSE(is_block_macro("nonexistent"));
}

void run_test_builtins(void) {
    RUN_TEST(test_alternate_h1_to_dash);
    RUN_TEST(test_alternate_headings);
    RUN_TEST(test_alternate_formatting);
    RUN_TEST(test_alternate_misc);
    RUN_TEST(test_alternate_unknown_passthrough);
    RUN_TEST(test_find_builtin_known);
    RUN_TEST(test_find_builtin_unknown);
    RUN_TEST(test_find_builtin_link_params);
    RUN_TEST(test_find_builtin_set_params);
    RUN_TEST(test_find_builtin_div_params);
    RUN_TEST(test_find_builtin_ifeq_params);
    RUN_TEST(test_find_builtin_bold_italic);
    RUN_TEST(test_is_wrapper_tag_positive);
    RUN_TEST(test_is_wrapper_tag_negative);
    RUN_TEST(test_is_block_macro_positive);
    RUN_TEST(test_is_block_macro_negative);
}
