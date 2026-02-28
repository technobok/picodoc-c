#include "unity.h"
#include "tokens.h"

/* --- is_ident_char --- */

void test_ident_letters(void) {
    TEST_ASSERT_TRUE(is_ident_char('a'));
    TEST_ASSERT_TRUE(is_ident_char('z'));
    TEST_ASSERT_TRUE(is_ident_char('A'));
    TEST_ASSERT_TRUE(is_ident_char('Z'));
    TEST_ASSERT_TRUE(is_ident_char('m'));
}

void test_ident_digits(void) {
    TEST_ASSERT_TRUE(is_ident_char('0'));
    TEST_ASSERT_TRUE(is_ident_char('9'));
    TEST_ASSERT_TRUE(is_ident_char('5'));
}

void test_ident_dot(void) {
    TEST_ASSERT_TRUE(is_ident_char('.'));
}

void test_ident_special_chars(void) {
    const char *specials = "!$%&*+-/<>@^_~|";
    for (const char *p = specials; *p; p++) {
        TEST_ASSERT_TRUE_MESSAGE(is_ident_char(*p), "expected ident char");
    }
}

void test_non_ident_chars(void) {
    TEST_ASSERT_FALSE(is_ident_char(' '));
    TEST_ASSERT_FALSE(is_ident_char('\t'));
    TEST_ASSERT_FALSE(is_ident_char('\n'));
    TEST_ASSERT_FALSE(is_ident_char('#'));
    TEST_ASSERT_FALSE(is_ident_char('['));
    TEST_ASSERT_FALSE(is_ident_char(']'));
    TEST_ASSERT_FALSE(is_ident_char(':'));
    TEST_ASSERT_FALSE(is_ident_char('='));
    TEST_ASSERT_FALSE(is_ident_char('?'));
    TEST_ASSERT_FALSE(is_ident_char('"'));
    TEST_ASSERT_FALSE(is_ident_char('\\'));
    TEST_ASSERT_FALSE(is_ident_char('('));
    TEST_ASSERT_FALSE(is_ident_char(')'));
    TEST_ASSERT_FALSE(is_ident_char('{'));
    TEST_ASSERT_FALSE(is_ident_char('}'));
    TEST_ASSERT_FALSE(is_ident_char(','));
    TEST_ASSERT_FALSE(is_ident_char(';'));
    TEST_ASSERT_FALSE(is_ident_char('\''));
}

/* --- is_hex_digit --- */

void test_hex_digits(void) {
    for (char ch = '0'; ch <= '9'; ch++) {
        TEST_ASSERT_TRUE(is_hex_digit(ch));
    }
    for (char ch = 'a'; ch <= 'f'; ch++) {
        TEST_ASSERT_TRUE(is_hex_digit(ch));
    }
    for (char ch = 'A'; ch <= 'F'; ch++) {
        TEST_ASSERT_TRUE(is_hex_digit(ch));
    }
}

void test_non_hex_digits(void) {
    TEST_ASSERT_FALSE(is_hex_digit('g'));
    TEST_ASSERT_FALSE(is_hex_digit('G'));
    TEST_ASSERT_FALSE(is_hex_digit('z'));
    TEST_ASSERT_FALSE(is_hex_digit(' '));
    TEST_ASSERT_FALSE(is_hex_digit('#'));
    TEST_ASSERT_FALSE(is_hex_digit('\0'));
}

/* --- Position and Span --- */

void test_position_make(void) {
    Position p = pos_make(10, 5, 42);
    TEST_ASSERT_EQUAL_INT(10, p.line);
    TEST_ASSERT_EQUAL_INT(5, p.column);
    TEST_ASSERT_EQUAL_INT(42, p.offset);
}

void test_span_make(void) {
    Position start = pos_make(1, 1, 0);
    Position end = pos_make(1, 5, 4);
    Span s = span_make(start, end);
    TEST_ASSERT_EQUAL_INT(1, s.start.line);
    TEST_ASSERT_EQUAL_INT(1, s.start.column);
    TEST_ASSERT_EQUAL_INT(0, s.start.offset);
    TEST_ASSERT_EQUAL_INT(1, s.end.line);
    TEST_ASSERT_EQUAL_INT(5, s.end.column);
    TEST_ASSERT_EQUAL_INT(4, s.end.offset);
}

/* --- Token type names --- */

void test_token_type_names(void) {
    TEST_ASSERT_EQUAL_STRING("HASH", token_type_name(TOK_HASH));
    TEST_ASSERT_EQUAL_STRING("IDENTIFIER", token_type_name(TOK_IDENTIFIER));
    TEST_ASSERT_EQUAL_STRING("EOF", token_type_name(TOK_EOF));
    TEST_ASSERT_EQUAL_STRING("RAW_STRING", token_type_name(TOK_RAW_STRING));
    TEST_ASSERT_EQUAL_STRING("NEWLINE", token_type_name(TOK_NEWLINE));
}

/* --- TokenArray --- */

void test_token_array_push(void) {
    TokenArray arr;
    token_array_init(&arr);
    TEST_ASSERT_EQUAL_INT(0, arr.count);

    Token tok = {.type = TOK_HASH, .value = "#", .value_len = 1,
                 .raw = "#", .raw_len = 1};
    token_array_push(&arr, tok);
    TEST_ASSERT_EQUAL_INT(1, arr.count);
    TEST_ASSERT_EQUAL_INT(TOK_HASH, arr.items[0].type);

    token_array_free(&arr);
    TEST_ASSERT_EQUAL_INT(0, arr.count);
    TEST_ASSERT_NULL(arr.items);
}

void test_token_array_grows(void) {
    TokenArray arr;
    token_array_init(&arr);

    for (int i = 0; i < 200; i++) {
        Token tok = {.type = TOK_TEXT, .value = "x", .value_len = 1,
                     .raw = "x", .raw_len = 1};
        token_array_push(&arr, tok);
    }
    TEST_ASSERT_EQUAL_INT(200, arr.count);
    TEST_ASSERT_TRUE(arr.capacity >= 200);

    token_array_free(&arr);
}

void run_test_tokens(void) {
    RUN_TEST(test_ident_letters);
    RUN_TEST(test_ident_digits);
    RUN_TEST(test_ident_dot);
    RUN_TEST(test_ident_special_chars);
    RUN_TEST(test_non_ident_chars);
    RUN_TEST(test_hex_digits);
    RUN_TEST(test_non_hex_digits);
    RUN_TEST(test_position_make);
    RUN_TEST(test_span_make);
    RUN_TEST(test_token_type_names);
    RUN_TEST(test_token_array_push);
    RUN_TEST(test_token_array_grows);
}
