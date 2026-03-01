CC       = cc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -Ilib -Isrc
LDFLAGS  =

SRC      = src/tokens.c src/strings.c src/errors.c src/lexer.c \
           src/ast.c src/parser.c src/builtins.c src/eval.c src/render.c \
           src/filters.c lib/bstrlib.c lib/cJSON.c
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

libpicodoc.so: $(SRC) src/ffi.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^ -lm

picodoc-lsp: $(LIB_OBJ) src/lsp.o src/lsp_main.o
	$(CC) $(LDFLAGS) -o $@ $^ -lm

test: $(TEST_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o run_tests $^
	./run_tests

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -Itests -c -o $@ $<

docs: picodoc
	$(MAKE) -C docs

clean:
	rm -f $(OBJ) $(TEST_OBJ) src/cli.o src/lsp.o src/lsp_main.o picodoc picodoc-lsp run_tests libpicodoc.so
	$(MAKE) -C docs clean

.PHONY: test docs clean
