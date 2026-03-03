" Filetype plugin for PicoDoc (.pdoc)
if exists('b:did_ftplugin')
  finish
endif
let b:did_ftplugin = 1

setlocal commentstring=#//:\ %s
setlocal shiftwidth=2
setlocal expandtab
setlocal softtabstop=2
" PicoDoc identifier characters (special chars use ASCII codes to avoid
" conflicts with iskeyword's range/separator syntax):
"   .     dot         33 !   36 $   37 %   38 &
"   42 *  43 +        45 -   47 /   94 ^   126 ~
"   @-@   literal @
setlocal iskeyword+=.,45,42,33,36,37,38,43,47,@-@,94,126

let b:undo_ftplugin = 'setlocal commentstring< shiftwidth< expandtab< softtabstop< iskeyword<'
