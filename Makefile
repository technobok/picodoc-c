CC       = cc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -Ilib -Isrc
LDFLAGS  =

SRC      = src/tokens.c src/strings.c src/errors.c src/lexer.c \
           src/ast.c src/parser.c src/builtins.c src/eval.c src/render.c \
           src/filters.c lib/bstrlib.c
OBJ      = $(SRC:.c=.o)

TEST_SRC = tests/unity.c tests/test_tokens.c tests/test_strings.c \
           tests/test_escapes.c tests/test_lexer.c tests/test_ast.c \
           tests/test_parser.c tests/test_builtins.c tests/test_eval.c \
           tests/test_render.c tests/run_tests.c
TEST_OBJ = $(TEST_SRC:.c=.o)

# Core library objects (everything except cli.o)
LIB_OBJ  = $(filter-out src/cli.o,$(OBJ))

picodoc: $(OBJ) src/cli.o
	$(CC) $(LDFLAGS) -o $@ $^

test: $(TEST_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o run_tests $^
	./run_tests

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -Itests -c -o $@ $<

clean:
	rm -f $(OBJ) $(TEST_OBJ) src/cli.o picodoc run_tests

.PHONY: test clean
