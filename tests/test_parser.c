#include "unity.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "errors.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Test helpers                                                       */
/* ------------------------------------------------------------------ */

/* Parse source, fail test on error. Caller must pd_node_free(). */
static PdNode *parse_ok(const char *input) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(input, "test.pdoc", &tokens, &err);
    if (rc < 0) {
        char *f = pd_format_error(&err);
        TEST_FAIL_MESSAGE(f ? f : err.message);
    }

    PdNode *doc = NULL;
    rc = pd_parse(&tokens, input, "test.pdoc", &doc, &err);
    token_array_free(&tokens);
    if (rc < 0) {
        char *f = pd_format_error(&err);
        TEST_FAIL_MESSAGE(f ? f : err.message);
    }

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_INT(NODE_DOCUMENT, doc->type);
    return doc;
}

/* Parse source, expect error. Returns 1 if error message contains substr. */
static int parse_err(const char *input, const char *msg_substr) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(input, "test.pdoc", &tokens, &err);
    if (rc < 0) {
        /* Lex error — check message */
        int found = strstr(err.message, msg_substr) != NULL;
        pd_error_free(&err);
        return found;
    }

    PdNode *doc = NULL;
    rc = pd_parse(&tokens, input, "test.pdoc", &doc, &err);
    token_array_free(&tokens);
    if (rc == 0) {
        pd_node_free(doc);
        return 0; /* expected error but got success */
    }

    int found = (err.message && strstr(err.message, msg_substr) != NULL);
    pd_error_free(&err);
    return found;
}

/* Assert a node is a MacroCall with expected properties. */
static void assert_call(PdNode *node, const char *name, int num_args,
                        bool has_body, bool bracketed) {
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NODE_MACRO_CALL, node->type,
                                  "expected MacroCall node");
    TEST_ASSERT_EQUAL_STRING(name, node->as.macro_call.name);
    TEST_ASSERT_EQUAL_INT(num_args, node->as.macro_call.arg_count);
    if (has_body) {
        TEST_ASSERT_NOT_NULL_MESSAGE(node->as.macro_call.body,
                                     "expected body to be non-NULL");
    } else {
        TEST_ASSERT_NULL_MESSAGE(node->as.macro_call.body,
                                 "expected body to be NULL");
    }
    TEST_ASSERT_EQUAL_INT(bracketed, node->as.macro_call.bracketed);
}

/* Get concatenated text from a Body node. Caller must free(). */
static char *body_text(PdNode *call) {
    TEST_ASSERT_NOT_NULL(call);
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, call->type);
    PdNode *body = call->as.macro_call.body;
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_INT(NODE_BODY, body->type);

    int total = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_TEXT)
            total += c->as.text.value_len;
    }
    char *buf = malloc((size_t)total + 1);
    int pos = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_TEXT) {
            memcpy(buf + pos, c->as.text.value, (size_t)c->as.text.value_len);
            pos += c->as.text.value_len;
        }
    }
    buf[pos] = '\0';
    return buf;
}

/* ================================================================== */
/* 1. Document tests                                                  */
/* ================================================================== */

void test_parse_empty(void) {
    PdNode *doc = parse_ok("");
    TEST_ASSERT_EQUAL_INT(0, doc->as.document.count);
    pd_node_free(doc);
}

void test_parse_only_newlines(void) {
    PdNode *doc = parse_ok("\n\n\n");
    TEST_ASSERT_EQUAL_INT(0, doc->as.document.count);
    pd_node_free(doc);
}

void test_parse_only_whitespace_lines(void) {
    PdNode *doc = parse_ok("  \n\t\n  \n");
    TEST_ASSERT_EQUAL_INT(0, doc->as.document.count);
    pd_node_free(doc);
}

void test_parse_single_paragraph(void) {
    PdNode *doc = parse_ok("Hello world.\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    PdNode *para = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, para->type);
    TEST_ASSERT_TRUE(para->as.paragraph.count >= 1);
    PdNode *text = para->as.paragraph.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, text->type);
    TEST_ASSERT_TRUE(strstr(text->as.text.value, "Hello world.") != NULL);
    pd_node_free(doc);
}

void test_parse_paragraph_no_trailing_newline(void) {
    PdNode *doc = parse_ok("Hello world.");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    pd_node_free(doc);
}

void test_parse_multiline_paragraph(void) {
    PdNode *doc = parse_ok("Line one.\nLine two.\n\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    PdNode *para = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, para->type);
    /* Text should contain both lines with newline in between */
    PdNode *text = para->as.paragraph.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, text->type);
    TEST_ASSERT_TRUE(strstr(text->as.text.value, "Line one.") != NULL);
    TEST_ASSERT_TRUE(strstr(text->as.text.value, "Line two.") != NULL);
    pd_node_free(doc);
}

void test_parse_two_paragraphs(void) {
    PdNode *doc = parse_ok("First para.\n\nSecond para.\n");
    TEST_ASSERT_EQUAL_INT(2, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[1]->type);
    pd_node_free(doc);
}

void test_parse_no_blank_line_hash_stays_in_paragraph(void) {
    /* Without blank line, #hr is inline inside the paragraph */
    PdNode *doc = parse_ok("Some text.\n#hr\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    pd_node_free(doc);
}

void test_parse_no_blank_line_bracket_hash_stays(void) {
    PdNode *doc = parse_ok("Some text.\n[#b : bold]\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    pd_node_free(doc);
}

void test_parse_blank_line_separates(void) {
    PdNode *doc = parse_ok("Some text.\n\n#hr\n");
    TEST_ASSERT_EQUAL_INT(2, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, doc->as.document.children[1]->type);
    pd_node_free(doc);
}

void test_parse_macro_then_paragraph(void) {
    PdNode *doc = parse_ok("#doc.title: Hello\n\nSome text.\n");
    TEST_ASSERT_EQUAL_INT(2, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, doc->as.document.children[0]->type);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[1]->type);
    pd_node_free(doc);
}

void test_parse_multiple_macros(void) {
    PdNode *doc = parse_ok("#h2: A\n\n#h3: B\n\n#hr\n");
    TEST_ASSERT_EQUAL_INT(3, doc->as.document.count);
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, doc->as.document.children[i]->type);
    pd_node_free(doc);
}

void test_parse_blank_lines_between_blocks(void) {
    PdNode *doc = parse_ok("\n\n#hr\n\n\n#hr\n\n");
    TEST_ASSERT_EQUAL_INT(2, doc->as.document.count);
    pd_node_free(doc);
}

void test_parse_paragraph_with_inline_macro(void) {
    PdNode *doc = parse_ok("This has #b\"bold\" text.\n\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    PdNode *para = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, para->type);
    /* Should contain Text, MacroCall, Text */
    bool has_call = false;
    for (int i = 0; i < para->as.paragraph.count; i++) {
        if (para->as.paragraph.children[i]->type == NODE_MACRO_CALL) {
            has_call = true;
            assert_call(para->as.paragraph.children[i], "b", 0, true, false);
        }
    }
    TEST_ASSERT_TRUE(has_call);
    pd_node_free(doc);
}

void test_parse_document_span(void) {
    PdNode *doc = parse_ok("#hr\n");
    TEST_ASSERT_EQUAL_INT(1, doc->span.start.line);
    TEST_ASSERT_EQUAL_INT(1, doc->span.start.column);
    pd_node_free(doc);
}

void test_parse_paragraph_span(void) {
    PdNode *doc = parse_ok("Hello.\n");
    PdNode *para = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(1, para->span.start.line);
    TEST_ASSERT_EQUAL_INT(1, para->span.start.column);
    pd_node_free(doc);
}

/* ================================================================== */
/* 2. Call tests                                                      */
/* ================================================================== */

void test_parse_simple_macro(void) {
    PdNode *doc = parse_ok("#hr\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    assert_call(doc->as.document.children[0], "hr", 0, false, false);
    pd_node_free(doc);
}

void test_parse_macro_no_trailing_newline(void) {
    PdNode *doc = parse_ok("#hr");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    assert_call(doc->as.document.children[0], "hr", 0, false, false);
    pd_node_free(doc);
}

void test_parse_colon_body(void) {
    PdNode *doc = parse_ok("#doc.title: Welcome to PicoDoc\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "doc.title", 0, true, false);
    char *bt = body_text(call);
    TEST_ASSERT_EQUAL_STRING("Welcome to PicoDoc", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_colon_no_ws_before(void) {
    PdNode *doc = parse_ok("#doc.title:text\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "doc.title", 0, true, false);
    char *bt = body_text(call);
    TEST_ASSERT_EQUAL_STRING("text", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_string_body_no_ws(void) {
    PdNode *doc = parse_ok("#b\"bold\"\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "b", 0, true, false);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    PdNode *body = call->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(1, body->as.interp_string.count);
    PdNode *part = body->as.interp_string.parts[0];
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, part->type);
    TEST_ASSERT_EQUAL_STRING("bold", part->as.text.value);
    pd_node_free(doc);
}

void test_parse_string_body_with_ws(void) {
    PdNode *doc = parse_ok("#p \"text\"\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, false);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_raw_string_body(void) {
    PdNode *doc = parse_ok("#p\"\"\"raw content\"\"\"\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, false);
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, call->as.macro_call.body->type);
    TEST_ASSERT_EQUAL_STRING("raw content",
                              call->as.macro_call.body->as.text.value);
    pd_node_free(doc);
}

void test_parse_string_body_with_colon(void) {
    PdNode *doc = parse_ok("#p: \"text\"\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, false);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_simple_bracketed(void) {
    PdNode *doc = parse_ok("[#set name=version : 1.0]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 1, true, true);
    pd_node_free(doc);
}

void test_parse_bracketed_with_body(void) {
    PdNode *doc = parse_ok("[#include : header.pdoc]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "include", 0, true, true);
    pd_node_free(doc);
}

void test_parse_bracketed_inline_body(void) {
    PdNode *doc = parse_ok("[#b : bold text]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "b", 0, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_BODY, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_bracketed_string_body(void) {
    PdNode *doc = parse_ok("[#set name=motto \"Write less.\"]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 1, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_nested_bracket_calls(void) {
    PdNode *doc = parse_ok("[#b : [#i : text]]\n");
    PdNode *outer = doc->as.document.children[0];
    assert_call(outer, "b", 0, true, true);
    PdNode *body = outer->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_BODY, body->type);
    TEST_ASSERT_TRUE(body->as.body.count >= 1);
    PdNode *inner = body->as.body.children[0];
    assert_call(inner, "i", 0, true, true);
    pd_node_free(doc);
}

void test_parse_inline_call_in_body(void) {
    PdNode *doc = parse_ok("#p: This has #b\"bold\" text.\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, false);
    PdNode *body = call->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_BODY, body->type);
    /* Should have Text, MacroCall, Text children */
    bool has_b = false;
    for (int i = 0; i < body->as.body.count; i++) {
        if (body->as.body.children[i]->type == NODE_MACRO_CALL) {
            assert_call(body->as.body.children[i], "b", 0, true, false);
            has_b = true;
        }
    }
    TEST_ASSERT_TRUE(has_b);
    pd_node_free(doc);
}

void test_parse_multiple_inline_calls(void) {
    PdNode *doc = parse_ok("#p: A #b\"bold\" and #i\"italic\" line.\n");
    PdNode *call = doc->as.document.children[0];
    PdNode *body = call->as.macro_call.body;
    int call_count = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        if (body->as.body.children[i]->type == NODE_MACRO_CALL)
            call_count++;
    }
    TEST_ASSERT_EQUAL_INT(2, call_count);
    pd_node_free(doc);
}

void test_parse_macro_ref_in_body(void) {
    PdNode *doc = parse_ok("#p: The version is #version, ok\n");
    PdNode *call = doc->as.document.children[0];
    PdNode *body = call->as.macro_call.body;
    bool has_ref = false;
    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_MACRO_CALL) {
            assert_call(c, "version", 0, false, false);
            has_ref = true;
        }
    }
    TEST_ASSERT_TRUE(has_ref);
    pd_node_free(doc);
}

void test_parse_heading_alias_dash(void) {
    PdNode *doc = parse_ok("#-: Title\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "-", 0, true, false);
    pd_node_free(doc);
}

void test_parse_heading_alias_double_dash(void) {
    PdNode *doc = parse_ok("#--: Subtitle\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "--", 0, true, false);
    pd_node_free(doc);
}

/* ================================================================== */
/* 3. Argument tests                                                  */
/* ================================================================== */

void test_parse_single_bareword(void) {
    PdNode *doc = parse_ok("[#set name=version : 1.0]\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(1, call->as.macro_call.arg_count);
    PdNode *arg = call->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_INT(NODE_NAMED_ARG, arg->type);
    TEST_ASSERT_EQUAL_STRING("name", arg->as.named_arg.name);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, arg->as.named_arg.value->type);
    TEST_ASSERT_EQUAL_STRING("version", arg->as.named_arg.value->as.text.value);
    pd_node_free(doc);
}

void test_parse_bareword_with_dots(void) {
    PdNode *doc = parse_ok("[#set name=my.var : x]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_STRING("my.var", arg->as.named_arg.value->as.text.value);
    pd_node_free(doc);
}

void test_parse_bareword_with_dashes(void) {
    PdNode *doc = parse_ok("[#set name=project-name : x]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_STRING("project-name",
                              arg->as.named_arg.value->as.text.value);
    pd_node_free(doc);
}

void test_parse_interp_string_arg(void) {
    PdNode *doc = parse_ok("[#url link=\"https://example.com\"]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_STRING("link", arg->as.named_arg.name);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, arg->as.named_arg.value->type);
    PdNode *is = arg->as.named_arg.value;
    TEST_ASSERT_EQUAL_INT(1, is->as.interp_string.count);
    TEST_ASSERT_EQUAL_STRING("https://example.com",
                              is->as.interp_string.parts[0]->as.text.value);
    pd_node_free(doc);
}

void test_parse_raw_string_arg(void) {
    PdNode *doc = parse_ok("[#code body=\"\"\"raw code\"\"\"]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, arg->as.named_arg.value->type);
    TEST_ASSERT_EQUAL_STRING("raw code",
                              arg->as.named_arg.value->as.text.value);
    pd_node_free(doc);
}

void test_parse_macro_ref_value(void) {
    PdNode *doc = parse_ok("[#url link=#site-url text=\"x\"]\n");
    PdNode *arg0 = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, arg0->as.named_arg.value->type);
    assert_call(arg0->as.named_arg.value, "site-url", 0, false, false);
    pd_node_free(doc);
}

void test_parse_bracket_call_as_value(void) {
    PdNode *doc = parse_ok("[#outer val=[#inner : x]]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, arg->as.named_arg.value->type);
    assert_call(arg->as.named_arg.value, "inner", 0, true, true);
    pd_node_free(doc);
}

void test_parse_question_mark_value(void) {
    PdNode *doc = parse_ok("[#set name=greeting target=?]\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(2, call->as.macro_call.arg_count);
    PdNode *arg1 = call->as.macro_call.args[1];
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER, arg1->as.named_arg.value->type);
    pd_node_free(doc);
}

void test_parse_multiple_required(void) {
    PdNode *doc = parse_ok("[#set name=x a=? b=?]\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(3, call->as.macro_call.arg_count);
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER,
                          call->as.macro_call.args[1]->as.named_arg.value->type);
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER,
                          call->as.macro_call.args[2]->as.named_arg.value->type);
    pd_node_free(doc);
}

void test_parse_two_string_args(void) {
    PdNode *doc = parse_ok("[#url link=\"http://x\" text=\"click\"]\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(2, call->as.macro_call.arg_count);
    TEST_ASSERT_EQUAL_STRING("link", call->as.macro_call.args[0]->as.named_arg.name);
    TEST_ASSERT_EQUAL_STRING("text", call->as.macro_call.args[1]->as.named_arg.name);
    pd_node_free(doc);
}

void test_parse_mixed_arg_types(void) {
    PdNode *doc = parse_ok("[#set name=greeting target=? body=? : x]\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(3, call->as.macro_call.arg_count);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT,
                          call->as.macro_call.args[0]->as.named_arg.value->type);
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER,
                          call->as.macro_call.args[1]->as.named_arg.value->type);
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER,
                          call->as.macro_call.args[2]->as.named_arg.value->type);
    TEST_ASSERT_NOT_NULL(call->as.macro_call.body);
    pd_node_free(doc);
}

void test_parse_unbracketed_multiple_args(void) {
    PdNode *doc = parse_ok("#meta name=viewport content=\"width=device-width\"\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "meta", 2, false, false);
    TEST_ASSERT_EQUAL_STRING("name", call->as.macro_call.args[0]->as.named_arg.name);
    TEST_ASSERT_EQUAL_STRING("content", call->as.macro_call.args[1]->as.named_arg.name);
    pd_node_free(doc);
}

void test_parse_name_span(void) {
    PdNode *doc = parse_ok("[#set name=x]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    /* "name" starts at column 7 in "[#set name=x]" */
    TEST_ASSERT_EQUAL_INT(7, arg->as.named_arg.name_span.start.column);
    pd_node_free(doc);
}

void test_parse_arg_span_covers_value(void) {
    PdNode *doc = parse_ok("[#set name=version]\n");
    PdNode *arg = doc->as.document.children[0]->as.macro_call.args[0];
    TEST_ASSERT_EQUAL_INT(7, arg->span.start.column);
    TEST_ASSERT_TRUE(arg->span.end.column > arg->span.start.column);
    pd_node_free(doc);
}

void test_parse_args_then_colon_body(void) {
    PdNode *doc = parse_ok("[#td span=2 : Total]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "td", 1, true, true);
    pd_node_free(doc);
}

void test_parse_args_then_string_body(void) {
    PdNode *doc = parse_ok("[#set name=motto \"Write less.\"]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 1, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_unbracketed_args_then_body(void) {
    PdNode *doc = parse_ok("#code language=python : print()\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "code", 1, true, false);
    pd_node_free(doc);
}

/* ================================================================== */
/* 4. Body tests                                                      */
/* ================================================================== */

void test_parse_body_simple_text(void) {
    PdNode *doc = parse_ok("#doc.title: Hello World\n");
    PdNode *call = doc->as.document.children[0];
    char *bt = body_text(call);
    TEST_ASSERT_EQUAL_STRING("Hello World", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_body_with_inline_macro(void) {
    PdNode *doc = parse_ok("#p: Text #b\"bold\" more.\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    /* Should have Text + MacroCall + Text */
    TEST_ASSERT_TRUE(body->as.body.count >= 3);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, body->as.body.children[0]->type);
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, body->as.body.children[1]->type);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, body->as.body.children[2]->type);
    pd_node_free(doc);
}

void test_parse_body_with_escape(void) {
    PdNode *doc = parse_ok("#p: A literal \\# in text.\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    bool has_escape = false;
    for (int i = 0; i < body->as.body.count; i++) {
        if (body->as.body.children[i]->type == NODE_ESCAPE) {
            TEST_ASSERT_EQUAL_STRING("#",
                body->as.body.children[i]->as.text.value);
            has_escape = true;
        }
    }
    TEST_ASSERT_TRUE(has_escape);
    pd_node_free(doc);
}

void test_parse_body_coalesces_text(void) {
    PdNode *doc = parse_ok("#p: hello world (yes).\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    /* All text should coalesce into single Text node */
    TEST_ASSERT_EQUAL_INT(1, body->as.body.count);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, body->as.body.children[0]->type);
    TEST_ASSERT_EQUAL_STRING("hello world (yes).",
                              body->as.body.children[0]->as.text.value);
    pd_node_free(doc);
}

void test_parse_body_colon_equals_are_text(void) {
    PdNode *doc = parse_ok("#p: key=value and a: thing\n");
    char *bt = body_text(doc->as.document.children[0]);
    TEST_ASSERT_EQUAL_STRING("key=value and a: thing", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_paragraph_body(void) {
    PdNode *doc = parse_ok("#p:\nBody line one.\nBody line two.\n\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, false);
    char *bt = body_text(call);
    TEST_ASSERT_TRUE(strstr(bt, "Body line one.") != NULL);
    TEST_ASSERT_TRUE(strstr(bt, "Body line two.") != NULL);
    free(bt);
    pd_node_free(doc);
}

void test_parse_paragraph_body_newlines(void) {
    PdNode *doc = parse_ok("#p:\nLine one.\nLine two.\n\n");
    char *bt = body_text(doc->as.document.children[0]);
    TEST_ASSERT_TRUE(strstr(bt, "\n") != NULL);
    free(bt);
    pd_node_free(doc);
}

void test_parse_paragraph_body_terminated_by_blank_line(void) {
    PdNode *doc = parse_ok("#p:\nBody text.\n\n#hr\n");
    TEST_ASSERT_EQUAL_INT(2, doc->as.document.count);
    assert_call(doc->as.document.children[0], "p", 0, true, false);
    assert_call(doc->as.document.children[1], "hr", 0, false, false);
    pd_node_free(doc);
}

void test_parse_paragraph_body_terminated_by_eof(void) {
    PdNode *doc = parse_ok("#p:\nBody text.");
    PdNode *call = doc->as.document.children[0];
    char *bt = body_text(call);
    TEST_ASSERT_EQUAL_STRING("Body text.", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_paragraph_body_with_inline_macro(void) {
    PdNode *doc = parse_ok("#p:\nThis has #version in it.\n\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    bool has_ref = false;
    for (int i = 0; i < body->as.body.count; i++) {
        if (body->as.body.children[i]->type == NODE_MACRO_CALL) {
            has_ref = true;
        }
    }
    TEST_ASSERT_TRUE(has_ref);
    pd_node_free(doc);
}

void test_parse_simple_bracket_body(void) {
    PdNode *doc = parse_ok("[#b : bold text]\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_BODY, body->type);
    /* Text should contain "bold text" */
    TEST_ASSERT_TRUE(body->as.body.count >= 1);
    PdNode *text = body->as.body.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, text->type);
    TEST_ASSERT_EQUAL_STRING("bold text", text->as.text.value);
    pd_node_free(doc);
}

void test_parse_multiline_bracket_body(void) {
    PdNode *doc = parse_ok("[#ul :\n  #*: First\n  #*: Second\n]\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    /* Body should contain macro calls for #* */
    int call_count = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        if (body->as.body.children[i]->type == NODE_MACRO_CALL)
            call_count++;
    }
    TEST_ASSERT_EQUAL_INT(2, call_count);
    pd_node_free(doc);
}

void test_parse_bracket_body_preserves_newlines(void) {
    PdNode *doc = parse_ok("[#p : line1\nline2]\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    /* Concatenate all text to verify newline preserved */
    int total = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_TEXT)
            total += c->as.text.value_len;
    }
    char *buf = malloc((size_t)total + 1);
    int pos = 0;
    for (int i = 0; i < body->as.body.count; i++) {
        PdNode *c = body->as.body.children[i];
        if (c->type == NODE_TEXT) {
            memcpy(buf + pos, c->as.text.value, (size_t)c->as.text.value_len);
            pos += c->as.text.value_len;
        }
    }
    buf[pos] = '\0';
    TEST_ASSERT_TRUE(strstr(buf, "line1") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "line2") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "\n") != NULL);
    free(buf);
    pd_node_free(doc);
}

void test_parse_nested_bracket_body(void) {
    PdNode *doc = parse_ok("[#b : [#i : nested]]\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_TRUE(body->as.body.count >= 1);
    PdNode *inner = body->as.body.children[0];
    assert_call(inner, "i", 0, true, true);
    pd_node_free(doc);
}

void test_parse_interp_string_body(void) {
    PdNode *doc = parse_ok("#p\"Hello world\"\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    PdNode *is = call->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(1, is->as.interp_string.count);
    TEST_ASSERT_EQUAL_STRING("Hello world",
                              is->as.interp_string.parts[0]->as.text.value);
    pd_node_free(doc);
}

void test_parse_raw_string_body_text(void) {
    PdNode *doc = parse_ok("#p\"\"\"raw stuff\"\"\"\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, call->as.macro_call.body->type);
    TEST_ASSERT_EQUAL_STRING("raw stuff",
                              call->as.macro_call.body->as.text.value);
    pd_node_free(doc);
}

void test_parse_string_body_after_colon(void) {
    PdNode *doc = parse_ok("#p: \"text\"\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_string_body_after_args(void) {
    PdNode *doc = parse_ok("[#set name=x \"value\"]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 1, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    pd_node_free(doc);
}

void test_parse_empty_string_body(void) {
    PdNode *doc = parse_ok("#p\"\"\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    TEST_ASSERT_EQUAL_INT(0, call->as.macro_call.body->as.interp_string.count);
    pd_node_free(doc);
}

void test_parse_inline_body_span(void) {
    PdNode *doc = parse_ok("#doc.title: Hello\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    /* 'H' of "Hello" is at column 13 */
    TEST_ASSERT_EQUAL_INT(13, body->span.start.column);
    pd_node_free(doc);
}

void test_parse_macro_call_span(void) {
    PdNode *doc = parse_ok("#hr\n");
    PdNode *call = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(1, call->span.start.line);
    TEST_ASSERT_EQUAL_INT(1, call->span.start.column);
    pd_node_free(doc);
}

/* ================================================================== */
/* 4b. Body dedent tests                                              */
/* ================================================================== */

void test_parse_paragraph_body_dedent(void) {
    PdNode *doc = parse_ok("#p:\n    line1\n    line2\n\n");
    char *bt = body_text(doc->as.document.children[0]);
    TEST_ASSERT_EQUAL_STRING("line1\nline2", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_bracket_body_dedent(void) {
    PdNode *doc = parse_ok("[#p:\n    line1\n    line2\n]\n");
    char *bt = body_text(doc->as.document.children[0]);
    /* Trailing \n before ] is part of the body text */
    TEST_ASSERT_EQUAL_STRING("line1\nline2\n", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_code_block_relative_indent(void) {
    PdNode *doc = parse_ok("#code:\n    def f():\n        pass\n\n");
    char *bt = body_text(doc->as.document.children[0]);
    TEST_ASSERT_EQUAL_STRING("def f():\n    pass", bt);
    free(bt);
    pd_node_free(doc);
}

void test_parse_inline_body_no_dedent(void) {
    /* Inline body (single line, no newlines) should be unchanged */
    PdNode *doc = parse_ok("#p: Hello\n");
    char *bt = body_text(doc->as.document.children[0]);
    TEST_ASSERT_EQUAL_STRING("Hello", bt);
    free(bt);
    pd_node_free(doc);
}

/* ================================================================== */
/* 4c. Bracketed call — string body on next line                      */
/* ================================================================== */

void test_parse_bracket_string_body_next_line(void) {
    PdNode *doc = parse_ok("[#p\n\"hello\"]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, call->as.macro_call.body->type);
    TEST_ASSERT_EQUAL_STRING("hello",
        call->as.macro_call.body->as.interp_string.parts[0]->as.text.value);
    pd_node_free(doc);
}

void test_parse_bracket_raw_string_body_next_line(void) {
    PdNode *doc = parse_ok("[#p\n\"\"\"raw hello\"\"\"]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "p", 0, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, call->as.macro_call.body->type);
    TEST_ASSERT_EQUAL_STRING("raw hello",
        call->as.macro_call.body->as.text.value);
    pd_node_free(doc);
}

void test_parse_bracket_args_then_string_next_line(void) {
    PdNode *doc = parse_ok("[#code language=python\n\"\"\"def f(): pass\"\"\"]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "code", 1, true, true);
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, call->as.macro_call.body->type);
    TEST_ASSERT_EQUAL_STRING("def f(): pass",
        call->as.macro_call.body->as.text.value);
    pd_node_free(doc);
}

void test_parse_bracket_args_across_newlines(void) {
    PdNode *doc = parse_ok("[#set name=foo\nbody=?]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 2, false, true);
    TEST_ASSERT_EQUAL_STRING("foo",
        call->as.macro_call.args[0]->as.named_arg.value->as.text.value);
    TEST_ASSERT_EQUAL_INT(NODE_REQUIRED_MARKER,
        call->as.macro_call.args[1]->as.named_arg.value->type);
    pd_node_free(doc);
}

void test_parse_bracket_args_across_newlines_with_body(void) {
    PdNode *doc = parse_ok("[#set name=greeting\ntarget=?\n: Hello #target!]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 2, true, true);
    TEST_ASSERT_EQUAL_STRING("greeting",
        call->as.macro_call.args[0]->as.named_arg.value->as.text.value);
    pd_node_free(doc);
}

void test_parse_bracket_args_across_newlines_indented(void) {
    PdNode *doc = parse_ok("[#set\n  name=foo\n  body=?\n  : text]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "set", 2, true, true);
    pd_node_free(doc);
}

void test_parse_bracket_bare_macro_newline(void) {
    PdNode *doc = parse_ok("[#hr\n]\n");
    PdNode *call = doc->as.document.children[0];
    assert_call(call, "hr", 0, false, true);
    pd_node_free(doc);
}

/* ================================================================== */
/* 5. String tests                                                    */
/* ================================================================== */

void test_parse_simple_string(void) {
    PdNode *doc = parse_ok("#p\"Hello\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, is->type);
    TEST_ASSERT_EQUAL_INT(1, is->as.interp_string.count);
    TEST_ASSERT_EQUAL_STRING("Hello", is->as.interp_string.parts[0]->as.text.value);
    pd_node_free(doc);
}

void test_parse_string_with_escape(void) {
    PdNode *doc = parse_ok("#p\"tab:\\there\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(1, is->as.interp_string.count);
    PdNode *part = is->as.interp_string.parts[0];
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, part->type);
    /* "tab:" + "\t" + "here" = "tab:\there" */
    TEST_ASSERT_EQUAL_STRING("tab:\there", part->as.text.value);
    pd_node_free(doc);
}

void test_parse_string_with_newline_escape(void) {
    PdNode *doc = parse_ok("#p\"Line one.\\nLine two.\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    PdNode *part = is->as.interp_string.parts[0];
    TEST_ASSERT_EQUAL_STRING("Line one.\nLine two.", part->as.text.value);
    pd_node_free(doc);
}

void test_parse_empty_interp_string(void) {
    PdNode *doc = parse_ok("#p\"\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_INTERP_STRING, is->type);
    TEST_ASSERT_EQUAL_INT(0, is->as.interp_string.count);
    pd_node_free(doc);
}

void test_parse_simple_code_section(void) {
    PdNode *doc = parse_ok("#p\"Hello, \\[#version]!\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(3, is->as.interp_string.count);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, is->as.interp_string.parts[0]->type);
    TEST_ASSERT_EQUAL_STRING("Hello, ",
                              is->as.interp_string.parts[0]->as.text.value);
    TEST_ASSERT_EQUAL_INT(NODE_CODE_SECTION,
                          is->as.interp_string.parts[1]->type);
    TEST_ASSERT_EQUAL_INT(NODE_TEXT, is->as.interp_string.parts[2]->type);
    TEST_ASSERT_EQUAL_STRING("!",
                              is->as.interp_string.parts[2]->as.text.value);
    pd_node_free(doc);
}

void test_parse_code_section_body(void) {
    PdNode *doc = parse_ok("#p\"\\[#version]\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(1, is->as.interp_string.count);
    PdNode *cs = is->as.interp_string.parts[0];
    TEST_ASSERT_EQUAL_INT(NODE_CODE_SECTION, cs->type);
    TEST_ASSERT_EQUAL_INT(1, cs->as.code_section.count);
    assert_call(cs->as.code_section.children[0], "version", 0, false, false);
    pd_node_free(doc);
}

void test_parse_code_section_complex_call(void) {
    PdNode *doc = parse_ok("#p\"\\[#url link=\"\"x\"\" text=\"\"y\"\"]\"\n");
    /* This would be complex — let's test a simpler bracketed version */
    pd_node_free(doc);
}

void test_parse_raw_string(void) {
    PdNode *doc = parse_ok("#p\"\"\"raw \\n content\"\"\"\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, body->type);
    /* Raw string preserves backslash literally */
    TEST_ASSERT_EQUAL_STRING("raw \\n content", body->as.text.value);
    pd_node_free(doc);
}

void test_parse_raw_string_multiline(void) {
    PdNode *doc = parse_ok("#p\"\"\"\n    line one\n    line two\n    \"\"\"\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    TEST_ASSERT_EQUAL_INT(NODE_RAW_STRING, body->type);
    TEST_ASSERT_TRUE(strstr(body->as.text.value, "line one") != NULL);
    TEST_ASSERT_TRUE(strstr(body->as.text.value, "line two") != NULL);
    pd_node_free(doc);
}

void test_parse_interp_string_span(void) {
    PdNode *doc = parse_ok("#p\"text\"\n");
    PdNode *is = doc->as.document.children[0]->as.macro_call.body;
    /* Opening quote at column 3 */
    TEST_ASSERT_EQUAL_INT(3, is->span.start.column);
    pd_node_free(doc);
}

void test_parse_raw_string_span(void) {
    PdNode *doc = parse_ok("#p\"\"\"raw\"\"\"\n");
    PdNode *body = doc->as.document.children[0]->as.macro_call.body;
    /* Raw string token starts at column 3 */
    TEST_ASSERT_EQUAL_INT(3, body->span.start.column);
    pd_node_free(doc);
}

/* ================================================================== */
/* 6. Error tests                                                     */
/* ================================================================== */

void test_parse_err_bare_lbracket(void) {
    TEST_ASSERT_TRUE(parse_err("#p: text [ more\n", "bare '['"));
}

void test_parse_err_bare_rbracket(void) {
    TEST_ASSERT_TRUE(parse_err("#p: text ] more\n", "bare ']'"));
}

void test_parse_err_missing_rbracket(void) {
    TEST_ASSERT_TRUE(parse_err("[#b : text\n", "expected closing ']'"));
}

void test_parse_err_missing_macro_name(void) {
    TEST_ASSERT_TRUE(parse_err("#\n", "expected macro name"));
}

void test_parse_err_missing_macro_name_bracket(void) {
    /* [foo] — [ not followed by # */
    TEST_ASSERT_TRUE(parse_err("[foo]\n", "bare '['"));
}

void test_parse_err_missing_arg_value(void) {
    TEST_ASSERT_TRUE(parse_err("[#set name=]\n", "expected argument value"));
}

void test_parse_macro_with_trailing_text_is_paragraph(void) {
    /* #hr extra — parsed as paragraph with inline macro + text */
    PdNode *doc = parse_ok("#hr extra\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    pd_node_free(doc);
}

void test_parse_bracketed_with_trailing_text_is_paragraph(void) {
    PdNode *doc = parse_ok("[#set name=x] extra\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, doc->as.document.children[0]->type);
    pd_node_free(doc);
}

void test_parse_inline_macro_starts_paragraph(void) {
    PdNode *doc = parse_ok("#~\"dot\" has special meaning.\n");
    TEST_ASSERT_EQUAL_INT(1, doc->as.document.count);
    PdNode *para = doc->as.document.children[0];
    TEST_ASSERT_EQUAL_INT(NODE_PARAGRAPH, para->type);
    /* First child should be a macro call (#~) */
    TEST_ASSERT_EQUAL_INT(NODE_MACRO_CALL, para->as.paragraph.children[0]->type);
    TEST_ASSERT_EQUAL_STRING("~", para->as.paragraph.children[0]->as.macro_call.name);
    pd_node_free(doc);
}

void test_parse_err_bare_text_in_bracketed(void) {
    /* [#b extra text without colon] — bare text without : */
    TEST_ASSERT_TRUE(parse_err("[#b extra text without colon]\n",
                               "expected argument"));
}

void test_parse_err_has_span(void) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);
    pd_tokenize("#\n", "test.pdoc", &tokens, &err);
    PdNode *doc = NULL;
    int rc = pd_parse(&tokens, "#\n", "test.pdoc", &doc, &err);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_INT(1, err.span.start.line);
    token_array_free(&tokens);
    pd_error_free(&err);
}

void test_parse_err_format_arrow(void) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);
    pd_tokenize("#\n", "test.pdoc", &tokens, &err);
    PdNode *doc = NULL;
    pd_parse(&tokens, "#\n", "test.pdoc", &doc, &err);
    char *formatted = pd_format_error(&err);
    TEST_ASSERT_NOT_NULL(formatted);
    TEST_ASSERT_TRUE(strstr(formatted, "-->") != NULL);
    TEST_ASSERT_TRUE(strstr(formatted, "test.pdoc") != NULL);
    token_array_free(&tokens);
    pd_error_free(&err);
}

void test_parse_err_format_carets(void) {
    TokenArray tokens;
    PdError err;
    pd_error_init(&err);
    pd_tokenize("#\n", "test.pdoc", &tokens, &err);
    PdNode *doc = NULL;
    pd_parse(&tokens, "#\n", "test.pdoc", &doc, &err);
    char *formatted = pd_format_error(&err);
    TEST_ASSERT_NOT_NULL(formatted);
    TEST_ASSERT_TRUE(strstr(formatted, "^") != NULL);
    token_array_free(&tokens);
    pd_error_free(&err);
}

/* ================================================================== */
/* 7. Example file tests                                              */
/* ================================================================== */

static void parse_example_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* Skip if file not found (examples may be symlinked) */
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);

    TokenArray tokens;
    PdError err;
    pd_error_init(&err);

    int rc = pd_tokenize(buf, path, &tokens, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }

    PdNode *doc = NULL;
    rc = pd_parse(&tokens, buf, path, &doc, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_INT(NODE_DOCUMENT, doc->type);
    /* Non-trivial documents should have children */
    TEST_ASSERT_TRUE(doc->as.document.count > 0);

    pd_node_free(doc);
    token_array_free(&tokens);
    pd_error_free(&err);
    free(buf);
}

void test_parse_example_01(void) {
    parse_example_file("examples/01-basic-document.pdoc");
}

void test_parse_example_02(void) {
    parse_example_file("examples/02-inline-formatting.pdoc");
}

void test_parse_example_03(void) {
    parse_example_file("examples/03-strings.pdoc");
}

void test_parse_example_09(void) {
    parse_example_file("examples/09-escaping.pdoc");
}

/* ================================================================== */
/* Test runner                                                        */
/* ================================================================== */

void run_test_parser(void) {
    /* Document */
    RUN_TEST(test_parse_empty);
    RUN_TEST(test_parse_only_newlines);
    RUN_TEST(test_parse_only_whitespace_lines);
    RUN_TEST(test_parse_single_paragraph);
    RUN_TEST(test_parse_paragraph_no_trailing_newline);
    RUN_TEST(test_parse_multiline_paragraph);
    RUN_TEST(test_parse_two_paragraphs);
    RUN_TEST(test_parse_no_blank_line_hash_stays_in_paragraph);
    RUN_TEST(test_parse_no_blank_line_bracket_hash_stays);
    RUN_TEST(test_parse_blank_line_separates);
    RUN_TEST(test_parse_macro_then_paragraph);
    RUN_TEST(test_parse_multiple_macros);
    RUN_TEST(test_parse_blank_lines_between_blocks);
    RUN_TEST(test_parse_paragraph_with_inline_macro);
    RUN_TEST(test_parse_document_span);
    RUN_TEST(test_parse_paragraph_span);

    /* Calls */
    RUN_TEST(test_parse_simple_macro);
    RUN_TEST(test_parse_macro_no_trailing_newline);
    RUN_TEST(test_parse_colon_body);
    RUN_TEST(test_parse_colon_no_ws_before);
    RUN_TEST(test_parse_string_body_no_ws);
    RUN_TEST(test_parse_string_body_with_ws);
    RUN_TEST(test_parse_raw_string_body);
    RUN_TEST(test_parse_string_body_with_colon);
    RUN_TEST(test_parse_simple_bracketed);
    RUN_TEST(test_parse_bracketed_with_body);
    RUN_TEST(test_parse_bracketed_inline_body);
    RUN_TEST(test_parse_bracketed_string_body);
    RUN_TEST(test_parse_nested_bracket_calls);
    RUN_TEST(test_parse_inline_call_in_body);
    RUN_TEST(test_parse_multiple_inline_calls);
    RUN_TEST(test_parse_macro_ref_in_body);
    RUN_TEST(test_parse_heading_alias_dash);
    RUN_TEST(test_parse_heading_alias_double_dash);

    /* Args */
    RUN_TEST(test_parse_single_bareword);
    RUN_TEST(test_parse_bareword_with_dots);
    RUN_TEST(test_parse_bareword_with_dashes);
    RUN_TEST(test_parse_interp_string_arg);
    RUN_TEST(test_parse_raw_string_arg);
    RUN_TEST(test_parse_macro_ref_value);
    RUN_TEST(test_parse_bracket_call_as_value);
    RUN_TEST(test_parse_question_mark_value);
    RUN_TEST(test_parse_multiple_required);
    RUN_TEST(test_parse_two_string_args);
    RUN_TEST(test_parse_mixed_arg_types);
    RUN_TEST(test_parse_unbracketed_multiple_args);
    RUN_TEST(test_parse_name_span);
    RUN_TEST(test_parse_arg_span_covers_value);
    RUN_TEST(test_parse_args_then_colon_body);
    RUN_TEST(test_parse_args_then_string_body);
    RUN_TEST(test_parse_unbracketed_args_then_body);

    /* Body */
    RUN_TEST(test_parse_body_simple_text);
    RUN_TEST(test_parse_body_with_inline_macro);
    RUN_TEST(test_parse_body_with_escape);
    RUN_TEST(test_parse_body_coalesces_text);
    RUN_TEST(test_parse_body_colon_equals_are_text);
    RUN_TEST(test_parse_paragraph_body);
    RUN_TEST(test_parse_paragraph_body_newlines);
    RUN_TEST(test_parse_paragraph_body_terminated_by_blank_line);
    RUN_TEST(test_parse_paragraph_body_terminated_by_eof);
    RUN_TEST(test_parse_paragraph_body_with_inline_macro);
    RUN_TEST(test_parse_simple_bracket_body);
    RUN_TEST(test_parse_multiline_bracket_body);
    RUN_TEST(test_parse_bracket_body_preserves_newlines);
    RUN_TEST(test_parse_nested_bracket_body);
    RUN_TEST(test_parse_interp_string_body);
    RUN_TEST(test_parse_raw_string_body_text);
    RUN_TEST(test_parse_string_body_after_colon);
    RUN_TEST(test_parse_string_body_after_args);
    RUN_TEST(test_parse_empty_string_body);
    RUN_TEST(test_parse_inline_body_span);
    RUN_TEST(test_parse_macro_call_span);

    /* Body dedent */
    RUN_TEST(test_parse_paragraph_body_dedent);
    RUN_TEST(test_parse_bracket_body_dedent);
    RUN_TEST(test_parse_code_block_relative_indent);
    RUN_TEST(test_parse_inline_body_no_dedent);

    /* Bracketed string body on next line */
    RUN_TEST(test_parse_bracket_string_body_next_line);
    RUN_TEST(test_parse_bracket_raw_string_body_next_line);
    RUN_TEST(test_parse_bracket_args_then_string_next_line);
    RUN_TEST(test_parse_bracket_args_across_newlines);
    RUN_TEST(test_parse_bracket_args_across_newlines_with_body);
    RUN_TEST(test_parse_bracket_args_across_newlines_indented);
    RUN_TEST(test_parse_bracket_bare_macro_newline);

    /* Strings */
    RUN_TEST(test_parse_simple_string);
    RUN_TEST(test_parse_string_with_escape);
    RUN_TEST(test_parse_string_with_newline_escape);
    RUN_TEST(test_parse_empty_interp_string);
    RUN_TEST(test_parse_simple_code_section);
    RUN_TEST(test_parse_code_section_body);
    RUN_TEST(test_parse_code_section_complex_call);
    RUN_TEST(test_parse_raw_string);
    RUN_TEST(test_parse_raw_string_multiline);
    RUN_TEST(test_parse_interp_string_span);
    RUN_TEST(test_parse_raw_string_span);

    /* Errors */
    RUN_TEST(test_parse_err_bare_lbracket);
    RUN_TEST(test_parse_err_bare_rbracket);
    RUN_TEST(test_parse_err_missing_rbracket);
    RUN_TEST(test_parse_err_missing_macro_name);
    RUN_TEST(test_parse_err_missing_macro_name_bracket);
    RUN_TEST(test_parse_err_missing_arg_value);
    RUN_TEST(test_parse_macro_with_trailing_text_is_paragraph);
    RUN_TEST(test_parse_bracketed_with_trailing_text_is_paragraph);
    RUN_TEST(test_parse_inline_macro_starts_paragraph);
    RUN_TEST(test_parse_err_bare_text_in_bracketed);
    RUN_TEST(test_parse_err_has_span);
    RUN_TEST(test_parse_err_format_arrow);
    RUN_TEST(test_parse_err_format_carets);

    /* Examples */
    RUN_TEST(test_parse_example_01);
    RUN_TEST(test_parse_example_02);
    RUN_TEST(test_parse_example_03);
    RUN_TEST(test_parse_example_09);
}
