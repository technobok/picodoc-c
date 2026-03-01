#define _POSIX_C_SOURCE 200809L

#include "render.h"
#include "builtins.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_ds — already has STB_DS_IMPLEMENTATION in eval.c */
#include "stb_ds.h"

/* bstrlib for output buffer */
#include "bstrlib.h"

/* --- Internal types --- */

/* Simple array-based map: node pointer → slug string */
typedef struct {
    const PdNode *node; /* key: pointer to heading node */
    char *slug;         /* heap-allocated slug string */
} HeadingIdEntry;

/* stb_ds string-keyed hash map: slug → heading plain text */
typedef struct {
    char *key;
    char *value;    /* heap-allocated */
} HeadingTextEntry;

/* stb_ds string-keyed hash map: base slug → usage count */
typedef struct {
    char *key;
    int value;
} SlugCountEntry;

typedef struct {
    int level;
    char *text;     /* heap-allocated */
    char *slug;     /* heap-allocated */
} TocEntry;

typedef struct {
    HeadingIdEntry *heading_ids;      /* stb_ds dynamic array */
    HeadingTextEntry *heading_texts;  /* stb_ds string hash map */
    TocEntry *toc_entries;            /* stb_ds dynamic array */
    int toc_level;                    /* 0 = not yet set */
    int heading_number_level;         /* 0 = disabled */
    int heading_anchor_level;         /* 0 = disabled */
    int heading_counters[6];          /* per-level numbering counters */
    bstring out;                      /* output buffer */
    const char *source;
    const char *filename;
    PdError *err;
} RenderState;

/* --- Forward declarations --- */

static void collect_headings(PdNode **nodes, int count,
                             RenderState *state);
static void collect_headings_recursive(PdNode *node,
                                       RenderState *state,
                                       SlugCountEntry **used);
static char *slugify(const char *text, SlugCountEntry **used);

static int render_node(const PdNode *node, RenderState *state);
static int render_body(const PdNode *body, RenderState *state);
static int render_child(const PdNode *child, RenderState *state);
static int render_interp_string(const PdNode *node, RenderState *state);

static bstring escape_html(const char *text, int len);
static bstring escape_attr(const char *text, int len);
static void render_escape_node(const PdNode *esc, RenderState *state);

static int render_heading(const PdNode *node, int level, RenderState *state);
static int render_link(const PdNode *node, RenderState *state);
static int render_block_code(const PdNode *node, RenderState *state);
static int render_inline_code(const PdNode *node, RenderState *state);
static int render_literal(const PdNode *node, RenderState *state);
static int render_list(const PdNode *node, const char *tag, RenderState *state);
static int render_li(const PdNode *node, RenderState *state);
static int render_table(const PdNode *node, RenderState *state);
static int render_tr(const PdNode *node, RenderState *state);
static int render_td(const PdNode *node, RenderState *state);
static int render_th(const PdNode *node, RenderState *state);
static int render_wrapper(const PdNode *node, const char *tag,
                          bool block, RenderState *state);
static int render_toc(RenderState *state);
static int render_head_item(const PdNode *node, RenderState *state);

static char *body_text(const PdNode *body);
static char *get_arg_text(const PdNode *node, const char *name);
static char *arg_value_text(const PdNode *value);

/* --- Heading level helpers --- */

static int heading_level(const char *name) {
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0')
        return name[1] - '0';
    return 0;
}

static bool is_heading(const char *name) {
    return heading_level(name) > 0;
}

/* --- Slugification --- */

static char *slugify(const char *text, SlugCountEntry **used) {
    int len = (int)strlen(text);
    /* Lowercase and replace non-alnum with '-' */
    char *buf = malloc((size_t)(len + 1));
    for (int i = 0; i < len; i++) {
        char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            buf[i] = c;
        } else if (c >= 'A' && c <= 'Z') {
            buf[i] = (char)(c + 32);
        } else {
            buf[i] = '-';
        }
    }
    buf[len] = '\0';

    /* Collapse consecutive dashes */
    int dst = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '-' && dst > 0 && buf[dst - 1] == '-')
            continue;
        buf[dst++] = buf[i];
    }
    buf[dst] = '\0';

    /* Strip leading/trailing dashes */
    int start = 0;
    while (buf[start] == '-') start++;
    int end = dst;
    while (end > start && buf[end - 1] == '-') end--;

    char *slug;
    if (end <= start) {
        slug = strdup("heading");
    } else {
        slug = malloc((size_t)(end - start + 1));
        memcpy(slug, buf + start, (size_t)(end - start));
        slug[end - start] = '\0';
    }
    free(buf);

    /* Deduplicate */
    char *base = strdup(slug);
    ptrdiff_t idx = shgeti(*used, base);
    int count = 0;
    if (idx >= 0) {
        count = (*used)[idx].value;
    }
    if (count > 0) {
        free(slug);
        int numlen = snprintf(NULL, 0, "%s-%d", base, count + 1);
        slug = malloc((size_t)(numlen + 1));
        snprintf(slug, (size_t)(numlen + 1), "%s-%d", base, count + 1);
    }
    shput(*used, base, count + 1);
    free(base);

    return slug;
}

/* --- Heading collection (Pass 1) --- */

static void collect_headings(PdNode **nodes, int count,
                             RenderState *state) {
    SlugCountEntry *used = NULL;
    sh_new_strdup(used);

    for (int i = 0; i < count; i++) {
        if (nodes[i]->type == NODE_MACRO_CALL) {
            collect_headings_recursive(nodes[i], state, &used);
        }
    }

    /* Free slug counter map (sh_new_strdup owns keys; shfree frees them) */
    shfree(used);
}

static void collect_headings_recursive(PdNode *node,
                                       RenderState *state,
                                       SlugCountEntry **used) {
    const char *name = resolve_alias(node->as.macro_call.name);
    if (is_heading(name)) {
        int level = heading_level(name);
        char *text = body_text(node->as.macro_call.body);
        char *slug = slugify(text, used);

        /* Store heading_ids: node pointer → slug */
        HeadingIdEntry id_entry;
        id_entry.node = node;
        id_entry.slug = strdup(slug);
        arrput(state->heading_ids, id_entry);

        /* Store heading_texts: slug → text (sh_new_strdup duplicates key) */
        char *text_copy = strdup(text);
        shput(state->heading_texts, slug, text_copy);

        /* Add TOC entry if within level */
        if (state->toc_level == 0 || level <= state->toc_level) {
            TocEntry entry;
            entry.level = level;
            entry.text = strdup(text);
            entry.slug = strdup(slug);
            arrput(state->toc_entries, entry);
        }

        free(text);
        free(slug);
    } else if (strcmp(name, "doc.toc") == 0) {
        char *level_str = get_arg_text(node, "level");
        if (level_str && level_str[0] != '\0') {
            state->toc_level = atoi(level_str);
        } else {
            state->toc_level = 3;
        }
        free(level_str);
    }

    /* Recurse into body children */
    PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            if (body->as.body.children[i]->type == NODE_MACRO_CALL) {
                collect_headings_recursive(body->as.body.children[i],
                                           state, used);
            }
        }
    }
}

/* --- HTML escaping --- */

static bstring escape_html(const char *text, int len) {
    bstring out = bfromcstr("");
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '&') {
            bcatcstr(out, "&amp;");
        } else if (ch == '<') {
            bcatcstr(out, "&lt;");
        } else if (ch == '>') {
            bcatcstr(out, "&gt;");
        } else if (ch > 0x7F) {
            /* Non-ASCII: encode as &#xHEX; */
            /* Decode UTF-8 codepoint */
            uint32_t cp = 0;
            if ((ch & 0xE0) == 0xC0 && i + 1 < len) {
                cp = ((uint32_t)(ch & 0x1F) << 6) |
                     ((uint32_t)((unsigned char)text[i + 1]) & 0x3F);
                i += 1;
            } else if ((ch & 0xF0) == 0xE0 && i + 2 < len) {
                cp = ((uint32_t)(ch & 0x0F) << 12) |
                     (((uint32_t)((unsigned char)text[i + 1]) & 0x3F) << 6) |
                     ((uint32_t)((unsigned char)text[i + 2]) & 0x3F);
                i += 2;
            } else if ((ch & 0xF8) == 0xF0 && i + 3 < len) {
                cp = ((uint32_t)(ch & 0x07) << 18) |
                     (((uint32_t)((unsigned char)text[i + 1]) & 0x3F) << 12) |
                     (((uint32_t)((unsigned char)text[i + 2]) & 0x3F) << 6) |
                     ((uint32_t)((unsigned char)text[i + 3]) & 0x3F);
                i += 3;
            } else {
                /* Invalid UTF-8, encode raw byte */
                cp = ch;
            }
            bformata(out, "&#x%X;", cp);
        } else {
            bcatblk(out, &text[i], 1);
        }
    }
    return out;
}

static bstring escape_attr(const char *text, int len) {
    bstring out = bfromcstr("");
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '&') {
            bcatcstr(out, "&amp;");
        } else if (ch == '<') {
            bcatcstr(out, "&lt;");
        } else if (ch == '>') {
            bcatcstr(out, "&gt;");
        } else if (ch == '"') {
            bcatcstr(out, "&quot;");
        } else if (ch > 0x7F) {
            uint32_t cp = 0;
            if ((ch & 0xE0) == 0xC0 && i + 1 < len) {
                cp = ((uint32_t)(ch & 0x1F) << 6) |
                     ((uint32_t)((unsigned char)text[i + 1]) & 0x3F);
                i += 1;
            } else if ((ch & 0xF0) == 0xE0 && i + 2 < len) {
                cp = ((uint32_t)(ch & 0x0F) << 12) |
                     (((uint32_t)((unsigned char)text[i + 1]) & 0x3F) << 6) |
                     ((uint32_t)((unsigned char)text[i + 2]) & 0x3F);
                i += 2;
            } else if ((ch & 0xF8) == 0xF0 && i + 3 < len) {
                cp = ((uint32_t)(ch & 0x07) << 18) |
                     (((uint32_t)((unsigned char)text[i + 1]) & 0x3F) << 12) |
                     (((uint32_t)((unsigned char)text[i + 2]) & 0x3F) << 6) |
                     ((uint32_t)((unsigned char)text[i + 3]) & 0x3F);
                i += 3;
            } else {
                cp = ch;
            }
            bformata(out, "&#x%X;", cp);
        } else {
            bcatblk(out, &text[i], 1);
        }
    }
    return out;
}

static void render_escape_node(const PdNode *esc, RenderState *state) {
    unsigned char ch = (unsigned char)esc->as.text.value[0];
    if (ch > 0x7F) {
        /* Decode UTF-8 and emit entity */
        bstring escaped = escape_html(esc->as.text.value, esc->as.text.value_len);
        bconcat(state->out, escaped);
        bdestroy(escaped);
    } else {
        bstring escaped = escape_html(esc->as.text.value, esc->as.text.value_len);
        bconcat(state->out, escaped);
        bdestroy(escaped);
    }
}

/* --- Text extraction helpers --- */

static char *body_text(const PdNode *body) {
    if (!body) return strdup("");

    bstring result = bfromcstr("");

    if (body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            const PdNode *c = body->as.body.children[i];
            if (c->type == NODE_TEXT || c->type == NODE_ESCAPE) {
                bcatblk(result, c->as.text.value, c->as.text.value_len);
            }
        }
    } else if (body->type == NODE_INTERP_STRING) {
        for (int i = 0; i < body->as.interp_string.count; i++) {
            const PdNode *p = body->as.interp_string.parts[i];
            if (p->type == NODE_TEXT) {
                bcatblk(result, p->as.text.value, p->as.text.value_len);
            }
        }
    } else if (body->type == NODE_RAW_STRING) {
        bcatblk(result, body->as.text.value, body->as.text.value_len);
    }

    char *out = bstr2cstr(result, '\0');
    bdestroy(result);
    return out;
}

static char *arg_value_text(const PdNode *value) {
    if (!value) return strdup("");

    if (value->type == NODE_TEXT) {
        return strndup(value->as.text.value, (size_t)value->as.text.value_len);
    }
    if (value->type == NODE_INTERP_STRING) {
        bstring result = bfromcstr("");
        for (int i = 0; i < value->as.interp_string.count; i++) {
            const PdNode *p = value->as.interp_string.parts[i];
            if (p->type == NODE_TEXT) {
                bcatblk(result, p->as.text.value, p->as.text.value_len);
            }
        }
        char *out = bstr2cstr(result, '\0');
        bdestroy(result);
        return out;
    }
    if (value->type == NODE_RAW_STRING) {
        return strndup(value->as.text.value, (size_t)value->as.text.value_len);
    }
    return strdup("");
}

static char *get_arg_text(const PdNode *node, const char *name) {
    for (int i = 0; i < node->as.macro_call.arg_count; i++) {
        const PdNode *arg = node->as.macro_call.args[i];
        if (arg->type == NODE_NAMED_ARG &&
            strcmp(arg->as.named_arg.name, name) == 0) {
            return arg_value_text(arg->as.named_arg.value);
        }
    }
    return NULL;
}

/* --- Body / child rendering --- */

static int render_body(const PdNode *body, RenderState *state) {
    if (!body) return 0;

    if (body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            int rc = render_child(body->as.body.children[i], state);
            if (rc < 0) return -1;
        }
        return 0;
    }
    if (body->type == NODE_INTERP_STRING) {
        return render_interp_string(body, state);
    }
    if (body->type == NODE_RAW_STRING) {
        bstring escaped = escape_html(body->as.text.value,
                                      body->as.text.value_len);
        bconcat(state->out, escaped);
        bdestroy(escaped);
        return 0;
    }
    return 0;
}

static int render_child(const PdNode *child, RenderState *state) {
    if (child->type == NODE_TEXT) {
        bstring escaped = escape_html(child->as.text.value,
                                      child->as.text.value_len);
        bconcat(state->out, escaped);
        bdestroy(escaped);
        return 0;
    }
    if (child->type == NODE_ESCAPE) {
        render_escape_node(child, state);
        return 0;
    }
    if (child->type == NODE_MACRO_CALL) {
        return render_node(child, state);
    }
    return 0;
}

static int render_interp_string(const PdNode *node, RenderState *state) {
    for (int i = 0; i < node->as.interp_string.count; i++) {
        const PdNode *part = node->as.interp_string.parts[i];
        if (part->type == NODE_TEXT) {
            bstring escaped = escape_html(part->as.text.value,
                                          part->as.text.value_len);
            bconcat(state->out, escaped);
            bdestroy(escaped);
        } else if (part->type == NODE_CODE_SECTION) {
            for (int j = 0; j < part->as.code_section.count; j++) {
                int rc = render_child(part->as.code_section.children[j], state);
                if (rc < 0) return -1;
            }
        }
    }
    return 0;
}

/* --- Heading rendering --- */

static int render_heading(const PdNode *node, int level, RenderState *state) {
    /* Render body to a temporary buffer */
    bstring saved = state->out;
    state->out = bfromcstr("");
    int rc = render_body(node->as.macro_call.body, state);
    if (rc < 0) {
        bdestroy(state->out);
        state->out = saved;
        return -1;
    }
    char *body_html = bstr2cstr(state->out, '\0');
    bdestroy(state->out);
    state->out = saved;

    /* Look up slug */
    const char *slug = "";
    for (ptrdiff_t si = 0; si < arrlen(state->heading_ids); si++) {
        if (state->heading_ids[si].node == node) {
            slug = state->heading_ids[si].slug;
            break;
        }
    }

    /* Heading numbering */
    char prefix[64] = "";
    if (state->heading_number_level > 0 && level >= 2 &&
        level <= state->heading_number_level) {
        int adj = level - 2;
        state->heading_counters[adj] += 1;
        for (int i = adj + 1; i < 6; i++) {
            state->heading_counters[i] = 0;
        }
        /* Build dot-separated number */
        bstring num = bfromcstr("");
        for (int i = 0; i <= adj; i++) {
            if (i > 0) bcatcstr(num, ".");
            bformata(num, "%d", state->heading_counters[i]);
        }
        bcatcstr(num, ". ");
        char *numstr = bstr2cstr(num, '\0');
        snprintf(prefix, sizeof(prefix), "%s", numstr);
        bcstrfree(numstr);
        bdestroy(num);
    }

    /* Build content (prefix + body, optionally wrapped in anchor) */
    bstring content = bfromcstr(prefix);
    bcatcstr(content, body_html);

    if (state->heading_anchor_level > 0 &&
        level <= state->heading_anchor_level && slug[0] != '\0') {
        bstring anchor = bfromcstr("<a href=\"#");
        bstring slug_escaped = escape_attr(slug, (int)strlen(slug));
        bconcat(anchor, slug_escaped);
        bdestroy(slug_escaped);
        bcatcstr(anchor, "\">");
        bconcat(anchor, content);
        bcatcstr(anchor, "</a>");
        bdestroy(content);
        content = anchor;
    }

    /* Output <hN id="slug">content</hN> */
    if (slug[0] != '\0') {
        bformata(state->out, "<h%d id=\"", level);
        bstring slug_escaped = escape_attr(slug, (int)strlen(slug));
        bconcat(state->out, slug_escaped);
        bdestroy(slug_escaped);
        bcatcstr(state->out, "\">");
    } else {
        bformata(state->out, "<h%d>", level);
    }
    bconcat(state->out, content);
    bformata(state->out, "</h%d>", level);

    bdestroy(content);
    bcstrfree(body_html);
    return 0;
}

/* --- Link rendering --- */

static int render_link(const PdNode *node, RenderState *state) {
    char *to = get_arg_text(node, "to");
    if (!to) to = strdup("");

    bool is_fragment = (strstr(to, "://") == NULL && strchr(to, '/') == NULL);

    /* Build href */
    bstring href = bfromcstr("");
    if (is_fragment) {
        bcatcstr(href, "#");
        bcatcstr(href, to);
    } else {
        bcatcstr(href, to);
    }

    /* Validate fragment references */
    if (is_fragment && to[0] != '\0') {
        ptrdiff_t idx = shgeti(state->heading_texts, to);
        if (idx < 0) {
            /* Broken internal link */
            char *detail = NULL;
            int dlen = snprintf(NULL, 0,
                "broken internal link: #%s — no heading with this anchor exists",
                to);
            detail = malloc((size_t)(dlen + 1));
            snprintf(detail, (size_t)(dlen + 1),
                "broken internal link: #%s — no heading with this anchor exists",
                to);
            state->err->detail = detail;
            free(to);
            bdestroy(href);
            return pd_render_error(state->err, detail,
                                   node->as.macro_call.body
                                       ? node->as.macro_call.body->span
                                       : node->span,
                                   state->source, state->filename);
        }

        /* Render body or use heading text */
        bcatcstr(state->out, "<a href=\"");
        bstring href_escaped = escape_attr(
            (char *)href->data, href->slen);
        bconcat(state->out, href_escaped);
        bdestroy(href_escaped);
        bcatcstr(state->out, "\">");

        if (node->as.macro_call.body == NULL) {
            /* Use heading text as body */
            const char *heading_text = state->heading_texts[idx].value;
            bstring escaped = escape_html(heading_text,
                                          (int)strlen(heading_text));
            bconcat(state->out, escaped);
            bdestroy(escaped);
        } else {
            int rc = render_body(node->as.macro_call.body, state);
            if (rc < 0) { free(to); bdestroy(href); return -1; }
        }

        bcatcstr(state->out, "</a>");
    } else {
        bcatcstr(state->out, "<a href=\"");
        bstring href_escaped = escape_attr(
            (char *)href->data, href->slen);
        bconcat(state->out, href_escaped);
        bdestroy(href_escaped);
        bcatcstr(state->out, "\">");

        if (node->as.macro_call.body != NULL) {
            int rc = render_body(node->as.macro_call.body, state);
            if (rc < 0) { free(to); bdestroy(href); return -1; }
        } else {
            bstring escaped = escape_html(to, (int)strlen(to));
            bconcat(state->out, escaped);
            bdestroy(escaped);
        }

        bcatcstr(state->out, "</a>");
    }

    free(to);
    bdestroy(href);
    return 0;
}

/* --- Code rendering --- */

static int render_block_code(const PdNode *node, RenderState *state) {
    char *lang = get_arg_text(node, "language");

    bcatcstr(state->out, "<pre><code");
    if (lang) {
        bcatcstr(state->out, " class=\"language-");
        bstring escaped = escape_attr(lang, (int)strlen(lang));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\"");
        free(lang);
    }
    bcatcstr(state->out, ">");

    const PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_RAW_STRING) {
        bstring escaped = escape_html(body->as.text.value,
                                      body->as.text.value_len);
        bconcat(state->out, escaped);
        bdestroy(escaped);
    } else {
        int rc = render_body(body, state);
        if (rc < 0) return -1;
    }

    bcatcstr(state->out, "</code></pre>");
    return 0;
}

static int render_inline_code(const PdNode *node, RenderState *state) {
    char *lang = get_arg_text(node, "language");

    bcatcstr(state->out, "<code");
    if (lang) {
        bcatcstr(state->out, " class=\"language-");
        bstring escaped = escape_attr(lang, (int)strlen(lang));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\"");
        free(lang);
    }
    bcatcstr(state->out, ">");

    const PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_RAW_STRING) {
        bstring escaped = escape_html(body->as.text.value,
                                      body->as.text.value_len);
        bconcat(state->out, escaped);
        bdestroy(escaped);
    } else {
        int rc = render_body(body, state);
        if (rc < 0) return -1;
    }

    bcatcstr(state->out, "</code>");
    return 0;
}

/* --- Literal rendering --- */

static int render_literal(const PdNode *node, RenderState *state) {
    const PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_RAW_STRING) {
        bcatblk(state->out, body->as.text.value, body->as.text.value_len);
    } else {
        char *text = body_text(body);
        bcatcstr(state->out, text);
        free(text);
    }
    return 0;
}

/* --- List rendering --- */

static int render_list(const PdNode *node, const char *tag,
                       RenderState *state) {
    bformata(state->out, "<%s>\n", tag);

    const PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            const PdNode *child = body->as.body.children[i];
            if (child->type == NODE_MACRO_CALL) {
                int rc = render_node(child, state);
                if (rc < 0) return -1;
                bcatcstr(state->out, "\n");
            }
        }
    }

    bformata(state->out, "</%s>", tag);
    return 0;
}

static int render_li(const PdNode *node, RenderState *state) {
    const PdNode *body = node->as.macro_call.body;

    if (!body || body->type != NODE_BODY) {
        bcatcstr(state->out, "<li>");
        int rc = render_body(body, state);
        if (rc < 0) return -1;
        bcatcstr(state->out, "</li>");
        return 0;
    }

    /* Separate inline content from nested block lists */
    /* Collect inline children (before first nested list) and block lists */
    const PdNode **inline_nodes = NULL;  /* stb_ds array */
    const PdNode **block_nodes = NULL;   /* stb_ds array */
    bool seen_block = false;

    for (int i = 0; i < body->as.body.count; i++) {
        const PdNode *child = body->as.body.children[i];
        if (child->type == NODE_MACRO_CALL) {
            const char *cname = resolve_alias(child->as.macro_call.name);
            if (strcmp(cname, "ul") == 0 || strcmp(cname, "ol") == 0) {
                arrput(block_nodes, child);
                seen_block = true;
                continue;
            }
        }
        if (!seen_block) {
            arrput(inline_nodes, child);
        }
    }

    /* Render inline content to temp buffer, then strip */
    bstring saved = state->out;
    state->out = bfromcstr("");
    for (ptrdiff_t i = 0; i < arrlen(inline_nodes); i++) {
        int rc = render_child(inline_nodes[i], state);
        if (rc < 0) {
            bdestroy(state->out);
            state->out = saved;
            arrfree(inline_nodes);
            arrfree(block_nodes);
            return -1;
        }
    }
    char *inline_html = bstr2cstr(state->out, '\0');
    bdestroy(state->out);
    state->out = saved;

    /* Strip whitespace from inline_html */
    int ilen = (int)strlen(inline_html);
    int istart = 0, iend = ilen;
    while (istart < iend && (inline_html[istart] == ' ' ||
           inline_html[istart] == '\t' || inline_html[istart] == '\n' ||
           inline_html[istart] == '\r'))
        istart++;
    while (iend > istart && (inline_html[iend - 1] == ' ' ||
           inline_html[iend - 1] == '\t' || inline_html[iend - 1] == '\n' ||
           inline_html[iend - 1] == '\r'))
        iend--;

    bcatcstr(state->out, "<li>");
    bcatblk(state->out, inline_html + istart, iend - istart);

    if (arrlen(block_nodes) > 0) {
        bcatcstr(state->out, "\n");
        for (ptrdiff_t i = 0; i < arrlen(block_nodes); i++) {
            if (i > 0) bcatcstr(state->out, "\n");
            int rc = render_node(block_nodes[i], state);
            if (rc < 0) {
                bcstrfree(inline_html);
                arrfree(inline_nodes);
                arrfree(block_nodes);
                return -1;
            }
        }
        bcatcstr(state->out, "\n");
    }

    bcatcstr(state->out, "</li>");

    bcstrfree(inline_html);
    arrfree(inline_nodes);
    arrfree(block_nodes);
    return 0;
}

/* --- Table rendering --- */

static bool is_header_row(const PdNode *node) {
    if (node->type != NODE_MACRO_CALL) return false;
    const PdNode *body = node->as.macro_call.body;
    if (!body || body->type != NODE_BODY) return false;

    int cell_count = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        const PdNode *c = body->as.body.children[i];
        if (c->type == NODE_MACRO_CALL) {
            cell_count++;
            if (strcmp(c->as.macro_call.name, "th") != 0) return false;
        }
    }
    return cell_count > 0;
}

static int render_table(const PdNode *node, RenderState *state) {
    bcatcstr(state->out, "<table>\n");

    const PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        /* Collect rows */
        const PdNode **rows = NULL; /* stb_ds array */
        for (int i = 0; i < body->as.body.count; i++) {
            if (body->as.body.children[i]->type == NODE_MACRO_CALL) {
                arrput(rows, body->as.body.children[i]);
            }
        }

        /* Split into head and body rows */
        ptrdiff_t head_end = 0;
        for (ptrdiff_t i = 0; i < arrlen(rows); i++) {
            if (is_header_row(rows[i])) {
                head_end = i + 1;
            } else {
                break;
            }
        }

        if (head_end > 0) {
            bcatcstr(state->out, "<thead>\n");
            for (ptrdiff_t i = 0; i < head_end; i++) {
                int rc = render_node(rows[i], state);
                if (rc < 0) { arrfree(rows); return -1; }
                bcatcstr(state->out, "\n");
            }
            bcatcstr(state->out, "</thead>\n");
        }

        if (head_end < arrlen(rows)) {
            bcatcstr(state->out, "<tbody>\n");
            for (ptrdiff_t i = head_end; i < arrlen(rows); i++) {
                int rc = render_node(rows[i], state);
                if (rc < 0) { arrfree(rows); return -1; }
                bcatcstr(state->out, "\n");
            }
            bcatcstr(state->out, "</tbody>\n");
        }

        arrfree(rows);
    }

    bcatcstr(state->out, "</table>");
    return 0;
}

static int render_tr(const PdNode *node, RenderState *state) {
    bcatcstr(state->out, "<tr>");

    const PdNode *body = node->as.macro_call.body;
    if (body && body->type == NODE_BODY) {
        for (int i = 0; i < body->as.body.count; i++) {
            if (body->as.body.children[i]->type == NODE_MACRO_CALL) {
                int rc = render_node(body->as.body.children[i], state);
                if (rc < 0) return -1;
            }
        }
    }

    bcatcstr(state->out, "</tr>");
    return 0;
}

static int render_td(const PdNode *node, RenderState *state) {
    char *span = get_arg_text(node, "span");

    if (span) {
        bcatcstr(state->out, "<td colspan=\"");
        bstring escaped = escape_attr(span, (int)strlen(span));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\">");
        free(span);
    } else {
        bcatcstr(state->out, "<td>");
    }

    int rc = render_body(node->as.macro_call.body, state);
    if (rc < 0) return -1;

    bcatcstr(state->out, "</td>");
    return 0;
}

static int render_th(const PdNode *node, RenderState *state) {
    char *span = get_arg_text(node, "span");

    if (span) {
        bcatcstr(state->out, "<th colspan=\"");
        bstring escaped = escape_attr(span, (int)strlen(span));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\">");
        free(span);
    } else {
        bcatcstr(state->out, "<th>");
    }

    int rc = render_body(node->as.macro_call.body, state);
    if (rc < 0) return -1;

    bcatcstr(state->out, "</th>");
    return 0;
}

/* --- Wrapper rendering --- */

static int render_wrapper(const PdNode *node, const char *tag,
                          bool block, RenderState *state) {
    char *cls = get_arg_text(node, "class");
    char *id_val = get_arg_text(node, "id");

    bformata(state->out, "<%s", tag);
    if (cls) {
        bcatcstr(state->out, " class=\"");
        bstring escaped = escape_attr(cls, (int)strlen(cls));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\"");
        free(cls);
    }
    if (id_val) {
        bcatcstr(state->out, " id=\"");
        bstring escaped = escape_attr(id_val, (int)strlen(id_val));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\"");
        free(id_val);
    }
    bcatcstr(state->out, ">");

    const PdNode *body = node->as.macro_call.body;
    if (block && body && body->type == NODE_BODY) {
        bcatcstr(state->out, "\n");
        for (int i = 0; i < body->as.body.count; i++) {
            const PdNode *child = body->as.body.children[i];
            int rc = render_child(child, state);
            if (rc < 0) return -1;
            if (child->type == NODE_MACRO_CALL) {
                bcatcstr(state->out, "\n");
            }
        }
    } else {
        int rc = render_body(body, state);
        if (rc < 0) return -1;
    }

    bformata(state->out, "</%s>", tag);
    return 0;
}

/* --- TOC rendering --- */

static int render_toc(RenderState *state) {
    if (arrlen(state->toc_entries) == 0) return 0;

    int *stack = NULL; /* stb_ds dynamic array of levels */

    for (ptrdiff_t i = 0; i < arrlen(state->toc_entries); i++) {
        TocEntry *e = &state->toc_entries[i];
        int level = e->level;

        if (arrlen(stack) == 0) {
            bcatcstr(state->out, "<ul>\n");
            arrput(stack, level);
        } else if (level > stack[arrlen(stack) - 1]) {
            while (level > stack[arrlen(stack) - 1]) {
                bcatcstr(state->out, "<ul>\n");
                arrput(stack, stack[arrlen(stack) - 2] + 1);
            }
        } else if (level < stack[arrlen(stack) - 1]) {
            while (arrlen(stack) > 0 && stack[arrlen(stack) - 1] > level) {
                bcatcstr(state->out, "</li>\n</ul>\n");
                arrpop(stack);
            }
            bcatcstr(state->out, "</li>\n");
        } else {
            bcatcstr(state->out, "</li>\n");
        }

        bstring text_escaped = escape_html(e->text, (int)strlen(e->text));
        bstring slug_escaped = escape_attr(e->slug, (int)strlen(e->slug));
        bcatcstr(state->out, "<li><a href=\"#");
        bconcat(state->out, slug_escaped);
        bcatcstr(state->out, "\">");
        bconcat(state->out, text_escaped);
        bcatcstr(state->out, "</a>\n");
        bdestroy(text_escaped);
        bdestroy(slug_escaped);
    }

    while (arrlen(stack) > 0) {
        bcatcstr(state->out, "</li>\n</ul>\n");
        arrpop(stack);
    }

    arrfree(stack);
    return 0;
}

/* --- Head item rendering --- */

static int render_head_item(const PdNode *node, RenderState *state) {
    const char *name = resolve_alias(node->as.macro_call.name);

    if (strcmp(name, "doc.title") == 0) {
        bcatcstr(state->out, "<title>");
        int rc = render_body(node->as.macro_call.body, state);
        if (rc < 0) return -1;
        bcatcstr(state->out, "</title>");
        return 0;
    }

    if (strcmp(name, "doc.meta") == 0) {
        char *meta_name = get_arg_text(node, "name");
        char *prop = get_arg_text(node, "property");
        char *content = get_arg_text(node, "content");
        if (!content) content = strdup("");

        if (prop) {
            bcatcstr(state->out, "<meta property=\"");
            bstring escaped = escape_attr(prop, (int)strlen(prop));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\" content=\"");
            escaped = escape_attr(content, (int)strlen(content));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\">");
        } else if (meta_name) {
            bcatcstr(state->out, "<meta name=\"");
            bstring escaped = escape_attr(meta_name, (int)strlen(meta_name));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\" content=\"");
            escaped = escape_attr(content, (int)strlen(content));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\">");
        }

        free(meta_name);
        free(prop);
        free(content);
        return 0;
    }

    if (strcmp(name, "doc.author") == 0) {
        char *content = body_text(node->as.macro_call.body);
        bcatcstr(state->out, "<meta name=\"author\" content=\"");
        bstring escaped = escape_attr(content, (int)strlen(content));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\">");
        free(content);
        return 0;
    }

    if (strcmp(name, "doc.version") == 0) {
        char *content = body_text(node->as.macro_call.body);
        bcatcstr(state->out, "<meta name=\"version\" content=\"");
        bstring escaped = escape_attr(content, (int)strlen(content));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\">");
        free(content);
        return 0;
    }

    if (strcmp(name, "doc.datecreated") == 0) {
        char *content = body_text(node->as.macro_call.body);
        bcatcstr(state->out, "<meta name=\"datecreated\" content=\"");
        bstring escaped = escape_attr(content, (int)strlen(content));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\">");
        free(content);
        return 0;
    }

    if (strcmp(name, "doc.datemodified") == 0) {
        char *content = body_text(node->as.macro_call.body);
        bcatcstr(state->out, "<meta name=\"datemodified\" content=\"");
        bstring escaped = escape_attr(content, (int)strlen(content));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\">");
        free(content);
        return 0;
    }

    if (strcmp(name, "doc.link") == 0) {
        char *rel = get_arg_text(node, "rel");
        char *href = get_arg_text(node, "href");
        char *type_val = get_arg_text(node, "type");
        char *sizes_val = get_arg_text(node, "sizes");
        if (!rel) rel = strdup("");
        if (!href) href = strdup("");

        bcatcstr(state->out, "<link rel=\"");
        bstring escaped = escape_attr(rel, (int)strlen(rel));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\" href=\"");
        escaped = escape_attr(href, (int)strlen(href));
        bconcat(state->out, escaped);
        bdestroy(escaped);
        bcatcstr(state->out, "\"");

        if (type_val) {
            bcatcstr(state->out, " type=\"");
            escaped = escape_attr(type_val, (int)strlen(type_val));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\"");
            free(type_val);
        }
        if (sizes_val) {
            bcatcstr(state->out, " sizes=\"");
            escaped = escape_attr(sizes_val, (int)strlen(sizes_val));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\"");
            free(sizes_val);
        }

        bcatcstr(state->out, ">");
        free(rel);
        free(href);
        return 0;
    }

    if (strcmp(name, "doc.script") == 0) {
        char *src = get_arg_text(node, "src");
        char *type_val = get_arg_text(node, "type");

        bcatcstr(state->out, "<script");
        if (type_val) {
            bcatcstr(state->out, " type=\"");
            bstring escaped = escape_attr(type_val, (int)strlen(type_val));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\"");
            free(type_val);
        }
        if (src) {
            bcatcstr(state->out, " src=\"");
            bstring escaped = escape_attr(src, (int)strlen(src));
            bconcat(state->out, escaped);
            bdestroy(escaped);
            bcatcstr(state->out, "\"");
            bcatcstr(state->out, "></script>");
            free(src);
            return 0;
        }

        const PdNode *body = node->as.macro_call.body;
        if (body && body->type == NODE_RAW_STRING) {
            bcatcstr(state->out, ">\n");
            bcatblk(state->out, body->as.text.value, body->as.text.value_len);
            bcatcstr(state->out, "\n</script>");
        } else if (body) {
            bcatcstr(state->out, ">\n");
            int rc = render_body(body, state);
            if (rc < 0) return -1;
            bcatcstr(state->out, "\n</script>");
        } else {
            bcatcstr(state->out, "></script>");
        }
        return 0;
    }

    return 0;
}

/* --- Main node dispatcher --- */

static int render_node(const PdNode *node, RenderState *state) {
    const char *name = resolve_alias(node->as.macro_call.name);

    int level = heading_level(name);
    if (level > 0) {
        return render_heading(node, level, state);
    }

    if (strcmp(name, "p") == 0) {
        bcatcstr(state->out, "<p>");
        int rc = render_body(node->as.macro_call.body, state);
        if (rc < 0) return -1;
        bcatcstr(state->out, "</p>");
        return 0;
    }
    if (strcmp(name, "hr") == 0) {
        bcatcstr(state->out, "<hr>");
        return 0;
    }
    if (strcmp(name, "b") == 0) {
        bcatcstr(state->out, "<strong>");
        int rc = render_body(node->as.macro_call.body, state);
        if (rc < 0) return -1;
        bcatcstr(state->out, "</strong>");
        return 0;
    }
    if (strcmp(name, "i") == 0) {
        bcatcstr(state->out, "<em>");
        int rc = render_body(node->as.macro_call.body, state);
        if (rc < 0) return -1;
        bcatcstr(state->out, "</em>");
        return 0;
    }
    if (strcmp(name, "link") == 0) {
        return render_link(node, state);
    }
    if (strcmp(name, "code") == 0) {
        return render_block_code(node, state);
    }
    if (strcmp(name, "~") == 0) {
        return render_inline_code(node, state);
    }
    if (strcmp(name, "literal") == 0) {
        return render_literal(node, state);
    }
    if (strcmp(name, "ul") == 0) {
        return render_list(node, "ul", state);
    }
    if (strcmp(name, "ol") == 0) {
        return render_list(node, "ol", state);
    }
    if (strcmp(name, "*") == 0) {
        return render_li(node, state);
    }
    if (strcmp(name, "table") == 0) {
        return render_table(node, state);
    }
    if (strcmp(name, "tr") == 0) {
        return render_tr(node, state);
    }
    if (strcmp(name, "td") == 0) {
        return render_td(node, state);
    }
    if (strcmp(name, "th") == 0) {
        return render_th(node, state);
    }
    if (strcmp(name, "doc.toc") == 0) {
        return render_toc(state);
    }
    if (strcmp(name, "span") == 0) {
        return render_wrapper(node, "span", false, state);
    }
    if (is_wrapper_tag(name)) {
        return render_wrapper(node, name, true, state);
    }

    /* Unknown macro — just render body */
    return render_body(node->as.macro_call.body, state);
}

/* --- Loose item wrapping (doc.content) --- */

typedef struct {
    const PdNode *node;  /* NULL = placeholder */
    bool is_placeholder;
} OutputSlot;

static const PdNode **wrap_loose_items(const PdNode *const *items, int count,
                                       const char *content_type,
                                       const char *content_class,
                                       const char *content_id,
                                       int *out_count) {
    OutputSlot *slots = NULL;    /* stb_ds array */
    const PdNode **loose = NULL; /* stb_ds array */
    int placeholder_idx = -1;

    for (int i = 0; i < count; i++) {
        const PdNode *item = items[i];
        const char *rname = resolve_alias(item->as.macro_call.name);
        if (is_wrapper_tag(rname)) {
            OutputSlot slot = {item, false};
            arrput(slots, slot);
        } else {
            if (placeholder_idx == -1) {
                placeholder_idx = (int)arrlen(slots);
                OutputSlot slot = {NULL, true};
                arrput(slots, slot);
            }
            arrput(loose, item);
        }
    }

    if (arrlen(loose) > 0 && placeholder_idx >= 0) {
        /* Build synthetic wrapper MacroCall */
        Span span = loose[0]->span;

        /* Build args */
        NodeArray args;
        node_array_init(&args);
        if (content_class) {
            node_array_push(&args,
                pd_node_named_arg("class", 5, span,
                    pd_node_text(content_class, (int)strlen(content_class), span),
                    span));
        }
        if (content_id) {
            node_array_push(&args,
                pd_node_named_arg("id", 2, span,
                    pd_node_text(content_id, (int)strlen(content_id), span),
                    span));
        }

        /* Build body from loose items (clone them since doc owns originals) */
        PdNode **body_children = malloc((size_t)arrlen(loose) * sizeof(PdNode *));
        for (ptrdiff_t i = 0; i < arrlen(loose); i++) {
            body_children[i] = pd_node_clone(loose[i]);
        }
        PdNode *wrapper_body = pd_node_body(body_children, (int)arrlen(loose), span);

        PdNode *wrapper = pd_node_macro_call(
            content_type, (int)strlen(content_type),
            args.items, args.count,
            wrapper_body, false, span);

        slots[placeholder_idx].node = wrapper;
        slots[placeholder_idx].is_placeholder = false;
    }

    /* Build output array — skip remaining placeholders */
    const PdNode **result = NULL; /* stb_ds array */
    for (ptrdiff_t i = 0; i < arrlen(slots); i++) {
        if (!slots[i].is_placeholder && slots[i].node) {
            arrput(result, slots[i].node);
        }
    }

    *out_count = (int)arrlen(result);

    /* Copy to malloc'd array and free stb_ds arrays */
    const PdNode **out = NULL;
    if (*out_count > 0) {
        out = malloc((size_t)*out_count * sizeof(PdNode *));
        memcpy(out, result, (size_t)*out_count * sizeof(PdNode *));
    }

    arrfree(result);
    arrfree(slots);
    arrfree(loose);
    return out;
}

/* --- State cleanup --- */

static void render_state_free(RenderState *state) {
    /* Free heading_ids entries */
    for (ptrdiff_t i = 0; i < arrlen(state->heading_ids); i++) {
        free(state->heading_ids[i].slug);
    }
    arrfree(state->heading_ids);

    /* Free heading_texts values (sh_new_strdup owns keys; shfree frees them) */
    for (ptrdiff_t i = 0; i < shlen(state->heading_texts); i++) {
        free(state->heading_texts[i].value);
    }
    shfree(state->heading_texts);

    /* Free TOC entries */
    for (ptrdiff_t i = 0; i < arrlen(state->toc_entries); i++) {
        free(state->toc_entries[i].text);
        free(state->toc_entries[i].slug);
    }
    arrfree(state->toc_entries);
}

/* --- Public entry point --- */

int pd_render(const PdNode *doc, const char *source, const char *filename,
              char **out, PdError *err) {
    if (!doc || doc->type != NODE_DOCUMENT) {
        *out = NULL;
        return pd_render_error(err, "expected Document node",
                               (Span){{0,0,0},{0,0,0}}, source, filename);
    }

    /* Classify top-level children */
    const char *lang = NULL;
    char *lang_text = NULL;
    char *body_class = NULL;
    char *body_id = NULL;
    const PdNode **head_items = NULL; /* stb_ds array */
    const PdNode **body_items = NULL; /* stb_ds array */
    char *content_type = NULL;
    char *content_class = NULL;
    char *content_id = NULL;
    int heading_number_level = 0;
    int heading_anchor_level = 0;

    for (int i = 0; i < doc->as.document.count; i++) {
        const PdNode *child = doc->as.document.children[i];
        if (child->type != NODE_MACRO_CALL) continue;

        const char *name = resolve_alias(child->as.macro_call.name);

        if (strcmp(name, "doc.lang") == 0) {
            free(lang_text);
            lang_text = body_text(child->as.macro_call.body);
            lang = lang_text;
        } else if (strcmp(name, "doc.body") == 0) {
            free(body_class);
            free(body_id);
            body_class = get_arg_text(child, "class");
            body_id = get_arg_text(child, "id");
        } else if (strcmp(name, "doc.content") == 0) {
            free(content_type);
            free(content_class);
            free(content_id);
            content_type = get_arg_text(child, "type");
            content_class = get_arg_text(child, "class");
            content_id = get_arg_text(child, "id");
        } else if (strcmp(name, "doc.toc") == 0) {
            arrput(body_items, child);
        } else if (strcmp(name, "doc.heading.number") == 0) {
            char *lvl = get_arg_text(child, "level");
            heading_number_level = (lvl && lvl[0]) ? atoi(lvl) : 3;
            free(lvl);
        } else if (strcmp(name, "doc.heading.anchor") == 0) {
            char *lvl = get_arg_text(child, "level");
            heading_anchor_level = (lvl && lvl[0]) ? atoi(lvl) : 3;
            free(lvl);
        } else if (strncmp(name, "doc.", 4) == 0) {
            arrput(head_items, child);
        } else {
            arrput(body_items, child);
        }
    }

    /* doc.content wrapping */
    const PdNode **wrapped_body = NULL;
    int wrapped_count = 0;
    PdNode *wrapper_node = NULL; /* synthetic node to free later */

    if (content_type) {
        wrapped_body = wrap_loose_items(
            body_items, (int)arrlen(body_items),
            content_type, content_class, content_id,
            &wrapped_count);
        /* Find the synthetic wrapper to free later */
        for (int i = 0; i < wrapped_count; i++) {
            bool found_in_original = false;
            for (ptrdiff_t j = 0; j < arrlen(body_items); j++) {
                if (wrapped_body[i] == body_items[j]) {
                    found_in_original = true;
                    break;
                }
            }
            if (!found_in_original) {
                wrapper_node = (PdNode *)wrapped_body[i];
            }
        }
    }

    const PdNode *const *final_body = content_type ? wrapped_body
                                                   : body_items;
    int final_body_count = content_type ? wrapped_count
                                        : (int)arrlen(body_items);

    /* Pre-pass: collect headings */
    RenderState state;
    memset(&state, 0, sizeof(state));
    state.heading_ids = NULL;
    state.heading_texts = NULL;
    sh_new_strdup(state.heading_texts);
    state.toc_entries = NULL;
    state.source = source;
    state.filename = filename;
    state.err = err;
    state.heading_number_level = heading_number_level;
    state.heading_anchor_level = heading_anchor_level;

    collect_headings(doc->as.document.children, doc->as.document.count, &state);

    /* Render output */
    state.out = bfromcstr("");

    bcatcstr(state.out, "<!DOCTYPE html>\n");

    if (lang && lang[0]) {
        bcatcstr(state.out, "<html lang=\"");
        bstring escaped = escape_attr(lang, (int)strlen(lang));
        bconcat(state.out, escaped);
        bdestroy(escaped);
        bcatcstr(state.out, "\">\n");
    } else {
        bcatcstr(state.out, "<html>\n");
    }

    bcatcstr(state.out, "<head>\n");
    bcatcstr(state.out, "<meta charset=\"utf-8\">\n");

    for (ptrdiff_t i = 0; i < arrlen(head_items); i++) {
        int rc = render_head_item(head_items[i], &state);
        if (rc < 0) {
            bdestroy(state.out);
            render_state_free(&state);
            arrfree(head_items);
            arrfree(body_items);
            free(lang_text);
            free(body_class);
            free(body_id);
            free(content_type);
            free(content_class);
            free(content_id);
            free(wrapped_body);
            pd_node_free(wrapper_node);
            *out = NULL;
            return -1;
        }
        bcatcstr(state.out, "\n");
    }

    bcatcstr(state.out, "</head>\n");

    bcatcstr(state.out, "<body");
    if (body_class) {
        bcatcstr(state.out, " class=\"");
        bstring escaped = escape_attr(body_class, (int)strlen(body_class));
        bconcat(state.out, escaped);
        bdestroy(escaped);
        bcatcstr(state.out, "\"");
    }
    if (body_id) {
        bcatcstr(state.out, " id=\"");
        bstring escaped = escape_attr(body_id, (int)strlen(body_id));
        bconcat(state.out, escaped);
        bdestroy(escaped);
        bcatcstr(state.out, "\"");
    }
    bcatcstr(state.out, ">\n");

    for (int i = 0; i < final_body_count; i++) {
        int rc = render_node(final_body[i], &state);
        if (rc < 0) {
            bdestroy(state.out);
            render_state_free(&state);
            arrfree(head_items);
            arrfree(body_items);
            free(lang_text);
            free(body_class);
            free(body_id);
            free(content_type);
            free(content_class);
            free(content_id);
            free(wrapped_body);
            pd_node_free(wrapper_node);
            *out = NULL;
            return -1;
        }
        bcatcstr(state.out, "\n");
    }

    bcatcstr(state.out, "</body>\n");
    bcatcstr(state.out, "</html>\n");

    *out = bstr2cstr(state.out, '\0');
    bdestroy(state.out);

    render_state_free(&state);
    arrfree(head_items);
    arrfree(body_items);
    free(lang_text);
    free(body_class);
    free(body_id);
    free(content_type);
    free(content_class);
    free(content_id);
    free(wrapped_body);
    pd_node_free(wrapper_node);

    return 0;
}
