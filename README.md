# picodoc-c

C port of the PicoDoc document compiler. Compiles `.pdoc` markup to HTML,
ported from the Python reference implementation.

## Documentation

The documentation is written in PicoDoc.

- [PicoDoc Reference (Rendered)](https://technobok.github.io/picodoc-c/reference.html).
- [PicoDoc Reference (Source)](https://technobok.github.io/picodoc-c/reference-source.html).
- [PicoDoc Tutorial (Rendered)](https://technobok.github.io/picodoc-c/tutorial.html).
- [PicoDoc Tutorial (Source)](https://technobok.github.io/picodoc-c/tutorial-source.html).

## Status

All components implemented: lexer, parser, AST, builtins, evaluator,
renderer, CLI, filters, and LSP server. 364 tests passing.

Documentation (reference and tutorial) builds from `.pdoc` sources in `docs/`.

## Building

```
make            # build the picodoc binary
make picodoc-lsp  # build the LSP server
make test       # build and run the test suite
make clean      # remove build artifacts
```

Requires a C11 compiler (cc).

## LSP server

`picodoc-lsp` is a Language Server Protocol server for PicoDoc, providing
diagnostics, hover, go-to-definition, and completion in any LSP-capable editor.

Build it with `make picodoc-lsp`.

### Neovim

Add the bundled editor support to your runtimepath and configure the LSP:

```lua
vim.opt.runtimepath:append("/path/to/picodoc-c/editor/nvim")

vim.lsp.config("picodoc", {
  cmd = { "/path/to/picodoc-c/picodoc-lsp" },
  filetypes = { "picodoc" },
})
vim.lsp.enable("picodoc")
```

The `editor/nvim` directory provides filetype detection, syntax highlighting,
and filetype settings for `.pdoc` files.

### VS Code

Add to `.vscode/settings.json` (requires a generic LSP client extension):

```json
{
  "lsp.server.picodoc": {
    "command": "/path/to/picodoc-c/picodoc-lsp",
    "filetypes": ["picodoc"]
  }
}
```

### Features

- **Diagnostics**: lex/parse errors (Error severity) and eval errors (Warning)
  on every save/change.
- **Hover**: shows builtin info (parameters, aliases, body) or user-defined
  macro info (parameters, definition location).
- **Go-to-definition**: jumps to the `#set` that defines a user macro.
- **Completion**: lists all builtins, aliases, and user-defined macros after `#`.

## Project structure

```
src/        lexer, parser, AST, builtins, evaluator, renderer, CLI, filters,
            tokens, strings, errors, LSP server
lib/        vendored dependencies — bstrlib, utf8.h, stb_ds.h, cJSON
tests/      test suite (Unity framework, also vendored)
editor/     editor support (Neovim: ftdetect, ftplugin, syntax)
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
| `lsp.c` | LSP server implementation |
| `filters.c` | External filter protocol |
| `tokens.c` | Token types and helpers |
| `strings.c` | String utilities |
| `errors.c` | Error types and formatting |

## Dependencies

All vendored in the repo, no external dependencies needed:

- [bstrlib](https://github.com/websnarf/bstrlib) — better string library
- [utf8.h](https://github.com/sheredom/utf8.h) — single-header UTF-8 support
- [stb_ds.h](https://github.com/nothings/stb) — dynamic arrays and hash maps
- [cJSON](https://github.com/DaveGamble/cJSON) — JSON parser/generator (used by LSP server)
- [Unity](https://github.com/ThrowTheSwitch/Unity) — C test framework (in `tests/`)
