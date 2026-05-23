" CloseCrab AI - Vim/Neovim Plugin
" Connects to CloseCrab backend at localhost:9001

if exists('g:loaded_closecrab')
    finish
endif
let g:loaded_closecrab = 1

let g:closecrab_server = get(g:, 'closecrab_server', 'http://localhost:9001')

" Chat command
command! -nargs=+ CrabChat call closecrab#chat(<q-args>)
" Explain selection
command! -range CrabExplain call closecrab#explain()
" Fix selection
command! -range CrabFix call closecrab#fix()
" Review current file
command! CrabReview call closecrab#review()

" Key mappings
nnoremap <leader>cc :CrabChat
vnoremap <leader>ce :CrabExplain<CR>
vnoremap <leader>cf :CrabFix<CR>
nnoremap <leader>cr :CrabReview<CR>
