#include "unity.h"
#include "ast.h"
#include "builtins.h"
#include "errors.h"
#include "eval.h"
#include "lexer.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Convenience helpers --- */

static Span S(void) {
    return span_make(pos_make(1, 1, 0), pos_make(1, 1, 0));
}

static PdNode *mk_text(const char *value) {
    return pd_node_text(value, (int)strlen(value), S());
}

static PdNode *mk_body_v(PdNode **kids, int count) {
    PdNode **arr = NULL;
    if (count > 0) {
        arr = malloc((size_t)count * sizeof(PdNode *));
        memcpy(arr, kids, (size_t)count * sizeof(PdNode *));
    }
    return pd_node_body(arr, count, S());
}

static PdNode *mk_arg(const char *name, const char *value) {
    return pd_node_named_arg(name, (int)strlen(name), S(),
                             mk_text(value), S());
}

static PdNode *mk_narg(const char *name, PdNode *value) {
    return pd_node_named_arg(name, (int)strlen(name), S(), value, S());
}

/* Variadic macro call builder. args and body are optional. */
static PdNode *mk_call(const char *name, PdNode **args, int arg_count,
                        PdNode *body, bool bracketed) {
    PdNode **args_arr = NULL;
    if (arg_count > 0) {
        args_arr = malloc((size_t)arg_count * sizeof(PdNode *));
        memcpy(args_arr, args, (size_t)arg_count * sizeof(PdNode *));
    }
    return pd_node_macro_call(name, (int)strlen(name),
                              args_arr, arg_count, body, bracketed, S());
}

static PdNode *mk_call0(const char *name) {
    return mk_call(name, NULL, 0, NULL, false);
}

static PdNode *mk_doc(PdNode **kids, int count) {
    PdNode **arr = NULL;
    if (count > 0) {
        arr = malloc((size_t)count * sizeof(PdNode *));
        memcpy(arr, kids, (size_t)count * sizeof(PdNode *));
    }
    return pd_node_document(arr, count, S());
}

static PdNode *mk_para(PdNode **kids, int count) {
    PdNode **arr = NULL;
    if (count > 0) {
        arr = malloc((size_t)count * sizeof(PdNode *));
        memcpy(arr, kids, (size_t)count * sizeof(PdNode *));
    }
    return pd_node_paragraph(arr, count, S());
}

/* Parse + evaluate, fail test on error. Caller frees result. */
static PdNode *eval_ok(const char *input) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(input, "test.pdoc", &tokens, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }

    PdNode *doc = NULL;
    rc = pd_parse(&tokens, input, "test.pdoc", &doc, &err);
    token_array_free(&tokens);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }

    PdNode *result = NULL;
    rc = pd_evaluate(doc, "test.pdoc", input, NULL, NULL, 0, &result, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        pd_node_free(doc);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }
    pd_node_free(doc);
    return result;
}

/* Evaluate AST directly, fail test on error. Caller frees result. */
static PdNode *eval_ast_ok(PdNode *doc) {
    PdError err;
    pd_error_init(&err);
    PdNode *result = NULL;
    int rc = pd_evaluate(doc, "test.pdoc", "", NULL, NULL, 0, &result, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        pd_node_free(doc);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }
    pd_node_free(doc);
    return result;
}

/* Evaluate AST directly with env, fail test on error. */
static PdNode *eval_ast_env_ok(PdNode *doc,
                                const char **env_keys, const char **env_vals,
                                int env_count) {
    PdError err;
    pd_error_init(&err);
    PdNode *result = NULL;
    int rc = pd_evaluate(doc, "test.pdoc", "", env_keys, env_vals,
                         env_count, &result, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        pd_node_free(doc);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }
    pd_node_free(doc);
    return result;
}

/* Expect eval error. Returns error message (heap-allocated in err->detail).
 * Frees doc on error. Uses static PdError so message survives return. */
static PdError _last_err;

static const char *eval_ast_err(PdNode *doc) {
    pd_error_free(&_last_err);
    pd_error_init(&_last_err);
    PdNode *result = NULL;
    int rc = pd_evaluate(doc, "test.pdoc", "", NULL, NULL, 0, &result, &_last_err);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_NULL(result);
    pd_node_free(doc);
    return _last_err.message;
}

static const char *eval_ast_env_err(PdNode *doc,
                                     const char **env_keys,
                                     const char **env_vals,
                                     int env_count) {
    pd_error_free(&_last_err);
    pd_error_init(&_last_err);
    PdNode *result = NULL;
    int rc = pd_evaluate(doc, "test.pdoc", "", env_keys, env_vals,
                         env_count, &result, &_last_err);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_NULL(result);
    pd_node_free(doc);
    return _last_err.message;
}

/* Extract concatenated text from a MacroCall's Body children. */
static void body_text(PdNode *node, char *buf, int bufsize) {
    buf[0] = '\0';
    int pos = 0;
    PdNode *body = node->as.macro_call.body;
    if (!body || body->type != NODE_BODY) return;

    for (int i = 0; i < body->as.body.count && pos < bufsize - 1; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_TEXT || c->type == NODE_ESCAPE) {
            int len = c->as.text.value_len;
            if (pos + len >= bufsize) len = bufsize - 1 - pos;
            memcpy(buf + pos, c->as.text.value, (size_t)len);
            pos += len;
        }
    }
    buf[pos] = '\0';
}

/* --- Tests: Comment removal --- */

void test_eval_comment_removed(void) {
    PdNode *body_kids[] = {mk_text("hidden")};
    PdNode *comment = mk_call("comment", NULL, 0,
                               mk_body_v(body_kids, 1), false);
    PdNode *doc_kids[] = {comment};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(0, result->as.document.count);
    pd_node_free(result);
}

void test_eval_comment_alias_removed(void) {
    PdNode *body_kids[] = {mk_text("hidden")};
    PdNode *comment = mk_call("//", NULL, 0,
                               mk_body_v(body_kids, 1), false);
    PdNode *doc_kids[] = {comment};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(0, result->as.document.count);
    pd_node_free(result);
}

void test_eval_comment_in_body_removed(void) {
    PdNode *comment_body[] = {mk_text("hidden")};
    PdNode *comment = mk_call("comment", NULL, 0,
                               mk_body_v(comment_body, 1), false);
    PdNode *li_body[] = {mk_text("visible")};
    PdNode *li = mk_call("*", NULL, 0, mk_body_v(li_body, 1), false);
    PdNode *ul_body_kids[] = {comment, li};
    PdNode *ul = mk_call("ul", NULL, 0, mk_body_v(ul_body_kids, 2), false);
    PdNode *doc_kids[] = {ul};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    PdNode *ul_out = result->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, ul_out->type);
    PdNode *ul_body = ul_out->as.macro_call.body;
    TEST_ASSERT_NOT_NULL(ul_body);
    TEST_ASSERT_EQUAL_INT(1, ul_body->as.body.count);
    TEST_ASSERT_EQUAL_STRING("*", ul_body->as.body.children[0]->as.macro_call.name);
    pd_node_free(result);
}

/* --- Tests: Set collection --- */

void test_eval_set_collected_and_removed(void) {
    PdNode *set_args[] = {mk_arg("name", "version")};
    PdNode *set_body[] = {mk_text("1.0")};
    PdNode *set = mk_call("set", set_args, 1, mk_body_v(set_body, 1), false);

    PdNode *title_body[] = {mk_text("Hello")};
    PdNode *title = mk_call("doc.title", NULL, 0,
                             mk_body_v(title_body, 1), false);

    PdNode *doc_kids[] = {set, title};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    TEST_ASSERT_EQUAL_STRING("doc.title",
        result->as.document.children[0]->as.macro_call.name);
    pd_node_free(result);
}

void test_eval_set_value_available_for_ifset(void) {
    PdNode *set_args[] = {mk_arg("name", "version")};
    PdNode *set_body[] = {mk_text("1.0")};
    PdNode *set = mk_call("set", set_args, 1, mk_body_v(set_body, 1), false);

    PdNode *p_body[] = {mk_text("defined")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *ifset_args[] = {mk_arg("name", "version")};
    PdNode *ifset_body[] = {p};
    PdNode *ifset = mk_call("ifset", ifset_args, 1,
                             mk_body_v(ifset_body, 1), false);

    PdNode *doc_kids[] = {set, ifset};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    TEST_ASSERT_EQUAL_STRING("p",
        result->as.document.children[0]->as.macro_call.name);
    pd_node_free(result);
}

void test_eval_set_from_parse(void) {
    PdNode *result = eval_ok("[#set name=version : 1.0]\n#doc.title: Hello");
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

/* --- Tests: Conditionals --- */

void test_eval_ifeq_true(void) {
    PdNode *p_body[] = {mk_text("match")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *args[] = {mk_arg("lhs", "a"), mk_arg("rhs", "a")};
    PdNode *body[] = {p};
    PdNode *ifeq = mk_call("ifeq", args, 2, mk_body_v(body, 1), false);
    PdNode *doc_kids[] = {ifeq};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    TEST_ASSERT_EQUAL_STRING("p",
        result->as.document.children[0]->as.macro_call.name);
    pd_node_free(result);
}

void test_eval_ifeq_false(void) {
    PdNode *p_body[] = {mk_text("match")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *args[] = {mk_arg("lhs", "a"), mk_arg("rhs", "b")};
    PdNode *body[] = {p};
    PdNode *ifeq = mk_call("ifeq", args, 2, mk_body_v(body, 1), false);
    PdNode *doc_kids[] = {ifeq};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(0, result->as.document.count);
    pd_node_free(result);
}

void test_eval_ifne_true(void) {
    PdNode *p_body[] = {mk_text("different")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *args[] = {mk_arg("lhs", "a"), mk_arg("rhs", "b")};
    PdNode *body[] = {p};
    PdNode *ifne = mk_call("ifne", args, 2, mk_body_v(body, 1), false);
    PdNode *doc_kids[] = {ifne};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_ifne_false(void) {
    PdNode *p_body[] = {mk_text("different")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *args[] = {mk_arg("lhs", "a"), mk_arg("rhs", "a")};
    PdNode *body[] = {p};
    PdNode *ifne = mk_call("ifne", args, 2, mk_body_v(body, 1), false);
    PdNode *doc_kids[] = {ifne};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(0, result->as.document.count);
    pd_node_free(result);
}

void test_eval_ifset_defined(void) {
    PdNode *set_args[] = {mk_arg("name", "x")};
    PdNode *set_body[] = {mk_text("val")};
    PdNode *set = mk_call("set", set_args, 1, mk_body_v(set_body, 1), false);

    PdNode *p_body[] = {mk_text("defined")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *ifset_args[] = {mk_arg("name", "x")};
    PdNode *ifset_body[] = {p};
    PdNode *ifset = mk_call("ifset", ifset_args, 1,
                             mk_body_v(ifset_body, 1), false);

    PdNode *doc_kids[] = {set, ifset};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_ifset_undefined(void) {
    PdNode *p_body[] = {mk_text("defined")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *ifset_args[] = {mk_arg("name", "x")};
    PdNode *ifset_body[] = {p};
    PdNode *ifset = mk_call("ifset", ifset_args, 1,
                             mk_body_v(ifset_body, 1), false);

    PdNode *doc_kids[] = {ifset};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(0, result->as.document.count);
    pd_node_free(result);
}

/* --- Tests: Paragraph wrapping --- */

void test_eval_paragraph_becomes_p(void) {
    PdNode *para_kids[] = {mk_text("Hello world")};
    PdNode *para = mk_para(para_kids, 1);
    PdNode *doc_kids[] = {para};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    PdNode *p = result->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, p->type);
    TEST_ASSERT_EQUAL_STRING("p", p->as.macro_call.name);
    TEST_ASSERT_NOT_NULL(p->as.macro_call.body);
    TEST_ASSERT_EQUAL_INT(NODE_BODY, p->as.macro_call.body->type);
    PdNode *body = p->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(1, body->as.body.count);
    TEST_ASSERT_EQUAL_STRING("Hello world",
        body->as.body.children[0]->as.text.value);
    pd_node_free(result);
}

void test_eval_paragraph_with_inline_macro(void) {
    PdNode **iparts = malloc(sizeof(PdNode *));
    iparts[0] = mk_text("here");
    PdNode *interp = pd_node_interp_string(iparts, 1, S());
    PdNode *bold = mk_call("**", NULL, 0, interp, false);
    PdNode *para_kids[] = {mk_text("Click "), bold};
    PdNode *para = mk_para(para_kids, 2);
    PdNode *doc_kids[] = {para};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    PdNode *p = result->as.document.children[0];
    TEST_ASSERT_EQUAL_STRING("p", p->as.macro_call.name);
    TEST_ASSERT_EQUAL_INT(2, p->as.macro_call.body->as.body.count);
    pd_node_free(result);
}

/* --- Tests: User macros --- */

void test_eval_simple_variable(void) {
    PdNode *set_args[] = {mk_arg("name", "version")};
    PdNode *set_body[] = {mk_text("1.0")};
    PdNode *set = mk_call("set", set_args, 1, mk_body_v(set_body, 1), false);

    PdNode *p_body[] = {mk_text("v="), mk_call0("version")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 2), false);

    PdNode *doc_kids[] = {set, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("v=1.0", text);
    pd_node_free(result);
}

void test_eval_variable_trailing_dot(void) {
    PdNode *set_args[] = {mk_arg("name", "version")};
    PdNode *set_body[] = {mk_text("1.0")};
    PdNode *set = mk_call("set", set_args, 1, mk_body_v(set_body, 1), false);

    PdNode *p_body[] = {mk_text("v="), mk_call0("version.")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 2), false);

    PdNode *doc_kids[] = {set, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("v=1.0.", text);
    pd_node_free(result);
}

void test_eval_macro_with_required_args(void) {
    PdNode *defn_args[] = {
        mk_arg("name", "greeting"),
        mk_narg("target", pd_node_required_marker(S())),
        mk_narg("body", pd_node_required_marker(S()))
    };
    PdNode *defn_body[] = {
        mk_text("Dear "), mk_call0("target"),
        mk_text(", "), mk_call0("body")
    };
    PdNode *defn = mk_call("set", defn_args, 3,
                            mk_body_v(defn_body, 4), false);

    PdNode *call_args[] = {mk_arg("target", "World")};
    PdNode *call_body[] = {mk_text("hello")};
    PdNode *call = mk_call("greeting", call_args, 1,
                            mk_body_v(call_body, 1), true);

    PdNode *p_body[] = {call};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Dear World, hello", text);
    pd_node_free(result);
}

void test_eval_macro_with_defaults(void) {
    PdNode *defn_args[] = {
        mk_arg("name", "box"),
        mk_narg("style", mk_text("default")),
        mk_narg("body", pd_node_required_marker(S()))
    };
    PdNode *defn_body[] = {
        mk_text("("), mk_call0("style"),
        mk_text(") "), mk_call0("body")
    };
    PdNode *defn = mk_call("set", defn_args, 3,
                            mk_body_v(defn_body, 4), false);

    /* Call with default style */
    PdNode *c1_body[] = {mk_text("content")};
    PdNode *call1 = mk_call("box", NULL, 0,
                             mk_body_v(c1_body, 1), true);
    PdNode *p1_body[] = {call1};
    PdNode *p1 = mk_call("p", NULL, 0, mk_body_v(p1_body, 1), false);

    /* Call with explicit style */
    PdNode *c2_args[] = {mk_arg("style", "fancy")};
    PdNode *c2_body[] = {mk_text("other")};
    PdNode *call2 = mk_call("box", c2_args, 1,
                             mk_body_v(c2_body, 1), true);
    PdNode *p2_body[] = {call2};
    PdNode *p2 = mk_call("p", NULL, 0, mk_body_v(p2_body, 1), false);

    PdNode *doc_kids[] = {defn, p1, p2};
    PdNode *doc = mk_doc(doc_kids, 3);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("(default) content", text);
    body_text(result->as.document.children[1], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("(fancy) other", text);
    pd_node_free(result);
}

void test_eval_out_of_order_definition(void) {
    PdNode *p_body[] = {mk_call0("project")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *set_args[] = {mk_arg("name", "project")};
    PdNode *set_body[] = {mk_text("PicoDoc")};
    PdNode *set = mk_call("set", set_args, 1, mk_body_v(set_body, 1), false);

    PdNode *doc_kids[] = {p, set};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("PicoDoc", text);
    pd_node_free(result);
}

void test_eval_duplicate_definition(void) {
    PdNode *set1_args[] = {mk_arg("name", "x")};
    PdNode *set1_body[] = {mk_text("1")};
    PdNode *set1 = mk_call("set", set1_args, 1,
                            mk_body_v(set1_body, 1), false);

    PdNode *set2_args[] = {mk_arg("name", "x")};
    PdNode *set2_body[] = {mk_text("2")};
    PdNode *set2 = mk_call("set", set2_args, 1,
                            mk_body_v(set2_body, 1), false);

    PdNode *doc_kids[] = {set1, set2};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "duplicate definition: x"));
}

void test_eval_body_param_binding(void) {
    PdNode *defn_args[] = {
        mk_arg("name", "wrap"),
        mk_narg("body", pd_node_required_marker(S()))
    };
    PdNode *defn_body[] = {mk_text("["), mk_call0("body"), mk_text("]")};
    PdNode *defn = mk_call("set", defn_args, 2,
                            mk_body_v(defn_body, 3), false);

    PdNode *call_body[] = {mk_text("inside")};
    PdNode *call = mk_call("wrap", NULL, 0,
                            mk_body_v(call_body, 1), true);
    PdNode *p_body[] = {call};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("[inside]", text);
    pd_node_free(result);
}

void test_eval_scope_shadowing(void) {
    PdNode *set_x_args[] = {mk_arg("name", "x")};
    PdNode *set_x_body[] = {mk_text("global")};
    PdNode *set_x = mk_call("set", set_x_args, 1,
                              mk_body_v(set_x_body, 1), false);

    PdNode *set_show_args[] = {
        mk_arg("name", "show"),
        mk_narg("x", pd_node_required_marker(S()))
    };
    PdNode *set_show_body[] = {mk_call0("x")};
    PdNode *set_show = mk_call("set", set_show_args, 2,
                                mk_body_v(set_show_body, 1), false);

    PdNode *show_args[] = {mk_arg("x", "local")};
    PdNode *show = mk_call("show", show_args, 1, NULL, true);
    PdNode *p_body[] = {show, mk_text(" "), mk_call0("x")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 3), false);

    PdNode *doc_kids[] = {set_x, set_show, p};
    PdNode *doc = mk_doc(doc_kids, 3);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("local global", text);
    pd_node_free(result);
}

void test_eval_missing_required_arg(void) {
    PdNode *defn_args[] = {
        mk_arg("name", "greet"),
        mk_narg("target", pd_node_required_marker(S()))
    };
    PdNode *defn_body[] = {mk_text("Hi "), mk_call0("target")};
    PdNode *defn = mk_call("set", defn_args, 2,
                            mk_body_v(defn_body, 2), false);

    PdNode *call = mk_call("greet", NULL, 0, NULL, true);
    PdNode *p_body[] = {call};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "missing required argument: target"));
}

void test_eval_recursion_limit(void) {
    PdNode *defn_args[] = {mk_arg("name", "loop")};
    PdNode *defn_body[] = {mk_call0("loop")};
    PdNode *defn = mk_call("set", defn_args, 1,
                            mk_body_v(defn_body, 1), false);

    PdNode *p_body[] = {mk_call0("loop")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "macro call depth limit"));
}

void test_eval_nested_user_macro(void) {
    PdNode *inner_args[] = {
        mk_arg("name", "inner"),
        mk_narg("x", pd_node_required_marker(S()))
    };
    PdNode *inner_body[] = {mk_text("("), mk_call0("x"), mk_text(")")};
    PdNode *inner = mk_call("set", inner_args, 2,
                              mk_body_v(inner_body, 3), false);

    PdNode *outer_args[] = {
        mk_arg("name", "outer"),
        mk_narg("y", pd_node_required_marker(S()))
    };
    PdNode *inner_call_args[] = {mk_narg("x", mk_call0("y"))};
    PdNode *inner_call = mk_call("inner", inner_call_args, 1, NULL, true);
    PdNode *outer_body[] = {inner_call};
    PdNode *outer = mk_call("set", outer_args, 2,
                              mk_body_v(outer_body, 1), false);

    PdNode *call_args[] = {mk_arg("y", "val")};
    PdNode *call = mk_call("outer", call_args, 1, NULL, true);
    PdNode *p_body[] = {call};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {inner, outer, p};
    PdNode *doc = mk_doc(doc_kids, 3);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("(val)", text);
    pd_node_free(result);
}

void test_eval_macro_ref_as_arg_value(void) {
    PdNode *result = eval_ok(
        "[#set name=site-url : https://example.com]\n"
        "#p: Visit [#link to=#site-url : our site] today.\n"
    );

    PdNode *p = result->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_BODY, p->as.macro_call.body->type);

    /* Find the link node */
    PdNode *body = p->as.macro_call.body;
    PdNode *link_node = NULL;
    for (int i = 0; i < body->as.body.count; i++) {
        if (body->as.body.children[i]->type == NODE_MACRO_CALL) {
            link_node = body->as.body.children[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(link_node);

    /* Find "to" arg */
    PdNode *to_val = NULL;
    for (int i = 0; i < link_node->as.macro_call.arg_count; i++) {
        PdNode *arg = link_node->as.macro_call.args[i];
        if (strcmp(arg->as.named_arg.name, "to") == 0) {
            to_val = arg->as.named_arg.value;
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(to_val);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, to_val->type);
    TEST_ASSERT_EQUAL_STRING("https://example.com", to_val->as.text.value);
    pd_node_free(result);
}

void test_eval_chained_macro_resolution(void) {
    PdNode *set1_args[] = {mk_arg("name", "alpha")};
    PdNode *set1_body[] = {mk_text("hello")};
    PdNode *set1 = mk_call("set", set1_args, 1,
                            mk_body_v(set1_body, 1), false);

    PdNode *set2_args[] = {mk_arg("name", "beta")};
    PdNode *set2_body[] = {mk_call0("alpha")};
    PdNode *set2 = mk_call("set", set2_args, 1,
                            mk_body_v(set2_body, 1), false);

    PdNode *p_body[] = {mk_call0("beta")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {set1, set2, p};
    PdNode *doc = mk_doc(doc_kids, 3);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("hello", text);
    pd_node_free(result);
}

/* --- Tests: Environment variables --- */

void test_eval_env_from_param(void) {
    PdNode *p_body[] = {mk_text("mode="), mk_call0("env.mode")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 2), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *keys[] = {"mode"};
    const char *vals[] = {"draft"};
    PdNode *result = eval_ast_env_ok(doc, keys, vals, 1);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("mode=draft", text);
    pd_node_free(result);
}

void test_eval_env_from_set(void) {
    PdNode *set_args[] = {mk_arg("name", "env.mode")};
    PdNode *set_body[] = {mk_text("draft")};
    PdNode *set = mk_call("set", set_args, 1,
                           mk_body_v(set_body, 1), false);

    PdNode *p_body[] = {mk_call0("env.mode")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {set, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("draft", text);
    pd_node_free(result);
}

void test_eval_env_set_overrides(void) {
    PdNode *set_args[] = {mk_arg("name", "env.mode")};
    PdNode *set_body[] = {mk_text("final")};
    PdNode *set = mk_call("set", set_args, 1,
                           mk_body_v(set_body, 1), false);

    PdNode *p_body[] = {mk_call0("env.mode")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {set, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *keys[] = {"mode"};
    const char *vals[] = {"draft"};
    PdNode *result = eval_ast_env_ok(doc, keys, vals, 1);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("final", text);
    pd_node_free(result);
}

void test_eval_env_ifset_preseeded(void) {
    PdNode *p_body[] = {mk_text("yes")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *ifset_args[] = {mk_arg("name", "env.mode")};
    PdNode *ifset_body[] = {p};
    PdNode *ifset = mk_call("ifset", ifset_args, 1,
                              mk_body_v(ifset_body, 1), false);
    PdNode *doc_kids[] = {ifset};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *keys[] = {"mode"};
    const char *vals[] = {"draft"};
    PdNode *result = eval_ast_env_ok(doc, keys, vals, 1);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("yes", text);
    pd_node_free(result);
}

void test_eval_env_ifset_unset(void) {
    PdNode *p_body[] = {mk_text("yes")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *ifset_args[] = {mk_arg("name", "env.missing")};
    PdNode *ifset_body[] = {p};
    PdNode *ifset = mk_call("ifset", ifset_args, 1,
                              mk_body_v(ifset_body, 1), false);
    PdNode *doc_kids[] = {ifset};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(0, result->as.document.count);
    pd_node_free(result);
}

void test_eval_env_ifeq(void) {
    PdNode *p_body[] = {mk_text("match")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *args[] = {
        mk_narg("lhs", mk_call0("env.mode")),
        mk_arg("rhs", "draft")
    };
    PdNode *body[] = {p};
    PdNode *ifeq = mk_call("ifeq", args, 2, mk_body_v(body, 1), false);
    PdNode *doc_kids[] = {ifeq};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *keys[] = {"mode"};
    const char *vals[] = {"draft"};
    PdNode *result = eval_ast_env_ok(doc, keys, vals, 1);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("match", text);
    pd_node_free(result);
}

void test_eval_env_undefined_returns_empty(void) {
    PdNode *p_body[] = {mk_text("x"), mk_call0("env.missing"), mk_text("y")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 3), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("xy", text);
    pd_node_free(result);
}

void test_eval_env_cannot_be_shadowed(void) {
    PdNode *defn_args[] = {
        mk_arg("name", "bad"),
        mk_narg("env.mode", pd_node_required_marker(S()))
    };
    PdNode *defn_body[] = {mk_call0("env.mode")};
    PdNode *defn = mk_call("set", defn_args, 2,
                            mk_body_v(defn_body, 1), false);

    PdNode *call_args[] = {mk_arg("env.mode", "hacked")};
    PdNode *call = mk_call("bad", call_args, 1, NULL, true);
    PdNode *p_body[] = {call};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *keys[] = {"mode"};
    const char *vals[] = {"safe"};
    const char *msg = eval_ast_env_err(doc, keys, vals, 1);
    TEST_ASSERT_NOT_NULL(strstr(msg, "cannot shadow environment variable"));
}

/* --- Tests: Table expansion --- */

void test_eval_pipe_delimited_table(void) {
    PdNode *result = eval_ok("#table:\n  Name | Age\n  Alice | 30\n");

    PdNode *table = result->as.document.children[0];
    TEST_ASSERT_EQUAL_STRING("table", table->as.macro_call.name);
    PdNode *tbody = table->as.macro_call.body;
    TEST_ASSERT_NOT_NULL(tbody);

    /* Count tr rows */
    int row_count = 0;
    for (int i = 0; i < tbody->as.body.count; i++) {
        if (tbody->as.body.children[i]->type == NODE_MACRO_CALL)
            row_count++;
    }
    TEST_ASSERT_EQUAL_INT(2, row_count);

    /* First row: th cells */
    PdNode *tr1 = tbody->as.body.children[0];
    TEST_ASSERT_EQUAL_STRING("tr", tr1->as.macro_call.name);
    PdNode *tr1_body = tr1->as.macro_call.body;
    for (int i = 0; i < tr1_body->as.body.count; i++) {
        if (tr1_body->as.body.children[i]->type == NODE_MACRO_CALL) {
            TEST_ASSERT_EQUAL_STRING("th",
                tr1_body->as.body.children[i]->as.macro_call.name);
        }
    }

    /* Second row: td cells */
    PdNode *tr2 = tbody->as.body.children[1];
    TEST_ASSERT_EQUAL_STRING("tr", tr2->as.macro_call.name);
    PdNode *tr2_body = tr2->as.macro_call.body;
    for (int i = 0; i < tr2_body->as.body.count; i++) {
        if (tr2_body->as.body.children[i]->type == NODE_MACRO_CALL) {
            TEST_ASSERT_EQUAL_STRING("td",
                tr2_body->as.body.children[i]->as.macro_call.name);
        }
    }

    pd_node_free(result);
}

void test_eval_no_pipe_table(void) {
    PdNode *result = eval_ok("[#table : [#tr : [#td: Cell]]]\n");
    PdNode *table = result->as.document.children[0];
    TEST_ASSERT_EQUAL_STRING("table", table->as.macro_call.name);
    pd_node_free(result);
}

/* --- Tests: Validation — nesting --- */

void test_eval_td_outside_tr(void) {
    PdNode *td_body[] = {mk_text("cell")};
    PdNode *td = mk_call("td", NULL, 0, mk_body_v(td_body, 1), false);
    PdNode *doc_kids[] = {td};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "#td must appear inside #tr"));
}

void test_eval_tr_outside_table(void) {
    PdNode *td_body[] = {mk_text("cell")};
    PdNode *td = mk_call("td", NULL, 0, mk_body_v(td_body, 1), false);
    PdNode *tr_body[] = {td};
    PdNode *tr = mk_call("tr", NULL, 0, mk_body_v(tr_body, 1), false);
    PdNode *doc_kids[] = {tr};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "#tr must appear inside #table"));
}

void test_eval_th_outside_tr(void) {
    PdNode *th_body[] = {mk_text("header")};
    PdNode *th = mk_call("th", NULL, 0, mk_body_v(th_body, 1), false);
    PdNode *doc_kids[] = {th};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "#th must appear inside #tr"));
}

void test_eval_li_outside_list(void) {
    PdNode *li_body[] = {mk_text("item")};
    PdNode *li = mk_call("*", NULL, 0, mk_body_v(li_body, 1), false);
    PdNode *doc_kids[] = {li};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "#* must appear inside #ol or #ul"));
}

void test_eval_valid_table_nesting(void) {
    PdNode *td_body[] = {mk_text("cell")};
    PdNode *td = mk_call("td", NULL, 0, mk_body_v(td_body, 1), false);
    PdNode *tr_body[] = {td};
    PdNode *tr = mk_call("tr", NULL, 0, mk_body_v(tr_body, 1), false);
    PdNode *table_body[] = {tr};
    PdNode *table = mk_call("table", NULL, 0,
                             mk_body_v(table_body, 1), false);
    PdNode *doc_kids[] = {table};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_valid_list_nesting(void) {
    PdNode *li_body[] = {mk_text("item")};
    PdNode *li = mk_call("*", NULL, 0, mk_body_v(li_body, 1), false);
    PdNode *ul_body[] = {li};
    PdNode *ul = mk_call("ul", NULL, 0, mk_body_v(ul_body, 1), false);
    PdNode *doc_kids[] = {ul};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_li_alias(void) {
    PdNode *li_body[] = {mk_text("item")};
    PdNode *li = mk_call("li", NULL, 0, mk_body_v(li_body, 1), false);
    PdNode *ol_body[] = {li};
    PdNode *ol = mk_call("ol", NULL, 0, mk_body_v(ol_body, 1), false);
    PdNode *doc_kids[] = {ol};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_block_inside_p(void) {
    PdNode *h2_body[] = {mk_text("bad")};
    PdNode *h2 = mk_call("h2", NULL, 0, mk_body_v(h2_body, 1), false);
    PdNode *p_body[] = {h2};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg,
        "block element #h2 cannot appear inside inline element #p"));
}

void test_eval_block_inside_b(void) {
    PdNode *h2_body[] = {mk_text("bad")};
    PdNode *h2 = mk_call("h2", NULL, 0, mk_body_v(h2_body, 1), false);
    PdNode *b_body[] = {h2};
    PdNode *b = mk_call("b", NULL, 0, mk_body_v(b_body, 1), false);
    PdNode *p_body[] = {b};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg,
        "block element #h2 cannot appear inside inline element #b"));
}

void test_eval_block_inside_span(void) {
    PdNode *p_body[] = {mk_text("bad")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *span_body[] = {p};
    PdNode *span = mk_call("span", NULL, 0,
                            mk_body_v(span_body, 1), false);
    PdNode *doc_kids[] = {span};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg,
        "block element #p cannot appear inside inline element #span"));
}

void test_eval_block_inside_div_ok(void) {
    PdNode *p_body[] = {mk_text("ok")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *div_body[] = {p};
    PdNode *div = mk_call("div", NULL, 0, mk_body_v(div_body, 1), false);
    PdNode *doc_kids[] = {div};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_block_inside_header_ok(void) {
    PdNode *h1_body[] = {mk_text("ok")};
    PdNode *h1 = mk_call("h1", NULL, 0, mk_body_v(h1_body, 1), false);
    PdNode *header_body[] = {h1};
    PdNode *header = mk_call("header", NULL, 0,
                              mk_body_v(header_body, 1), false);
    PdNode *doc_kids[] = {header};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_h1_at_top_level_ok(void) {
    PdNode *h1_body[] = {mk_text("ok")};
    PdNode *h1 = mk_call("h1", NULL, 0, mk_body_v(h1_body, 1), false);
    PdNode *doc_kids[] = {h1};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_block_code_inside_p(void) {
    PdNode *code_body[] = {mk_text("bad")};
    PdNode *code = mk_call("code", NULL, 0,
                            mk_body_v(code_body, 1), false);
    PdNode *p_body[] = {code};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg,
        "block element #code cannot appear inside inline element #p"));
}

void test_eval_inline_code_inside_p_ok(void) {
    PdNode *tilde_body[] = {mk_text("ok")};
    PdNode *tilde = mk_call("~", NULL, 0,
                              mk_body_v(tilde_body, 1), false);
    PdNode *p_body[] = {tilde};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

/* --- Tests: Validation — doc.content --- */

void test_eval_doc_content_duplicate(void) {
    PdNode *dc1_args[] = {mk_arg("type", "main")};
    PdNode *dc1 = mk_call("doc.content", dc1_args, 1, NULL, false);
    PdNode *dc2_args[] = {mk_arg("type", "div")};
    PdNode *dc2 = mk_call("doc.content", dc2_args, 1, NULL, false);
    PdNode *p_body[] = {mk_text("Hello")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {dc1, dc2, p};
    PdNode *doc = mk_doc(doc_kids, 3);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "duplicate #doc.content"));
}

void test_eval_doc_content_invalid_type(void) {
    PdNode *dc_args[] = {mk_arg("type", "banana")};
    PdNode *dc = mk_call("doc.content", dc_args, 1, NULL, false);
    PdNode *p_body[] = {mk_text("Hello")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {dc, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "invalid doc.content type 'banana'"));
}

void test_eval_doc_content_valid_type(void) {
    PdNode *dc_args[] = {mk_arg("type", "main")};
    PdNode *dc = mk_call("doc.content", dc_args, 1, NULL, false);
    PdNode *p_body[] = {mk_text("Hello")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {dc, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    /* Check doc.content is preserved */
    bool found = false;
    for (int i = 0; i < result->as.document.count; i++) {
        if (strcmp(result->as.document.children[i]->as.macro_call.name,
                   "doc.content") == 0)
            found = true;
    }
    TEST_ASSERT_TRUE(found);
    pd_node_free(result);
}

/* --- Tests: Validation — builtin args --- */

void test_eval_unknown_arg_raises(void) {
    PdNode *args[] = {mk_arg("bogus", "val")};
    PdNode *p_body[] = {mk_text("hi")};
    PdNode *p = mk_call("p", args, 1, mk_body_v(p_body, 1), false);
    PdNode *doc_kids[] = {p};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "unknown argument 'bogus' for #p"));
}

void test_eval_known_arg_accepted(void) {
    PdNode *args[] = {mk_arg("to", "https://x.com")};
    PdNode *link_body[] = {mk_text("click")};
    PdNode *link = mk_call("link", args, 1,
                            mk_body_v(link_body, 1), false);
    PdNode *doc_kids[] = {link};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

void test_eval_set_allows_extra_args(void) {
    PdNode *defn_args[] = {
        mk_arg("name", "g"),
        mk_narg("who", pd_node_required_marker(S()))
    };
    PdNode *defn_body[] = {mk_text("hi")};
    PdNode *defn = mk_call("set", defn_args, 2,
                            mk_body_v(defn_body, 1), false);

    PdNode *call_args[] = {mk_arg("who", "World")};
    PdNode *call = mk_call("g", call_args, 1, NULL, false);
    PdNode *p_body[] = {call};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    pd_node_free(result);
}

/* --- Tests: Alias resolution in eval --- */

void test_eval_dash_alias(void) {
    /* Use AST-level test since parser doesn't support #- syntax */
    PdNode *body[] = {mk_text("Title")};
    PdNode *h = mk_call("-", NULL, 0, mk_body_v(body, 1), false);
    PdNode *doc_kids[] = {h};
    PdNode *doc = mk_doc(doc_kids, 1);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    PdNode *h_out = result->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, h_out->type);
    /* Alias "-" is resolved to "h1" at expansion time,
     * but the output keeps the original name for rendering. */
    pd_node_free(result);
}

/* --- Tests: End-to-end --- */

void test_eval_e2e_simple(void) {
    PdNode *result = eval_ok(
        "#doc.title: My Page\n"
        "#h1: Welcome\n"
        "\n"
        "This is a paragraph.\n"
    );
    TEST_ASSERT_TRUE(result->as.document.count >= 3);
    pd_node_free(result);
}

void test_eval_e2e_with_set_and_conditional(void) {
    PdNode *result = eval_ok(
        "[#set name=mode : draft]\n"
        "[#ifeq lhs=#mode rhs=draft : [#p: DRAFT MODE]]\n"
        "#p: Content here\n"
    );
    /* Should have the conditional p and the content p */
    TEST_ASSERT_TRUE(result->as.document.count >= 2);
    pd_node_free(result);
}

void test_eval_e2e_user_macro_in_table(void) {
    PdNode *result = eval_ok(
        "[#set name=status : Active]\n"
        "#table:\n"
        "  Name | Status\n"
        "  Alice | #status\n"
    );
    PdNode *table = result->as.document.children[0];
    TEST_ASSERT_EQUAL_STRING("table", table->as.macro_call.name);
    PdNode *tbody = table->as.macro_call.body;
    TEST_ASSERT_NOT_NULL(tbody);

    int row_count = 0;
    for (int i = 0; i < tbody->as.body.count; i++) {
        if (tbody->as.body.children[i]->type == NODE_MACRO_CALL)
            row_count++;
    }
    TEST_ASSERT_EQUAL_INT(2, row_count);

    /* Second row, second cell should contain "Active" */
    PdNode *tr2 = tbody->as.body.children[1];
    PdNode *tr2_body = tr2->as.macro_call.body;
    int cell_idx = 0;
    for (int i = 0; i < tr2_body->as.body.count; i++) {
        if (tr2_body->as.body.children[i]->type == NODE_MACRO_CALL) {
            if (cell_idx == 1) {
                PdNode *cell = tr2_body->as.body.children[i];
                char text[256];
                body_text(cell, text, sizeof(text));
                TEST_ASSERT_EQUAL_STRING("Active", text);
            }
            cell_idx++;
        }
    }
    pd_node_free(result);
}

void test_eval_e2e_nested_macro_in_conditional(void) {
    PdNode *set1_args[] = {mk_arg("name", "mode")};
    PdNode *set1_body[] = {mk_text("draft")};
    PdNode *set1 = mk_call("set", set1_args, 1,
                            mk_body_v(set1_body, 1), false);

    PdNode *set2_args[] = {mk_arg("name", "label")};
    PdNode *set2_body[] = {mk_text("DRAFT")};
    PdNode *set2 = mk_call("set", set2_args, 1,
                            mk_body_v(set2_body, 1), false);

    PdNode *p_body[] = {mk_call0("label")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);
    PdNode *ifeq_args[] = {
        mk_narg("lhs", mk_call0("mode")),
        mk_arg("rhs", "draft")
    };
    PdNode *ifeq_body[] = {p};
    PdNode *ifeq = mk_call("ifeq", ifeq_args, 2,
                            mk_body_v(ifeq_body, 1), false);

    PdNode *doc_kids[] = {set1, set2, ifeq};
    PdNode *doc = mk_doc(doc_kids, 3);

    PdNode *result = eval_ast_ok(doc);
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("DRAFT", text);
    pd_node_free(result);
}

void test_eval_mutual_recursion(void) {
    PdNode *ping_args[] = {mk_arg("name", "ping")};
    PdNode *ping_body[] = {mk_call0("pong")};
    PdNode *ping = mk_call("set", ping_args, 1,
                            mk_body_v(ping_body, 1), false);

    PdNode *pong_args[] = {mk_arg("name", "pong")};
    PdNode *pong_body[] = {mk_call0("ping")};
    PdNode *pong = mk_call("set", pong_args, 1,
                            mk_body_v(pong_body, 1), false);

    PdNode *p_body[] = {mk_call0("ping")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {ping, pong, p};
    PdNode *doc = mk_doc(doc_kids, 3);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "macro call depth limit"));
}

void test_eval_td_in_table_not_tr(void) {
    PdNode *td_body[] = {mk_text("cell")};
    PdNode *td = mk_call("td", NULL, 0, mk_body_v(td_body, 1), false);
    PdNode *table_body[] = {td};
    PdNode *table = mk_call("table", NULL, 0,
                             mk_body_v(table_body, 1), false);
    PdNode *doc_kids[] = {table};
    PdNode *doc = mk_doc(doc_kids, 1);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "#td must appear inside #tr"));
}

void test_eval_pipe_table_valid(void) {
    PdNode *result = eval_ok("#table:\n  Name | Age\n  Alice | 30\n");
    TEST_ASSERT_EQUAL_INT(1, result->as.document.count);
    TEST_ASSERT_EQUAL_STRING("table",
        result->as.document.children[0]->as.macro_call.name);
    pd_node_free(result);
}

void test_eval_env_inherited_in_user_macro(void) {
    PdNode *defn_args[] = {mk_arg("name", "show-mode")};
    PdNode *defn_body[] = {mk_call0("env.mode")};
    PdNode *defn = mk_call("set", defn_args, 1,
                            mk_body_v(defn_body, 1), false);

    PdNode *p_body[] = {mk_call0("show-mode")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *keys[] = {"mode"};
    const char *vals[] = {"draft"};
    PdNode *result = eval_ast_env_ok(doc, keys, vals, 1);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("draft", text);
    pd_node_free(result);
}

void test_eval_string_literal_body_set(void) {
    PdNode *defn_args[] = {mk_arg("name", "motto")};
    PdNode **iparts = malloc(sizeof(PdNode *));
    iparts[0] = mk_text("Write less, mean more.");
    PdNode *interp = pd_node_interp_string(iparts, 1, S());
    PdNode *defn = mk_call("set", defn_args, 1, interp, false);

    PdNode *p_body[] = {mk_call0("motto")};
    PdNode *p = mk_call("p", NULL, 0, mk_body_v(p_body, 1), false);

    PdNode *doc_kids[] = {defn, p};
    PdNode *doc = mk_doc(doc_kids, 2);

    PdNode *result = eval_ast_ok(doc);
    char text[256];
    body_text(result->as.document.children[0], text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Write less, mean more.", text);
    pd_node_free(result);
}

void test_eval_env_duplicate_set_error(void) {
    PdNode *set1_args[] = {mk_arg("name", "env.mode")};
    PdNode *set1_body[] = {mk_text("a")};
    PdNode *set1 = mk_call("set", set1_args, 1,
                            mk_body_v(set1_body, 1), false);

    PdNode *set2_args[] = {mk_arg("name", "env.mode")};
    PdNode *set2_body[] = {mk_text("b")};
    PdNode *set2 = mk_call("set", set2_args, 1,
                            mk_body_v(set2_body, 1), false);

    PdNode *doc_kids[] = {set1, set2};
    PdNode *doc = mk_doc(doc_kids, 2);

    const char *msg = eval_ast_err(doc);
    TEST_ASSERT_NOT_NULL(strstr(msg, "duplicate definition: env.mode"));
}

/* --- Test runner --- */

void run_test_eval(void) {
    /* Comments */
    RUN_TEST(test_eval_comment_removed);
    RUN_TEST(test_eval_comment_alias_removed);
    RUN_TEST(test_eval_comment_in_body_removed);

    /* Set collection */
    RUN_TEST(test_eval_set_collected_and_removed);
    RUN_TEST(test_eval_set_value_available_for_ifset);
    RUN_TEST(test_eval_set_from_parse);

    /* Conditionals */
    RUN_TEST(test_eval_ifeq_true);
    RUN_TEST(test_eval_ifeq_false);
    RUN_TEST(test_eval_ifne_true);
    RUN_TEST(test_eval_ifne_false);
    RUN_TEST(test_eval_ifset_defined);
    RUN_TEST(test_eval_ifset_undefined);

    /* Paragraph wrapping */
    RUN_TEST(test_eval_paragraph_becomes_p);
    RUN_TEST(test_eval_paragraph_with_inline_macro);

    /* User macros */
    RUN_TEST(test_eval_simple_variable);
    RUN_TEST(test_eval_variable_trailing_dot);
    RUN_TEST(test_eval_macro_with_required_args);
    RUN_TEST(test_eval_macro_with_defaults);
    RUN_TEST(test_eval_out_of_order_definition);
    RUN_TEST(test_eval_duplicate_definition);
    RUN_TEST(test_eval_body_param_binding);
    RUN_TEST(test_eval_scope_shadowing);
    RUN_TEST(test_eval_missing_required_arg);
    RUN_TEST(test_eval_recursion_limit);
    RUN_TEST(test_eval_nested_user_macro);
    RUN_TEST(test_eval_macro_ref_as_arg_value);
    RUN_TEST(test_eval_chained_macro_resolution);
    RUN_TEST(test_eval_string_literal_body_set);
    RUN_TEST(test_eval_mutual_recursion);

    /* Environment variables */
    RUN_TEST(test_eval_env_from_param);
    RUN_TEST(test_eval_env_from_set);
    RUN_TEST(test_eval_env_set_overrides);
    RUN_TEST(test_eval_env_ifset_preseeded);
    RUN_TEST(test_eval_env_ifset_unset);
    RUN_TEST(test_eval_env_ifeq);
    RUN_TEST(test_eval_env_undefined_returns_empty);
    RUN_TEST(test_eval_env_cannot_be_shadowed);
    RUN_TEST(test_eval_env_inherited_in_user_macro);
    RUN_TEST(test_eval_env_duplicate_set_error);

    /* Table expansion */
    RUN_TEST(test_eval_pipe_delimited_table);
    RUN_TEST(test_eval_no_pipe_table);
    RUN_TEST(test_eval_pipe_table_valid);

    /* Validation — nesting */
    RUN_TEST(test_eval_td_outside_tr);
    RUN_TEST(test_eval_tr_outside_table);
    RUN_TEST(test_eval_th_outside_tr);
    RUN_TEST(test_eval_li_outside_list);
    RUN_TEST(test_eval_td_in_table_not_tr);
    RUN_TEST(test_eval_valid_table_nesting);
    RUN_TEST(test_eval_valid_list_nesting);
    RUN_TEST(test_eval_li_alias);
    RUN_TEST(test_eval_block_inside_p);
    RUN_TEST(test_eval_block_inside_b);
    RUN_TEST(test_eval_block_inside_span);
    RUN_TEST(test_eval_block_inside_div_ok);
    RUN_TEST(test_eval_block_inside_header_ok);
    RUN_TEST(test_eval_h1_at_top_level_ok);
    RUN_TEST(test_eval_block_code_inside_p);
    RUN_TEST(test_eval_inline_code_inside_p_ok);

    /* Validation — doc.content */
    RUN_TEST(test_eval_doc_content_duplicate);
    RUN_TEST(test_eval_doc_content_invalid_type);
    RUN_TEST(test_eval_doc_content_valid_type);

    /* Validation — builtin args */
    RUN_TEST(test_eval_unknown_arg_raises);
    RUN_TEST(test_eval_known_arg_accepted);
    RUN_TEST(test_eval_set_allows_extra_args);

    /* Alias resolution in eval */
    RUN_TEST(test_eval_dash_alias);

    /* End-to-end */
    RUN_TEST(test_eval_e2e_simple);
    RUN_TEST(test_eval_e2e_with_set_and_conditional);
    RUN_TEST(test_eval_e2e_user_macro_in_table);
    RUN_TEST(test_eval_e2e_nested_macro_in_conditional);
}
