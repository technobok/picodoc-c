#include "builtins.h"
#include <string.h>

/* --- Alias table --- */

static const AliasEntry ALIASES[] = {
    {"-",      "h1"},
    {"--",     "h2"},
    {"---",    "h3"},
    {"----",   "h4"},
    {"-----",  "h5"},
    {"------", "h6"},
    {"//",     "comment"},
    {">",      "link"},
    {"**",     "b"},
    {"__",     "i"},
    {"li",     "*"},
};

#define ALIAS_COUNT ((int)(sizeof(ALIASES) / sizeof(ALIASES[0])))

const char *resolve_alias(const char *name) {
    for (int i = 0; i < ALIAS_COUNT; i++) {
        if (strcmp(name, ALIASES[i].alias) == 0)
            return ALIASES[i].canonical;
    }
    return name;
}

/* --- Builtin parameter declarations --- */

/* Shared param arrays for common patterns. */
static const ParamDecl params_link[]     = {{"to", false}};
static const ParamDecl params_code[]     = {{"language", false}};
static const ParamDecl params_td[]       = {{"span", false}};
static const ParamDecl params_wrapper[]  = {{"class", false}, {"id", false}};
static const ParamDecl params_doc_meta[] = {
    {"name", false}, {"property", false}, {"content", true}
};
static const ParamDecl params_doc_link[] = {
    {"rel", true}, {"href", true}, {"type", false}, {"sizes", false}
};
static const ParamDecl params_doc_script[]  = {{"src", false}, {"type", false}};
static const ParamDecl params_doc_content[] = {
    {"type", true}, {"class", false}, {"id", false}
};
static const ParamDecl params_doc_body[] = {{"class", false}, {"id", false}};
static const ParamDecl params_doc_toc[]  = {{"level", false}};
static const ParamDecl params_set[]      = {{"name", true}};
static const ParamDecl params_ifeq[]     = {{"lhs", true}, {"rhs", true}};
static const ParamDecl params_ifset[]    = {{"name", true}};
static const ParamDecl params_include[]  = {{"literal", false}};

/* --- Builtin definitions table --- */

#define B(n, p, body, exp)  {n, p, (int)(sizeof(p)/sizeof(p[0])), body, exp}
#define B0(n, body, exp)    {n, NULL, 0, body, exp}

static const BuiltinDef BUILTINS[] = {
    /* Structural */
    B0("h1", true, false),
    B0("h2", true, false),
    B0("h3", true, false),
    B0("h4", true, false),
    B0("h5", true, false),
    B0("h6", true, false),
    B0("p",  true, false),
    B0("hr", false, false),

    /* Inline */
    B0("b", true, false),
    B0("i", true, false),
    B("link", params_link, true, false),

    /* Code / literal */
    B("code", params_code, true, false),
    B("~",    params_code, true, false),
    B0("literal", true, false),

    /* Lists */
    B0("ul", true, false),
    B0("ol", true, false),
    B0("*",  true, false),

    /* Tables */
    B0("table", true, true),
    B0("tr",    true, false),
    B("td", params_td, true, false),
    B("th", params_td, true, false),

    /* Wrapper / container */
    B("div",     params_wrapper, true, false),
    B("section", params_wrapper, true, false),
    B("span",    params_wrapper, true, false),
    B("nav",     params_wrapper, true, false),
    B("header",  params_wrapper, true, false),
    B("footer",  params_wrapper, true, false),
    B("main",    params_wrapper, true, false),
    B("article", params_wrapper, true, false),
    B("aside",   params_wrapper, true, false),

    /* Document */
    B("doc.meta",   params_doc_meta,   false, false),
    B("doc.link",   params_doc_link,   false, false),
    B("doc.script", params_doc_script, true, false),
    B0("doc.title",         true, false),
    B0("doc.lang",          true, false),
    B0("doc.author",        true, false),
    B0("doc.version",       true, false),
    B0("doc.datecreated",   true, false),
    B0("doc.datemodified",  true, false),
    B("doc.content", params_doc_content, false, false),
    B("doc.body",    params_doc_body,    false, false),
    B("doc.toc",     params_doc_toc,     false, false),
    B("doc.heading.number", params_doc_toc, false, false),
    B("doc.heading.anchor", params_doc_toc, false, false),

    /* Expansion-time */
    B0("comment", true, true),
    B("set",     params_set,     true, true),
    B("ifeq",    params_ifeq,   true, true),
    B("ifne",    params_ifeq,   true, true),
    B("ifset",   params_ifset,  true, true),
    B("include", params_include, true, true),
};

#undef B
#undef B0

#define BUILTIN_COUNT ((int)(sizeof(BUILTINS) / sizeof(BUILTINS[0])))

const BuiltinDef *find_builtin(const char *name) {
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(name, BUILTINS[i].name) == 0)
            return &BUILTINS[i];
    }
    return NULL;
}

int pd_builtin_count(void) { return BUILTIN_COUNT; }
const BuiltinDef *pd_builtin_at(int index) { return &BUILTINS[index]; }
int pd_alias_count(void) { return ALIAS_COUNT; }
const AliasEntry *pd_alias_at(int index) { return &ALIASES[index]; }

/* --- Tag classification --- */

static const char *WRAPPER_TAGS[] = {
    "div", "section", "span", "nav",
    "header", "footer", "main", "article", "aside",
};

#define WRAPPER_COUNT ((int)(sizeof(WRAPPER_TAGS) / sizeof(WRAPPER_TAGS[0])))

bool is_wrapper_tag(const char *name) {
    for (int i = 0; i < WRAPPER_COUNT; i++) {
        if (strcmp(name, WRAPPER_TAGS[i]) == 0)
            return true;
    }
    return false;
}

static const char *BLOCK_MACROS[] = {
    "h1", "h2", "h3", "h4", "h5", "h6",
    "p", "hr", "ul", "ol", "table",
    "div", "section", "nav",
    "header", "footer", "main", "article", "aside",
    "code",
};

#define BLOCK_COUNT ((int)(sizeof(BLOCK_MACROS) / sizeof(BLOCK_MACROS[0])))

bool is_block_macro(const char *name) {
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (strcmp(name, BLOCK_MACROS[i]) == 0)
            return true;
    }
    return false;
}
