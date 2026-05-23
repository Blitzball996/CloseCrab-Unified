# CloseCrab AI - Vim/Neovim Plugin

## Install

### vim-plug
```vim
Plug 'Blitzball996/CloseCrab-Unified', {'rtp': 'extensions/vim'}
```

### Manual
Copy `plugin/` and `autoload/` to your vim runtime path.

## Usage
- `:CrabChat <message>` - Chat with AI
- Visual select + `<leader>ce` - Explain selection
- Visual select + `<leader>cf` - Fix selection
- `<leader>cr` - Review current file

## Configuration
```vim
let g:closecrab_server = 'http://localhost:9001'
```

## Requirements
- CloseCrab-Unified running on localhost:9001
- curl installed
