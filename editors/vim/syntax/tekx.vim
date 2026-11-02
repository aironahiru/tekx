" Vim syntax file for tekx (https://github.com/aironahiru/tekx)
if exists("b:current_syntax")
  finish
endif

syn case match

" comments: // at the start of a line or after a blank
syn match tekxComment "\(^\|\s\)\zs//.*$" contains=@Spell

" settings at the start of a line
syn match tekxSetting "^\(title\|subtitle\|author\|date\|language\|lang\|keywords\|subject\|paper\|size\|font\|msc\|numbering\|bibliography\|citations\|running\|linenumbers\|notation\|tekx\|document\)\>"
syn match tekxKey "\<\(affiliation\|email\|orcid\|url\):" contained containedin=tekxString

" blocks
syn match tekxHeading "^\(section\|subsection\|subsubsection\)\*\=\>.*$" contains=tekxLabel,tekxMath
syn match tekxHeading "^#\{1,3} .*$" contains=tekxLabel,tekxMath
syn match tekxBlock "^\s*\(theorem\|lemma\|proposition\|corollary\|conjecture\|claim\|axiom\|hypothesis\|fact\|definition\|example\|exercise\|problem\|solution\|notation\|assumption\|remark\|note\|observation\)\*\=\>"
syn match tekxBlock "^\s*\(proof\|abstract\|quote\|figure\|references\|contents\|newpage\|appendix\|let\)\>"
syn match tekxBlock "\<do$"
syn match tekxBlock "^\s*end$"
syn match tekxDefine "^\s*\(define\|use\)\>"
syn match tekxDefine "\<\(like\|titled\|read as\|as\|block\|function\|operator\)\>" contained

" inline
syn region tekxMath matchgroup=tekxMathDelim start="\$\ze[^ $]" skip="\\\$" end="\$" oneline keepend
syn region tekxMath matchgroup=tekxMathDelim start="^\s*\$\$" end="\$\$"
syn region tekxString start=+"+ skip=+\\"+ end=+"+ oneline contains=tekxKey
syn match tekxLabel "\s\zs#[A-Za-z0-9_:-]\+$"
syn match tekxRef "@[A-Za-z0-9_:-]\+"
syn match tekxCite "\[@[^]]*\]" contains=tekxRef,tekxMath
syn match tekxMacro "{[A-Za-z][A-Za-z0-9_-]*\([ }][^}]*\)\=}"
syn region tekxEmph start="\*\ze[^ *]" end="\*" oneline contains=tekxMath
syn region tekxStrong start="\*\*\ze[^ ]" end="\*\*" oneline contains=tekxMath
syn region tekxCode start="`" end="`" oneline
syn region tekxCodeBlock start="^\s*```" end="^\s*```"
syn match tekxFootnote "\^\["
" a line indented under the text is display mathematics
syn match tekxDisplay "^\s\{4,}\S.*$" contains=tekxLabel,tekxComment

hi def link tekxComment    Comment
hi def link tekxSetting    PreProc
hi def link tekxKey        Identifier
hi def link tekxHeading    Title
hi def link tekxBlock      Statement
hi def link tekxDefine     Define
hi def link tekxMath       Special
hi def link tekxMathDelim  Delimiter
hi def link tekxDisplay    Special
hi def link tekxString     String
hi def link tekxLabel      Label
hi def link tekxRef        Underlined
hi def link tekxCite       Underlined
hi def link tekxMacro      Macro
hi def link tekxCode       Constant
hi def link tekxCodeBlock  Constant
hi def link tekxFootnote   Delimiter
hi def tekxEmph   term=italic cterm=italic gui=italic
hi def tekxStrong term=bold cterm=bold gui=bold

let b:current_syntax = "tekx"
