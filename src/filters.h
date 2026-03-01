#ifndef PD_FILTERS_H
#define PD_FILTERS_H

#include "errors.h"

typedef struct {
    char *source_dir;    /* directory of the input document */
    char **extra_paths;  /* NULL-terminated array of extra search dirs (heap) */
    int path_count;
    float timeout;       /* seconds, default 5.0 */
} PdFilterRegistry;

void pd_filter_init(PdFilterRegistry *reg, const char *source_dir);
void pd_filter_free(PdFilterRegistry *reg);
void pd_filter_add_path(PdFilterRegistry *reg, const char *path);

/* Find filter executable. Returns heap-allocated path or NULL. */
char *pd_filter_find(PdFilterRegistry *reg, const char *name);

/* Invoke filter. Returns heap-allocated markup string on success, NULL on error.
 * On error, fills err. */
char *pd_filter_invoke(PdFilterRegistry *reg, const char *name,
                       const char *filter_path,
                       const char *json_payload,
                       Span span, PdError *err);

#endif /* PD_FILTERS_H */
