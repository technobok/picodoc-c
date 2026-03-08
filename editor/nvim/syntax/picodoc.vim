" Vim syntax file for PicoDoc (.pdoc)
if exists('b:current_syntax')
  finish
endif

" --- Interpreted strings with escapes ---------------------------------------
" Negative lookahead prevents matching at """ (raw string delimiters).
syn region picodocString start=/"\%(""\)\@!/ skip=/\\"/ end=/"/ contains=picodocStringEscape
syn match picodocStringEscape /\\[\\"\[\]nt]/ contained
syn match picodocStringEscape /\\x\x\{2}/ contained
syn match picodocStringEscape /\\U\x\{8}/ contained

" --- Raw strings (N-quoted, no escapes) ------------------------------------
" Defined after picodocString so they take priority at the same position.
" Longer delimiters defined last so they win over shorter ones.
syn region picodocRawString start=/"""/ end=/"""/
syn region picodocRawString start=/""""/ end=/""""/
syn region picodocRawString start=/"""""/ end=/"""""/
syn region picodocRawString start=/""""""/ end=/""""""/

" --- Prose escapes (outside strings) ----------------------------------------
syn match picodocProseEscape /\\[\\#\[\]:=]/
syn match picodocProseEscape /\\x\x\{2}/
syn match picodocProseEscape /\\U\x\{8}/

" --- Macro calls: # prefix + name ------------------------------------------
" Structural macros
syn match picodocStructural /#\%(h[1-6]\|-\{1,6}\|p\|hr\|ul\|ol\|table\|tr\|td\|th\|code\|div\|section\|span\|nav\|header\|footer\|main\|article\|aside\)\>/
" Conditional / expansion-time macros
syn match picodocConditional /#\%(set\|ifeq\|ifne\|ifset\|include\)\>/
" Inline macros
syn match picodocInline /#\%(\*\*\|__\|b\|i\|link\|>\|\~\|literal\)\>/
" Environment variable pattern and doc.* namespace
syn match picodocEnv /#\%(env\|doc\)\.[A-Za-z0-9._\-]*/
" Comment macro (hash form — region below handles the full extent)
syn match picodocMacroHash /#/ contained containedin=picodocStructural,picodocConditional,picodocInline,picodocEnv
" Fallback: any other #identifier
syn match picodocMacroName /#[A-Za-z!$%&*+\-/<>@^_~|.][A-Za-z0-9!$%&*+\-/<>@^_~|.]*/

" --- Bracketed calls [#...] ------------------------------------------------
syn region picodocBracketCall matchgroup=picodocBracket start=/\[\ze#/ end=/\]/ transparent contains=TOP

" --- Arguments: name=value --------------------------------------------------
syn match picodocEquals /=/ contained containedin=picodocArgAssign
syn match picodocArgAssign /[A-Za-z][A-Za-z0-9_.-]*=/ contains=picodocArgName,picodocEquals
syn match picodocArgName /[A-Za-z][A-Za-z0-9_.-]*\ze=/ contained

" --- List item alternate form -----------------------------------------------
syn match picodocStructural /#\*\>/
syn match picodocStructural /#li\>/

" --- Comments (defined last to take priority) -------------------------------
" Single-line: text after colon on same line, ends at EOL
syn region picodocComment start=/#\%(comment\|\/\/\)\s*:\s*\S/ end=/$/ contains=@NoSpell
" Paragraph: nothing after colon, body continues until blank line
syn region picodocComment start=/#\%(comment\|\/\/\)\s*:\s*$/ skip=/\n\s*\S/ end=/\n\|\%$/ contains=@NoSpell
syn region picodocComment start=/\[#\%(comment\>\|\/\/\)/ end=/\]/ contains=@NoSpell

" --- Highlight links --------------------------------------------------------
hi def link picodocComment      Comment
hi def link picodocMacroHash    Keyword
hi def link picodocMacroName    Function
hi def link picodocStructural   Statement
hi def link picodocConditional  Conditional
hi def link picodocInline       Type
hi def link picodocEnv          Macro
hi def link picodocString       String
hi def link picodocRawString    String
hi def link picodocStringEscape SpecialChar
hi def link picodocProseEscape  SpecialChar
hi def link picodocArgName      Identifier
hi def link picodocEquals       Operator
hi def link picodocBracket      Delimiter

" --- Sync: look back enough lines to find multi-line raw string starts ------
syn sync minlines=500

let b:current_syntax = 'picodoc'
