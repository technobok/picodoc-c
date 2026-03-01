# picodoc-c

C port of the PicoDoc document compiler. Compiles `.pdoc` markup to HTML,
ported from the Python reference implementation.

## Documentation

The documentation is written in PicoDoc.

- [PicoDoc Reference (Rendered)](https://technobok.github.io/picodoc-c/reference.html).
- [PicoDoc Reference (Source)](https://technobok.github.io/picodoc-c/reference.pdoc).
- [PicoDoc Tutorial (Rendered)](https://technobok.github.io/picodoc-c/tutorial.html).
- [PicoDoc Tutorial (Source)](https://technobok.github.io/picodoc-c/tutorial.pdoc).

## Status

All components implemented: lexer, parser, AST, builtins, evaluator,
renderer, CLI, and filters. 364 tests passing.

Documentation (reference and tutorial) builds from `.pdoc` sources in `docs/`.

## Building

```
make            # build the picodoc binary
make test       # build and run the test suite
make clean      # remove build artifacts
```

Requires a C11 compiler (cc).

## Project structure

```
src/        lexer, parser, AST, builtins, evaluator, renderer, CLI, filters,
            tokens, strings, errors
lib/        vendored dependencies — bstrlib, utf8.h, stb_ds.h
tests/      test suite (Unity framework, also vendored)
docs/       reference and tutorial (.pdoc sources, style assets, generated HTML)
examples/   sample .pdoc documents with expected HTML output
```

Source files in `src/`:

| File | Purpose |
|------|---------|
| `lexer.c` | Tokenization |
| `parser.c` | Token stream to AST |
| `ast.c` | AST node definitions |
| `builtins.c` | Built-in macro registry |
| `eval.c` | Multi-pass macro expansion |
| `render.c` | AST to HTML |
| `cli.c` | Command-line interface |
| `filters.c` | External filter protocol |
| `tokens.c` | Token types and helpers |
| `strings.c` | String utilities |
| `errors.c` | Error types and formatting |

## Dependencies

All vendored in the repo, no external dependencies needed:

- [bstrlib](https://github.com/websnarf/bstrlib) — better string library
- [utf8.h](https://github.com/sheredom/utf8.h) — single-header UTF-8 support
- [stb_ds.h](https://github.com/nothings/stb) — dynamic arrays and hash maps
- [Unity](https://github.com/ThrowTheSwitch/Unity) — C test framework (in `tests/`)
