" CloseCrab autoload functions

function! closecrab#chat(msg) abort
    let l:response = closecrab#send(a:msg)
    call closecrab#show_result(l:response)
endfunction

function! closecrab#explain() abort
    let l:code = closecrab#get_visual_selection()
    let l:msg = "Explain this code:\n```\n" . l:code . "\n```"
    let l:response = closecrab#send(l:msg)
    call closecrab#show_result(l:response)
endfunction

function! closecrab#fix() abort
    let l:code = closecrab#get_visual_selection()
    let l:msg = "Fix bugs in this code:\n```\n" . l:code . "\n```"
    let l:response = closecrab#send(l:msg)
    call closecrab#show_result(l:response)
endfunction

function! closecrab#review() abort
    let l:content = join(getline(1, '$'), "\n")
    let l:file = expand('%:t')
    let l:msg = "Review this file (" . l:file . "):\n```\n" . l:content[:5000] . "\n```"
    let l:response = closecrab#send(l:msg)
    call closecrab#show_result(l:response)
endfunction

function! closecrab#send(message) abort
    let l:data = json_encode({'message': a:message, 'session_id': 'vim'})
    let l:cmd = 'curl -s -X POST ' . g:closecrab_server . '/chat'
                \ . ' -H "Content-Type: application/json"'
                \ . ' -d ' . shellescape(l:data)
    let l:result = system(l:cmd)
    try
        let l:json = json_decode(l:result)
        return get(l:json, 'response', get(l:json, 'error', 'No response'))
    catch
        return 'Error: ' . l:result
    endtry
endfunction

function! closecrab#get_visual_selection() abort
    let [l:lnum1, l:col1] = getpos("'<")[1:2]
    let [l:lnum2, l:col2] = getpos("'>")[1:2]
    let l:lines = getline(l:lnum1, l:lnum2)
    if len(l:lines) == 0
        return ''
    endif
    let l:lines[-1] = l:lines[-1][:l:col2 - 1]
    let l:lines[0] = l:lines[0][l:col1 - 1:]
    return join(l:lines, "\n")
endfunction

function! closecrab#show_result(text) abort
    botright new
    setlocal buftype=nofile bufhidden=wipe noswapfile
    setlocal filetype=markdown
    call setline(1, split(a:text, "\n"))
    setlocal nomodifiable
    nnoremap <buffer> q :close<CR>
endfunction
