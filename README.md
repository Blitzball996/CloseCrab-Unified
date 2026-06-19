<div align="center">

**English** | [中文](README.zh-CN.md)

</div>

# CloseCrab-Unified

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CUDA](https://img.shields.io/badge/CUDA-12.x-green.svg)](https://developer.nvidia.com/cuda-toolkit)
[![Windows](https://img.shields.io/badge/Platform-Windows%20|%20Linux%20|%20macOS-0078d7.svg)](#platforms)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Tools](https://img.shields.io/badge/Tools-59-orange.svg)](#tools-59)
[![Team Mode](https://img.shields.io/badge/Team%20Mode-Parallel-ff69b4.svg)](#team-mode)
[![Skills](https://img.shields.io/badge/Skills-11-purple.svg)](#skills)
[![Binary Size](https://img.shields.io/badge/Binary-~3.2MB-brightgreen.svg)](#)
[![Context](https://img.shields.io/badge/Context-1M%20tokens-blueviolet.svg)](#)

A local-first AI coding assistant. Single C++17 binary, 59 tools, 84 commands, Team Mode, Voice (TTS+ASR), runs LLMs on your GPU or connects to cloud APIs with 1M token context window.

---

## What is CloseCrab-Unified?

CloseCrab-Unified is a terminal-based AI coding assistant written in C++17. It runs large language models locally on your hardware via llama.cpp, or connects to Anthropic (Claude), OpenAI, and compatible APIs through a single configuration change. The AI gets the same 59 tools either way: file operations, shell execution, code search, multi-agent collaboration, web access, voice input/output, and more.

The project merges two predecessors: **CloseCrab** (a C++ local inference engine with RAG and MoE streaming) and **JackProAi-claudecode** (a TypeScript CLI with 40+ tools and 95 commands). The result is a single ~3.2 MB executable built from ~170 source files, with 59 tools, 84 commands, and 30+ service modules.

### Why use it?

- **Privacy** -- Local mode keeps everything on your machine. No data leaves your network.
- **Flexibility** -- Switch between local and remote models by editing one line in a config file.
- **Real tools** -- The AI reads files, writes code, runs tests, searches the web, manages git, and more.
- **Extensible** -- Plugin system, MCP protocol support, skill directories, cron scheduling, hooks.
- **Fast** -- C++17, CUDA GPU acceleration, concurrent tool execution, no runtime dependencies, single binary.
- **Multi-agent** -- Coordinator mode with cache-sharing sub-agents for complex task decomposition.
- **Team Mode** -- Multi-client parallel inference with gamification and shared knowledge base.
- **Voice** -- TTS output + ASR input (Windows SAPI / whisper.cpp).
- **1M Context** -- Full Claude Opus 4.7 context window support (800K auto-compact threshold).

---

## What's New in 0.4.0 (The 503 / "forgets what it was doing" Fix + MCP Tool Expansion)

This release fixes the real root cause behind "卡 503 一会会就忘了之前在做什么" and makes MCP server tools first-class. The 503 fixes are all aligned to JackProAi's actual context-management source (`services/compact/`).

**The 503 "amnesia" loop — root cause and fix.** The trigger was never random provider downtime; it was CloseCrab's own context management. During a deep investigation the model would read many files, piling up tool-result blocks until the request hit ~160-200K tokens. The proxy rejected the oversized request with a 503, and CloseCrab — which assumed "503 = too big" — kept compacting harder on every retry (L1→L9) and finally summarized the whole conversation from 302 messages down to 11, destroying 96% of context. After you typed "继续" the model genuinely had no idea what it was doing.

- **Pre-flight threshold now tracks the real context window** — The auto-compaction threshold was hardcoded to 800K, ~4× the model's real 200K limit, so it *never fired* before the request was already too big. It's now derived the way JackProAi does it (`getContextWindowForModel` / `getAutoCompactThreshold`): `env CLOSECRAB_MAX_CONTEXT_TOKENS` > `config api.context_window` > `"[1m]"` model tag → 1M > 200K default, then `effectiveWindow = window − 20K output`, `threshold = effectiveWindow − 13K`. CloseCrab now compacts at ~167K — *before* the request is ever rejected. Set `api.context_window` in `config.yaml` to pin it to your proxy's real limit.
- **503 = "wait", not "shrink"** — A 503/504 whose body says "供应商暂时不可用 / overloaded / service_unavailable" on a request that isn't actually large is now treated as a transient outage: exponential-backoff retry with context **preserved**, no compaction. Only a genuinely oversized request (or an explicit context-limit error) raises the compaction level. Mirrors JackProAi's `Hw6` (overloaded → pure retry) vs `fX4` (real size error → adjust).
- **Surgical compaction instead of a nuke** — When trimming IS needed, CloseCrab now uses micro-compaction (clear *old tool-result content* only, keep every message and the recent results) instead of summarizing 302→11. Your task context survives, so "继续" continues. Mirrors JackProAi's `microCompact.ts`.
- **Compaction circuit breaker** — After 3 consecutive compactions that free nothing, CloseCrab stops hammering the API with doomed retries (JackProAi `MAX_CONSECUTIVE_AUTOCOMPACT_FAILURES = 3`); resets on the next successful turn.

**MCP tools are now first-class.** Each connected MCP server tool is registered as its own tool (`mcp__<server>__<tool>`) with the server's real name, description and schema, so the model can call e.g. `mcp__codebase-memory__search_graph` directly instead of routing through one opaque proxy. Verified with codebase-memory-mcp over stdio (14 tools exposed).

---

## What's New in 0.3.9 (Crash Fixes, Crash-Safe Save & Per-Project Sessions)

This release stops the "用着用着疯狂闪退" crashes, stops losing progress on abrupt exit, and isolates session history per project.

- **Fixed the streaming-callback crash (`0xE06D7363`)** -- The big one. The OpenAI-compatible client's libcurl write callback had no exception guard, so a C++ exception thrown anywhere in the parse/callback chain (a 503/504 turned into a throw, bad UTF-8, a JSON error, an OOM on a huge tool result) would unwind **through libcurl's C stack frames** — undefined behavior that on MSVC lands as `0xE06D7363` hitting the top-level handler and killing the process. This is why it crashed "用着用着" (after several turns, on some streamed response) rather than at startup. The callback is now `noexcept` with a try/catch that, on error, returns a short byte count so curl aborts cleanly (`CURLE_WRITE_ERROR`) and the existing retry/error path takes over instead of the process dying. (`RemoteAPIClient` already had this guard since an earlier fix; the OpenAI-compatible path — the one actually used against relays — was missed.)
- **Worker-thread crash on remote control** -- The mobile/web `SessionRouter` worker ran `submitMessage` with no try/catch, so the same class of exception crashed the process via `std::terminate` when driving CloseCrab from a phone/browser. The worker now catches everything, reports the error to the client, and stays alive; a scope guard also clears the per-client "generating" flag so a thrown turn no longer leaves the client stuck.
- **Crash-safe progress save** -- Closing the window (the X button), `taskkill`, Ctrl-C, logoff and shutdown previously skipped the end-of-session save entirely, losing the current conversation. A `SetConsoleCtrlHandler` now flushes the transcript before the process dies. The assistant turn (text + tool calls) is also persisted to the JSONL transcript *before* tools run, so a mid-turn crash loses at most the in-flight tool's result, never the whole turn.
- **Per-project session isolation** -- Transcripts are now bucketed by your project directory (`data/transcripts/<hash>__<project>/`) instead of one flat shared folder. `/resume` and the default session-reuse only ever see the current project's history, so opening a different project no longer resurfaces CloseCrab's own sessions. Same project resolves to the same bucket regardless of slash style / case / trailing slash; pre-existing flat transcripts stay loadable via a legacy fallback.

---

## What's New in 0.3.8 (503/504 Resilience & Resume)

> **0.3.8 hotfix:** 0.3.7 crashed on launch (a duplicate `-c` short flag made CLI parsing throw before `main` could run). 0.3.8 fixes that — use `--continue` (no short form). Everything below shipped in 0.3.7 and is now actually runnable.

This release breaks the "long conversation eventually 503/504s" loop and fixes session resume:

- **No more 503/504 death-loop** -- The root cause was that retry-time compaction only shrank a *copy* of the request, never the persisted history, so every retry (and your next message) resent the same oversized payload and failed again. Compaction is now carried across retries and re-applied to each rebuilt request, and it triggers on server errors (`503`/`504`/`500`/`502`), not just network timeouts.
- **504 handled explicitly** -- Gateway-timeout is now classified as a retryable server error (it usually means the request was too large for the proxy to process in time).
- **Server-side context management on by default** -- Old read-tool results (`Bash`/`Glob`/`Grep`/`Read`/`WebFetch`/`WebSearch`) are cleared by the server once input crosses the threshold, without disturbing the cached prefix. Auto-strips the field and retries if a proxy rejects it; opt out with `CLOSECRAB_API_CLEAR_TOOL_RESULTS=0`.
- **Post-retry auto-compaction** -- If all retries are exhausted on a server error, the conversation is compacted once so your *next* message starts smaller instead of 503ing again.
- **`/resume` actually shows your history** -- Previously it loaded messages into memory but only printed "Restored N messages"; the past turns never appeared. It now replays the restored conversation on screen.
- **Transcripts keep tool calls** -- Session transcripts now store full content blocks (`tool_use`/`tool_result`/`thinking`) instead of flattened text, so resumed sessions are complete. Legacy transcripts still load.
- **Session continuity** -- Launches now reuse the most recent session id (the transcript keeps growing across runs) without preloading past turns into context. New flags: `--continue` loads past history into context, `--new` forces a fresh session.
- **Version banner fix** -- The in-app version (banner / `/version`) was stuck at an old number because `CMakeLists.txt` lagged behind the installer/tag; the single source of truth is now corrected.

---

## What's New in 0.3.1 (Licensing & macOS/Linux Persistence)

This release adds offline license activation and improves cross-platform persistence:

- **Offline Activation** -- License activation works without internet after initial online activation
- **macOS/Linux License Persistence** -- Licenses now persist across restarts on macOS and Linux (was Windows-only)
- **Windows Installer** -- Full working Windows installer ships runtime DLLs and icons
- **SSL Thread Safety** -- Fixed libcurl SSL connect error on macOS ("用着用着SSL connect error") by adding `CURLOPT_NOSIGNAL` for thread safety
- **Merge Consecutive User Messages** -- Multiple user messages without assistant responses are automatically merged
- **Pause Turn Handling** -- Handle `pause_turn` in streaming API responses (fixes infinite "waiting for response")
- **CI Fixes** -- Vendor ChineseSimplified Inno Setup language file, track installer icon in git

---

## What's New in 0.2.1 (Reliability & Cost)

This release hardens the agent loop and cuts API cost on repeated turns:

- **Read-before-write safety** -- Write/Edit refuse to overwrite a file the model hasn't read, and detect external modifications via mtime + content hash. A failed write no longer leads the model down a "delete and rebuild" path that could lose your files.
- **Dangerous `rm` guard** -- `rm`/`rmdir`/`del` targeting protected paths (drive roots, home, root-level dirs, wildcards) always prompt for confirmation -- this cannot be bypassed by auto-approve mode.
- **Windows shell quoting fix** -- Commands like `node -e "..."`, `sed -i "..."`, and `python -c "..."` no longer break on Git Bash. Commands are run through a single-quoted `eval` wrapper with proper Windows argv escaping; `>nul` is rewritten to `/dev/null`.
- **Working-directory persistence** -- `cd subdir` now carries over to the next Bash call (captured via `pwd -P`).
- **Prompt-cache optimization** -- The system prompt is split into a stable (cached) prefix and a dynamic tail; old tool results are compacted deterministically so the message prefix stays byte-stable. This restores prompt-cache hits and sharply reduces cost on multi-turn sessions.
- **Model fallback chain** -- After repeated `503`/overload errors, the client automatically falls back from the primary model (e.g. Opus) to a configured fallback (e.g. Sonnet). Set `api.fallback_model` in `config.yaml`.

---

## Quick Start

### Option 1: Installer

Download `CloseCrab-Unified_Setup.exe` from Releases. The installer lets you choose local or API mode and configures everything automatically.

### Option 2: Manual

```bash
# Clone or extract the project
cd G:\CMakePJ\CloseCrab-Unified

# Edit config/config.yaml to set your provider (local, anthropic, or openai)
# See "Switching Between Local and API Mode" below

# Run
.\run.bat
```

---

## Switching Between Local and API Mode

There are three ways to configure the provider. Pick whichever suits your workflow.

### Method 1: Edit config/config.yaml (recommended)

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
# Works with OpenAI, LM Studio, SiliconFlow, Ollama, vLLM, etc.
# provider: "openai"
# api:
#   base_url: "http://127.0.0.1:1234"
#   api_key: "your-key"
#   model: "qwen2.5-7b"
```

Uncomment the mode you want, comment out the others, and restart.

### Method 2: Environment variables

```bash
set ANTHROPIC_AUTH_TOKEN=sk-your-key
set ANTHROPIC_BASE_URL=https://your-proxy.com
set ANTHROPIC_MODEL=claude-opus-4-20250514
closecrab.exe -c config/config.yaml
```

### Method 3: Command-line arguments

```bash
closecrab.exe --provider anthropic --api-key sk-xxx --api-url https://api.anthropic.com --api-model claude-sonnet-4-20250514
```

### Priority

CLI arguments > Environment variables > config.yaml

### Runtime verification

Once running, check your setup with:

```
/provider    -- show current provider
/api         -- show API configuration
/model       -- show current model name
/doctor      -- full diagnostics
```

---

## Provider Reference

| Provider | `provider` value | `api.base_url` | `api.model` example |
|----------|-----------------|----------------|---------------------|
| Local GGUF | `local` | -- | Set `llm.model_path` instead |
| Anthropic Claude | `anthropic` | `https://api.anthropic.com` | `claude-sonnet-4-20250514` |
| Claude Proxy | `anthropic` | `https://your-proxy.com` | your model name |
| OpenAI | `openai` | `https://api.openai.com` | `gpt-4o` |
| LM Studio | `openai` | `http://127.0.0.1:1234` | loaded model name |
| SiliconFlow | `openai` | `https://api.siliconflow.cn` | `moonshotai/Kimi-K2-Instruct` |
| Ollama | `openai` | `http://127.0.0.1:11434` | `qwen2.5` |
| vLLM | `openai` | `http://127.0.0.1:8000` | deployed model name |

---

## Commands

All commands start with `/`. Use `/help` to see the full list at runtime.

### Session

| Command | Aliases | Description |
|---------|---------|-------------|
| `/help` | | Show all available commands |
| `/quit` | `/exit` | Exit the program |
| `/clear` | | Clear the terminal screen |
| `/new` | | Start a new conversation session |
| `/session` | | Show current session info (ID, working dir, message count) |
| `/resume` | | Restore the last saved conversation session |
| `/history [N]` | | Show last N messages (default 20) |
| `/export [file]` | | Export conversation to a markdown file |
| `/share [file]` | | Export conversation as shareable markdown with model info |
| `/compact` | | Compress conversation history to save context window |
| `/context` | | Show context info (message count, token estimate, CLAUDE.md status) |
| `/status` | | Show session status summary |
| `/version` | | Show version and current model |
| `/env` | | Show environment variables and config paths |

### Model

| Command | Aliases | Description |
|---------|---------|-------------|
| `/model [name]` | | Show or change the current model |
| `/provider [name]` | | Show or switch provider (local/anthropic/openai) |
| `/api` | | Show or set API configuration |
| `/cost` | | Show API cost summary (input/output tokens, USD) |
| `/fast` | | Toggle fast mode (shorter responses) |
| `/brief` | | Toggle brief/concise output mode |
| `/thinking [on/off/N]` | `/think`, `/effort` | Toggle extended thinking; optionally set token budget |
| `/permissions [mode]` | | Set permission mode: default, auto, or bypass |

### Git

| Command | Aliases | Description |
|---------|---------|-------------|
| `/commit [msg]` | | Stage all and commit; prompts for message if omitted |
| `/diff [args]` | | Show staged and unstaged git diff |
| `/branch [name]` | | List, create, or switch branches |
| `/log [N]` | | Show last N commits (default 10) |
| `/push [args]` | | Push to remote repository |
| `/pull [args]` | | Pull from remote repository |
| `/stash [args]` | | Git stash operations (push, pop, list, etc.) |
| `/pr [args]` | | Create a pull request via `gh` CLI |
| `/review [ref]` | `/cr` | Code review: git diff piped to LLM analysis |
| `/pr_comments [N]` | | View comments on a pull request |
| `/issue [cmd]` | | GitHub issues: list, view N, create |
| `/autofix-pr [N]` | | Auto-fix issues in a PR (reads comments + checks, applies fixes) |

### Tools

| Command | Aliases | Description |
|---------|---------|-------------|
| `/tools` | | List all 51 registered tools |
| `/skills` | | List available skill files from `.crab/skills/` |
| `/plugin` | `/plugins` | List loaded plugins from `.crab/plugins/` |
| `/mcp` | | List MCP servers and their connection status |
| `/hooks` | | List configured PreToolUse/PostToolUse hooks |
| `/agents` | | List running and completed sub-agents |
| `/tasks` | | Show current task list |
| `/audit` | | Show permission audit log |
| `/usage` | | Detailed token usage stats (per-model, context size, API/tool time) |

### Advanced

| Command | Aliases | Description |
|---------|---------|-------------|
| `/doctor` | | Run full environment diagnostics (git, rg, node, python, gh, configs) |
| `/coordinator [task]` | `/coord`, `/ultraplan` | Multi-agent task decomposition and parallel execution |
| `/plan` | | Toggle plan mode (read-only exploration, no writes) |
| `/rag [cmd]` | | RAG management: enable, disable, load, clear, list, status |
| `/ssd` | | Show SSD Expert Streaming status for MoE models |
| `/sandbox [mode]` | | Set sandbox mode: disable, ask, auto, trusted |
| `/vim` | | Toggle vim keybinding mode (Normal/Insert/Command) |
| `/voice [on/off]` | | Toggle text-to-speech output |
| `/memory [cmd]` | | Manage file-based memories: list, show, delete |
| `/theme [name]` | | Switch color theme (dark, light, minimal) |
| `/add-dir <path>` | | Add a directory to the working context |
| `/files [path]` | | List files in current or specified directory |
| `/reload` | | Reload configuration (requires restart) |
| `/rename <name>` | | Rename the current session |
| `/tag <label>` | | Add a tag to the current session |
| `/copy` | | Copy last assistant response to clipboard |
| `/summary` | | Generate a conversation summary via LLM |
| `/rewind [N]` | | Undo last N conversation turns (default 2) |
| `/effort [level]` | | Set reasoning effort: low, medium, high |
| `/output-style [fmt]` | `/style` | Switch output format: markdown, plain, json |
| `/thinkback` | | Replay the AI's thinking process from last response |
| `/bughunter [path]` | | Automated bug search mode (systematic codebase scan) |
| `/passes N <task>` | | Run N automated passes on a task |

### Input shortcuts

| Shortcut | Description |
|----------|-------------|
| `!command` | Shell escape -- runs the command directly (e.g., `!git status`) |
| `\` at end of line | Multi-line input -- backslash continuation |

---

## Tools (51)

Tools are the capabilities exposed to the LLM. Each tool has a JSON Schema for input validation, a permission check, and a result renderer. The LLM calls tools via the API `tool_use` protocol (Anthropic/OpenAI) or via `SKILL:` / function-call format (local models).

### File

| Tool | Description | Read-only |
|------|-------------|-----------|
| Read | Read file contents (with line range support) | Yes |
| Write | Write or overwrite a file | No |
| Edit | Apply targeted string replacements in a file | No |
| Glob | Fast file pattern matching (`**/*.ts`, `src/**/*.h`) | Yes |
| Grep | Regex content search powered by ripgrep | Yes |
| NotebookEdit | Edit Jupyter notebook cells | No |

### Execution

| Tool | Description | Read-only |
|------|-------------|-----------|
| Bash | Execute shell commands (bash/sh) | No |
| PowerShell | Execute PowerShell commands (Windows) | No |
| REPL | Run code in a persistent REPL session | No |

### AI / Agents

| Tool | Description | Read-only |
|------|-------------|-----------|
| Agent | Spawn a sub-agent (5 types: general-purpose, explore, plan, verification, code-guide) | No |
| Skill | Invoke a skill from `.crab/skills/` | No |

### MCP (Model Context Protocol)

| Tool | Description | Read-only |
|------|-------------|-----------|
| MCPTool | Call a tool on a connected MCP server | No |
| ListMcpResources | List resources from MCP servers | Yes |
| ReadMcpResource | Read a specific MCP resource | Yes |
| McpAuth | Authenticate with an MCP server | No |

### Tasks

| Tool | Description | Read-only |
|------|-------------|-----------|
| TaskCreate | Create a background task | No |
| TaskUpdate | Update task status or data | No |
| TaskGet | Get task details | Yes |
| TaskList | List all tasks | Yes |
| TaskStop | Stop a running task | No |
| TaskOutput | Get task output | Yes |

### Team

| Tool | Description | Read-only |
|------|-------------|-----------|
| TeamCreate | Create a team of agents | No |
| TeamDelete | Delete a team | No |

### Workflow

| Tool | Description | Read-only |
|------|-------------|-----------|
| EnterPlanMode | Switch to read-only plan mode | No |
| ExitPlanMode | Exit plan mode | No |
| EnterWorktree | Create and enter a git worktree | No |
| ExitWorktree | Leave a git worktree | No |
| CronCreate | Create a scheduled cron job (5-field expressions) | No |
| CronDelete | Delete a cron job | No |
| CronList | List active cron jobs | Yes |
| Sleep | Pause execution for a duration | No |

### Web

| Tool | Description | Read-only |
|------|-------------|-----------|
| WebSearch | Search the web and return results | Yes |
| WebFetch | Fetch a URL, convert to markdown, process with LLM | Yes |

### Interaction

| Tool | Description | Read-only |
|------|-------------|-----------|
| AskUserQuestion | Prompt the user for input | Yes |
| SendMessage | Send a message to the user | Yes |
| TodoWrite | Write to the todo/checklist | No |

### System

| Tool | Description | Read-only |
|------|-------------|-----------|
| Config | Read or write configuration values | No |
| LSP | Interact with Language Server Protocol servers | Yes |
| RemoteTrigger | Trigger a remote execution endpoint | No |
| ToolSearch | Search for tools by name or description | Yes |
| Brief | Generate a brief summary of content | Yes |
| SyntheticOutput | Generate synthetic tool output for testing | No |

### Discovery & Workflow

| Tool | Description | Read-only |
|------|-------------|-----------|
| DiscoverSkills | Search local and remote skill registries | Yes |
| Snip | Save code snippets or trim conversation history | No |
| VerifyPlanExecution | Verify plan outcomes (file checks, command checks) | Yes |
| WebBrowser | Browser automation via Chrome DevTools Protocol | No |

---

## Reverse Engineering

Built-in reverse engineering capabilities with CUDA acceleration:

| Feature | Description |
|---------|-------------|
| Capstone Disassembly | Multi-architecture disassembly (x86, x64, ARM, AArch64, MIPS) |
| CUDA-Accelerated Binary Analysis | GPU-accelerated pattern matching and entropy computation |
| PE/ELF Parsing | Structured parsing of Windows PE and Linux ELF binaries |
| Entropy Analysis | Section-level entropy calculation for packer/encryption detection |
| Crypto Detection | Identify cryptographic constants and algorithm signatures in binaries |

---

## Key Features

### History Compression (5-Strategy System)

Five compaction strategies work together to manage context: **AutoCompact** (LLM-based summary at 75% usage), **MicroCompact** (truncates large tool results at 60%), **ReactiveCompact** (emergency compaction at 95%), **ContextCollapse** (collapses consecutive read/search results), and **EnhancedSnip** (relevance-scored group removal at 80%). Strategies are evaluated in priority order by the HistoryCompactor orchestrator. Trigger manually with `/compact` or let it run automatically.

### Concurrent Tool Execution

When the LLM issues multiple `tool_use` calls in a single response, the StreamingToolExecutor runs them in parallel with configurable concurrency limits, budget trimming, and interrupt handling. Single tool calls execute directly without thread overhead.

### API Retry with Exponential Backoff

HTTP 429 (rate limit), 500, 502, and 503 responses trigger automatic retry with exponential backoff, up to 3 attempts. This handles transient API failures and rate limiting without user intervention.

### Error Recovery

Automatic recovery from common API failures: MAX_OUTPUT_TOKENS triggers re-compaction and retry, prompt-too-long errors trigger aggressive reactive compaction, and primary model failures can fall back to a configured fallback model. Up to 3 recovery attempts before giving up.

### Token Budget System

Three-layer budget control: per-query budget, per-task budget, and per-tool-result budget. Tool results exceeding the budget are automatically truncated with a size note. Budget state is tracked atomically and reset between queries.

### Hooks System

Configure `PreToolUse` and `PostToolUse` event hooks in `.crab/settings.json`. Hooks run shell commands before or after specific tools execute, enabling custom validation, logging, or side effects. Supports 4 event types: `PreToolUse`, `PostToolUse`, `PostSampling` (after API response), and `StopFailure` (when stop hooks fail). Hook results include execution duration metrics.

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "tool": "Write",
        "command": "echo 'About to write: $TOOL_INPUT_PATH'"
      }
    ],
    "PostToolUse": [
      {
        "tool": "Bash",
        "command": "echo 'Bash completed with exit code $TOOL_EXIT_CODE'"
      }
    ]
  }
}
```

### File-Based Memory System

Persistent memory stored as markdown files with YAML frontmatter in `.crab/memory/`. An index file (`MEMORY.md`) tracks all entries. Four memory types:

| Type | Purpose |
|------|---------|
| `user` | User preferences and patterns |
| `feedback` | Corrections and learned behaviors |
| `project` | Project-specific context and conventions |
| `reference` | Reusable reference material |

Manage with `/memory list`, `/memory show <name>`, `/memory delete <name>`.

### Service Modules (v0.2.0)

| Module | Header | Description |
|--------|--------|-------------|
| CompactStrategy | core/*.h | 5-strategy compaction (auto/micro/reactive/collapse/snip) |
| BudgetTracker | core/BudgetTracker.h | 3-layer token budget (query/task/tool-result) |
| ErrorRecovery | core/ErrorRecovery.h | MAX_OUTPUT_TOKENS + prompt-too-long + fallback recovery |
| TokenEstimator | core/TokenEstimator.h | Fast CJK-aware token estimation without API |
| StreamingToolExecutor | tools/StreamingToolExecutor.h | Parallel tool execution with budget trimming |
| LSPServerManager | lsp/LSPServerManager.h | Multi-LSP server management |
| SkillSearchService | services/SkillSearchService.h | Local + remote skill discovery with prefetch |
| TeamMemorySync | services/TeamMemorySync.h | Team memory sync with secret scanning |
| FeatureFlags | services/FeatureFlags.h | Local feature flag system |
| AnalyticsService | services/AnalyticsService.h | Local file-based event logging |
| NotifierService | services/NotifierService.h | Cross-platform system notifications |
| PreventSleepService | services/PreventSleepService.h | Prevent system sleep during long tasks |

### Coordinator Mode

Multi-agent task decomposition via `/coordinator <task>`. The coordinator analyzes the task, spawns specialized sub-agents (explore for research, plan for design, general-purpose for implementation, verification for testing), and merges their results. Also available as `/coord` or `/ultraplan`.

### New Commands (v0.2.0)

20 new commands added: `/config`, `/model`, `/cost`, `/permissions`, `/status`, `/clear`, `/fork`, `/security-review`, `/sandbox-toggle`, `/keybindings`, `/privacy-settings`, `/rate-limit-options`, `/commit-push-pr`, `/release-notes`, `/stats`, `/bridge`, `/buddy`, `/peers`, `/workflows`, `/oauth-refresh`. Total: 83 commands.

### Vim Mode

Toggle with `/vim`. Provides Normal, Insert, and Command modes:
- `i` / `a` to enter insert mode
- `Esc` to return to normal mode
- `:q` to quit, `:w` to save

### Voice Output

Text-to-speech for assistant responses. Toggle with `/voice`. Uses platform-native TTS:
- Windows: SAPI (Speech API)
- macOS: `say` command
- Linux: `espeak`

### Session Persistence

Sessions auto-save to the SQLite database on exit. Use `/resume` to restore the last session, including full message history. Session data is stored as serialized JSON in the `sessions` table.

### Process Sandbox

Resource limits for spawned processes to prevent runaway commands:
- Windows: Job Objects with memory and CPU time limits
- Linux: `rlimit` constraints (RLIMIT_AS, RLIMIT_CPU, RLIMIT_NPROC)

Configure with `/sandbox [disable|ask|auto|trusted]`.

### Terminal UI

- Spinner animation during LLM inference and tool execution
- Markdown rendering for assistant output (headers, code blocks, lists, bold/italic)
- Table formatting for structured data
- Input history with up/down arrow navigation
- Color themes (`/theme dark|light|minimal`)

### Multi-Line Input

End a line with `\` to continue input on the next line. Useful for pasting multi-line prompts or code snippets.

### Shell Escape

Prefix any input with `!` to run it directly as a shell command:

```
!git status
!ls -la src/
!python test.py
```

### /doctor Diagnostics

Checks the health of your environment:
- External tools: `git`, `rg` (ripgrep), `node`, `python`, `gh`
- Config files: `CLAUDE.md`, `settings.json`, `config.yaml`
- Directories: `.crab/memory/`, `.crab/skills/`
- Core status: model, session, tools, permissions, RAG, cost

### /review -- Code Review

Runs `git diff --staged` (or a custom ref), pipes the diff to the LLM, and gets a code review focusing on bugs, security issues, and improvements. Alias: `/cr`.

### /pr -- Pull Request Creation

Creates pull requests via the `gh` CLI. Without arguments, the LLM analyzes recent commits and generates the PR automatically. With arguments, passes them directly to `gh pr`.

### /share -- Conversation Export

Exports the current conversation as a shareable markdown file with model metadata.

### FileStateCache

Mtime-based file read caching. When the LLM reads a file that hasn't changed since the last read (same `mtime`), the cached content is returned instead of re-reading from disk. Reduces I/O for repetitive file access patterns.

### Tool Definition Caching

`ToolRegistry` caches the JSON API tool definitions (`toApiToolDefinitions()`). The cache is invalidated only when tools are registered or unregistered, avoiding repeated serialization on every API call.

### CostTracker

Per-model token pricing with running totals:

| Model | Input (per 1M tokens) | Output (per 1M tokens) |
|-------|----------------------|------------------------|
| claude-opus | $15.00 | $75.00 |
| claude-sonnet | $3.00 | $15.00 |
| claude-haiku | $0.80 | $4.00 |

View with `/cost`. Tracks cumulative input tokens, output tokens, and total USD spent.

### Token Count Display

When the conversation grows long, the prompt displays `~Nk tok` to show the approximate token count, helping you gauge how close you are to the context limit.

### Plugin System

Place a `manifest.json` in `.crab/plugins/<plugin-name>/` to register a plugin. Plugins can provide additional tools, commands, or system prompt extensions. Manage with `/plugin`.

### Skill System

Place `.md` files with YAML frontmatter in `.crab/skills/`. Skills are triggered by name or by matching trigger patterns. Frontmatter fields: `name`, `description`, `trigger`. List with `/skills`.

### Cron Scheduler

Real 5-field cron expression parsing (`minute hour day month weekday`). Create scheduled tasks with the `CronCreate` tool, list with `CronList`, delete with `CronDelete`.

### WebFetch Cache

URL fetch results are cached with a 15-minute TTL. Repeated fetches of the same URL within the window return the cached response, reducing latency and API usage.

### LocalLLMClient -- 3-Strategy Tool Parsing

The local LLM client supports three strategies for parsing tool calls from model output:

1. **JSON** -- Standard JSON tool_use blocks (for models that support structured output)
2. **SKILL:** -- `SKILL: tool_name` format with parameters on subsequent lines
3. **Function call** -- `function_name(param1, param2)` format

The client tries each strategy in order and uses the first successful parse.

---

## Team Mode

Team Mode enables multiple developers to connect to a single CloseCrab server and work in parallel, each with independent conversation history and full tool access.

### How it works

- **Multi-client connections** -- Multiple phones, laptops, or PCs connect to one CloseCrab server via CloseCrab-Web. Each client is assigned a unique ID.
- **Independent conversation history** -- Each connected client maintains its own conversation context. One developer's session does not interfere with another's.
- **True parallel inference** -- In API mode, requests are dispatched as concurrent HTTP calls. In local mode, llama.cpp's `n_parallel` slots handle simultaneous inference for multiple clients.
- **Gamification** -- Built-in leaderboard tracks coding stats (lines written, tools used, bugs fixed). Achievements unlock as developers hit milestones.
- **Shared knowledge base** -- Team Q&A is automatically indexed and searchable. When one developer solves a problem, the solution becomes discoverable by the whole team.
- **Session persistence** -- Collaboration sessions can be saved and restored. Pick up where you left off, even after server restarts.

### Quick start

```bash
# Start CloseCrab server (Team Mode auto-enabled on port 9002)
closecrab --config config/config.yaml

# Connect from multiple phones/PCs via CloseCrab-Web
# Each client gets unique ID, independent history, and appears on leaderboard
```

### Configuration

Add to `config/config.yaml`:

```yaml
team:
  enabled: true
  port: 9002                    # Team Mode WebSocket port
  max_clients: 8                # Maximum simultaneous connections
  leaderboard: true             # Enable gamification leaderboard
  shared_knowledge: true        # Enable shared Q&A knowledge base
  session_persistence: true     # Save/restore collaboration sessions
```

---

## Architecture

~160 source files (.h + .cpp) organized into focused modules:

```
src/
├── main.cpp              Entry point, component initialization, main loop
├── core/                 QueryEngine, Message, AppState, CostTracker, SessionManager
├── api/                  API clients: LocalLLMClient, AnthropicClient, OpenAIClient
├── tools/                51 tool implementations (see Tools section)
│   ├── Tool.h            Base class with validation, permissions, schema
│   ├── ToolRegistry.*    Registry with alias lookup and definition caching
│   ├── FileReadTool/     Read, Write, Edit, Glob, Grep, NotebookEdit
│   ├── BashTool/         Bash, PowerShell
│   ├── REPLTool/         Persistent REPL sessions
│   ├── AgentTool/        Sub-agent spawning (5 types)
│   ├── SkillTool/        Skill invocation
│   ├── MCPTools/         MCP protocol tools
│   ├── TaskTools/        Background task management
│   ├── TeamTools/        Agent team management
│   ├── WorkflowTools/    Plan mode, worktrees, cron, sleep
│   ├── WebTools/         WebSearch, WebFetch (with 15-min cache)
│   ├── CronTools/        Cron expression parsing and scheduling
│   └── SystemTools/      Config, LSP, RemoteTrigger, ToolSearch, Brief
├── commands/             83 slash commands (5 categories)
│   ├── CommandRegistry.* Registry with alias support and categorized help
│   ├── SessionCommands.h Session, history, export, compact, context, env
│   ├── GitCommands.h     Commit, diff, branch, log, push, pull, stash
│   ├── ExtendedCommands.h Review, hooks, memory, tasks, agents, MCP, PR, share, vim, voice, coordinator
│   └── AdvancedCommands.h RAG, SSD, sandbox, plan, doctor, files, provider, API config
├── agents/               AgentManager -- 5 agent types (general-purpose, explore, plan, verification, code-guide)
├── coordinator/          Multi-agent task decomposition
├── compact/              SnipCompact history compression
├── hooks/                HookManager -- PreToolUse/PostToolUse event hooks
├── memory/               FileMemoryManager (MEMORY.md index + .md files with frontmatter)
├── mcp/                  MCP protocol client and server manager
├── plugins/              Plugin loader (manifest.json) and skill directory (.md frontmatter)
├── permissions/          Permission rule engine (default/auto/bypass modes)
├── rag/                  FAISS + ONNX Runtime vector search
├── ssd/                  SSD Expert Streaming for Mixture-of-Experts models
├── llm/                  llama.cpp inference engine wrapper
├── config/               YAML config loader, settings.json parser
├── network/              HTTP server (httplib), WebSocket (ixwebsocket), SSE
├── security/             Process sandbox (Windows Job Objects / Linux rlimit)
├── git/                  Git CLI wrapper
├── lsp/                  LSP client
│   └── LSPServerManager  Multi-server management with extension routing
├── bridge/               Remote execution bridge
│   └── BridgeTransport   Abstract transport (HTTP/SSE)
├── services/             14 service modules (analytics, feature flags, memory sync, etc.)
├── voice/                VoiceEngine (Windows SAPI, macOS say, Linux espeak)
├── ui/                   Terminal UI: spinner, markdown renderer, table formatter, input history
└── utils/                Shared utilities
```

---

## Configuration

### config/config.yaml

The primary configuration file. Full structure:

```yaml
server:
  port: 9001
  host: "127.0.0.1"

database:
  path: "data/closecrab.db"

logging:
  level: "info"              # trace, debug, info, warn, error

provider: "local"            # local, anthropic, openai

api:
  base_url: ""               # API endpoint URL
  api_key: ""                # API key (or use ANTHROPIC_AUTH_TOKEN env var)
  model: ""                  # Model name (e.g., claude-sonnet-4-20250514)

llm:
  model_path: "models/your-model.gguf"
  max_tokens: 4096
  temperature: 0.7

rag:
  embedding_model_path: "models/bge-small-zh/onnx/model_quantized.onnx"
  embedding_tokenizer_path: "models/bge-small-zh/tokenizer.json"
  reranker_model_path: "models/bge-reranker-base/onnx/model_uint8.onnx"
  reranker_tokenizer_path: "models/bge-reranker-base/tokenizer.json"
```

### .crab/settings.json

Project-level settings for permissions, hooks, MCP servers, and allowed/denied patterns:

```json
{
  "permissions": {
    "mode": "default",
    "allow": ["Read", "Glob", "Grep"],
    "deny": []
  },
  "hooks": {
    "PreToolUse": [],
    "PostToolUse": []
  },
  "mcpServers": {
    "my-server": {
      "command": "npx",
      "args": ["-y", "@my/mcp-server"]
    }
  }
}
```

### CLAUDE.md

Place a `CLAUDE.md` file in your project root to provide persistent instructions to the AI. This file is loaded into the system prompt on startup and persists across sessions. Use it for project conventions, coding standards, or context the AI should always have.

### Memory files

Stored in `.crab/memory/` as markdown files with YAML frontmatter:

```markdown
---
type: project
name: coding-conventions
description: Project coding standards
---

- Use camelCase for variables
- All public methods need docstrings
- Tests go in tests/ directory
```

The `MEMORY.md` index file in `.crab/memory/` tracks all memory entries.

---

## Build from Source

### Requirements

| Requirement | Version |
|-------------|---------|
| CMake | 3.20+ |
| C++ compiler | C++17 support (Visual Studio 2022 recommended) |
| vcpkg | Latest |
| CUDA Toolkit | 12.x (optional, for GPU acceleration) |

### Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Windows x64 | Primary | Visual Studio 2022, MSVC |
| Linux x64 | Supported | GCC 10+ or Clang 12+, via CMake presets |
| macOS (arm64/x64) | Supported | Apple Clang, via CMake presets |

### Dependencies

Managed via vcpkg and git submodules:

| Library | Purpose |
|---------|---------|
| spdlog | Logging |
| nlohmann/json | JSON parsing |
| yaml-cpp | YAML config parsing |
| CLI11 | Command-line argument parsing |
| sqlite3 | Session and data persistence |
| httplib (cpp-httplib) | HTTP server |
| FAISS | Vector similarity search (RAG) |
| llama.cpp | Local LLM inference |
| ONNX Runtime | Embedding and reranker models (RAG) |
| libcurl | HTTP client for API calls |
| ixwebsocket | WebSocket support |
| CUDA | GPU acceleration (optional) |

### Build steps

```bash
# 1. Install vcpkg dependencies
vcpkg install curl:x64-windows faiss:x64-windows zlib:x64-windows

# 2. Configure with CMake
cd G:\CMakePJ\CloseCrab-Unified
cmake -B build -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows

# 3. Build
cmake --build build --config Release
```

For Linux/macOS, use the appropriate CMake preset:

```bash
cmake --preset linux-release    # or macos-release
cmake --build --preset linux-release
```

### Run tests

```bash
cmake --build build --config Release --target tests
cd build && ctest -C Release --output-on-failure
```

### Create installer (Windows)

1. Install [Inno Setup](https://jrsoftware.org/isinfo.php)
2. Open `installer.iss` in Inno Setup
3. Compile to produce `CloseCrab-Unified_Setup.exe`

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Failed to load config" | Run from the project root directory, or pass `-c` with an absolute path to `config.yaml` |
| "No model path" | Set `llm.model_path` in `config.yaml` to an absolute path to your `.gguf` file |
| HTTP 429 errors | Rate limited -- the built-in retry with exponential backoff handles this automatically (up to 3 retries) |
| HTTP 503 errors | API request too large, wrong model name, or server overloaded. Check `/doctor` output |
| SSL/CURL errors | The program auto-skips SSL verification for proxy endpoints. Check `api.base_url` is correct |
| Tools not executing | Local models need `SKILL:` format or function-call format. Remote APIs use native `tool_use`. Check `/tools` |
| High memory usage | Use `/compact` to compress history, or start a `/new` session |
| "No saved session data" | Session persistence requires the SQLite database at `data/closecrab.db`. Check the path exists |
| Voice not working | Requires platform TTS: Windows SAPI, macOS `say`, Linux `espeak`. Install the appropriate package |
| MCP server disconnected | Check `.crab/settings.json` for correct server config. Use `/mcp` to see status |
| `/doctor` shows tool "not found" | Install the missing tool (`git`, `rg`, `node`, `python`, `gh`) and ensure it's on your PATH |
| CUDA out of memory | Reduce `llm.max_tokens` or use a smaller quantized model (Q4_K_M instead of Q8) |

---

## License

MIT. See [LICENSE](LICENSE) for the full text.






