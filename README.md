# picodoc-c

C port of the PicoDoc document compiler. Compiles `.pdoc` markup to HTML,
ported from the Python reference implementation.

## Status

Work in progress. The lexer is complete (69 tests passing). Parser, evaluator,
and HTML renderer are not yet implemented.

## Building

```
make            # build the picodoc binary
make test       # build and run the test suite
make clean      # remove build artifacts
```

Requires a C11 compiler (cc).

## Project structure

```
src/        tokens, strings, errors, lexer (and eventually parser/eval/render)
lib/        vendored dependencies — bstrlib, utf8.h, stb_ds.h
tests/      test suite (Unity framework, also vendored)
examples/   sample .pdoc documents with expected HTML output
```

## Dependencies

All vendored in the repo, no external dependencies needed:

- [bstrlib](https://github.com/websnarf/bstrlib) — better string library
- [utf8.h](https://github.com/sheredom/utf8.h) — single-header UTF-8 support
- [stb_ds.h](https://github.com/nothings/stb) — dynamic arrays and hash maps
- [Unity](https://github.com/ThrowTheSwitch/Unity) — C test framework (in `tests/`)
