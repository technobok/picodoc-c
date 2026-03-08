#include "unity.h"
#include "tokens.h"
#include "lexer.h"
#include "errors.h"
#include <string.h>
#include <stdlib.h>

/* Helper: tokenize and return token array. Fails test on error. */
static TokenArray tokenize_ok(const char *input) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(input, "test", &tokens, &err);
    if (rc < 0) {
        char *formatted = pd_format_error(&err);
        TEST_FAIL_MESSAGE(formatted ? formatted : err.message);
    }
    return tokens;
}

/* Helper: assert token at index has expected type and value. */
static void assert_tok(TokenArray *tokens, int idx, TokenType type,
                       const char *value) {
    TEST_ASSERT_TRUE_MESSAGE(idx < tokens->count, "token index out of range");
    TEST_ASSERT_EQUAL_INT_MESSAGE(type, tokens->items[idx].type,
                                  token_type_name(type));
    int vlen = (int)strlen(value);
    TEST_ASSERT_EQUAL_INT(vlen, tokens->items[idx].value_len);
    if (vlen > 0) {
        TEST_ASSERT_EQUAL_MEMORY(value, tokens->items[idx].value, (unsigned)vlen);
    }
}

/* --- Basic structural tokens --- */

void test_lex_hash(void) {
    TokenArray t = tokenize_ok("#");
    assert_tok(&t, 0, TOK_HASH, "#");
    assert_tok(&t, 1, TOK_EOF, "");
    TEST_ASSERT_EQUAL_INT(2, t.count);
    token_array_free(&t);
}

void test_lex_brackets(void) {
    TokenArray t = tokenize_ok("[]");
    assert_tok(&t, 0, TOK_LBRACKET, "[");
    assert_tok(&t, 1, TOK_RBRACKET, "]");
    assert_tok(&t, 2, TOK_EOF, "");
    token_array_free(&t);
}

void test_lex_structural(void) {
    TokenArray t = tokenize_ok(":=?");
    assert_tok(&t, 0, TOK_COLON, ":");
    assert_tok(&t, 1, TOK_EQUALS, "=");
    assert_tok(&t, 2, TOK_QUESTION, "?");
    token_array_free(&t);
}

/* --- Whitespace and newlines --- */

void test_lex_whitespace(void) {
    TokenArray t = tokenize_ok("  \t ");
    assert_tok(&t, 0, TOK_WS, "  \t ");
    token_array_free(&t);
}

void test_lex_newline(void) {
    TokenArray t = tokenize_ok("\n");
    assert_tok(&t, 0, TOK_NEWLINE, "\n");
    token_array_free(&t);
}

void test_lex_crlf(void) {
    TokenArray t = tokenize_ok("\r\n");
    assert_tok(&t, 0, TOK_NEWLINE, "\n");
    /* raw should be \r\n */
    TEST_ASSERT_EQUAL_INT(2, t.items[0].raw_len);
    TEST_ASSERT_EQUAL_MEMORY("\r\n", t.items[0].raw, 2);
    token_array_free(&t);
}

/* --- Identifiers and text --- */

void test_lex_identifier(void) {
    TokenArray t = tokenize_ok("hello");
    assert_tok(&t, 0, TOK_IDENTIFIER, "hello");
    token_array_free(&t);
}

void test_lex_identifier_with_specials(void) {
    TokenArray t = tokenize_ok("my-macro!");
    assert_tok(&t, 0, TOK_IDENTIFIER, "my-macro!");
    token_array_free(&t);
}

void test_lex_text(void) {
    TokenArray t = tokenize_ok("(hello)");
    assert_tok(&t, 0, TOK_TEXT, "(");
    assert_tok(&t, 1, TOK_IDENTIFIER, "hello");
    assert_tok(&t, 2, TOK_TEXT, ")");
    token_array_free(&t);
}

/* --- Macro call pattern --- */

void test_lex_macro_call(void) {
    TokenArray t = tokenize_ok("#heading[Hello World]");
    assert_tok(&t, 0, TOK_HASH, "#");
    assert_tok(&t, 1, TOK_IDENTIFIER, "heading");
    assert_tok(&t, 2, TOK_LBRACKET, "[");
    assert_tok(&t, 3, TOK_IDENTIFIER, "Hello");
    assert_tok(&t, 4, TOK_WS, " ");
    assert_tok(&t, 5, TOK_IDENTIFIER, "World");
    assert_tok(&t, 6, TOK_RBRACKET, "]");
    assert_tok(&t, 7, TOK_EOF, "");
    token_array_free(&t);
}

/* --- Interpreted strings --- */

void test_lex_empty_string(void) {
    /* x="" — string after EQUALS */
    TokenArray t = tokenize_ok("x=\"\"");
    assert_tok(&t, 2, TOK_STRING_START, "\"");
    assert_tok(&t, 3, TOK_STRING_END, "\"");
    token_array_free(&t);
}

void test_lex_interp_string(void) {
    /* x="hello" — string after EQUALS */
    TokenArray t = tokenize_ok("x=\"hello\"");
    assert_tok(&t, 2, TOK_STRING_START, "\"");
    assert_tok(&t, 3, TOK_STRING_TEXT, "hello");
    assert_tok(&t, 4, TOK_STRING_END, "\"");
    token_array_free(&t);
}

void test_lex_string_with_escape(void) {
    /* x="hello\nworld" */
    TokenArray t = tokenize_ok("x=\"hello\\nworld\"");
    assert_tok(&t, 2, TOK_STRING_START, "\"");
    assert_tok(&t, 3, TOK_STRING_TEXT, "hello");
    assert_tok(&t, 4, TOK_STRING_ESCAPE, "\n");
    assert_tok(&t, 5, TOK_STRING_TEXT, "world");
    assert_tok(&t, 6, TOK_STRING_END, "\"");
    token_array_free(&t);
}

void test_lex_string_code_mode(void) {
    /* x="value: \[x]" */
    TokenArray t = tokenize_ok("x=\"value: \\[x]\"");
    assert_tok(&t, 2, TOK_STRING_START, "\"");
    assert_tok(&t, 3, TOK_STRING_TEXT, "value: ");
    assert_tok(&t, 4, TOK_CODE_OPEN, "\\[");
    assert_tok(&t, 5, TOK_IDENTIFIER, "x");
    assert_tok(&t, 6, TOK_CODE_CLOSE, "]");
    assert_tok(&t, 7, TOK_STRING_END, "\"");
    token_array_free(&t);
}

void test_lex_string_nested_brackets(void) {
    /* x="\[a[b]]" — nested brackets in code mode */
    TokenArray t = tokenize_ok("x=\"\\[a[b]]\"");
    assert_tok(&t, 2, TOK_STRING_START, "\"");
    assert_tok(&t, 3, TOK_CODE_OPEN, "\\[");
    assert_tok(&t, 4, TOK_IDENTIFIER, "a");
    assert_tok(&t, 5, TOK_LBRACKET, "[");
    assert_tok(&t, 6, TOK_IDENTIFIER, "b");
    assert_tok(&t, 7, TOK_RBRACKET, "]");
    assert_tok(&t, 8, TOK_CODE_CLOSE, "]");
    assert_tok(&t, 9, TOK_STRING_END, "\"");
    token_array_free(&t);
}

/* --- Raw strings --- */

void test_lex_raw_string(void) {
    /* x="""hello""" */
    TokenArray t = tokenize_ok("x=\"\"\"hello\"\"\"");
    assert_tok(&t, 2, TOK_RAW_STRING, "hello");
    token_array_free(&t);
}

void test_lex_raw_string_multiline(void) {
    /* x="""\n    hello\n    world\n    """ */
    TokenArray t = tokenize_ok("x=\"\"\"\n    hello\n    world\n    \"\"\"");
    assert_tok(&t, 2, TOK_RAW_STRING, "hello\nworld");
    token_array_free(&t);
}

/* --- Position tracking --- */

void test_lex_positions(void) {
    TokenArray t = tokenize_ok("#a");
    /* # at line 1, col 1, offset 0 */
    TEST_ASSERT_EQUAL_INT(1, t.items[0].span.start.line);
    TEST_ASSERT_EQUAL_INT(1, t.items[0].span.start.column);
    TEST_ASSERT_EQUAL_INT(0, t.items[0].span.start.offset);
    /* a at line 1, col 2, offset 1 */
    TEST_ASSERT_EQUAL_INT(1, t.items[1].span.start.line);
    TEST_ASSERT_EQUAL_INT(2, t.items[1].span.start.column);
    TEST_ASSERT_EQUAL_INT(1, t.items[1].span.start.offset);
    token_array_free(&t);
}

void test_lex_multiline_positions(void) {
    TokenArray t = tokenize_ok("a\nb");
    /* a at line 1 */
    TEST_ASSERT_EQUAL_INT(1, t.items[0].span.start.line);
    /* \n at line 1 (it's the last char on line 1) */
    TEST_ASSERT_EQUAL_INT(1, t.items[1].span.start.line);
    /* b at line 2, col 1 */
    TEST_ASSERT_EQUAL_INT(2, t.items[2].span.start.line);
    TEST_ASSERT_EQUAL_INT(1, t.items[2].span.start.column);
    token_array_free(&t);
}

/* --- Error cases --- */

void test_lex_unterminated_string(void) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);
    int rc = pd_tokenize("x=\"hello", "test", &tokens, &err);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_INT(PD_ERR_LEX, err.kind);
    pd_error_free(&err);
}

void test_lex_adjacent_strings(void) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);
    int rc = pd_tokenize("x=\"a\"\"b\"", "test", &tokens, &err);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    pd_error_free(&err);
}

void test_lex_nul_char(void) {
    /* Source with embedded NUL — we need to be careful with strlen.
       Actually our tokenizer uses strlen for source_len, so NUL would
       terminate early. The Python version checks for \0 explicitly.
       This test verifies the source ends before any NUL is processed. */
    /* Can't test embedded NUL with C strings (strlen stops at \0).
       The Python version checks for \0 explicitly. Skipped. */
}

/* --- Complex inputs --- */

void test_lex_full_document(void) {
    const char *input =
        "#title[My Document]\n"
        "\n"
        "Hello, world!\n";
    TokenArray t = tokenize_ok(input);

    assert_tok(&t, 0, TOK_HASH, "#");
    assert_tok(&t, 1, TOK_IDENTIFIER, "title");
    assert_tok(&t, 2, TOK_LBRACKET, "[");
    assert_tok(&t, 3, TOK_IDENTIFIER, "My");
    assert_tok(&t, 4, TOK_WS, " ");
    assert_tok(&t, 5, TOK_IDENTIFIER, "Document");
    assert_tok(&t, 6, TOK_RBRACKET, "]");
    assert_tok(&t, 7, TOK_NEWLINE, "\n");
    assert_tok(&t, 8, TOK_NEWLINE, "\n");
    assert_tok(&t, 9, TOK_IDENTIFIER, "Hello");
    /* , is TEXT */
    assert_tok(&t, 10, TOK_TEXT, ",");
    assert_tok(&t, 11, TOK_WS, " ");
    /* ! is an ident char, so world! is one identifier */
    assert_tok(&t, 12, TOK_IDENTIFIER, "world!");

    token_array_free(&t);
}

void run_test_lexer(void) {
    RUN_TEST(test_lex_hash);
    RUN_TEST(test_lex_brackets);
    RUN_TEST(test_lex_structural);
    RUN_TEST(test_lex_whitespace);
    RUN_TEST(test_lex_newline);
    RUN_TEST(test_lex_crlf);
    RUN_TEST(test_lex_identifier);
    RUN_TEST(test_lex_identifier_with_specials);
    RUN_TEST(test_lex_text);
    RUN_TEST(test_lex_macro_call);
    RUN_TEST(test_lex_empty_string);
    RUN_TEST(test_lex_interp_string);
    RUN_TEST(test_lex_string_with_escape);
    RUN_TEST(test_lex_string_code_mode);
    RUN_TEST(test_lex_string_nested_brackets);
    RUN_TEST(test_lex_raw_string);
    RUN_TEST(test_lex_raw_string_multiline);
    RUN_TEST(test_lex_positions);
    RUN_TEST(test_lex_multiline_positions);
    RUN_TEST(test_lex_unterminated_string);
    RUN_TEST(test_lex_adjacent_strings);
    RUN_TEST(test_lex_nul_char);
    RUN_TEST(test_lex_full_document);
}
