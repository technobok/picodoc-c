#include "unity.h"
#include "builtins.h"
#include <string.h>

/* --- Alias resolution --- */

void test_alias_dash_h1(void) {
    TEST_ASSERT_EQUAL_STRING("h1", resolve_alias("-"));
}

void test_alias_dashes(void) {
    TEST_ASSERT_EQUAL_STRING("h2", resolve_alias("--"));
    TEST_ASSERT_EQUAL_STRING("h3", resolve_alias("---"));
    TEST_ASSERT_EQUAL_STRING("h4", resolve_alias("----"));
    TEST_ASSERT_EQUAL_STRING("h5", resolve_alias("-----"));
    TEST_ASSERT_EQUAL_STRING("h6", resolve_alias("------"));
}

void test_alias_formatting(void) {
    TEST_ASSERT_EQUAL_STRING("b", resolve_alias("**"));
    TEST_ASSERT_EQUAL_STRING("i", resolve_alias("__"));
}

void test_alias_misc(void) {
    TEST_ASSERT_EQUAL_STRING("comment", resolve_alias("//"));
    TEST_ASSERT_EQUAL_STRING("link", resolve_alias(">"));
    TEST_ASSERT_EQUAL_STRING("*", resolve_alias("li"));
}

void test_alias_unknown_passthrough(void) {
    /* Non-alias names should be returned unchanged (same pointer). */
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
    const BuiltinDef *b = find_builtin("link");
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(1, b->param_count);
    TEST_ASSERT_EQUAL_STRING("to", b->params[0].name);
    TEST_ASSERT_TRUE(b->params[0].required);
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
    TEST_ASSERT_FALSE(is_wrapper_tag("h1"));
    TEST_ASSERT_FALSE(is_wrapper_tag("table"));
    TEST_ASSERT_FALSE(is_wrapper_tag("b"));
    TEST_ASSERT_FALSE(is_wrapper_tag("nonexistent"));
}

/* --- is_block_macro --- */

void test_is_block_macro_positive(void) {
    TEST_ASSERT_TRUE(is_block_macro("h1"));
    TEST_ASSERT_TRUE(is_block_macro("h6"));
    TEST_ASSERT_TRUE(is_block_macro("p"));
    TEST_ASSERT_TRUE(is_block_macro("hr"));
    TEST_ASSERT_TRUE(is_block_macro("ul"));
    TEST_ASSERT_TRUE(is_block_macro("ol"));
    TEST_ASSERT_TRUE(is_block_macro("table"));
    TEST_ASSERT_TRUE(is_block_macro("div"));
    TEST_ASSERT_TRUE(is_block_macro("code"));
}

void test_is_block_macro_negative(void) {
    TEST_ASSERT_FALSE(is_block_macro("b"));
    TEST_ASSERT_FALSE(is_block_macro("i"));
    TEST_ASSERT_FALSE(is_block_macro("link"));
    TEST_ASSERT_FALSE(is_block_macro("~"));
    TEST_ASSERT_FALSE(is_block_macro("*"));
    TEST_ASSERT_FALSE(is_block_macro("nonexistent"));
}

void run_test_builtins(void) {
    RUN_TEST(test_alias_dash_h1);
    RUN_TEST(test_alias_dashes);
    RUN_TEST(test_alias_formatting);
    RUN_TEST(test_alias_misc);
    RUN_TEST(test_alias_unknown_passthrough);
    RUN_TEST(test_find_builtin_known);
    RUN_TEST(test_find_builtin_unknown);
    RUN_TEST(test_find_builtin_link_params);
    RUN_TEST(test_find_builtin_set_params);
    RUN_TEST(test_find_builtin_div_params);
    RUN_TEST(test_find_builtin_ifeq_params);
    RUN_TEST(test_is_wrapper_tag_positive);
    RUN_TEST(test_is_wrapper_tag_negative);
    RUN_TEST(test_is_block_macro_positive);
    RUN_TEST(test_is_block_macro_negative);
}
