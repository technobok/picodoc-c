#ifndef PD_FFI_H
#define PD_FFI_H

/* Error kind constants (matches PdErrorKind). */
#define PD_FFI_ERR_NONE   0
#define PD_FFI_ERR_LEX    1
#define PD_FFI_ERR_PARSE  2
#define PD_FFI_ERR_EVAL   3
#define PD_FFI_ERR_RENDER 4

/* Compile PicoDoc source to HTML.
 * Returns heap-allocated HTML on success (caller must pd_free_string).
 * Returns NULL on error with *error_kind and *error_msg set.
 * error_msg is heap-allocated (caller must pd_free_string). */
char *pd_compile(const char *source, const char *filename,
                 const char *const *env_keys, const char *const *env_vals,
                 int env_count,
                 int *error_kind, char **error_msg);

/* Free a string returned by pd_compile or error_msg. Safe with NULL. */
void pd_free_string(char *s);

#endif /* PD_FFI_H */
