# CloseCrab-Unified — 本地 AI 编程助手

一个用 C++17 编写的本地优先 AI 编程助手。可以在你自己的 GPU 上运行大语言模型，也可以连接 Claude、OpenAI 等远程 API — 只需改一行配置。AI 拥有 42 个工具、50+ 个命令、多智能体协作、记忆系统、语音输出，全部受权限系统保护。单文件可执行程序，约 2.9MB。

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CUDA](https://img.shields.io/badge/CUDA-12.x-green.svg)](https://developer.nvidia.com/cuda-toolkit)
[![Windows](https://img.shields.io/badge/Platform-Windows%20|%20Linux%20|%20macOS-0078d7.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

---

## 这是什么？

大多数 AI 编程工具需要联网。CloseCrab-Unified 给你选择权：在本地 GPU 上运行模型（零网络依赖），或者连接 Claude/OpenAI/任何兼容 API。无论哪种方式，AI 都能使用同一套 42 个工具。

它融合了两个项目：**CloseCrab**（C++ 本地推理引擎，带 RAG 和 MoE 流式加载）和 **JackProAi-claudecode**（TypeScript CLI，40+ 工具和 100+ 命令）。最终产出一个 C++ 单文件可执行程序，109 个源文件编译为 ~2.9MB。

### 为什么用它？

- **隐私**：本地模式下所有数据留在你的机器上，零网络依赖
- **灵活**：改一行配置就能在本地和远程模型之间切换
- **真工具**：AI 不只是聊天 — 它能读文件、写代码、跑测试、搜索网络
- **智能**：多 Agent 协调、历史压缩、记忆系统、Hooks 自动化
- **可扩展**：42 个内置工具 + 插件系统 + MCP 协议 + 技能目录
- **快**：C++17，CUDA GPU 加速，无 Python 运行时

---

## 快速开始

### 方式 1：安装器

下载 `CloseCrab-Unified_Setup.exe`，安装时选择本地或 API 模式，自动配置。

### 方式 2：手动运行

```bash
cd G:\CMakePJ\CloseCrab-Unified
# 编辑 config/config.yaml
.\run.bat
```

---

## 切换本地模式和 API 模式

### 方法 1：编辑 config/config.yaml（推荐）

```yaml
# ========== 本地模式 ==========
provider: "local"
llm:
  model_path: "models/qwen2.5-7b-instruct-q4_k_m.gguf"

# ========== API 模式 (Anthropic) ==========
# provider: "anthropic"
# api:
#   base_url: "https://api.anthropic.com"
#   api_key: "sk-ant-你的密钥"
#   model: "claude-sonnet-4-20250514"

# ========== API 模式 (OpenAI 兼容) ==========
# provider: "openai"
# api:
#   base_url: "http://127.0.0.1:1234"
#   api_key: "你的密钥"
#   model: "qwen2.5-7b"
```

### 方法 2：环境变量

```bash
set ANTHROPIC_AUTH_TOKEN=sk-你的密钥
set ANTHROPIC_BASE_URL=https://你的中转站.com
closecrab-unified.exe
```

### 方法 3：命令行参数

```bash
closecrab-unified.exe --provider anthropic --api-key sk-xxx --api-url https://api.anthropic.com --api-model claude-sonnet-4-20250514
```

### 优先级：命令行参数 > 环境变量 > config.yaml

---

## 提供商参考

| 提供商 | `provider` | `api.base_url` | `api.model` |
|--------|-----------|----------------|-------------|
| 本地 GGUF | `local` | — | 设置 `llm.model_path` |
| Anthropic Claude | `anthropic` | `https://api.anthropic.com` | `claude-sonnet-4-20250514` |
| Claude 中转站 | `anthropic` | `https://你的中转站.com` | 中转站的模型名 |
| OpenAI | `openai` | `https://api.openai.com` | `gpt-4o` |
| LM Studio | `openai` | `http://127.0.0.1:1234` | 你加载的模型 |
| SiliconFlow | `openai` | `https://api.siliconflow.cn` | `moonshotai/Kimi-K2-Instruct` |
| Ollama | `openai` | `http://127.0.0.1:11434` | `qwen2.5` |

---

## 命令参考 (50+)

### 会话

| 命令 | 说明 |
|------|------|
| `/help` | 显示所有命令（按分类） |
| `/quit` `/exit` | 退出程序 |
| `/clear` | 清空对话历史 |
| `/new` | 开始新会话 |
| `/resume` | 恢复上次保存的会话 |
| `/history [N]` | 显示最近 N 条对话 |
| `/export [文件]` | 导出对话到文件 |
| `/share` | 导出对话为可分享的 Markdown |
| `/compact` | 压缩对话历史（节省上下文） |
| `/context` | 显示上下文信息 |
| `/status` | 会话状态（表格显示） |
| `/env` | 环境变量信息 |
| `/version` | 版本信息 |

### 模型

| 命令 | 说明 |
|------|------|
| `/model [名称]` | 查看/切换模型 |
| `/provider` | 查看当前提供商 |
| `/api` | API 配置帮助 |
| `/cost` | 费用统计（按模型分列，含 token 估算） |
| `/fast` | 切换快速模式 |
| `/brief` | 切换简洁输出模式 |
| `/thinking [on/off/N]` | 切换扩展思考模式 |
| `/permissions [模式]` | 权限模式：default/auto/bypass |

### Git

| 命令 | 说明 |
|------|------|
| `/commit [消息]` | Git 提交 |
| `/diff` | Git 差异 |
| `/branch [名称]` | Git 分支 |
| `/log [N]` | Git 日志 |
| `/push` | Git 推送 |
| `/pull` | Git 拉取 |
| `/stash` | Git 暂存 |
| `/review` | 代码审查（git diff + LLM 分析） |
| `/pr [参数]` | 创建 Pull Request（通过 gh CLI） |

### 工具管理

| 命令 | 说明 |
|------|------|
| `/tools` | 列出所有工具 |
| `/skills` | 列出可用技能 |
| `/plugin` | 列出已加载插件 |
| `/hooks` | 列出配置的 Hooks |
| `/memory [list/show/delete]` | 管理文件记忆 |
| `/tasks` | 查看任务列表 |
| `/agents` | 查看运行中的 Agent |
| `/mcp` | MCP 服务器状态 |
| `/audit` | 权限审计日志 |

### 高级

| 命令 | 说明 |
|------|------|
| `/rag [命令]` | RAG 管理：enable/disable/load/clear |
| `/ssd` | SSD Expert Streaming 状态 |
| `/sandbox [模式]` | 沙箱：disable/ask/auto/trusted |
| `/plan` | 切换计划模式（只读探索） |
| `/doctor` | 环境诊断（检查工具/配置/连通性） |
| `/coordinator [任务]` | 多 Agent 协调执行复杂任务 |
| `/vim` | 切换 Vim 键绑定模式 |
| `/voice [on/off]` | 切换语音输出（TTS） |
| `/theme [dark/light]` | 切换颜色主题 |
| `/add-dir [路径]` | 添加目录到上下文 |
| `/files [路径]` | 列出目录文件 |
| `/reload` | 重新加载配置 |

---

## 工具 (42 个)

| 分类 | 工具 |
|------|------|
| 文件 | Read, Write, Edit, Glob, Grep, NotebookEdit |
| 执行 | Bash（超时+后台）, PowerShell, REPL（Python/Node） |
| AI | Agent（5 种类型）, Skill |
| MCP | MCPTool, ListMcpResources, ReadMcpResource, McpAuth |
| 任务 | TaskCreate, TaskUpdate, TaskGet, TaskList, TaskStop, TaskOutput |
| 团队 | TeamCreate, TeamDelete |
| 工作流 | EnterPlanMode, ExitPlanMode, EnterWorktree, ExitWorktree, CronCreate, CronDelete, CronList, Sleep |
| 网络 | WebSearch（DuckDuckGo）, WebFetch（带 15 分钟缓存） |
| 交互 | AskUserQuestion, SendMessage, TodoWrite |
| 系统 | Config, LSP, RemoteTrigger, ToolSearch, Brief, SyntheticOutput |

---

## 核心特性

### 历史压缩 (SnipCompact)

长对话接近上下文窗口限制（默认 75%）时自动触发压缩。旧消息被摘要为一条系统消息，保留最近 10 条完整消息。远程 API 模式下使用 LLM 生成高质量摘要，本地模式使用统计摘要。也可用 `/compact` 手动触发。

### 并发 Tool 执行

当 LLM 一次返回多个 tool_use 请求时，通过 `std::async` 并行执行，mutex 保护消息写入。单个 tool 调用仍然串行执行。

### API 重试与错误分类

所有 API 调用自动分类错误类型（AUTH/RATE_LIMIT/OVERLOADED/SERVER/NETWORK/INVALID_REQUEST），可重试错误（429/500/502/503/529）自动指数退避重试（1s/2s/4s），最多 3 次。不可重试错误（401/400）立即报错。

### Hooks 系统

在 `.claude/settings.json` 中配置事件钩子：

```json
{
  "hooks": [
    {"event": "PreToolUse", "matcher": "Bash", "command": "echo 'About to run Bash'"},
    {"event": "PostToolUse", "matcher": ".*", "command": "echo 'Tool finished'"}
  ]
}
```

Hook 返回非零退出码会阻止 tool 执行。支持 `HOOK_TOOL` 和 `HOOK_EVENT` 环境变量。

### 文件记忆系统

在 `.claude/memory/` 目录中存储持久化记忆，每个记忆是一个带 YAML frontmatter 的 .md 文件：

```markdown
---
name: 用户偏好
description: 用户是高级 C++ 开发者，偏好简洁回复
type: user
---
用户是高级 C++ 开发者，10 年经验...
```

四种类型：`user`（用户画像）、`feedback`（行为纠正）、`project`（项目上下文）、`reference`（外部资源指针）。`MEMORY.md` 作为索引文件，内容自动注入 system prompt。用 `/memory` 命令管理。

### 多 Agent 协调

`/coordinator` 命令将复杂任务分解为子任务，分配给不同类型的 Agent 并行执行：

- **general-purpose**：所有工具可用
- **explore**：只读（Glob, Grep, Read, WebSearch, WebFetch）
- **plan**：只读 + 分析
- **verification**：测试 + 检查
- **code-guide**：搜索 + 文档

子 Agent 通过 `allowedTools` 过滤器限制可用工具，在独立线程中运行。

### Vim 模式

`/vim` 启用 Vim 键绑定。支持 Normal/Insert/Command 三种模式：
- `i`/`a` 进入插入模式
- `Esc` 回到 Normal 模式
- `:q` 退出程序
- `:set novim` 关闭 Vim 模式

提示符显示当前模式：`[N]`/`[I]`/`[:]`

### 语音输出

`/voice` 启用文本转语音。使用系统 TTS：
- Windows：SAPI（PowerShell SpeechSynthesizer）
- macOS：`say` 命令
- Linux：`espeak`

AI 回复完成后自动朗读，长文本自动截断到 500 字符。

### 会话持久化

退出时自动将对话历史序列化为 JSON 保存到 SQLite。下次启动后用 `/resume` 恢复上次会话。

### 进程沙箱

`ProcessSandbox` 为 BashTool 等执行工具提供 OS 级资源限制：
- Windows：Job Object 限制内存/CPU
- Linux：`setrlimit` 限制 AS/CPU/FSIZE
- 危险命令模式检测（`rm -rf /`、`mkfs` 等自动拦截）

### 终端 UI

- Spinner 动画：tool 执行时显示旋转指示器
- Markdown 渲染：代码块、标题、粗体、行内代码的 ANSI 着色
- 表格格式化：`/status`、`/cost` 等命令对齐输出
- 输入历史：记录历史输入
- 多行输入：行尾 `\` 续行
- Shell 转义：`!` 前缀直接执行 shell 命令
- Token 计数：长对话时提示符显示 `[~Nk tok]`

### 费用追踪

内置价格表（claude-opus $15/$75, sonnet $3/$15, haiku $0.8/$4），每次 API 调用自动计算费用。`/cost` 命令按模型分列显示 input/output token 数和费用。

### 本地模型 Tool 解析

LocalLLMClient 使用 3 层策略解析本地模型的 tool 调用：
1. JSON 格式：`{"name": "Read", "input": {"file_path": "..."}}`
2. SKILL 格式：`SKILL: Read\nPARAMS: {"file_path": "..."}`
3. 函数调用格式：`Read(file_path="...")`

---

## 架构

详见 [docs/architecture.md](docs/architecture.md)。

```
src/
├── main.cpp              # 入口，组件初始化，主循环
├── core/                 # QueryEngine, Message, AppState, HistoryCompactor, FileStateCache, CostTracker
├── api/                  # LocalLLM, Anthropic, OpenAI API 客户端, APIError, StreamParser
├── tools/                # 42 个工具实现（每个工具一个目录）
├── commands/             # 50+ 斜杠命令（Git/Session/Advanced/Extended）
├── agents/               # 多 Agent 系统（AgentManager, 5 种类型）
├── coordinator/          # 多 Agent 协调器（任务分解 + 并行执行）
├── mcp/                  # MCP 协议客户端（JSON-RPC 2.0 stdio）
├── plugins/              # 插件管理器 + 技能目录
├── permissions/          # 权限规则引擎（default/auto/bypass）
├── hooks/                # 事件钩子系统（PreToolUse/PostToolUse）
├── memory/               # 会话记忆（SQLite）+ 文件记忆（MEMORY.md）
├── rag/                  # FAISS + ONNX 向量搜索
├── ssd/                  # SSD Expert Streaming（MoE 三级缓存）
├── llm/                  # llama.cpp 推理引擎
├── network/              # HTTP, WebSocket, SSE 服务器
├── security/             # 沙箱 + 进程资源限制
├── ui/                   # 终端 UI（Spinner, Markdown, Table, VimMode）
├── git/                  # Git CLI 封装
├── lsp/                  # LSP 客户端
├── bridge/               # 远程执行（HTTP + 重连）
├── voice/                # 语音引擎（系统 TTS）
└── utils/                # 日志, 字符串, UUID, ProcessRunner
```

---

## 配置

### config.yaml 完整结构

```yaml
server:
  port: 9001
  host: "127.0.0.1"

database:
  path: "data/closecrab.db"

logging:
  level: "info"          # debug, info, warn, error

provider: "local"        # local, anthropic, openai

api:
  base_url: ""
  api_key: ""
  model: ""

llm:
  model_path: "models/qwen2.5-7b-instruct-q4_k_m.gguf"
  max_tokens: 4096
  temperature: 0.7

rag:
  embedding_model_path: "models/bge-small-zh/onnx/model_quantized.onnx"
  embedding_tokenizer_path: "models/bge-small-zh/tokenizer.json"
  reranker_model_path: "models/bge-reranker-base/onnx/model_uint8.onnx"
  reranker_tokenizer_path: "models/bge-reranker-base/tokenizer.json"
```

### .claude/settings.json

```json
{
  "permissions": {
    "allow": ["Read *", "Glob *", "Grep *"],
    "deny": ["Bash rm -rf *"]
  },
  "mcpServers": {
    "my-server": {
      "command": "node",
      "args": ["server.js"],
      "env": {}
    }
  },
  "hooks": [
    {"event": "PreToolUse", "matcher": "Bash", "command": "echo pre"}
  ]
}
```

### CLAUDE.md

项目根目录的 `CLAUDE.md` 文件内容会自动注入 system prompt，用于给 AI 提供项目特定的指令。

---

## 从源码构建

### 环境要求

- Windows 10/11, Visual Studio 2022, CMake 3.20+, Ninja
- vcpkg（用于 curl, faiss, zlib）
- CUDA 12.x（可选，用于 GPU 加速）

### 构建步骤

```bash
# 安装 vcpkg 依赖
vcpkg install curl:x64-windows faiss:x64-windows zlib:x64-windows

# 配置
cmake --preset x64-release

# 构建
cmake --build out/build/x64-release --target closecrab-unified

# 运行
out\build\x64-release\closecrab-unified.exe
```

### 运行测试

```bash
cmake --preset x64-release -DBUILD_TESTS=ON
cmake --build out/build/x64-release --target closecrab-tests
out\build\x64-release\closecrab-tests.exe
```

### 创建安装器

安装 [Inno Setup](https://jrsoftware.org/isinfo.php)，打开 `installer.iss`，编译。

---

## 依赖

| 库 | 版本 | 用途 |
|----|------|------|
| spdlog | 1.14.1 | 结构化日志 |
| nlohmann/json | 3.11.3 | JSON 解析 |
| yaml-cpp | 0.8.0 | YAML 配置 |
| CLI11 | 2.4.2 | 命令行解析 |
| sqlite3 | 3.46.0 | 会话/记忆存储 |
| cpp-httplib | 0.18.1 | HTTP 服务器 |
| ixwebsocket | — | WebSocket |
| FAISS | vcpkg | GPU 向量搜索 |
| llama.cpp | 预编译 | 本地 LLM 推理 |
| ONNX Runtime | 1.24.4 | Embedding/Reranker |
| libcurl | vcpkg | HTTP 客户端 |
| CUDA Toolkit | 12.x | GPU 加速 |

---

## 故障排查

| 问题 | 解决 |
|------|------|
| "Failed to load config" | 从项目根目录运行，或用 `-c` 指定绝对路径 |
| "No model path" | 在 config.yaml 中设置 `llm.model_path`，用绝对路径 |
| HTTP 429 错误 | 限流 — 内置自动重试（指数退避，最多 3 次） |
| HTTP 503 错误 | 请求太大或模型名错误，用 `/doctor` 检查 |
| SSL/CURL 错误 | 程序自动跳过 SSL 验证 |
| 工具不执行 | 本地模型用 SKILL: 格式；远程 API 用原生 tool_use。用 `/tools` 检查 |
| 内存占用高 | 用 `/compact` 压缩历史，或 `/new` 开始新会话 |
| 语音不工作 | 需要系统 TTS：Windows SAPI, macOS say, Linux espeak |
| MCP 服务器断开 | 检查 `.claude/settings.json` 配置，用 `/mcp` 查看状态 |
| `/doctor` 显示工具 "not found" | 安装缺失工具（git, rg, node, python, gh）并确保在 PATH 中 |
| CUDA 内存不足 | 减小 `llm.max_tokens` 或使用更小的量化模型 |

---

## 许可证

MIT



