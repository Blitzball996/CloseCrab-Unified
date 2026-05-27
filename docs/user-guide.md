# CloseCrab-Unified 用户与技术文档

> **版本:** 0.1.0  
> **更新日期:** 2026-04-06

---

## 1. 项目简介

CloseCrab-Unified 是一个本地 AI 助手，融合了 CloseCrab (C++ 本地推理引擎) 和 JackProAi-claudecode (TypeScript CLI) 的全部功能。

**核心能力：**
- 本地 LLM 推理 (llama.cpp, CUDA GPU 加速)
- 远程 API 兼容 (Anthropic Claude / OpenAI / LM Studio / SiliconFlow)
- 42 个工具 (文件操作、Shell 执行、网络搜索、智能体、MCP 协议等)
- 33 个斜杠命令 (Git、会话管理、RAG、配置等)
- 多智能体协作 (5 种类型)
- RAG 检索增强生成 (FAISS + ONNX)
- MoE 模型 SSD 专家流式加载
- MCP (Model Context Protocol) 集成
- 插件与技能目录系统
- 安全权限引擎

---

## 2. 快速开始

### 2.1 编译

```bash
cd G:\CMakePJ\CloseCrab-Unified

# 配置
cmake -B build ^
  -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build --config Release
```

### 2.2 运行

```bash
# 从项目根目录运行 (重要！config.yaml 用相对路径)
cd G:\CMakePJ\CloseCrab-Unified
.\build\Release\closecrab-unified.exe

# 或指定配置文件
closecrab-unified.exe -c G:\CMakePJ\CloseCrab-Unified\config\config.yaml
```

### 2.3 启动参数

| 参数 | 缩写 | 默认值 | 说明 |
|------|------|--------|------|
| `--config` | `-c` | `config/config.yaml` | 配置文件路径 |
| `--model` | `-m` | config 中指定 | 本地模型路径覆盖 |
| `--provider` | | `local` | 提供商: local / anthropic / openai / lmstudio / siliconflow |
| `--api-key` | | 环境变量 | API 密钥 |
| `--api-url` | | 自动 | API 地址 |
| `--api-model` | | 自动 | API 模型名 |
| `--verbose` | `-v` | false | 详细日志 |

### 2.4 环境变量

| 变量 | 说明 |
|------|------|
| `ANTHROPIC_AUTH_TOKEN` | Anthropic API 密钥 |
| `ANTHROPIC_BASE_URL` | API 地址 |
| `ANTHROPIC_MODEL` | 模型名 |
| `CLAUDE_LOCAL_PROVIDER` | 本地提供商 (lmstudio/siliconflow/openai) |

### 2.5 使用示例

```
> 帮我在 C 盘创建一个 test.txt，内容写 Hello World
[Tool: Write]
[Write OK]
文件已创建。

> /help
Available commands:
  /help - Show available commands
  /quit - Exit the program
  ...

> /model anthropic
Model set to: anthropic

> /doctor
=== CloseCrab-Unified Diagnostics ===
Model: local:deepseek-moe-16b-chat.Q4_K_M.gguf
Tools: 42
Permission mode: default
...
```

---

## 3. 配置文件

### 3.1 config/config.yaml

```yaml
server:
  port: 9001
  host: "127.0.0.1"

database:
  path: "data/closecrab.db"

logging:
  level: "info"

llm:
  model_path: "models/your-model.gguf"  # 本地模型路径
  max_tokens: 8192
  temperature: 0.7

rag:
  embedding_model_path: "models/bge-small-zh/onnx/model.onnx"
  embedding_tokenizer_path: "models/bge-small-zh/tokenizer.json"
  reranker_model_path: "models/bge-reranker-base/onnx/model.onnx"
  reranker_tokenizer_path: "models/bge-reranker-base/tokenizer.json"
```

### 3.2 .claude/settings.json

```json
{
  "provider": "local",
  "permissions": {
    "mode": "default",
    "allow": {
      "Read": ["*"],
      "Glob": ["*"],
      "Grep": ["*"]
    },
    "deny": {
      "Bash": ["rm -rf *"]
    }
  },
  "mcpServers": {},
  "thinking": {
    "enabled": false,
    "budgetTokens": 10000
  }
}
```

---

## 4. 命令参考

### 4.1 基础命令

| 命令 | 说明 |
|------|------|
| `/help` | 显示所有可用命令 |
| `/quit` `/exit` `/q` | 退出程序 |
| `/clear` | 清除对话历史 |
| `/model [name]` | 查看或切换模型 |
| `/status` | 显示会话状态 |
| `/version` | 显示版本信息 |
| `/doctor` | 运行诊断 |

### 4.2 会话命令

| 命令 | 说明 |
|------|------|
| `/session` | 显示当前会话信息 |
| `/new` | 开始新会话 |
| `/history [N]` | 显示最近 N 条对话 (默认 20) |
| `/export [file]` | 导出对话到文件 |
| `/compact` | 压缩对话历史节省上下文 |
| `/context` | 显示上下文信息 (消息数、token 估算) |

### 4.3 Git 命令

| 命令 | 说明 |
|------|------|
| `/commit [msg]` | Git 提交 (无消息则交互输入) |
| `/diff [args]` | 显示 staged + unstaged 差异 |
| `/branch [name]` | 列出分支 / 创建并切换分支 |
| `/log [N]` | 显示最近 N 条 git log |
| `/push [args]` | 推送到远程 |
| `/pull [args]` | 从远程拉取 |
| `/stash [args]` | Git stash 操作 |

### 4.4 高级命令

| 命令 | 说明 |
|------|------|
| `/permissions [mode]` | 查看/设置权限模式 (default/auto/bypass) |
| `/tools` `/skills` | 列出所有可用工具 |
| `/cost` | 显示 API 费用统计 |
| `/audit` | 显示权限审计日志 |
| `/fast` | 切换快速模式 |
| `/thinking [on/off/N]` | 切换思考模式 / 设置 token 预算 |
| `/plan` | 切换计划模式 (只读探索) |
| `/env` | 显示环境变量 |

### 4.5 RAG 命令

| 命令 | 说明 |
|------|------|
| `/rag` | 显示 RAG 状态 |
| `/rag enable` | 启用 RAG |
| `/rag disable` | 禁用 RAG |
| `/rag load <path>` | 从目录加载文档 |
| `/rag clear` | 清除所有文档 |
| `/rag list` | 列出已加载文档 |

### 4.6 其他命令

| 命令 | 说明 |
|------|------|
| `/sandbox [mode]` | 沙箱模式 (disable/ask/auto/trusted) |
| `/ssd` | SSD Expert Streaming 状态 |
| `/add-dir <path>` | 添加工作目录 |
| `/files [path]` | 列出目录文件 |

---

## 5. 工具参考 (42 个)

### 5.1 文件工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `Read` | 读取文件内容 | file_path, offset?, limit? |
| `Write` | 创建/覆盖文件 | file_path, content |
| `Edit` | 精确替换编辑文件 | file_path, old_string, new_string, replace_all? |
| `Glob` | 按模式搜索文件 | pattern, path? |
| `Grep` | 正则搜索文件内容 | pattern, path?, glob?, output_mode?, -i?, -n?, -A?, -B?, -C? |
| `NotebookEdit` | 编辑 Jupyter notebook | notebook_path, cell_number, new_source, cell_type?, edit_mode? |

### 5.2 执行工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `Bash` | 执行 Shell 命令 | command, description?, timeout? |
| `PowerShell` | 执行 PowerShell 命令 (Windows) | command, description?, timeout? |
| `REPL` | 执行 Python/Node.js 代码 | code, language? |

### 5.3 AI/智能体工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `Agent` | 启动子智能体 | description, prompt, subagent_type?, run_in_background? |
| `Skill` | 执行用户定义技能 | skill, args? |

### 5.4 MCP 工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `MCPTool` | 调用 MCP 服务器工具 | server_name, tool_name, arguments? |
| `ListMcpResources` | 列出 MCP 资源 | server_name |
| `ReadMcpResource` | 读取 MCP 资源 | server_name, uri |
| `McpAuth` | MCP 认证 | server_name, auth_type, token? |

### 5.5 任务工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `TaskCreate` | 创建任务 | subject, description |
| `TaskUpdate` | 更新任务 | taskId, status?, subject?, description? |
| `TaskGet` | 获取任务详情 | taskId |
| `TaskList` | 列出所有任务 | (无) |
| `TaskStop` | 停止运行中的任务 | task_id |
| `TaskOutput` | 获取任务输出 | task_id, block?, timeout? |

### 5.6 团队工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `TeamCreate` | 创建团队 | name, description?, members? |
| `TeamDelete` | 删除团队 | team_id |

### 5.7 工作流工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `EnterPlanMode` | 进入计划模式 (只读) | (无) |
| `ExitPlanMode` | 退出计划模式 | (无) |
| `EnterWorktree` | 创建 Git Worktree | name? |
| `ExitWorktree` | 退出 Worktree | action (keep/remove), discard_changes? |
| `CronCreate` | 创建定时任务 | cron, prompt, recurring?, durable? |
| `CronDelete` | 删除定时任务 | id |
| `CronList` | 列出定时任务 | (无) |
| `Sleep` | 暂停执行 | seconds |

### 5.8 交互工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `AskUserQuestion` | 向用户提问 (支持选项) | questions |
| `SendMessage` | 发送消息 | to, content |
| `TodoWrite` | 管理待办列表 | todos |

### 5.9 网络工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `WebSearch` | 网络搜索 (DuckDuckGo) | query |
| `WebFetch` | 抓取 URL 内容 | url, prompt? |

### 5.10 系统工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `Config` | 读写运行时配置 | action (get/set/list), key?, value? |
| `LSP` | LSP 语言服务操作 | action, file_path, line?, character? |
| `RemoteTrigger` | 远程触发操作 | server_url, action, payload? |
| `ToolSearch` | 搜索可用工具 | query |
| `Brief` | 切换简报模式 | enabled? |
| `SyntheticOutput` | 结构化 JSON 输出 | data, schema? |

---

## 6. 架构概览

### 6.1 模块结构

```
src/
├── main.cpp              # 主入口，初始化所有组件
├── core/                  # 核心引擎
│   ├── QueryEngine        # 查询引擎 (多轮工具调用循环)
│   ├── Message            # 消息类型系统 (6 种)
│   ├── AppState           # 全局应用状态
│   └── CostTracker        # 费用追踪
├── api/                   # API 客户端
│   ├── APIClient          # 抽象接口
│   ├── LocalLLMClient     # 本地 llama.cpp
│   ├── RemoteAPIClient    # Anthropic Claude API
│   ├── OpenAICompatClient # OpenAI 兼容 (LM Studio 等)
│   └── StreamParser       # SSE 流解析
├── tools/                 # 42 个工具实现
├── commands/              # 33 个斜杠命令
├── agents/                # 多智能体系统
├── mcp/                   # MCP 协议客户端
├── plugins/               # 插件与技能目录
├── permissions/           # 权限规则引擎
├── config/                # 配置管理
├── memory/                # 对话记忆 (SQLite)
├── llm/                   # LLM 推理引擎
├── rag/                   # RAG 检索增强 (FAISS + ONNX)
├── ssd/                   # SSD Expert Streaming (MoE)
├── security/              # 安全沙箱
├── network/               # HTTP / WebSocket / SSE 服务
├── git/                   # Git 操作封装
├── lsp/                   # LSP 语言服务客户端
├── bridge/                # 远程桥接执行
└── voice/                 # 语音引擎接口
```

### 6.2 请求处理流程

```
用户输入
  ↓
是命令 (/xxx)? → CommandRegistry 执行
  ↓ 否
QueryEngine.submitMessage()
  ↓
构建 System Prompt (CLAUDE.md + 工具列表 + RAG 上下文)
  ↓
APIClient.streamChat() → 流式生成
  ↓
解析响应 → 文本输出 / 工具调用
  ↓ 工具调用
validateInput → checkPermissions → Tool.call()
  ↓
工具结果回传 → 继续生成 (多轮循环)
  ↓
保存到 MemorySystem
```

### 6.3 API 客户端选择

| Provider | 客户端 | API 格式 | 工具调用方式 |
|----------|--------|----------|-------------|
| `local` | LocalLLMClient | Qwen ChatML | SKILL: 格式解析 |
| `anthropic` | RemoteAPIClient | /v1/messages SSE | 原生 tool_use |
| `openai/lmstudio/siliconflow` | OpenAICompatClient | /v1/chat/completions SSE | function calling |

### 6.4 权限系统

三种模式：
- **default** — 只读工具自动放行，其他询问用户
- **auto** — 只读和安全操作自动放行，危险操作询问
- **bypass** — 全部放行 (危险)

规则优先级：deny > allow > ask > 模式默认行为

---

## 7. 智能体类型

| 类型 | 可用工具 | 用途 |
|------|----------|------|
| `general-purpose` | 全部 | 通用任务 |
| `explore` | Read, Glob, Grep, WebSearch, WebFetch | 代码探索 |
| `plan` | Read, Glob, Grep, WebSearch, WebFetch, AskUser | 方案设计 |
| `verification` | Read, Glob, Grep, Bash, WebSearch | 测试验证 |
| `code-guide` | Read, Glob, Grep, WebSearch, WebFetch | 代码导航 |

---

## 8. MCP 集成

在 `.claude/settings.json` 中配置 MCP 服务器：

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path"],
      "transport": "stdio"
    }
  }
}
```

程序启动时自动连接配置的 MCP 服务器，通过 MCPTool 调用其工具。

---

## 9. 技能目录

在项目根目录创建 `.claude/skills/` 目录，每个技能一个子目录：

```
.claude/skills/
├── my-skill/
│   ├── SKILL.md          # 技能定义 (名称/描述)
│   ├── prompt.md         # 技能提示词
│   └── references/       # 参考文档
```

通过 `/skills` 命令查看已加载技能，通过 `Skill` 工具调用。

---

## 10. 依赖库

| 库 | 用途 |
|----|------|
| llama.cpp | 本地 LLM 推理 |
| ONNX Runtime | 嵌入/重排序模型 |
| FAISS | 向量相似搜索 |
| libcurl | HTTP 客户端 |
| nlohmann/json | JSON 处理 |
| yaml-cpp | YAML 配置 |
| spdlog | 日志 |
| CLI11 | 命令行解析 |
| sqlite3 | 数据库 |
| cpp-httplib | HTTP 服务 |
| ixwebsocket | WebSocket 服务 |
| BS::thread_pool | 线程池 |

---

## 11. 故障排查

| 问题 | 原因 | 解决 |
|------|------|------|
| "Failed to load config" | 工作目录不对 | 从项目根目录运行，或用 `-c` 指定绝对路径 |
| "No model path specified" | config.yaml 中模型路径不存在 | 检查 `llm.model_path`，用绝对路径 |
| "No settings.json found" | 正常提示，非错误 | 创建 `.claude/settings.json` 或忽略 |
| 工具不执行 | 本地模型未按格式输出 | 换更大的模型，或用远程 API |
| CURL 错误 | 网络问题 | 检查 API 地址和密钥 |
| 权限被拒绝 | 权限模式限制 | 用 `/permissions auto` 或 `/permissions bypass` |
| HTTP 429 | API 限流 | 内置自动重试（指数退避，最多 3 次） |
| 内存占用高 | 对话历史太长 | 用 `/compact` 压缩，或 `/new` 新会话 |
| 语音不工作 | 缺少系统 TTS | Windows 自带 SAPI；Linux 安装 espeak；macOS 自带 say |
| MCP 服务器断开 | 配置错误 | 检查 `.claude/settings.json`，用 `/mcp` 查看状态 |
| CUDA 内存不足 | 模型太大 | 用更小的量化模型（Q4_K_M），或减小 max_tokens |

---

## 10. 新增功能 (v0.2.0)

### 10.1 历史压缩

长对话接近上下文窗口限制（默认 75%）时自动压缩旧消息为摘要。也可用 `/compact` 手动触发。

- 远程 API：用 LLM 生成高质量摘要
- 本地模型：统计摘要（消息数、tool 调用数）
- 保留最近 10 条完整消息
- 不会在 tool_use/tool_result 对中间切割

### 10.2 并发 Tool 执行

LLM 一次返回多个 tool_use 时，通过 `std::async` 并行执行。单个 tool 调用串行。

### 10.3 API 重试

429/500/502/503/529 自动指数退避重试（1s/2s/4s），最多 3 次。401/400 不重试。

### 10.4 Hooks 系统

在 `.claude/settings.json` 中配置：

```json
{
  "hooks": [
    {"event": "PreToolUse", "matcher": "Bash", "command": "echo pre", "timeout": 10000},
    {"event": "PostToolUse", "matcher": ".*", "command": "logger tool-done"}
  ]
}
```

- `event`: PreToolUse, PostToolUse, Notification, Stop
- `matcher`: 正则匹配 tool 名称
- Hook 返回非零退出码会阻止 tool 执行
- 环境变量：`HOOK_TOOL`, `HOOK_EVENT`

### 10.5 文件记忆系统

`.claude/memory/` 目录存储持久化记忆：

```
.claude/memory/
├── MEMORY.md              # 索引（自动注入 system prompt）
├── user_role.md           # type: user
├── feedback_testing.md    # type: feedback
└── project_auth.md        # type: project
```

每个 .md 文件格式：

```markdown
---
name: 记忆名称
description: 一行描述
type: user|feedback|project|reference
---

记忆正文内容...
```

管理命令：`/memory list`、`/memory show <名称>`、`/memory delete <名称>`

### 10.6 多 Agent 协调

`/coordinator <任务描述>` 将复杂任务分解为子任务，分配给不同类型 Agent 并行执行。

5 种 Agent 类型及可用工具：

| 类型 | 可用工具 |
|------|---------|
| general-purpose | 全部 |
| explore | Read, Glob, Grep, WebSearch, WebFetch |
| plan | Read, Glob, Grep, WebSearch, WebFetch, AskUserQuestion |
| verification | Read, Glob, Grep, Bash, WebSearch |
| code-guide | Read, Glob, Grep, WebSearch, WebFetch |

### 10.7 Vim 模式

`/vim` 启用。三种模式：

| 模式 | 提示符 | 进入方式 | 说明 |
|------|--------|---------|------|
| Normal | `[N]` | Esc | 命令模式 |
| Insert | `[I]` | i / a | 输入文本 |
| Command | `[:]` | : | 执行 :q / :set novim |

### 10.8 语音输出

`/voice` 启用。AI 回复完成后自动朗读。

| 平台 | TTS 引擎 |
|------|---------|
| Windows | SAPI (PowerShell SpeechSynthesizer) |
| macOS | say 命令 |
| Linux | espeak |

### 10.9 会话持久化

退出时自动保存对话到 SQLite。`/resume` 恢复上次会话。

### 10.10 进程沙箱

`ProcessSandbox` 为执行工具提供 OS 级资源限制：

- Windows: Job Object（内存/CPU 限制）
- Linux: setrlimit（RLIMIT_AS/CPU/FSIZE）
- 危险命令自动拦截（rm -rf /, mkfs 等）

### 10.11 终端 UI 增强

- Spinner 动画：tool 执行时旋转指示器
- Markdown 渲染：代码块、标题、粗体 ANSI 着色
- 表格格式化：/status, /cost 对齐输出
- 多行输入：行尾 `\` 续行，提示符变为 `. `
- Shell 转义：`!` 前缀直接执行 shell 命令（如 `!git status`）
- Token 计数：长对话时提示符显示 `[~Nk tok]`

### 10.12 费用追踪

内置价格表：

| 模型 | Input ($/M tok) | Output ($/M tok) |
|------|-----------------|------------------|
| claude-opus-4-6 | $15.00 | $75.00 |
| claude-sonnet-4-6 | $3.00 | $15.00 |
| claude-haiku-4-5 | $0.80 | $4.00 |
| local-llm | $0.00 | $0.00 |

`/cost` 命令按模型分列显示。

### 10.13 新增命令

| 命令 | 说明 |
|------|------|
| `/resume` | 恢复上次保存的会话 |
| `/review` | 代码审查（git diff + LLM） |
| `/pr` | 创建 Pull Request（gh CLI） |
| `/share` | 导出对话为 Markdown |
| `/hooks` | 列出配置的 Hooks |
| `/memory` | 管理文件记忆 |
| `/tasks` | 查看任务列表 |
| `/agents` | 查看运行中的 Agent |
| `/mcp` | MCP 服务器状态 |
| `/brief` | 切换简洁模式 |
| `/plugin` | 列出已加载插件 |
| `/skills` | 列出可用技能 |
| `/coordinator` | 多 Agent 协调执行 |
| `/vim` | 切换 Vim 模式 |
| `/voice` | 切换语音输出 |
| `/theme` | 切换颜色主题 |

---

> **文档结束** — CloseCrab-Unified v0.2.0
