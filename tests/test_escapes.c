#include "unity.h"
#include "tokens.h"
#include "lexer.h"
#include "errors.h"
#include <string.h>

/* Helper: tokenize and check first non-WS token */
static void assert_escape(const char *input, TokenType expected_type,
                           const char *expected_value, int expected_value_len,
                           const char *expected_raw) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(input, "test", &tokens, &err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, err.message ? err.message : "tokenize failed");

    /* Find the escape token */
    int found = 0;
    for (int i = 0; i < tokens.count; i++) {
        if (tokens.items[i].type == expected_type) {
            TEST_ASSERT_EQUAL_INT(expected_value_len, tokens.items[i].value_len);
            TEST_ASSERT_EQUAL_MEMORY(expected_value, tokens.items[i].value,
                                     (unsigned)expected_value_len);
            TEST_ASSERT_EQUAL_INT((int)strlen(expected_raw), tokens.items[i].raw_len);
            TEST_ASSERT_EQUAL_MEMORY(expected_raw, tokens.items[i].raw,
                                     (unsigned)tokens.items[i].raw_len);
            found = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "expected escape token not found");

    token_array_free(&tokens);
    pd_error_free(&err);
}

static void assert_lex_error(const char *input) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(input, "test", &tokens, &err);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_TRUE(err.kind != PD_ERR_NONE);

    pd_error_free(&err);
}

/* --- Prose escapes --- */

void test_escape_backslash(void) {
    assert_escape("\\\\", TOK_ESCAPE, "\\", 1, "\\\\");
}

void test_escape_hash(void) {
    assert_escape("\\#", TOK_ESCAPE, "#", 1, "\\#");
}

void test_escape_lbracket(void) {
    assert_escape("\\[", TOK_ESCAPE, "[", 1, "\\[");
}

void test_escape_rbracket(void) {
    assert_escape("\\]", TOK_ESCAPE, "]", 1, "\\]");
}

void test_escape_hex_ascii(void) {
    /* \x41 = 'A' */
    assert_escape("\\x41", TOK_ESCAPE, "A", 1, "\\x41");
}

void test_escape_hex_space(void) {
    /* \x20 = ' ' */
    assert_escape("\\x20", TOK_ESCAPE, " ", 1, "\\x20");
}

void test_escape_unicode_basic(void) {
    /* \U000000E9 = 'é' (U+00E9, 2 UTF-8 bytes: C3 A9) */
    assert_escape("\\U000000E9", TOK_ESCAPE, "\xC3\xA9", 2, "\\U000000E9");
}

void test_escape_unicode_emoji(void) {
    /* \U0001F600 = 😀 (U+1F600, 4 UTF-8 bytes: F0 9F 98 80) */
    assert_escape("\\U0001F600", TOK_ESCAPE, "\xF0\x9F\x98\x80", 4, "\\U0001F600");
}

/* --- Invalid prose escapes --- */

void test_escape_invalid_char(void) {
    assert_lex_error("\\n");  /* \n not valid in prose mode */
}

void test_escape_eof_after_backslash(void) {
    assert_lex_error("\\");
}

void test_escape_incomplete_hex(void) {
    assert_lex_error("\\x4");  /* only 1 hex digit */
}

void test_escape_invalid_hex_digit(void) {
    assert_lex_error("\\xGG");
}

void test_escape_incomplete_unicode(void) {
    assert_lex_error("\\U0001F6");  /* only 6 hex digits */
}

/* --- String escapes --- */

void test_string_escape_newline(void) {
    assert_escape("x=\"\\n\"", TOK_STRING_ESCAPE, "\n", 1, "\\n");
}

void test_string_escape_tab(void) {
    assert_escape("x=\"\\t\"", TOK_STRING_ESCAPE, "\t", 1, "\\t");
}

void test_string_escape_quote(void) {
    assert_escape("x=\"\\\"\"", TOK_STRING_ESCAPE, "\"", 1, "\\\"");
}

void test_string_escape_backslash(void) {
    assert_escape("x=\"\\\\\"", TOK_STRING_ESCAPE, "\\", 1, "\\\\");
}

void test_string_escape_hex(void) {
    assert_escape("x=\"\\x41\"", TOK_STRING_ESCAPE, "A", 1, "\\x41");
}

void test_string_escape_unicode(void) {
    assert_escape("x=\"\\U000000E9\"", TOK_STRING_ESCAPE, "\xC3\xA9", 2, "\\U000000E9");
}

/* --- Invalid string escapes --- */

void test_string_escape_invalid(void) {
    assert_lex_error("x=\"\\q\"");
}

void test_string_escape_eof(void) {
    assert_lex_error("x=\"\\");
}

void run_test_escapes(void) {
    RUN_TEST(test_escape_backslash);
    RUN_TEST(test_escape_hash);
    RUN_TEST(test_escape_lbracket);
    RUN_TEST(test_escape_rbracket);
    RUN_TEST(test_escape_hex_ascii);
    RUN_TEST(test_escape_hex_space);
    RUN_TEST(test_escape_unicode_basic);
    RUN_TEST(test_escape_unicode_emoji);
    RUN_TEST(test_escape_invalid_char);
    RUN_TEST(test_escape_eof_after_backslash);
    RUN_TEST(test_escape_incomplete_hex);
    RUN_TEST(test_escape_invalid_hex_digit);
    RUN_TEST(test_escape_incomplete_unicode);
    RUN_TEST(test_string_escape_newline);
    RUN_TEST(test_string_escape_tab);
    RUN_TEST(test_string_escape_quote);
    RUN_TEST(test_string_escape_backslash);
    RUN_TEST(test_string_escape_hex);
    RUN_TEST(test_string_escape_unicode);
    RUN_TEST(test_string_escape_invalid);
    RUN_TEST(test_string_escape_eof);
}
