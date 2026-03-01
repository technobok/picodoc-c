"""Python ctypes bindings for picodoc-c."""

from __future__ import annotations

import ctypes
import ctypes.util
import os
from pathlib import Path

__all__ = [
    "compile",
    "PicoDocError",
    "LexError",
    "ParseError",
    "EvalError",
    "RenderError",
]


# ---------------------------------------------------------------------------
# Error hierarchy
# ---------------------------------------------------------------------------

class PicoDocError(Exception):
    """Base class for picodoc compilation errors."""


class LexError(PicoDocError):
    """Tokenization error."""


class ParseError(PicoDocError):
    """Parse error."""


class EvalError(PicoDocError):
    """Evaluation/macro-expansion error."""


class RenderError(PicoDocError):
    """HTML rendering error."""


_ERR_CLASSES: dict[int, type[PicoDocError]] = {
    1: LexError,
    2: ParseError,
    3: EvalError,
    4: RenderError,
}


# ---------------------------------------------------------------------------
# Library loading (lazy singleton)
# ---------------------------------------------------------------------------

_lib: ctypes.CDLL | None = None


def _find_library() -> str:
    # 1. PICODOC_LIB environment variable
    env = os.environ.get("PICODOC_LIB")
    if env:
        return env

    # 2. Relative to this package (../../libpicodoc.so)
    pkg_dir = Path(__file__).resolve().parent
    relative = pkg_dir.parent.parent / "libpicodoc.so"
    if relative.is_file():
        return str(relative)

    # 3. System library path
    found = ctypes.util.find_library("picodoc")
    if found:
        return found

    raise OSError(
        "Cannot find libpicodoc.so. "
        "Set PICODOC_LIB or run 'make libpicodoc.so' in the picodoc-c directory."
    )


def _get_lib() -> ctypes.CDLL:
    global _lib
    if _lib is None:
        path = _find_library()
        _lib = ctypes.CDLL(path)

        # char *pd_compile(const char *source, const char *filename,
        #                  const char *const *env_keys,
        #                  const char *const *env_vals,
        #                  int env_count,
        #                  int *error_kind, char **error_msg)
        _lib.pd_compile.argtypes = [
            ctypes.c_char_p,                          # source
            ctypes.c_char_p,                          # filename
            ctypes.POINTER(ctypes.c_char_p),          # env_keys
            ctypes.POINTER(ctypes.c_char_p),          # env_vals
            ctypes.c_int,                             # env_count
            ctypes.POINTER(ctypes.c_int),             # error_kind
            ctypes.POINTER(ctypes.c_char_p),          # error_msg
        ]
        _lib.pd_compile.restype = ctypes.c_void_p  # avoid auto-free

        # void pd_free_string(char *s)
        _lib.pd_free_string.argtypes = [ctypes.c_void_p]
        _lib.pd_free_string.restype = None

    return _lib


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def compile(
    source: str,
    filename: str = "input.pdoc",
    env: dict[str, str] | None = None,
) -> str:
    """Compile PicoDoc source to HTML.

    Args:
        source: PicoDoc markup string.
        filename: Filename used in error messages.
        env: Optional dict of environment variables accessible via ``#env.*``.

    Returns:
        HTML string.

    Raises:
        LexError: Tokenization failed.
        ParseError: Parsing failed.
        EvalError: Macro evaluation failed.
        RenderError: HTML rendering failed.
        PicoDocError: Other compilation error.
    """
    lib = _get_lib()

    src_bytes = source.encode("utf-8")
    fname_bytes = filename.encode("utf-8")

    # Build env arrays
    if env:
        keys_list = list(env.keys())
        vals_list = list(env.values())
        n = len(keys_list)
        KeysArray = ctypes.c_char_p * n
        ValsArray = ctypes.c_char_p * n
        env_keys = KeysArray(*(k.encode("utf-8") for k in keys_list))
        env_vals = ValsArray(*(v.encode("utf-8") for v in vals_list))
    else:
        n = 0
        env_keys = None
        env_vals = None

    error_kind = ctypes.c_int(0)
    error_msg = ctypes.c_char_p(None)

    result_ptr = lib.pd_compile(
        src_bytes,
        fname_bytes,
        env_keys,
        env_vals,
        n,
        ctypes.byref(error_kind),
        ctypes.byref(error_msg),
    )

    if result_ptr is None:
        # Error path — extract message, free C string, raise
        kind = error_kind.value
        msg_ptr = error_msg.value
        msg = msg_ptr.decode("utf-8", errors="replace") if msg_ptr else "unknown error"
        if error_msg.value is not None:
            # error_msg is a c_char_p; its .value was already copied to Python str.
            # We need to free the original C pointer. Cast back to void* for pd_free_string.
            lib.pd_free_string(ctypes.cast(error_msg, ctypes.c_void_p).value)
        exc_class = _ERR_CLASSES.get(kind, PicoDocError)
        raise exc_class(msg)

    # Success — copy result to Python str, free C string
    html_bytes = ctypes.string_at(result_ptr)
    lib.pd_free_string(result_ptr)
    return html_bytes.decode("utf-8")
