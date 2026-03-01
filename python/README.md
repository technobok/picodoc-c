# picodoc-c Python bindings

Python ctypes wrapper around `libpicodoc.so`. No compiled extensions or
third-party dependencies — just the standard library `ctypes` module.

## Prerequisites

Build the shared library from the picodoc-c project root:

```bash
make libpicodoc.so
```

This produces `libpicodoc.so` in the project root.

## Installation

**Option A — use directly from the repo** (recommended for development):

```python
import sys
sys.path.insert(0, "path/to/picodoc-c/python")
from picodoc import compile
```

The library is found automatically via the relative path
`python/picodoc/../../libpicodoc.so`.

**Option B — pip install** (editable):

```bash
cd picodoc-c/python
pip install -e .
```

Then point to the shared library with the `PICODOC_LIB` environment variable:

```bash
export PICODOC_LIB=/path/to/picodoc-c/libpicodoc.so
```

## Library search order

1. `PICODOC_LIB` environment variable (explicit path)
2. `../../libpicodoc.so` relative to the package (works in-tree)
3. System library path (`ldconfig` / `LD_LIBRARY_PATH`)

## Usage

### Basic compilation

```python
from picodoc import compile

html = compile("#-: Hello, world!")
print(html)
```

Output:

```html
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
</head>
<body>
<h1 id="hello-world">Hello, world!</h1>
</body>
</html>
```

### Environment variables

Pass a dict of variables accessible in the document via `#env.<name>`:

```python
source = """\
#set mode = draft

#ifeq mode draft:
  #p: This is a DRAFT.

#ifeq mode final:
  #p: This is the final version.
"""

html = compile(source, env={"mode": "draft"})
```

### Custom filename for error messages

The `filename` parameter appears in error diagnostics:

```python
html = compile(source, filename="chapter1.pdoc")
```

### Error handling

Compilation errors raise typed exceptions with formatted diagnostic messages:

```python
from picodoc import compile, LexError, ParseError, EvalError, RenderError

try:
    compile('#p: [#b "unterminated string]')
except LexError as e:
    print(e)
    # error: unterminated string literal
    #   --> input.pdoc:1:6
    #   |
    # 1 | #p: [#b "unterminated string]
    #   |         ^
```

All error classes inherit from `PicoDocError`, so you can catch broadly:

```python
from picodoc import compile, PicoDocError

try:
    html = compile(source)
except PicoDocError as e:
    print(f"Compilation failed: {e}")
```

### Error class hierarchy

| Exception | Raised when |
|-----------|------------|
| `PicoDocError` | Base class for all compilation errors |
| `LexError` | Tokenization fails (unterminated strings, invalid escapes) |
| `ParseError` | Parsing fails (unexpected tokens, missing brackets) |
| `EvalError` | Macro evaluation fails (undefined variables, missing arguments) |
| `RenderError` | HTML rendering fails |

## API reference

### `compile(source, filename="input.pdoc", env=None) -> str`

Compile PicoDoc markup to an HTML string.

**Parameters:**

- `source` (`str`) — PicoDoc markup.
- `filename` (`str`) — Filename shown in error messages. Default: `"input.pdoc"`.
- `env` (`dict[str, str] | None`) — Environment variables accessible via `#env.*` in the document.

**Returns:** HTML string.

**Raises:** `LexError`, `ParseError`, `EvalError`, `RenderError`, or `PicoDocError`.

## Requirements

- Python >= 3.10
- `libpicodoc.so` built from the picodoc-c project
