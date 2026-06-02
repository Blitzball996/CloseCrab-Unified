# CloseCrab-Unified User Guide

## Before You Start

### What is CloseCrab?

CloseCrab is an AI coding assistant that runs on your computer. You talk to it in a terminal, and it can:

- Write and edit code
- Read and search files
- Run commands
- Manage Git repositories
- Answer programming questions

It can use local models (no internet needed) or connect to cloud APIs like Claude and OpenAI.

### System Requirements

| Item | Minimum | Recommended |
|------|---------|-------------|
| OS | Windows 10 / macOS 12 / Ubuntu 20.04 | Windows 11 / macOS 14 / Ubuntu 22.04 |
| RAM | 8 GB | 16 GB or more |
| Disk Space | 2 GB (API mode only) | 20 GB (local models) |
| GPU | Not needed (API mode) | NVIDIA 8GB+ VRAM (local models) |
| Network | Required for API mode | Stable connection |

If you only use API mode (connecting to Claude or OpenAI), you don't need a powerful GPU. Any modern computer will work.

If you want to use local models (no internet), you need a good NVIDIA GPU with at least 8GB of VRAM.

### Download

- GitHub: https://github.com/Blitzball996/CloseCrab-Unified
- Pre-built packages are available on the Releases page

---

## Installation

### Windows

1. Open your browser and go to the GitHub Releases page
2. Download `closecrab-windows-x64.zip`
3. Right-click the downloaded zip file and select "Extract All"
4. Move the extracted folder to a location you like (e.g., `C:\Program Files\CloseCrab`)
5. Add the folder to your system PATH:
   - Right-click "This PC" > "Properties" > "Advanced system settings" > "Environment Variables"
   - Find `Path` under "System variables" and double-click it
   - Click "New" and enter the CloseCrab folder path
   - Click "OK" to save

### macOS

1. Download `closecrab-macos.dmg`
2. Double-click the dmg file to open it
3. Drag the CloseCrab icon to the Applications folder
4. Open Terminal and run:

```bash
sudo ln -s /Applications/CloseCrab.app/Contents/MacOS/closecrab /usr/local/bin/closecrab
```

### Linux

1. Download `closecrab-linux-x64.AppImage`
2. Open a terminal and navigate to your downloads:

```bash
cd ~/Downloads
```

3. Make the file executable:

```bash
chmod +x closecrab-linux-x64.AppImage
```

4. Move it to a system path (so you can run it from anywhere):

```bash
sudo mv closecrab-linux-x64.AppImage /usr/local/bin/closecrab
```

---

## First Time Use

### Step 1: Open a Terminal

**Windows:**
- Press `Win + R`, type `cmd`, press Enter
- Or: Press `Win`, search for "Terminal" or "PowerShell", click to open

**macOS:**
- Press `Cmd + Space`, type `Terminal`, press Enter
- Or: Open "Applications" > "Utilities" > "Terminal"

**Linux:**
- Press `Ctrl + Alt + T`
- Or: Search for "Terminal" in your app menu

### Step 2: Start CloseCrab

Type in your terminal:

```bash
closecrab
```

Press Enter. You will see the welcome screen.

### Step 3: Choose a Mode

CloseCrab supports two modes:

**Mode A: Local Model (no internet needed)**

```bash
closecrab --provider local --model /path/to/your/model.gguf
```

Replace `/path/to/your/model.gguf` with the actual path to your downloaded model file.

**Mode B: API Mode (internet required)**

Using Claude API:

```bash
closecrab --provider anthropic --api-key sk-ant-xxxxx --api-model claude-sonnet-4-20250514
```

Using OpenAI API:

```bash
closecrab --provider openai --api-key sk-xxxxx --api-model gpt-4o
```

Replace `sk-ant-xxxxx` or `sk-xxxxx` with your actual API key.

### Step 4: Configure Your API Key

You can save your API key in a config file so you don't have to type it every time.

1. Create a file called `config/config.yaml` in your project folder
2. Add the following content:

```yaml
provider: anthropic
api:
  key: sk-ant-your-key-here
  model: claude-sonnet-4-20250514
  base_url: https://api.anthropic.com
```

Then just run `closecrab` without any extra arguments.

---

## Basic Commands

In CloseCrab, all commands start with `/`.

### /help - Show Help

Displays a list of all available commands.

```
> /help
```

### /model - Switch Model

Check current model:

```
> /model
Current model: claude-sonnet-4-20250514
```

Switch to a different model:

```
> /model gpt-4o
Model set to: gpt-4o
```

### /clear - Clear Conversation

Erases all conversation history and starts fresh.

```
> /clear
Conversation cleared.
```

### /compact - Compress History

When the conversation gets too long, compress the history to save memory and tokens.

```
> /compact
```

### /session - Manage Sessions

View current session information:

```
> /session
```

### /status - View Status

Shows detailed status of the current session, including model, message count, and cost.

```
> /status
```

### /tools - List Available Tools

Shows all tools the AI can use (read files, write files, run commands, etc.).

```
> /tools
```

### /permissions - Permission Management

Check current permission mode:

```
> /permissions
Permission mode: default
```

Change permission mode:

```
> /permissions auto
> /permissions bypass
> /permissions default
```

- `default`: Asks you before every risky operation
- `auto`: Automatically allows safe operations
- `bypass`: Allows all operations (use with caution!)

### /memory - Memory System

CloseCrab can remember your preferences and project information.

```
> /memory
```

### /cost - View Spending

Shows API usage cost statistics.

```
> /cost
```

### /audit - View Audit Log

Shows the history of permission operations.

```
> /audit
```

### /quit - Exit

Exits CloseCrab. You can also use `/exit` or `/q`.

```
> /quit
```

### Running Shell Commands

Add `!` before any input to run it as a system command:

```
> !git status
> !ls -la
> !npm install
```

---

## Team Mode

Team Mode lets multiple people connect to the same CloseCrab instance on one computer.

### How to Connect Multiple People to One Computer

1. Start CloseCrab on the server computer (it enables WebSocket service):

```bash
closecrab
```

CloseCrab starts a WebSocket service on port 9002.

2. Other people connect through CloseCrab-Web (mobile interface). See the CloseCrab-Web User Guide for details.

### How to View the Leaderboard

On the CloseCrab-Web Team page, you can see:

- Everyone's score
- Rankings
- Earned badges

### How to Search the Team Knowledge Base

CloseCrab has a built-in RAG (Retrieval-Augmented Generation) system. It automatically indexes your codebase, and team members can search shared knowledge.

```
> /rag search how to configure database connection
```

---

## Troubleshooting

### Model Fails to Load

1. Check that the model file path is correct
2. Check that the file is complete (not a partial download)
3. Check if you have enough GPU memory:

```bash
nvidia-smi
```

4. Try reducing GPU layers:

In `config/config.yaml`:

```yaml
gpu:
  layers: 20
```

Lower numbers use less GPU memory.

### API Errors

1. Check that your API key is correct
2. Check your internet connection
3. Check that your API account has sufficient balance
4. Common error codes:
   - `401`: Invalid API key
   - `429`: Too many requests, wait and try again
   - `500`: Server error, try again later

### Not Enough Memory

1. Use `/compact` to compress conversation history
2. Use `/clear` to start a fresh conversation
3. Close other memory-heavy programs
4. If using a local model, switch to a smaller one

### How to Update

**Windows / Linux:**

Download the latest version and replace the old files.

**macOS:**

Download the new dmg and drag it to Applications to overwrite the old version.

---

## Keyboard Shortcuts

| Shortcut | Function |
|----------|----------|
| `Enter` | Send message |
| `Ctrl + C` | Interrupt current operation |
| `Ctrl + D` | Exit program |
| `Ctrl + L` | Clear screen |
| `Tab` | Auto-complete commands |
| `Up` / `Down` | Browse input history |
| `!command` | Run shell command |

---

## Configuration File Reference

Config file location: `config/config.yaml`

```yaml
# Model provider: local, anthropic, openai
provider: anthropic

# API settings
api:
  key: your-api-key-here
  model: claude-sonnet-4-20250514
  base_url: https://api.anthropic.com

# Local model settings
local:
  model_path: /path/to/model.gguf

# GPU settings (local models)
gpu:
  layers: -1        # -1 = use all GPU, number = specific layer count
  batch_size: 512
  threads: 0        # 0 = auto-detect

# Permission mode: default, auto, bypass
permissions: default
```

---

## Need More Help?

- GitHub Issues: https://github.com/Blitzball996/CloseCrab-Unified/issues
- Type `/help` inside CloseCrab to see command help
