#include "unity.h"
#include "errors.h"
#include "eval.h"
#include "render.h"
#include "lexer.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Convenience helpers --- */

/* Parse + evaluate + render. Fail test on error. Caller frees result. */
static char *render_ok(const char *input) {
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

    PdNode *expanded = NULL;
    rc = pd_evaluate(doc, "test.pdoc", input, NULL, NULL, 0, NULL, &expanded, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        pd_node_free(doc);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }
    pd_node_free(doc);

    char *html = NULL;
    rc = pd_render(expanded, input, "test.pdoc", &html, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        pd_node_free(expanded);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }
    pd_node_free(expanded);
    return html;
}

/* Parse + evaluate + render, expect render error containing substr. */
static PdError _last_render_err;

static const char *render_err(const char *input, const char *msg_substr) {
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

    PdNode *expanded = NULL;
    rc = pd_evaluate(doc, "test.pdoc", input, NULL, NULL, 0, NULL, &expanded, &err);
    if (rc < 0) {
        char *fmt = pd_format_error(&err);
        pd_node_free(doc);
        TEST_FAIL_MESSAGE(fmt ? fmt : err.message);
    }
    pd_node_free(doc);

    pd_error_free(&_last_render_err);
    pd_error_init(&_last_render_err);

    char *html = NULL;
    rc = pd_render(expanded, input, "test.pdoc", &html, &_last_render_err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, rc, "expected render error");
    TEST_ASSERT_NULL(html);
    pd_node_free(expanded);

    TEST_ASSERT_NOT_NULL_MESSAGE(_last_render_err.message,
                                 "expected error message");
    if (msg_substr) {
        TEST_ASSERT_NOT_NULL_MESSAGE(
            strstr(_last_render_err.message, msg_substr),
            _last_render_err.message);
    }
    return _last_render_err.message;
}

static bool contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

/* ========================================================================
 * 1. Document structure tests
 * ======================================================================== */

static void test_doctype_present(void) {
    char *html = render_ok("");
    TEST_ASSERT_TRUE(contains(html, "<!DOCTYPE html>"));
    free(html);
}

static void test_html_head_body_tags(void) {
    char *html = render_ok("");
    TEST_ASSERT_TRUE(contains(html, "<html>"));
    TEST_ASSERT_TRUE(contains(html, "<head>"));
    TEST_ASSERT_TRUE(contains(html, "</head>"));
    TEST_ASSERT_TRUE(contains(html, "<body>"));
    TEST_ASSERT_TRUE(contains(html, "</body>"));
    TEST_ASSERT_TRUE(contains(html, "</html>"));
    free(html);
}

static void test_charset_meta(void) {
    char *html = render_ok("");
    TEST_ASSERT_TRUE(contains(html, "<meta charset=\"utf-8\">"));
    free(html);
}

/* ========================================================================
 * 2. Heading tests
 * ======================================================================== */

static void test_h1_rendering(void) {
    char *html = render_ok("#-: Hello\n");
    TEST_ASSERT_TRUE(contains(html, "<h1 id=\"hello\">Hello</h1>"));
    free(html);
}

static void test_h2_rendering(void) {
    char *html = render_ok("#--: World\n");
    TEST_ASSERT_TRUE(contains(html, "<h2 id=\"world\">World</h2>"));
    free(html);
}

static void test_h3_rendering(void) {
    char *html = render_ok("#---: Test\n");
    TEST_ASSERT_TRUE(contains(html, "<h3 id=\"test\">Test</h3>"));
    free(html);
}

static void test_h4_h5_h6(void) {
    char *html = render_ok("#----: H4\n\n#-----: H5\n\n#------: H6\n");
    TEST_ASSERT_TRUE(contains(html, "<h4 id=\"h4\">H4</h4>"));
    TEST_ASSERT_TRUE(contains(html, "<h5 id=\"h5\">H5</h5>"));
    TEST_ASSERT_TRUE(contains(html, "<h6 id=\"h6\">H6</h6>"));
    free(html);
}

static void test_heading_empty(void) {
    char *html = render_ok("#-:\n");
    TEST_ASSERT_TRUE(contains(html, "<h1 id=\"heading\">"));
    free(html);
}

/* ========================================================================
 * 3. Heading IDs (slugs)
 * ======================================================================== */

static void test_slug_special_chars(void) {
    char *html = render_ok("#-: Hello, World!\n");
    TEST_ASSERT_TRUE(contains(html, "id=\"hello-world\""));
    free(html);
}

static void test_slug_dedup(void) {
    char *html = render_ok("#-: Foo\n\n#--: Foo\n");
    TEST_ASSERT_TRUE(contains(html, "<h1 id=\"foo\">"));
    TEST_ASSERT_TRUE(contains(html, "<h2 id=\"foo-2\">"));
    free(html);
}

static void test_slug_triple_dedup(void) {
    char *html = render_ok("#-: Foo\n\n#--: Foo\n\n#---: Foo\n");
    TEST_ASSERT_TRUE(contains(html, "<h1 id=\"foo\">"));
    TEST_ASSERT_TRUE(contains(html, "<h2 id=\"foo-2\">"));
    TEST_ASSERT_TRUE(contains(html, "<h3 id=\"foo-3\">"));
    free(html);
}

static void test_slug_empty_heading(void) {
    char *html = render_ok("#-:\n");
    TEST_ASSERT_TRUE(contains(html, "id=\"heading\""));
    free(html);
}

/* ========================================================================
 * 4. Heading numbering
 * ======================================================================== */

static void test_heading_numbering_disabled(void) {
    char *html = render_ok("#--: Intro\n");
    TEST_ASSERT_TRUE(contains(html, ">Intro</h2>"));
    free(html);
}

static void test_heading_numbering_enabled(void) {
    char *html = render_ok(
        "#doc.heading.number level=3\n\n"
        "#--: First\n\n"
        "#--: Second\n");
    TEST_ASSERT_TRUE(contains(html, ">1. First</h2>"));
    TEST_ASSERT_TRUE(contains(html, ">2. Second</h2>"));
    free(html);
}

static void test_heading_numbering_counter_reset(void) {
    char *html = render_ok(
        "#doc.heading.number level=3\n\n"
        "#--: A\n\n#---: Sub1\n\n"
        "#--: B\n\n#---: Sub2\n");
    TEST_ASSERT_TRUE(contains(html, ">1. A</h2>"));
    TEST_ASSERT_TRUE(contains(html, ">1.1. Sub1</h3>"));
    TEST_ASSERT_TRUE(contains(html, ">2. B</h2>"));
    TEST_ASSERT_TRUE(contains(html, ">2.1. Sub2</h3>"));
    free(html);
}

static void test_heading_numbering_h1_skipped(void) {
    char *html = render_ok(
        "#doc.heading.number level=3\n\n"
        "#-: Title\n\n#--: Intro\n");
    TEST_ASSERT_TRUE(contains(html, ">Title</h1>"));
    TEST_ASSERT_TRUE(contains(html, ">1. Intro</h2>"));
    free(html);
}

/* ========================================================================
 * 5. Heading anchors
 * ======================================================================== */

static void test_heading_anchor_wrapping(void) {
    char *html = render_ok(
        "#doc.heading.anchor level=3\n\n#--: Test\n");
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#test\">Test</a>"));
    free(html);
}

static void test_heading_anchor_level_filtering(void) {
    char *html = render_ok(
        "#doc.heading.anchor level=2\n\n#--: Yes\n\n#---: No\n");
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#yes\">Yes</a>"));
    TEST_ASSERT_FALSE(contains(html, "<a href=\"#no\">No</a>"));
    free(html);
}

static void test_heading_anchor_with_numbering(void) {
    char *html = render_ok(
        "#doc.heading.number level=3\n\n"
        "#doc.heading.anchor level=3\n\n"
        "#--: Test\n");
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#test\">1. Test</a>"));
    free(html);
}

/* ========================================================================
 * 6. TOC tests
 * ======================================================================== */

static void test_toc_basic(void) {
    char *html = render_ok(
        "#doc.toc\n\n#-: A\n\n#--: B\n");
    TEST_ASSERT_TRUE(contains(html, "<ul>"));
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#a\">A</a>"));
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#b\">B</a>"));
    TEST_ASSERT_TRUE(contains(html, "</ul>"));
    free(html);
}

static void test_toc_nested(void) {
    char *html = render_ok(
        "#doc.toc\n\n#-: A\n\n#--: B\n");
    TEST_ASSERT_TRUE(contains(html, "<ul>\n<li><a href=\"#a\">A</a>"));
    free(html);
}

static void test_toc_level_filtering(void) {
    char *html = render_ok(
        "#doc.toc level=1\n\n#-: A\n\n#--: B\n");
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#a\">A</a>"));
    TEST_ASSERT_FALSE(contains(html, "<a href=\"#b\">B</a>"));
    free(html);
}

static void test_toc_no_headings(void) {
    char *html = render_ok("#doc.toc\n");
    TEST_ASSERT_FALSE(contains(html, "<ul>"));
    free(html);
}

/* ========================================================================
 * 7. Paragraph tests
 * ======================================================================== */

static void test_paragraph_basic(void) {
    char *html = render_ok("Hello world\n");
    TEST_ASSERT_TRUE(contains(html, "<p>Hello world</p>"));
    free(html);
}

static void test_paragraph_with_inline(void) {
    char *html = render_ok("Hello #b\"bold\" world\n");
    TEST_ASSERT_TRUE(contains(html, "<p>Hello <strong>bold</strong> world</p>"));
    free(html);
}

/* ========================================================================
 * 8. HR test
 * ======================================================================== */

static void test_hr(void) {
    char *html = render_ok("#hr\n");
    TEST_ASSERT_TRUE(contains(html, "<hr>"));
    free(html);
}

/* ========================================================================
 * 9. Bold / Italic tests
 * ======================================================================== */

static void test_bold(void) {
    char *html = render_ok("#p: Text #b\"bold text\" end\n");
    TEST_ASSERT_TRUE(contains(html, "<strong>bold text</strong>"));
    free(html);
}

static void test_italic(void) {
    char *html = render_ok("#p: Text #i\"italic text\" end\n");
    TEST_ASSERT_TRUE(contains(html, "<em>italic text</em>"));
    free(html);
}

static void test_bold_italic(void) {
    char *html = render_ok("#p: Text [#*_: bold italic] end\n");
    TEST_ASSERT_TRUE(contains(html, "<strong><em>bold italic</em></strong>"));
    free(html);
}

static void test_italic_bold(void) {
    char *html = render_ok("#p: Text [#_*: italic bold] end\n");
    TEST_ASSERT_TRUE(contains(html, "<em><strong>italic bold</strong></em>"));
    free(html);
}

/* ========================================================================
 * 10. Link tests
 * ======================================================================== */

static void test_link_external(void) {
    char *html = render_ok(
        "#p: [#link to=\"https://example.com\" : Click]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"https://example.com\">Click</a>"));
    free(html);
}

static void test_link_fragment(void) {
    char *html = render_ok(
        "#-: Section\n\n"
        "#p: [#link to=section : Go]\n");
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#section\">Go</a>"));
    free(html);
}

static void test_link_broken_fragment(void) {
    render_err(
        "#p: [#link to=nonexistent : Go]\n",
        "broken internal link");
}

static void test_link_no_body_fragment(void) {
    char *html = render_ok(
        "#-: My Section\n\n"
        "#p: [#link to=my-section]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"#my-section\">My Section</a>"));
    free(html);
}

static void test_link_path(void) {
    char *html = render_ok(
        "#p: [#link to=/path/to/page : Link]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"/path/to/page\">Link</a>"));
    free(html);
}

static void test_link_body_as_to_external(void) {
    char *html = render_ok(
        "#p: [#link : https://example.com]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"https://example.com\">https://example.com</a>"));
    free(html);
}

static void test_link_body_as_to_fragment(void) {
    char *html = render_ok(
        "#-: Section\n\n"
        "#p: [#link : section]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"#section\">Section</a>"));
    free(html);
}

static void test_link_no_to_no_body_error(void) {
    render_err(
        "#p: [#link]\n",
        "missing link target");
}

static void test_link_alias_body_as_to(void) {
    char *html = render_ok(
        "#p: [#> : https://example.com]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"https://example.com\">https://example.com</a>"));
    free(html);
}

/* ========================================================================
 * 11. Block code tests
 * ======================================================================== */

static void test_block_code_basic(void) {
    char *html = render_ok("#code: hello\n");
    TEST_ASSERT_TRUE(contains(html, "<pre><code>hello</code></pre>"));
    free(html);
}

static void test_block_code_with_language(void) {
    char *html = render_ok("#code language=python : print\n");
    TEST_ASSERT_TRUE(contains(html,
        "<pre><code class=\"language-python\">print</code></pre>"));
    free(html);
}

static void test_block_code_html_escaping(void) {
    char *html = render_ok("[#code : <div>&]\n");
    TEST_ASSERT_TRUE(contains(html, "&lt;div&gt;&amp;"));
    free(html);
}

static void test_block_code_bracketed_no_leading_newline(void) {
    char *html = render_ok("[#code:\nint x;\n]\n");
    TEST_ASSERT_TRUE(contains(html, "<pre><code>int x;\n</code></pre>"));
    free(html);
}

/* ========================================================================
 * 12. Inline code tests
 * ======================================================================== */

static void test_inline_code_basic(void) {
    char *html = render_ok("#p: Use #~\"hello\" here\n");
    TEST_ASSERT_TRUE(contains(html, "<code>hello</code>"));
    TEST_ASSERT_FALSE(contains(html, "<pre>"));
    free(html);
}

static void test_inline_code_with_language(void) {
    char *html = render_ok("#p: Use [#~ language=js : var x] here\n");
    TEST_ASSERT_TRUE(contains(html,
        "<code class=\"language-js\">var x</code>"));
    free(html);
}

/* ========================================================================
 * 13. Literal tests
 * ======================================================================== */

static void test_literal_raw(void) {
    char *html = render_ok("#literal\"\"\"<b>raw</b>\"\"\"\n");
    TEST_ASSERT_TRUE(contains(html, "<b>raw</b>"));
    free(html);
}

static void test_literal_no_escaping(void) {
    char *html = render_ok(
        "#literal\"\"\"<script>alert(1)</script>\"\"\"\n");
    TEST_ASSERT_TRUE(contains(html, "<script>alert(1)</script>"));
    free(html);
}

/* ========================================================================
 * 14. List tests
 * ======================================================================== */

static void test_ul_basic(void) {
    char *html = render_ok(
        "[#ul :\n"
        "  #*: one\n"
        "  #*: two\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<ul>"));
    TEST_ASSERT_TRUE(contains(html, "<li>one</li>"));
    TEST_ASSERT_TRUE(contains(html, "<li>two</li>"));
    TEST_ASSERT_TRUE(contains(html, "</ul>"));
    free(html);
}

static void test_ol_basic(void) {
    char *html = render_ok(
        "[#ol :\n"
        "  #*: one\n"
        "  #*: two\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<ol>"));
    TEST_ASSERT_TRUE(contains(html, "<li>one</li>"));
    TEST_ASSERT_TRUE(contains(html, "</ol>"));
    free(html);
}

static void test_nested_list(void) {
    char *html = render_ok(
        "[#ul :\n"
        "  [#* : item\n"
        "    [#ul :\n"
        "      #*: nested\n"
        "    ]\n"
        "  ]\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<li>item"));
    TEST_ASSERT_TRUE(contains(html, "<li>nested</li>"));
    free(html);
}

static void test_empty_list(void) {
    char *html = render_ok("[#ul :]\n");
    TEST_ASSERT_TRUE(contains(html, "<ul>"));
    TEST_ASSERT_TRUE(contains(html, "</ul>"));
    free(html);
}

/* ========================================================================
 * 15. Table tests
 * ======================================================================== */

static void test_table_basic(void) {
    char *html = render_ok("#table:\n  A | B\n  C | D\n");
    TEST_ASSERT_TRUE(contains(html, "<table>"));
    TEST_ASSERT_TRUE(contains(html, "<thead>"));
    TEST_ASSERT_TRUE(contains(html, "<th>A</th>"));
    TEST_ASSERT_TRUE(contains(html, "<th>B</th>"));
    TEST_ASSERT_TRUE(contains(html, "<tbody>"));
    TEST_ASSERT_TRUE(contains(html, "<td>C</td>"));
    TEST_ASSERT_TRUE(contains(html, "<td>D</td>"));
    TEST_ASSERT_TRUE(contains(html, "</tbody>"));
    TEST_ASSERT_TRUE(contains(html, "</table>"));
    free(html);
}

static void test_table_thead_tbody(void) {
    char *html = render_ok(
        "[#table :\n"
        "  [#tr : [#th : H1] [#th : H2]]\n"
        "  [#tr : [#td : A] [#td : B]]\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<thead>"));
    TEST_ASSERT_TRUE(contains(html, "<th>H1</th>"));
    TEST_ASSERT_TRUE(contains(html, "</thead>"));
    TEST_ASSERT_TRUE(contains(html, "<tbody>"));
    TEST_ASSERT_TRUE(contains(html, "<td>A</td>"));
    TEST_ASSERT_TRUE(contains(html, "</tbody>"));
    free(html);
}

static void test_table_colspan(void) {
    char *html = render_ok(
        "[#table :\n"
        "  [#tr : [#td span=2 : Wide]]\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<td colspan=\"2\">Wide</td>"));
    free(html);
}

static void test_table_header_only(void) {
    char *html = render_ok(
        "[#table :\n"
        "  [#tr : [#th : H1] [#th : H2]]\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<thead>"));
    TEST_ASSERT_TRUE(contains(html, "</thead>"));
    TEST_ASSERT_FALSE(contains(html, "<tbody>"));
    free(html);
}

static void test_table_body_only(void) {
    char *html = render_ok(
        "[#table :\n"
        "  [#tr : [#td : A] [#td : B]]\n"
        "]\n");
    TEST_ASSERT_FALSE(contains(html, "<thead>"));
    TEST_ASSERT_TRUE(contains(html, "<tbody>"));
    free(html);
}

/* ========================================================================
 * 16. Wrapper tests
 * ======================================================================== */

static void test_div_with_class_id(void) {
    char *html = render_ok(
        "[#div class=container id=main : content]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<div class=\"container\" id=\"main\">"));
    TEST_ASSERT_TRUE(contains(html, "</div>"));
    free(html);
}

static void test_section(void) {
    char *html = render_ok("[#section : hello]\n");
    TEST_ASSERT_TRUE(contains(html, "<section>"));
    TEST_ASSERT_TRUE(contains(html, "</section>"));
    free(html);
}

static void test_span_inline(void) {
    char *html = render_ok(
        "#p: Text [#span class=hl : highlighted] end\n");
    TEST_ASSERT_TRUE(contains(html,
        "<span class=\"hl\">highlighted</span>"));
    free(html);
}

static void test_nested_wrappers(void) {
    char *html = render_ok(
        "[#div :\n"
        "  [#section : inner]\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html, "<div>"));
    TEST_ASSERT_TRUE(contains(html, "<section>"));
    TEST_ASSERT_TRUE(contains(html, "</section>"));
    TEST_ASSERT_TRUE(contains(html, "</div>"));
    free(html);
}

/* ========================================================================
 * 17. HTML escaping tests
 * ======================================================================== */

static void test_escape_amp_lt_gt(void) {
    char *html = render_ok("A & B < C > D\n");
    TEST_ASSERT_TRUE(contains(html, "A &amp; B &lt; C &gt; D"));
    free(html);
}

static void test_escape_non_ascii(void) {
    char *html = render_ok("\xc3\xa9\n");  /* UTF-8 for é (U+00E9) */
    TEST_ASSERT_TRUE(contains(html, "&#xE9;"));
    free(html);
}

static void test_escape_attribute(void) {
    char *html = render_ok(
        "[#div class=\"a\\\"b\" : x]\n");
    TEST_ASSERT_TRUE(contains(html, "class=\"a&quot;b\""));
    free(html);
}

/* ========================================================================
 * 18. doc.title test
 * ======================================================================== */

static void test_doc_title(void) {
    char *html = render_ok("#doc.title: My Page\n");
    TEST_ASSERT_TRUE(contains(html, "<title>My Page</title>"));
    free(html);
}

/* ========================================================================
 * 19. doc.meta tests
 * ======================================================================== */

static void test_doc_meta_name(void) {
    char *html = render_ok(
        "#doc.meta name=description content=Test\n");
    TEST_ASSERT_TRUE(contains(html,
        "<meta name=\"description\" content=\"Test\">"));
    free(html);
}

static void test_doc_meta_property(void) {
    char *html = render_ok(
        "#doc.meta property=\"og:title\" content=Title\n");
    TEST_ASSERT_TRUE(contains(html,
        "<meta property=\"og:title\" content=\"Title\">"));
    free(html);
}

/* ========================================================================
 * 20. doc.author / doc.version / dates
 * ======================================================================== */

static void test_doc_author(void) {
    char *html = render_ok("#doc.author: John\n");
    TEST_ASSERT_TRUE(contains(html,
        "<meta name=\"author\" content=\"John\">"));
    free(html);
}

static void test_doc_version_dates(void) {
    char *html = render_ok(
        "#doc.version: 1.0\n\n"
        "#doc.datecreated: 2024-01-01\n\n"
        "#doc.datemodified: 2024-06-01\n");
    TEST_ASSERT_TRUE(contains(html,
        "<meta name=\"version\" content=\"1.0\">"));
    TEST_ASSERT_TRUE(contains(html,
        "<meta name=\"datecreated\" content=\"2024-01-01\">"));
    TEST_ASSERT_TRUE(contains(html,
        "<meta name=\"datemodified\" content=\"2024-06-01\">"));
    free(html);
}

/* ========================================================================
 * 21. doc.link tests
 * ======================================================================== */

static void test_doc_link_stylesheet(void) {
    char *html = render_ok(
        "#doc.link rel=stylesheet href=style.css\n");
    TEST_ASSERT_TRUE(contains(html,
        "<link rel=\"stylesheet\" href=\"style.css\">"));
    free(html);
}

static void test_doc_link_with_type_sizes(void) {
    char *html = render_ok(
        "#doc.link rel=icon href=icon.png type=image/png sizes=32x32\n");
    TEST_ASSERT_TRUE(contains(html, "rel=\"icon\""));
    TEST_ASSERT_TRUE(contains(html, "href=\"icon.png\""));
    TEST_ASSERT_TRUE(contains(html, "type=\"image/png\""));
    TEST_ASSERT_TRUE(contains(html, "sizes=\"32x32\""));
    free(html);
}

/* ========================================================================
 * 22. doc.script tests
 * ======================================================================== */

static void test_doc_script_external(void) {
    char *html = render_ok("#doc.script src=app.js\n");
    TEST_ASSERT_TRUE(contains(html,
        "<script src=\"app.js\"></script>"));
    free(html);
}

static void test_doc_script_inline(void) {
    char *html = render_ok(
        "#doc.script\"\"\"alert(1)\"\"\"\n");
    TEST_ASSERT_TRUE(contains(html, "<script>"));
    TEST_ASSERT_TRUE(contains(html, "alert(1)"));
    TEST_ASSERT_TRUE(contains(html, "</script>"));
    free(html);
}

/* ========================================================================
 * 23. doc.lang test
 * ======================================================================== */

static void test_doc_lang(void) {
    char *html = render_ok("#doc.lang: en\n");
    TEST_ASSERT_TRUE(contains(html, "<html lang=\"en\">"));
    free(html);
}

/* ========================================================================
 * 24. doc.body test
 * ======================================================================== */

static void test_doc_body_class_id(void) {
    char *html = render_ok("#doc.body class=main id=app\n");
    TEST_ASSERT_TRUE(contains(html,
        "<body class=\"main\" id=\"app\">"));
    free(html);
}

/* ========================================================================
 * 25. doc.content tests
 * ======================================================================== */

static void test_doc_content_wrapping(void) {
    char *html = render_ok(
        "#doc.content type=main\n\n"
        "#-: Title\n\n"
        "Hello\n");
    TEST_ASSERT_TRUE(contains(html, "<main>"));
    TEST_ASSERT_TRUE(contains(html, "</main>"));
    free(html);
}

static void test_doc_content_preserves_wrappers(void) {
    char *html = render_ok(
        "#doc.content type=main\n\n"
        "[#div : sidebar]\n\n"
        "#-: Title\n");
    TEST_ASSERT_TRUE(contains(html, "<div>"));
    TEST_ASSERT_TRUE(contains(html, "<main>"));
    free(html);
}

static void test_doc_content_with_class(void) {
    char *html = render_ok(
        "#doc.content type=article class=content\n\n"
        "Hello\n");
    TEST_ASSERT_TRUE(contains(html,
        "<article class=\"content\">"));
    free(html);
}

/* ========================================================================
 * 26. End-to-end tests
 * ======================================================================== */

static void test_full_document(void) {
    char *html = render_ok(
        "#doc.lang: en\n\n"
        "#doc.title: Test\n\n"
        "#doc.meta name=description content=A_test_page\n\n"
        "#-: Welcome\n\n"
        "Hello #b\"world\"\n"
    );
    TEST_ASSERT_TRUE(contains(html, "<!DOCTYPE html>"));
    TEST_ASSERT_TRUE(contains(html, "<html lang=\"en\">"));
    TEST_ASSERT_TRUE(contains(html, "<title>Test</title>"));
    TEST_ASSERT_TRUE(contains(html,
        "<meta name=\"description\" content=\"A_test_page\">"));
    TEST_ASSERT_TRUE(contains(html, "<h1 id=\"welcome\">Welcome</h1>"));
    TEST_ASSERT_TRUE(contains(html, "<strong>world</strong>"));
    TEST_ASSERT_TRUE(contains(html, "</body>"));
    TEST_ASSERT_TRUE(contains(html, "</html>"));
    free(html);
}

static void test_combined_features(void) {
    char *html = render_ok(
        "#doc.toc\n\n"
        "#doc.heading.number level=3\n\n"
        "#-: Intro\n\n"
        "#--: Part 1\n\n"
        "[#ul :\n"
        "  #*: item\n"
        "]\n\n"
        "#code language=c : int x;\n"
    );
    TEST_ASSERT_TRUE(contains(html, "<a href=\"#intro\">Intro</a>"));
    TEST_ASSERT_TRUE(contains(html, "1. Part 1"));
    TEST_ASSERT_TRUE(contains(html, "<li>item</li>"));
    TEST_ASSERT_TRUE(contains(html, "class=\"language-c\""));
    TEST_ASSERT_TRUE(contains(html, "int x;"));
    free(html);
}

/* ========================================================================
 * Additional edge case tests
 * ======================================================================== */

static void test_link_no_body_external(void) {
    char *html = render_ok(
        "#p: [#link to=\"https://example.com\"]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<a href=\"https://example.com\">https://example.com</a>"));
    free(html);
}

static void test_th_colspan(void) {
    char *html = render_ok(
        "[#table :\n"
        "  [#tr : [#th span=3 : Wide Header]]\n"
        "]\n");
    TEST_ASSERT_TRUE(contains(html,
        "<th colspan=\"3\">Wide Header</th>"));
    free(html);
}

static void test_wrapper_nav_header_footer(void) {
    char *html = render_ok(
        "[#nav : links]\n\n"
        "[#header : top]\n\n"
        "[#footer : bottom]\n");
    TEST_ASSERT_TRUE(contains(html, "<nav>"));
    TEST_ASSERT_TRUE(contains(html, "</nav>"));
    TEST_ASSERT_TRUE(contains(html, "<header>"));
    TEST_ASSERT_TRUE(contains(html, "</header>"));
    TEST_ASSERT_TRUE(contains(html, "<footer>"));
    TEST_ASSERT_TRUE(contains(html, "</footer>"));
    free(html);
}

static void test_heading_alias(void) {
    char *html = render_ok("#h1: Direct\n");
    TEST_ASSERT_TRUE(contains(html, "<h1 id=\"direct\">Direct</h1>"));
    free(html);
}

/* ========================================================================
 * Test runner
 * ======================================================================== */

void run_test_render(void) {
    /* 1. Document structure */
    RUN_TEST(test_doctype_present);
    RUN_TEST(test_html_head_body_tags);
    RUN_TEST(test_charset_meta);

    /* 2. Headings */
    RUN_TEST(test_h1_rendering);
    RUN_TEST(test_h2_rendering);
    RUN_TEST(test_h3_rendering);
    RUN_TEST(test_h4_h5_h6);
    RUN_TEST(test_heading_empty);

    /* 3. Heading IDs */
    RUN_TEST(test_slug_special_chars);
    RUN_TEST(test_slug_dedup);
    RUN_TEST(test_slug_triple_dedup);
    RUN_TEST(test_slug_empty_heading);

    /* 4. Heading numbering */
    RUN_TEST(test_heading_numbering_disabled);
    RUN_TEST(test_heading_numbering_enabled);
    RUN_TEST(test_heading_numbering_counter_reset);
    RUN_TEST(test_heading_numbering_h1_skipped);

    /* 5. Heading anchors */
    RUN_TEST(test_heading_anchor_wrapping);
    RUN_TEST(test_heading_anchor_level_filtering);
    RUN_TEST(test_heading_anchor_with_numbering);

    /* 6. TOC */
    RUN_TEST(test_toc_basic);
    RUN_TEST(test_toc_nested);
    RUN_TEST(test_toc_level_filtering);
    RUN_TEST(test_toc_no_headings);

    /* 7. Paragraph */
    RUN_TEST(test_paragraph_basic);
    RUN_TEST(test_paragraph_with_inline);

    /* 8. HR */
    RUN_TEST(test_hr);

    /* 9. Bold / Italic */
    RUN_TEST(test_bold);
    RUN_TEST(test_italic);
    RUN_TEST(test_bold_italic);
    RUN_TEST(test_italic_bold);

    /* 10. Links */
    RUN_TEST(test_link_external);
    RUN_TEST(test_link_fragment);
    RUN_TEST(test_link_broken_fragment);
    RUN_TEST(test_link_no_body_fragment);
    RUN_TEST(test_link_path);
    RUN_TEST(test_link_body_as_to_external);
    RUN_TEST(test_link_body_as_to_fragment);
    RUN_TEST(test_link_no_to_no_body_error);
    RUN_TEST(test_link_alias_body_as_to);

    /* 11. Block code */
    RUN_TEST(test_block_code_basic);
    RUN_TEST(test_block_code_with_language);
    RUN_TEST(test_block_code_html_escaping);
    RUN_TEST(test_block_code_bracketed_no_leading_newline);

    /* 12. Inline code */
    RUN_TEST(test_inline_code_basic);
    RUN_TEST(test_inline_code_with_language);

    /* 13. Literal */
    RUN_TEST(test_literal_raw);
    RUN_TEST(test_literal_no_escaping);

    /* 14. Lists */
    RUN_TEST(test_ul_basic);
    RUN_TEST(test_ol_basic);
    RUN_TEST(test_nested_list);
    RUN_TEST(test_empty_list);

    /* 15. Tables */
    RUN_TEST(test_table_basic);
    RUN_TEST(test_table_thead_tbody);
    RUN_TEST(test_table_colspan);
    RUN_TEST(test_table_header_only);
    RUN_TEST(test_table_body_only);

    /* 16. Wrappers */
    RUN_TEST(test_div_with_class_id);
    RUN_TEST(test_section);
    RUN_TEST(test_span_inline);
    RUN_TEST(test_nested_wrappers);

    /* 17. HTML escaping */
    RUN_TEST(test_escape_amp_lt_gt);
    RUN_TEST(test_escape_non_ascii);
    RUN_TEST(test_escape_attribute);

    /* 18. doc.title */
    RUN_TEST(test_doc_title);

    /* 19. doc.meta */
    RUN_TEST(test_doc_meta_name);
    RUN_TEST(test_doc_meta_property);

    /* 20. doc.author / doc.version / dates */
    RUN_TEST(test_doc_author);
    RUN_TEST(test_doc_version_dates);

    /* 21. doc.link */
    RUN_TEST(test_doc_link_stylesheet);
    RUN_TEST(test_doc_link_with_type_sizes);

    /* 22. doc.script */
    RUN_TEST(test_doc_script_external);
    RUN_TEST(test_doc_script_inline);

    /* 23. doc.lang */
    RUN_TEST(test_doc_lang);

    /* 24. doc.body */
    RUN_TEST(test_doc_body_class_id);

    /* 25. doc.content */
    RUN_TEST(test_doc_content_wrapping);
    RUN_TEST(test_doc_content_preserves_wrappers);
    RUN_TEST(test_doc_content_with_class);

    /* 26. End-to-end */
    RUN_TEST(test_full_document);
    RUN_TEST(test_combined_features);

    /* Additional edge cases */
    RUN_TEST(test_link_no_body_external);
    RUN_TEST(test_th_colspan);
    RUN_TEST(test_wrapper_nav_header_footer);
    RUN_TEST(test_heading_alias);
}
