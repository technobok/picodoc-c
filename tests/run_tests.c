#include "unity.h"

/* Test suite runners — defined in each test_*.c file */
extern void run_test_tokens(void);
extern void run_test_strings(void);
extern void run_test_escapes(void);
extern void run_test_lexer(void);
extern void run_test_ast(void);
extern void run_test_parser(void);
extern void run_test_builtins(void);
extern void run_test_eval(void);

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    run_test_tokens();
    run_test_strings();
    run_test_escapes();
    run_test_lexer();
    run_test_ast();
    run_test_parser();
    run_test_builtins();
    run_test_eval();

    return UNITY_END();
}
