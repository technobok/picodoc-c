#include "builtins.h"
#include <string.h>

/* --- Alias table --- */

typedef struct {
    const char *alias;
    const char *canonical;
} AliasEntry;

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
static const ParamDecl params_link[]     = {{"to", true}};
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

#define B(n, p, body) {n, p, (int)(sizeof(p)/sizeof(p[0])), body}
#define B0(n, body)   {n, NULL, 0, body}

static const BuiltinDef BUILTINS[] = {
    /* Structural */
    B0("h1", true),
    B0("h2", true),
    B0("h3", true),
    B0("h4", true),
    B0("h5", true),
    B0("h6", true),
    B0("p",  true),
    B0("hr", false),

    /* Inline */
    B0("b", true),
    B0("i", true),
    B("link", params_link, true),

    /* Code / literal */
    B("code", params_code, true),
    B("~",    params_code, true),
    B0("literal", true),

    /* Lists */
    B0("ul", true),
    B0("ol", true),
    B0("*",  true),

    /* Tables */
    B0("table", true),
    B0("tr",    true),
    B("td", params_td, true),
    B("th", params_td, true),

    /* Wrapper / container */
    B("div",     params_wrapper, true),
    B("section", params_wrapper, true),
    B("span",    params_wrapper, true),
    B("nav",     params_wrapper, true),
    B("header",  params_wrapper, true),
    B("footer",  params_wrapper, true),
    B("main",    params_wrapper, true),
    B("article", params_wrapper, true),
    B("aside",   params_wrapper, true),

    /* Document */
    B("doc.meta",   params_doc_meta,   false),
    B("doc.link",   params_doc_link,   false),
    B("doc.script", params_doc_script, true),
    B0("doc.title",         true),
    B0("doc.lang",          true),
    B0("doc.author",        true),
    B0("doc.version",       true),
    B0("doc.datecreated",   true),
    B0("doc.datemodified",  true),
    B("doc.content", params_doc_content, false),
    B("doc.body",    params_doc_body,    false),
    B("doc.toc",     params_doc_toc,     false),
    B("doc.heading.number", params_doc_toc, false),
    B("doc.heading.anchor", params_doc_toc, false),

    /* Expansion-time */
    B0("comment", true),
    B("set",     params_set,     true),
    B("ifeq",    params_ifeq,   true),
    B("ifne",    params_ifeq,   true),
    B("ifset",   params_ifset,  true),
    B("include", params_include, true),
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
