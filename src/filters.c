#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "filters.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void pd_filter_init(PdFilterRegistry *reg, const char *source_dir) {
    memset(reg, 0, sizeof(*reg));
    reg->source_dir = strdup(source_dir);
    reg->timeout = 5.0f;
}

void pd_filter_free(PdFilterRegistry *reg) {
    free(reg->source_dir);
    for (int i = 0; i < reg->path_count; i++)
        free(reg->extra_paths[i]);
    free(reg->extra_paths);
    memset(reg, 0, sizeof(*reg));
}

void pd_filter_add_path(PdFilterRegistry *reg, const char *path) {
    reg->extra_paths = realloc(reg->extra_paths,
                               (size_t)(reg->path_count + 1) * sizeof(char *));
    reg->extra_paths[reg->path_count] = strdup(path);
    reg->path_count++;
}

/* Check if path exists and is executable. */
static int is_executable(const char *path) {
    return access(path, X_OK) == 0;
}

char *pd_filter_find(PdFilterRegistry *reg, const char *name) {
    char buf[1024];

    /* 1. <source_dir>/filters/<name> */
    snprintf(buf, sizeof(buf), "%s/filters/%s", reg->source_dir, name);
    if (is_executable(buf))
        return strdup(buf);

    /* 2. Extra configured paths */
    for (int i = 0; i < reg->path_count; i++) {
        snprintf(buf, sizeof(buf), "%s/%s", reg->extra_paths[i], name);
        if (is_executable(buf))
            return strdup(buf);
    }

    /* 3. picodoc-<name> on $PATH */
    const char *path_env = getenv("PATH");
    if (path_env) {
        char prefixed[256];
        snprintf(prefixed, sizeof(prefixed), "picodoc-%s", name);

        const char *p = path_env;
        while (*p) {
            const char *colon = strchr(p, ':');
            size_t dir_len = colon ? (size_t)(colon - p) : strlen(p);
            if (dir_len > 0 && dir_len + strlen(prefixed) + 2 < sizeof(buf)) {
                memcpy(buf, p, dir_len);
                buf[dir_len] = '/';
                strcpy(buf + dir_len + 1, prefixed);
                if (is_executable(buf))
                    return strdup(buf);
            }
            if (!colon) break;
            p = colon + 1;
        }
    }

    return NULL;
}

char *pd_filter_invoke(PdFilterRegistry *reg, const char *name,
                       const char *filter_path,
                       const char *json_payload,
                       Span span, PdError *err) {
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        pd_eval_error(err, "filter pipe creation failed", span, "", "");
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        pd_eval_error(err, "filter fork failed", span, "", "");
        return NULL;
    }

    if (pid == 0) {
        /* Child */
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        char *argv[] = {(char *)filter_path, NULL};
        execv(filter_path, argv);
        _exit(127);
    }

    /* Parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Write JSON payload to stdin */
    if (json_payload) {
        size_t len = strlen(json_payload);
        ssize_t written = write(stdin_pipe[1], json_payload, len);
        (void)written;
    }
    close(stdin_pipe[1]);

    /* Read stdout and stderr with timeout using poll */
    char *out_buf = NULL;
    size_t out_len = 0, out_cap = 0;
    char *err_buf = NULL;
    size_t err_len = 0, err_cap = 0;

    int timeout_ms = (int)(reg->timeout * 1000.0f);
    struct pollfd fds[2];
    fds[0].fd = stdout_pipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = stderr_pipe[0];
    fds[1].events = POLLIN;

    int open_fds = 2;
    int timed_out = 0;

    while (open_fds > 0) {
        int ret = poll(fds, 2, timeout_ms);
        if (ret == 0) {
            timed_out = 1;
            break;
        }
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < 2; i++) {
            if (fds[i].fd < 0) continue;
            if (fds[i].revents & (POLLIN | POLLHUP)) {
                char tmp[4096];
                ssize_t n = read(fds[i].fd, tmp, sizeof(tmp));
                if (n > 0) {
                    char **buf_p = (i == 0) ? &out_buf : &err_buf;
                    size_t *len_p = (i == 0) ? &out_len : &err_len;
                    size_t *cap_p = (i == 0) ? &out_cap : &err_cap;
                    if (*len_p + (size_t)n >= *cap_p) {
                        *cap_p = (*len_p + (size_t)n) * 2 + 64;
                        *buf_p = realloc(*buf_p, *cap_p);
                    }
                    memcpy(*buf_p + *len_p, tmp, (size_t)n);
                    *len_p += (size_t)n;
                } else {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    open_fds--;
                }
            }
        }
    }

    /* Close any remaining fds */
    if (fds[0].fd >= 0) close(fds[0].fd);
    if (fds[1].fd >= 0) close(fds[1].fd);

    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        free(out_buf);
        free(err_buf);
        char msg[256];
        snprintf(msg, sizeof(msg), "filter '%s' timed out after %.1fs",
                 name, (double)reg->timeout);
        err->kind = PD_ERR_EVAL;
        err->detail = strdup(msg);
        err->message = err->detail;
        err->span = span;
        return NULL;
    }

    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        /* NUL-terminate stderr */
        if (err_buf) {
            err_buf = realloc(err_buf, err_len + 1);
            err_buf[err_len] = '\0';
            /* Trim trailing whitespace */
            while (err_len > 0 && (err_buf[err_len-1] == '\n' ||
                                   err_buf[err_len-1] == '\r' ||
                                   err_buf[err_len-1] == ' '))
                err_buf[--err_len] = '\0';
        }
        char msg[512];
        if (err_buf && err_len > 0)
            snprintf(msg, sizeof(msg), "filter '%s' failed (exit %d): %s",
                     name, code, err_buf);
        else
            snprintf(msg, sizeof(msg), "filter '%s' failed (exit %d)",
                     name, code);
        free(out_buf);
        free(err_buf);
        err->kind = PD_ERR_EVAL;
        err->detail = strdup(msg);
        err->message = err->detail;
        err->span = span;
        return NULL;
    }

    free(err_buf);

    /* NUL-terminate stdout */
    out_buf = realloc(out_buf, out_len + 1);
    out_buf[out_len] = '\0';

    return out_buf;
}
