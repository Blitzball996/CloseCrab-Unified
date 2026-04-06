# CloseCrab-Unified

A local-first AI coding assistant written in C++. Runs large language models on your own hardware, connects to remote APIs when you want, and gives the AI real tools to read files, write code, run commands, and search the web — all protected by a permission system you control.

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CUDA](https://img.shields.io/badge/CUDA-12.x-green.svg)](https://developer.nvidia.com/cuda-toolkit)
[![Windows](https://img.shields.io/badge/Platform-Windows-0078d7.svg)](https://www.microsoft.com/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

---

## What is CloseCrab-Unified?

Most AI coding tools require cloud access. CloseCrab-Unified gives you a choice: run a model locally on your GPU with zero internet dependency, or connect to Claude, OpenAI, or any compatible API through a single config change. Either way, the AI gets the same set of 42 tools — file operations, shell execution, code search, multi-agent collaboration, and more.

It started as a merger of two projects: **CloseCrab** (a C++ local inference engine with RAG and MoE streaming) and **JackProAi-claudecode** (a TypeScript CLI with 40+ tools and 100+ commands). The result is a single C++ binary that combines both worlds.

### Why use it?

- **Privacy**: Local mode keeps everything on your machine. No data leaves your network.
- **Flexibility**: Switch between local and remote models by editing one line in a config file.
- **Real tools**: The AI doesn't just talk — it reads your files, writes code, runs tests, searches the web.
- **Extensible**: 42 built-in tools, plugin system, MCP protocol support, skill directories.
- **Fast**: C++17, CUDA GPU acceleration, no Python runtime, single executable.

---

## Quick Start

### Option 1: Installer

Download `CloseCrab-Unified_Setup.exe` from Releases. The installer lets you choose local or API mode and configures everything.

### Option 2: Manual

```bash
cd G:\CMakePJ\CloseCrab-Unified
# Edit config/config.yaml (see below)
.\run.bat
```

---

## Switching Between Local and API Mode

This is the most common thing you'll want to do. There are three ways, pick whichever you prefer.

### Method 1: Edit config/config.yaml (recommended)

Open `config/config.yaml` in any text editor:

```yaml
# ========== LOCAL MODE ==========
# Runs a GGUF model on your GPU. No internet needed.
provider: "local"
llm:
  model_path: "models/qwen2.5-7b-instruct-q4_k_m.gguf"

# ========== API MODE (Anthropic) ==========
# Uses Claude API or a compatible proxy.
# provider: "anthropic"
# api:
#   base_url: "https://api.anthropic.com"
#   api_key: "sk-ant-your-key"
#   model: "claude-sonnet-4-20250514"

# ========== API MODE (OpenAI compatible) ==========
# Works with OpenAI, LM Studio, SiliconFlow, Ollama, vLLM.
# provider: "openai"
# api:
#   base_url: "http://127.0.0.1:1234"
#   api_key: "your-key"
#   model: "qwen2.5-7b"
```

To switch: uncomment the mode you want, comment out the others. Restart the program.

### Method 2: Environment variables

```bash
set ANTHROPIC_AUTH_TOKEN=sk-your-key
set ANTHROPIC_BASE_URL=https://your-proxy.com
set ANTHROPIC_MODEL=claude-opus-4-20250514
closecrab-unified.exe -c config/config.yaml
```

### Method 3: Command-line arguments

```bash
closecrab-unified.exe --provider anthropic --api-key sk-xxx --api-url https://api.anthropic.com --api-model claude-sonnet-4-20250514
```

### Priority

CLI arguments > Environment variables > config.yaml

### Runtime commands

Once running, use these to check your current setup:

```
/provider    — show current provider
/api         — show API configuration
/model       — show current model name
/doctor      — full diagnostics
```

---

## Provider Reference

| Provider | `provider` | `api.base_url` | `api.model` |
|----------|-----------|----------------|-------------|
| Local GGUF | `local` | — | set `llm.model_path` |
| Anthropic Claude | `anthropic` | `https://api.anthropic.com` | `claude-sonnet-4-20250514` |
| Claude Proxy | `anthropic` | `https://your-proxy.com` | your model name |
| OpenAI | `openai` | `https://api.openai.com` | `gpt-4o` |
| LM Studio | `openai` | `http://127.0.0.1:1234` | loaded model |
| SiliconFlow | `openai` | `https://api.siliconflow.cn` | `moonshotai/Kimi-K2-Instruct` |
| Ollama | `openai` | `http://127.0.0.1:11434` | `qwen2.5` |

---

## Commands

| Command | Description |
|---------|-------------|
| `/help` | Show all commands |
| `/quit` `/exit` | Exit program |
| `/model [name]` | Show or change model |
| `/provider` | Show provider info |
| `/api` | Show API config help |
| `/status` | Session status |
| `/doctor` | Full diagnostics |
| `/commit [msg]` | Git commit |
| `/diff` | Git diff |
| `/branch [name]` | Git branch |
| `/log [N]` | Git log |
| `/push` `/pull` | Git push/pull |
| `/history [N]` | Conversation history |
| `/new` | New session |
| `/export [file]` | Export conversation |
| `/compact` | Compress history |
| `/rag [cmd]` | RAG: enable/disable/load/clear/list |
| `/permissions [mode]` | default/auto/bypass |
| `/plan` | Toggle plan mode |
| `/thinking [on/off/N]` | Toggle thinking mode |
| `/fast` | Toggle fast mode |
| `/tools` | List all tools |
| `/cost` | API cost summary |
| `/audit` | Permission audit log |
| `/sandbox [mode]` | Sandbox: disable/ask/auto/trusted |
| `/env` | Environment info |
| `/files [path]` | List directory |
| `/context` | Context info |
| `/reload` | Reload hint |

---

## Tools (42)

**File**: Read, Write, Edit, Glob, Grep, NotebookEdit
**Execution**: Bash, PowerShell, REPL
**AI**: Agent (5 types), Skill
**MCP**: MCPTool, ListMcpResources, ReadMcpResource, McpAuth
**Tasks**: TaskCreate/Update/Get/List/Stop/Output
**Team**: TeamCreate, TeamDelete
**Workflow**: EnterPlanMode, ExitPlanMode, EnterWorktree, ExitWorktree, CronCreate/Delete/List, Sleep
**Web**: WebSearch, WebFetch
**Interaction**: AskUserQuestion, SendMessage, TodoWrite
**System**: Config, LSP, RemoteTrigger, ToolSearch, Brief, SyntheticOutput

---

## Build from Source

### Requirements

- Windows 10/11, Visual Studio 2022, CMake 3.20+, CUDA 12.x (optional), vcpkg

### Build

```bash
vcpkg install curl:x64-windows faiss:x64-windows zlib:x64-windows

cd G:\CMakePJ\CloseCrab-Unified
cmake -B build -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

### Create Installer

Install [Inno Setup](https://jrsoftware.org/isinfo.php), open `installer.iss`, compile.

---

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed diagrams.

```
src/
├── main.cpp           # Entry point, component init, main loop
├── core/              # QueryEngine, Message, AppState, CostTracker
├── api/               # LocalLLM, Anthropic, OpenAI API clients
├── tools/             # 42 tool implementations
├── commands/          # 36 slash commands
├── agents/            # Multi-agent system (5 types)
├── mcp/               # MCP protocol client
├── plugins/           # Plugin & skill directory loader
├── permissions/       # Permission rule engine
├── rag/               # FAISS + ONNX vector search
├── ssd/               # SSD Expert Streaming for MoE
├── llm/               # llama.cpp inference engine
├── network/           # HTTP, WebSocket, SSE servers
├── git/               # Git CLI wrapper
├── lsp/               # LSP client
├── bridge/            # Remote execution
└── voice/             # Voice engine interface
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Failed to load config" | Run from project root, or `-c` with absolute path |
| "No model path" | Set `llm.model_path` in config.yaml with absolute path |
| HTTP 503 | API request too large or wrong model name |
| SSL/CURL errors | Program auto-skips SSL verify for proxies |
| Tools not executing | Local models need SKILL: format; remote APIs use native tool_use |
| Says "I'm Claude/Kiro" | Normal for Claude API — system prompt overrides identity |

---

## License

MIT
