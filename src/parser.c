#include "parser.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>

/* --- Token set bitmask --- */

typedef unsigned int TokenSet;
#define TSET(t) (1u << (t))
#define IN_SET(t, set) (((set) & TSET(t)) != 0)

static const TokenSet STOP_NEWLINE_EOF =
    TSET(TOK_NEWLINE) | TSET(TOK_EOF);

static const TokenSet STOP_NEWLINE_RBRACKET_EOF =
    TSET(TOK_NEWLINE) | TSET(TOK_RBRACKET) | TSET(TOK_EOF);

static const TokenSet STOP_RBRACKET_EOF =
    TSET(TOK_RBRACKET) | TSET(TOK_EOF);

static const TokenSet STOP_CODE_CLOSE_EOF =
    TSET(TOK_CODE_CLOSE) | TSET(TOK_EOF);

static const TokenSet TEXT_TOKENS =
    TSET(TOK_IDENTIFIER) | TSET(TOK_TEXT) | TSET(TOK_WS) |
    TSET(TOK_COLON) | TSET(TOK_EQUALS) | TSET(TOK_QUESTION);

/* --- Parser state --- */

typedef struct {
    const Token *tokens;
    int count;
    int pos;
    int bracket_depth;
    const char *source;
    const char *filename;
    PdError *err;
} Parser;

/* --- Text accumulation buffer --- */

typedef struct {
    char *buf;
    int len;
    int cap;
    Position start;
    Position end;
    bool active;
} TextAccum;

static void accum_init(TextAccum *a) {
    a->buf = NULL;
    a->len = 0;
    a->cap = 0;
    a->active = false;
}

static void accum_free(TextAccum *a) {
    free(a->buf);
    a->buf = NULL;
    a->len = 0;
    a->cap = 0;
    a->active = false;
}

static void accum_append(TextAccum *a, const char *data, int data_len,
                         Position tok_start, Position tok_end) {
    if (a->len + data_len > a->cap) {
        int new_cap = a->cap == 0 ? 64 : a->cap;
        while (new_cap < a->len + data_len) new_cap *= 2;
        char *new_buf = realloc(a->buf, (size_t)new_cap);
        if (!new_buf) return;
        a->buf = new_buf;
        a->cap = new_cap;
    }
    memcpy(a->buf + a->len, data, (size_t)data_len);
    a->len += data_len;
    if (!a->active) {
        a->start = tok_start;
        a->active = true;
    }
    a->end = tok_end;
}

static PdNode *accum_flush(TextAccum *a) {
    if (!a->active || a->len == 0) return NULL;
    PdNode *n = pd_node_text(a->buf, a->len, span_make(a->start, a->end));
    a->len = 0;
    a->active = false;
    return n;
}

/* --- Navigation helpers --- */

static const Token *peek(Parser *P, int offset) {
    int idx = P->pos + offset;
    if (idx < P->count)
        return &P->tokens[idx];
    return &P->tokens[P->count - 1]; /* EOF */
}

static bool at_type(Parser *P, TokenType t) {
    return peek(P, 0)->type == t;
}

static bool at_eof(Parser *P) {
    return peek(P, 0)->type == TOK_EOF;
}

static const Token *advance(Parser *P) {
    const Token *tok = &P->tokens[P->pos];
    if (tok->type != TOK_EOF)
        P->pos++;
    return tok;
}

static int parser_error(Parser *P, const char *message, Span span) {
    return pd_parse_error(P->err, message, span, P->source, P->filename);
}

static const Token *expect(Parser *P, TokenType tt, const char *message) {
    const Token *tok = peek(P, 0);
    if (tok->type != tt) {
        parser_error(P, message, tok->span);
        return NULL;
    }
    return advance(P);
}

static void skip_ws(Parser *P) {
    if (at_type(P, TOK_WS))
        advance(P);
}

static Position prev_end(Parser *P) {
    if (P->pos > 0)
        return P->tokens[P->pos - 1].span.end;
    return P->tokens[0].span.start;
}

static TokenSet unbr_body_stop(Parser *P) {
    if (P->bracket_depth > 0)
        return STOP_NEWLINE_RBRACKET_EOF;
    return STOP_NEWLINE_EOF;
}

/* --- Helpers --- */

static bool is_blank_line(Parser *P) {
    if (at_type(P, TOK_NEWLINE))
        return true;
    if (at_type(P, TOK_WS)) {
        TokenType next = peek(P, 1)->type;
        return next == TOK_NEWLINE || next == TOK_EOF;
    }
    return false;
}

static void skip_blank_line(Parser *P) {
    if (at_type(P, TOK_WS))
        advance(P);
    if (at_type(P, TOK_NEWLINE))
        advance(P);
}

static bool at_block_start(Parser *P) {
    if (at_type(P, TOK_HASH))
        return true;
    return at_type(P, TOK_LBRACKET) && peek(P, 1)->type == TOK_HASH;
}

static bool is_named_arg_start(Parser *P) {
    return at_type(P, TOK_IDENTIFIER) && peek(P, 1)->type == TOK_EQUALS;
}

/* --- Forward declarations --- */

static PdNode *parse_unbracketed_call(Parser *P);
static PdNode *parse_bracketed_call(Parser *P);
static int parse_inline_content(Parser *P, TokenSet stop, NodeArray *out);
static PdNode *parse_interp_string(Parser *P);
static PdNode *parse_raw_string(Parser *P);
static PdNode *parse_code_section(Parser *P);
static PdNode *parse_colon_unbr_body(Parser *P);
static PdNode *parse_colon_bracket_body(Parser *P);
static PdNode *parse_string_body(Parser *P);
static PdNode *parse_body_paragraph(Parser *P);

/* --- Argument parsing --- */

static PdNode *parse_bareword(Parser *P) {
    if (!at_type(P, TOK_IDENTIFIER) && !at_type(P, TOK_TEXT)) {
        parser_error(P, "expected argument value", peek(P, 0)->span);
        return NULL;
    }
    Position start = peek(P, 0)->span.start;
    TextAccum acc;
    accum_init(&acc);
    while (at_type(P, TOK_IDENTIFIER) || at_type(P, TOK_TEXT)) {
        const Token *t = advance(P);
        accum_append(&acc, t->value, t->value_len, t->span.start, t->span.end);
    }
    PdNode *n = pd_node_text(acc.buf, acc.len,
                              span_make(start, prev_end(P)));
    accum_free(&acc);
    return n;
}

static PdNode *parse_macro_ref(Parser *P) {
    Position start = peek(P, 0)->span.start;
    advance(P); /* consume HASH */
    const Token *name_tok = expect(P, TOK_IDENTIFIER, "expected macro name after '#'");
    if (!name_tok) return NULL;
    return pd_node_macro_call(name_tok->value, name_tok->value_len,
                               NULL, 0, NULL, false,
                               span_make(start, name_tok->span.end));
}

static PdNode *parse_arg_value(Parser *P) {
    if (at_type(P, TOK_STRING_START))
        return parse_interp_string(P);
    if (at_type(P, TOK_RAW_STRING))
        return parse_raw_string(P);
    if (at_type(P, TOK_LBRACKET) && peek(P, 1)->type == TOK_HASH)
        return parse_bracketed_call(P);
    if (at_type(P, TOK_HASH))
        return parse_macro_ref(P);
    if (at_type(P, TOK_QUESTION)) {
        const Token *tok = advance(P);
        return pd_node_required_marker(tok->span);
    }
    return parse_bareword(P);
}

static PdNode *parse_named_arg(Parser *P) {
    const Token *name_tok = expect(P, TOK_IDENTIFIER, "expected argument name");
    if (!name_tok) return NULL;
    Span name_span = name_tok->span;

    if (!expect(P, TOK_EQUALS, "expected '=' after argument name"))
        return NULL;

    skip_ws(P);
    PdNode *value = parse_arg_value(P);
    if (!value) return NULL;

    return pd_node_named_arg(name_tok->value, name_tok->value_len,
                              name_span, value,
                              span_make(name_span.start, value->span.end));
}

static int parse_named_args(Parser *P, NodeArray *args) {
    PdNode *arg = parse_named_arg(P);
    if (!arg) return -1;
    node_array_push(args, arg);

    while (at_type(P, TOK_WS)) {
        advance(P); /* consume WS */
        if (is_named_arg_start(P)) {
            arg = parse_named_arg(P);
            if (!arg) return -1;
            node_array_push(args, arg);
        } else {
            break;
        }
    }
    return 0;
}

/* --- String parsing --- */

static PdNode *parse_interp_string(Parser *P) {
    const Token *start_tok = advance(P); /* consume STRING_START */
    Position start = start_tok->span.start;

    NodeArray parts;
    node_array_init(&parts);
    TextAccum acc;
    accum_init(&acc);

    while (!at_type(P, TOK_STRING_END) && !at_eof(P)) {
        const Token *tok = peek(P, 0);

        if (tok->type == TOK_STRING_TEXT || tok->type == TOK_STRING_ESCAPE) {
            accum_append(&acc, tok->value, tok->value_len,
                         tok->span.start, tok->span.end);
            advance(P);
        } else if (tok->type == TOK_CODE_OPEN) {
            PdNode *flushed = accum_flush(&acc);
            if (flushed) node_array_push(&parts, flushed);
            PdNode *cs = parse_code_section(P);
            if (!cs) goto interp_fail;
            node_array_push(&parts, cs);
        } else {
            parser_error(P, "unexpected token in string", tok->span);
            goto interp_fail;
        }
    }

    PdNode *flushed = accum_flush(&acc);
    if (flushed) node_array_push(&parts, flushed);
    accum_free(&acc);

    const Token *end_tok = expect(P, TOK_STRING_END, "expected closing '\"'");
    if (!end_tok) goto interp_fail;

    PdNode *result = pd_node_interp_string(parts.items, parts.count,
                                            span_make(start, end_tok->span.end));
    return result;

interp_fail:
    accum_free(&acc);
    node_array_free_deep(&parts);
    return NULL;
}

static PdNode *parse_raw_string(Parser *P) {
    const Token *tok = expect(P, TOK_RAW_STRING, "expected raw string");
    if (!tok) return NULL;
    return pd_node_raw_string(tok->value, tok->value_len, tok->span);
}

static PdNode *parse_code_section(Parser *P) {
    Position start = peek(P, 0)->span.start;
    advance(P); /* consume CODE_OPEN */

    NodeArray children;
    node_array_init(&children);
    if (parse_inline_content(P, STOP_CODE_CLOSE_EOF, &children) < 0) {
        node_array_free_deep(&children);
        return NULL;
    }

    const Token *end_tok = expect(P, TOK_CODE_CLOSE,
                                   "expected closing ']' for code section");
    if (!end_tok) {
        node_array_free_deep(&children);
        return NULL;
    }

    return pd_node_code_section(children.items, children.count,
                                 span_make(start, end_tok->span.end));
}

/* --- Body parsing --- */

static PdNode *parse_string_body(Parser *P) {
    if (at_type(P, TOK_STRING_START))
        return parse_interp_string(P);
    if (at_type(P, TOK_RAW_STRING))
        return parse_raw_string(P);
    parser_error(P, "expected string literal", peek(P, 0)->span);
    return NULL;
}

static PdNode *parse_colon_unbr_body(Parser *P) {
    advance(P); /* consume COLON */
    skip_ws(P);

    if (at_type(P, TOK_STRING_START))
        return parse_interp_string(P);
    if (at_type(P, TOK_RAW_STRING))
        return parse_raw_string(P);

    /* NEWLINE or EOF → paragraph body */
    if (at_type(P, TOK_NEWLINE) || at_eof(P)) {
        if (at_type(P, TOK_NEWLINE))
            advance(P);
        return parse_body_paragraph(P);
    }

    /* Inline body: content to end of line (or enclosing bracket) */
    Position start = peek(P, 0)->span.start;
    NodeArray children;
    node_array_init(&children);
    if (parse_inline_content(P, unbr_body_stop(P), &children) < 0) {
        node_array_free_deep(&children);
        return NULL;
    }
    coalesce_text(&children);
    Position end = children.count > 0
        ? children.items[children.count - 1]->span.end : start;
    return pd_node_body(children.items, children.count,
                         span_make(start, end));
}

static PdNode *parse_colon_bracket_body(Parser *P) {
    advance(P); /* consume COLON */
    skip_ws(P);

    if (at_type(P, TOK_STRING_START))
        return parse_interp_string(P);
    if (at_type(P, TOK_RAW_STRING))
        return parse_raw_string(P);

    /* Skip leading newline (matches unbracketed body behavior) */
    if (at_type(P, TOK_NEWLINE))
        advance(P);

    Position start = peek(P, 0)->span.start;
    NodeArray children;
    node_array_init(&children);
    if (parse_inline_content(P, STOP_RBRACKET_EOF, &children) < 0) {
        node_array_free_deep(&children);
        return NULL;
    }
    coalesce_text(&children);
    dedent_body_children(children.items, children.count);
    Position end = children.count > 0
        ? children.items[children.count - 1]->span.end : start;
    return pd_node_body(children.items, children.count,
                         span_make(start, end));
}

static PdNode *parse_body_paragraph(Parser *P) {
    NodeArray children;
    node_array_init(&children);
    Position start = peek(P, 0)->span.start;

    while (!at_eof(P) && !is_blank_line(P)) {
        NodeArray line;
        node_array_init(&line);
        if (parse_inline_content(P, unbr_body_stop(P), &line) < 0) {
            node_array_free_deep(&line);
            node_array_free_deep(&children);
            return NULL;
        }
        for (int i = 0; i < line.count; i++)
            node_array_push(&children, line.items[i]);
        node_array_free_shallow(&line);

        if (at_type(P, TOK_NEWLINE)) {
            const Token *nl_tok = advance(P);
            if (!at_eof(P) && !is_blank_line(P)) {
                PdNode *nl = pd_node_text("\n", 1, nl_tok->span);
                node_array_push(&children, nl);
            }
        } else {
            break;
        }
    }

    coalesce_text(&children);
    dedent_body_children(children.items, children.count);
    Position end = children.count > 0
        ? children.items[children.count - 1]->span.end : start;
    return pd_node_body(children.items, children.count,
                         span_make(start, end));
}

/* --- Inline content --- */

static int parse_inline_content(Parser *P, TokenSet stop, NodeArray *out) {
    TextAccum acc;
    accum_init(&acc);

    while (!at_eof(P)) {
        const Token *tok = peek(P, 0);

        if (IN_SET(tok->type, stop))
            break;

        if (tok->type == TOK_HASH) {
            PdNode *flushed = accum_flush(&acc);
            if (flushed) node_array_push(out, flushed);
            PdNode *call = parse_unbracketed_call(P);
            if (!call) goto inline_fail;
            node_array_push(out, call);

        } else if (tok->type == TOK_LBRACKET && peek(P, 1)->type == TOK_HASH) {
            PdNode *flushed = accum_flush(&acc);
            if (flushed) node_array_push(out, flushed);
            PdNode *call = parse_bracketed_call(P);
            if (!call) goto inline_fail;
            node_array_push(out, call);

        } else if (tok->type == TOK_LBRACKET) {
            parser_error(P, "bare '[' in text — use \\[ for a literal bracket",
                         tok->span);
            goto inline_fail;

        } else if (tok->type == TOK_RBRACKET && !IN_SET(TOK_RBRACKET, stop)) {
            parser_error(P, "bare ']' in text — use \\] for a literal bracket",
                         tok->span);
            goto inline_fail;

        } else if (tok->type == TOK_ESCAPE) {
            PdNode *flushed = accum_flush(&acc);
            if (flushed) node_array_push(out, flushed);
            const Token *t = advance(P);
            PdNode *esc = pd_node_escape(t->value, t->value_len, t->span);
            node_array_push(out, esc);

        } else if (IN_SET(tok->type, TEXT_TOKENS)) {
            accum_append(&acc, tok->value, tok->value_len,
                         tok->span.start, tok->span.end);
            advance(P);

        } else if (tok->type == TOK_NEWLINE && !IN_SET(TOK_NEWLINE, stop)) {
            /* For bracketed body, newlines become text */
            accum_append(&acc, "\n", 1, tok->span.start, tok->span.end);
            advance(P);

        } else if (tok->type == TOK_STRING_START) {
            /* String in body context — reconstruct as text including quotes */
            accum_append(&acc, "\"", 1, tok->span.start, tok->span.end);
            advance(P);
            while (!at_type(P, TOK_STRING_END) && !at_eof(P)) {
                const Token *inner = peek(P, 0);
                if (inner->type == TOK_STRING_TEXT ||
                    inner->type == TOK_STRING_ESCAPE) {
                    accum_append(&acc, inner->value, inner->value_len,
                                 inner->span.start, inner->span.end);
                    advance(P);
                } else if (inner->type == TOK_CODE_OPEN) {
                    accum_append(&acc, inner->raw, inner->raw_len,
                                 inner->span.start, inner->span.end);
                    advance(P);
                    while (!at_type(P, TOK_CODE_CLOSE) &&
                           !at_type(P, TOK_STRING_END) && !at_eof(P)) {
                        const Token *ct = peek(P, 0);
                        accum_append(&acc, ct->raw, ct->raw_len,
                                     ct->span.start, ct->span.end);
                        advance(P);
                    }
                    if (at_type(P, TOK_CODE_CLOSE)) {
                        const Token *cc = peek(P, 0);
                        accum_append(&acc, cc->raw, cc->raw_len,
                                     cc->span.start, cc->span.end);
                        advance(P);
                    }
                } else {
                    break;
                }
            }
            if (at_type(P, TOK_STRING_END)) {
                const Token *se = peek(P, 0);
                accum_append(&acc, "\"", 1, se->span.start, se->span.end);
                advance(P);
            }

        } else if (tok->type == TOK_RAW_STRING) {
            accum_append(&acc, tok->value, tok->value_len,
                         tok->span.start, tok->span.end);
            advance(P);

        } else {
            break;
        }
    }

    PdNode *flushed = accum_flush(&acc);
    if (flushed) node_array_push(out, flushed);
    accum_free(&acc);
    return 0;

inline_fail:
    accum_free(&acc);
    return -1;
}

/* --- Macro call parsing --- */

static PdNode *parse_unbracketed_call(Parser *P) {
    Position start = peek(P, 0)->span.start;
    advance(P); /* consume HASH */

    const Token *name_tok = expect(P, TOK_IDENTIFIER,
                                    "expected macro name after '#'");
    if (!name_tok) return NULL;

    PdNode **args = NULL;
    int arg_count = 0;
    PdNode *body = NULL;

    if (at_type(P, TOK_STRING_START) || at_type(P, TOK_RAW_STRING)) {
        body = parse_string_body(P);
        if (!body) return NULL;
    } else if (at_type(P, TOK_COLON)) {
        body = parse_colon_unbr_body(P);
        if (!body) return NULL;
    } else if (at_type(P, TOK_WS)) {
        int saved_pos = P->pos;
        advance(P); /* consume WS */

        if (is_named_arg_start(P)) {
            NodeArray arg_arr;
            node_array_init(&arg_arr);
            if (parse_named_args(P, &arg_arr) < 0) {
                node_array_free_deep(&arg_arr);
                return NULL;
            }
            args = arg_arr.items;
            arg_count = arg_arr.count;

            if (at_type(P, TOK_COLON)) {
                body = parse_colon_unbr_body(P);
                if (!body) {
                    for (int i = 0; i < arg_count; i++) pd_node_free(args[i]);
                    free(args);
                    return NULL;
                }
            } else if (at_type(P, TOK_STRING_START) || at_type(P, TOK_RAW_STRING)) {
                body = parse_string_body(P);
                if (!body) {
                    for (int i = 0; i < arg_count; i++) pd_node_free(args[i]);
                    free(args);
                    return NULL;
                }
            }
        } else if (at_type(P, TOK_COLON)) {
            body = parse_colon_unbr_body(P);
            if (!body) return NULL;
        } else if (at_type(P, TOK_STRING_START) || at_type(P, TOK_RAW_STRING)) {
            body = parse_string_body(P);
            if (!body) return NULL;
        } else {
            /* Nothing useful after WS — restore position */
            P->pos = saved_pos;
        }
    }

    Position end = prev_end(P);
    return pd_node_macro_call(name_tok->value, name_tok->value_len,
                               args, arg_count, body, false,
                               span_make(start, end));
}

static PdNode *parse_bracketed_call(Parser *P) {
    Position start = peek(P, 0)->span.start;
    advance(P); /* consume LBRACKET */

    if (!expect(P, TOK_HASH, "expected '#' after '['"))
        return NULL;

    const Token *name_tok = expect(P, TOK_IDENTIFIER,
                                    "expected macro name after '#'");
    if (!name_tok) return NULL;

    P->bracket_depth++;

    PdNode **args = NULL;
    int arg_count = 0;
    PdNode *body = NULL;

    if (at_type(P, TOK_STRING_START) || at_type(P, TOK_RAW_STRING)) {
        body = parse_string_body(P);
        if (!body) goto bracket_fail;
    } else if (at_type(P, TOK_COLON)) {
        body = parse_colon_bracket_body(P);
        if (!body) goto bracket_fail;
    } else if (at_type(P, TOK_WS) || at_type(P, TOK_NEWLINE)) {
        advance(P); /* consume WS or NEWLINE */

        /* After a newline, skip further whitespace/newlines */
        while (at_type(P, TOK_WS) || at_type(P, TOK_NEWLINE))
            advance(P);

        if (is_named_arg_start(P)) {
            NodeArray arg_arr;
            node_array_init(&arg_arr);
            if (parse_named_args(P, &arg_arr) < 0) {
                node_array_free_deep(&arg_arr);
                goto bracket_fail;
            }
            args = arg_arr.items;
            arg_count = arg_arr.count;

            /* Skip whitespace/newlines between args and body */
            while (at_type(P, TOK_WS) || at_type(P, TOK_NEWLINE))
                advance(P);

            if (at_type(P, TOK_COLON)) {
                body = parse_colon_bracket_body(P);
                if (!body) goto bracket_fail_args;
            } else if (at_type(P, TOK_STRING_START) || at_type(P, TOK_RAW_STRING)) {
                body = parse_string_body(P);
                if (!body) goto bracket_fail_args;
            }
        } else if (at_type(P, TOK_COLON)) {
            body = parse_colon_bracket_body(P);
            if (!body) goto bracket_fail;
        } else if (at_type(P, TOK_STRING_START) || at_type(P, TOK_RAW_STRING)) {
            body = parse_string_body(P);
            if (!body) goto bracket_fail;
        } else if (!at_type(P, TOK_RBRACKET)) {
            parser_error(P,
                "expected argument, ':' body, string body, or ']'",
                peek(P, 0)->span);
            goto bracket_fail;
        }
    }

    P->bracket_depth--;

    const Token *end_tok = expect(P, TOK_RBRACKET, "expected closing ']'");
    if (!end_tok) goto bracket_fail_args;

    return pd_node_macro_call(name_tok->value, name_tok->value_len,
                               args, arg_count, body, true,
                               span_make(start, end_tok->span.end));

bracket_fail_args:
    for (int i = 0; i < arg_count; i++) pd_node_free(args[i]);
    free(args);
    pd_node_free(body);
    return NULL;

bracket_fail:
    pd_node_free(body);
    return NULL;
}

/* --- Document-level parsing --- */

static PdNode *parse_macro_block(Parser *P) {
    PdNode *call;
    if (at_type(P, TOK_LBRACKET))
        call = parse_bracketed_call(P);
    else
        call = parse_unbracketed_call(P);

    if (!call) return NULL;

    skip_ws(P);

    if (!at_type(P, TOK_NEWLINE) && !at_eof(P)) {
        parser_error(P, "unexpected text after macro call", peek(P, 0)->span);
        pd_node_free(call);
        return NULL;
    }

    if (at_type(P, TOK_NEWLINE))
        advance(P);

    return call;
}

static PdNode *parse_paragraph(Parser *P) {
    NodeArray children;
    node_array_init(&children);
    Position start = peek(P, 0)->span.start;

    while (!at_eof(P) && !is_blank_line(P)) {
        NodeArray line;
        node_array_init(&line);
        if (parse_inline_content(P, STOP_NEWLINE_EOF, &line) < 0) {
            node_array_free_deep(&line);
            node_array_free_deep(&children);
            return NULL;
        }
        for (int i = 0; i < line.count; i++)
            node_array_push(&children, line.items[i]);
        node_array_free_shallow(&line);

        if (at_type(P, TOK_NEWLINE)) {
            const Token *nl_tok = advance(P);
            if (!at_eof(P) && !is_blank_line(P)) {
                PdNode *nl = pd_node_text("\n", 1, nl_tok->span);
                node_array_push(&children, nl);
            }
        }
    }

    coalesce_text(&children);
    Position end = children.count > 0
        ? children.items[children.count - 1]->span.end : start;
    return pd_node_paragraph(children.items, children.count,
                              span_make(start, end));
}

static PdNode *parse_block(Parser *P) {
    /* Skip blank lines */
    while (!at_eof(P) && is_blank_line(P))
        skip_blank_line(P);

    if (at_eof(P))
        return NULL;

    if (at_block_start(P))
        return parse_macro_block(P);

    return parse_paragraph(P);
}

/* --- Public API --- */

int pd_parse(const TokenArray *tokens, const char *source,
             const char *filename, PdNode **out, PdError *err) {
    Parser P;
    P.tokens = tokens->items;
    P.count = tokens->count;
    P.pos = 0;
    P.bracket_depth = 0;
    P.source = source;
    P.filename = filename;
    P.err = err;

    NodeArray children;
    node_array_init(&children);
    Position start = peek(&P, 0)->span.start;

    while (!at_eof(&P)) {
        PdNode *block = parse_block(&P);
        if (!block && P.err->kind != PD_ERR_NONE) {
            node_array_free_deep(&children);
            *out = NULL;
            return -1;
        }
        if (block)
            node_array_push(&children, block);
    }

    Position end = peek(&P, 0)->span.end;
    *out = pd_node_document(children.items, children.count,
                             span_make(start, end));
    return 0;
}
