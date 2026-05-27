# CloseCrab-Unified 深度整合计划

> 基于 JackProAi-claudecode3.1 (TypeScript, 1884文件) 与 CloseCrab-Unified (C++17) 的全量代码对比分析
> 生成日期: 2026-04-07
> 最后更新: 2026-04-07

## 实施进度

| 阶段 | 项目 | 状态 |
|------|------|------|
| P0.1 | 修复 AgentTool APIClient 传递 | DONE |
| P0.2 | BashTool 超时和后台执行 | DONE |
| P0.3 | API 层重试和错误分类 | DONE |
| P1.1 | 历史压缩 SnipCompact | DONE |
| P1.2 | 并发 Tool 执行 | DONE |
| P1.3 | 解除 6-tool 限制 | DONE |
| P1.4 | 终端 UI 增强 (Spinner/Markdown/Table/History) | DONE |
| P1.5 | Hooks 系统 | DONE |
| P2.1 | 补全剩余 Tool (Cron/WebCache) | DONE |
| P2.2 | 记忆系统增强 (FileMemoryManager) | DONE |
| P2.3 | 插件系统完善 (manifest.json 解析) | DONE |
| P2.4 | Skill 系统完善 (单文件 .md + 用户目录) | DONE |
| P2.5 | Slash Commands 第一批 (/review/hooks/memory/tasks/agents/mcp) | DONE |
| P2.5 | Slash Commands 第二批 (/brief/plugin/pr/share/skills/vim/coordinator/resume) | DONE |
| P4.2 | 错误处理统一 (Tool 异常捕获) | DONE |
| P3.1 | Coordinator 多 Agent 协调 | DONE |
| P3.2 | Bridge 远程执行完善 (HTTP+重连) | DONE |
| P3.3 | Voice 引擎 (系统 TTS: SAPI/say/espeak + Whisper 预留) | DONE |
| P3.4 | Sandbox 进程隔离 (ProcessSandbox) | DONE |
| P3.5 | Vim 模式 (Normal/Insert/Command) | DONE |
| P4.1 | 单元测试框架 (GTest + 15 tests) | DONE |
| P4.3 | 性能优化 (FileStateCache/ToolDefCache) | DONE |
| P4.4 | 跨平台完善 (ProcessRunner) | DONE |
| Extra | System prompt 增强 (本地模型 tool 注入/环境上下文) | DONE |
| Extra | /help 分类显示 + 多行输入 (\\ 续行) | DONE |
| Extra | 会话持久化 (自动保存/resume 恢复) | DONE |
| Extra | /doctor 环境诊断 (外部工具/配置文件检查) | DONE |
| Extra | /cost 改进 (TableFormatter + token 估算) | DONE |
| Extra | /voice + /theme 命令 | DONE |
| Extra | LocalLLMClient tool 解析强化 (3 策略: JSON/SKILL/函数调用) | DONE |
| Extra | 长对话 token 计数提示 | DONE |

---

## 现状总结

### CloseCrab-Unified 已有 (可用)
- QueryEngine 多轮对话循环 (含 tool_use 解析、权限检查、流式回调)
- 42 个 Tool 已注册 (大部分为骨架实现，核心 6 个有真实逻辑: Read/Write/Edit/Glob/Grep/Bash)
- 36 个 Slash Command (help/quit/clear/model/status/git系列/session系列/advanced系列)
- 3 种 API Provider (local llama.cpp / anthropic / openai-compat)
- RAG 系统 (FAISS + ONNX embedding + reranker) — Unified 独有
- SSD Expert Streaming (MoE GPU/RAM/NVMe 三级缓存) — Unified 独有
- Permission Engine (default/auto/bypass + 规则加载)
- MCP Client (JSON-RPC 2.0 stdio)
- AgentManager (5 种 agent 类型，已有 spawn/getResult/kill)
- SessionManager + MemorySystem (SQLite)
- SSE/WebSocket/HTTP Server
- AppState 全局状态 + CostTracker
- config.yaml + settings.json 双层配置

### CloseCrab-Unified 缺失或仅为骨架
对比 JackProAi 的完整实现，以下功能需要补全或从零实现:

| 类别 | 缺失项 | JackProAi 对标 |
|------|--------|---------------|
| 核心引擎 | 历史压缩 (SnipCompact) | services/compact/ |
| 核心引擎 | 上下文窗口管理 (自动截断/摘要) | context/, query/ |
| 核心引擎 | 完整 tool_use JSON Schema 验证 | Zod schemas |
| 核心引擎 | 并发 tool 执行 | QueryEngine 并行 tool_use |
| API 层 | 重试/退避/错误分类 | services/api/ |
| API 层 | 缓存读写 token 追踪 + 真实费用计算 | cost-tracker.ts |
| API 层 | 流式中断恢复 | StreamParser 断线重连 |
| Tool 实现 | AgentTool 传递 APIClient (当前传 nullptr) | AgentTool/ |
| Tool 实现 | FileEditTool 真实 diff 替换逻辑 | FileEditTool/ |
| Tool 实现 | GlobTool 递归 + 排除模式 | GlobTool/ |
| Tool 实现 | GrepTool regex + 上下文行 + 输出模式 | GrepTool/ |
| Tool 实现 | WebSearchTool / WebFetchTool 真实 HTTP | WebTools/ |
| Tool 实现 | NotebookEditTool ipynb 解析 | NotebookEditTool/ |
| Tool 实现 | REPLTool 交互式进程 | REPLTool/ |
| Tool 实现 | PowerShellTool 真实 PS 管道 | PowerShellTool/ |
| Tool 实现 | CronTools 定时调度器 | ScheduleCronTool/ |
| Tool 实现 | TeamTools 多人协作 | TeamCreateTool/ |
| Tool 实现 | LSPTool 真实 LSP 调用 | LSPTool/ |
| 命令系统 | 65+ 缺失命令 (/review, /pr, /voice, /vim, /theme 等) | commands/ (87个目录) |
| 记忆系统 | 会话记忆提取 + 团队记忆同步 | SessionMemory/, teamMemorySync/ |
| 插件系统 | 插件加载/信任验证/命令注册 | services/plugins/ |
| Skill 系统 | Skill 目录扫描 + 执行 | skills/ |
| Hooks 系统 | 事件钩子 (before/after tool) | hooks/ |
| UI/UX | 终端富文本 (颜色/进度条/表格) | React/Ink components/ |
| UI/UX | Vim 模式 | vim/ |
| 安全 | Sandbox 真实进程隔离 | security/ |
| 网络 | Bridge 远程执行 | bridge/ (14文件) |
| 语音 | Voice 引擎 | voice/ |

---

## 整合计划 — 按优先级排列

### P0 — 关键基础 (必须先做，其他功能依赖这些)

#### P0.1 修复 AgentTool 的 APIClient 传递
- 优先级: **最高** — 当前 AgentTool 传 nullptr，子 agent 完全无法工作
- 文件: `src/tools/AgentTool/AgentTool.h`, `src/agents/AgentManager.h/.cpp`
- 工作量: 小 (1-2天)
- 做法:
  1. 在 `ToolContext` 中增加 `APIClient*` 和 `ToolRegistry*` 指针
  2. `main.cpp` 创建 QueryEngine 时将 apiClient 注入 ToolContext
  3. AgentTool::call() 从 ctx 取 apiClient 传给 AgentManager::spawnAgent()
  4. AgentManager 内部为子 agent 创建独立的 QueryEngine 实例，根据 AgentType 过滤可用 tool
- 验证: 用 `Agent(type=explore, prompt="列出当前目录文件")` 测试子 agent 能否正常返回

#### P0.2 完善核心 Tool 实现 (Read/Write/Edit/Glob/Grep/Bash)
- 优先级: **最高** — 这 6 个 tool 是所有工作的基础
- 文件: `src/tools/File*/`, `src/tools/GlobTool/`, `src/tools/GrepTool/`, `src/tools/BashTool/`
- 工作量: 中 (3-5天)
- 具体补全:
  - **FileEditTool**: 实现真实的 old_string→new_string 精确替换 (当前可能是骨架)
    - 支持 `replace_all` 参数
    - 唯一性检查: old_string 在文件中必须唯一，否则报错
    - 保留原始缩进和换行符
  - **GlobTool**: 实现 `std::filesystem::recursive_directory_iterator` + glob 模式匹配
    - 支持 `**/*.cpp` 递归模式
    - 按修改时间排序返回
    - 排除 `.git/`, `node_modules/`, `build/` 等目录
  - **GrepTool**: 集成 ripgrep 或实现 `std::regex` 搜索
    - 支持 `-A/-B/-C` 上下文行
    - 支持 `output_mode`: content / files_with_matches / count
    - 支持 `head_limit` 截断
    - 支持 `glob` 文件过滤
  - **BashTool**: 增加超时机制 (当前无 timeout 实现)
    - 用 `std::thread` + `WaitForSingleObject`/`waitpid` 实现超时杀进程
    - 增加 `run_in_background` 支持
- 验证: 对每个 tool 写单元测试，确保与 JackProAi 行为一致

#### P0.3 API 层健壮性
- 优先级: **最高** — 网络不稳定时整个程序会崩
- 文件: `src/api/RemoteAPIClient.cpp`, `src/api/OpenAICompatClient.cpp`, `src/api/StreamParser.cpp`
- 工作量: 中 (2-3天)
- 做法:
  1. **重试机制**: 429/500/502/503 自动指数退避重试 (最多 3 次)
  2. **错误分类**: 区分 AuthError / RateLimitError / OverloadError / NetworkError / APIError
  3. **流式断线恢复**: SSE 连接断开时自动重连，从上次 event ID 继续
  4. **超时控制**: 连接超时 30s，读取超时 300s，可配置
  5. **真实费用计算**: 根据模型名查价格表 (claude-opus/sonnet/haiku 各有不同单价)
     - 在 CostTracker 中维护价格表 map
     - 每次 API 返回 usage 时计算并累加 totalCostUSD

---

### P1 — 核心体验 (直接影响日常使用质量)

#### P1.1 历史压缩 (SnipCompact)
- 优先级: **高** — 长对话会撑爆上下文窗口，导致 API 报错或费用暴涨
- 对标: JackProAi `services/compact/`
- 新增文件: `src/core/HistoryCompactor.h/.cpp`
- 工作量: 中 (3-4天)
- 做法:
  1. 监控 messages_ 总 token 数 (用 APIClient::countTokens 估算)
  2. 当接近上下文限制 (如 80%) 时触发压缩
  3. 压缩策略: 保留最近 N 条消息 + 将旧消息摘要为一条 SYSTEM 消息
  4. 插入 CompactBoundaryMessage 标记压缩点
  5. 在 QueryEngine::submitMessage() 的循环开头检查并触发
  6. `/compact` 命令手动触发

#### P1.2 并发 Tool 执行
- 优先级: **高** — JackProAi 支持一次 API 返回多个 tool_use 并行执行
- 文件: `src/core/QueryEngine.cpp` (processToolUse 部分)
- 工作量: 小 (1-2天)
- 做法:
  1. 当 `pendingToolCalls.size() > 1` 时，用 ThreadPool 并行执行
  2. 收集所有 ToolResult 后统一添加到 messages_
  3. 注意: 有依赖关系的 tool (如先 Read 再 Edit) 不能并行 — 但这由 LLM 控制，同一批次的 tool_use 天然无依赖
  4. 加锁保护 messages_ 的写入

#### P1.3 完善 RemoteAPIClient 的 tool_use 发送
- 优先级: **高** — 当前只发送 6 个核心 tool 定义，LLM 无法使用其他 tool
- 文件: `src/core/QueryEngine.cpp` (buildModelConfig)
- 工作量: 小 (1天)
- 做法:
  1. 移除 `coreToolNames` 硬编码限制
  2. 发送所有已注册且 `isEnabled()` 的 tool 定义
  3. 对 tool 定义做 token 预算控制: 如果总 tool schema 超过阈值，按优先级裁剪
  4. Plan 模式下只发送 readOnly tool 的定义

#### P1.4 终端 UI 增强
- 优先级: **高** — 当前只有 cout 原始输出，用户体验差
- 新增文件: `src/ui/TerminalUI.h/.cpp`
- 工作量: 中 (3-5天)
- 做法:
  1. **Markdown 渲染**: 代码块语法高亮 (用 ANSI 转义码)
  2. **进度指示器**: tool 执行时显示 spinner
  3. **表格输出**: /cost, /status 等命令用对齐表格
  4. **颜色主题**: 支持 light/dark 主题切换
  5. **输入增强**: 集成 readline 或 linenoise 库
     - 历史记录 (上下箭头)
     - Tab 补全 (命令名、文件路径)
     - 多行输入 (Shift+Enter 或 `\` 续行)
  6. **宽字符支持**: 已有 Win32 ReadConsoleW，补全 Linux 的 UTF-8 处理

#### P1.5 Hooks 系统
- 优先级: **高** — JackProAi 的 hooks 是自动化工作流的基础
- 对标: JackProAi `hooks/`
- 新增文件: `src/hooks/HookManager.h/.cpp`
- 工作量: 中 (2-3天)
- 做法:
  1. 从 settings.json 读取 hooks 配置
  2. 支持事件: `PreToolUse`, `PostToolUse`, `Notification`, `Stop`
  3. 每个 hook 定义: `{ event, matcher (tool/command pattern), command (shell) }`
  4. 在 QueryEngine::processToolUse() 前后触发对应 hook
  5. Hook 输出可注入为 system message 影响后续对话

---

### P2 — 功能完善 (提升能力覆盖面)

#### P2.1 补全剩余 Tool 的真实实现
- 优先级: **中高**
- 工作量: 大 (7-10天)
- 按子优先级排列:

  **P2.1a WebSearchTool / WebFetchTool** (2天)
  - 用 libcurl 实现真实 HTTP GET
  - WebFetch: 下载 HTML → 用简单解析器转 markdown (去 script/style/nav 标签)
  - WebSearch: 调用搜索 API (Brave Search / SerpAPI / 自建)
  - 支持 15 分钟缓存 (避免重复请求)

  **P2.1b PowerShellTool** (1天)
  - Windows: `powershell.exe -NoProfile -Command "..."` 执行
  - 支持超时和输出截断
  - 与 BashTool 共享安全命令白名单逻辑

  **P2.1c REPLTool** (2天)
  - 启动持久化子进程 (python3 / node)
  - 通过 stdin/stdout 管道交互
  - 支持超时和输出截断
  - 进程池管理: 复用已启动的 REPL 进程

  **P2.1d NotebookEditTool** (1天)
  - 解析 .ipynb JSON 格式
  - 支持 replace/insert/delete cell
  - 保持 notebook metadata 不变

  **P2.1e LSPTool** (2天)
  - 完善 LSPClient: 支持 initialize/textDocument/diagnostics
  - getDiagnostics: 获取文件的语法/类型错误
  - semanticRename: 重命名符号
  - 通过 stdio 与 LSP server 通信

  **P2.1f CronTools 定时调度器** (2天)
  - 实现 cron 表达式解析器 (5 字段标准格式)
  - 后台线程定时检查并触发
  - 支持 durable (持久化到 .claude/scheduled_tasks.json) 和 session-only
  - 支持 one-shot (recurring=false) 和循环任务

#### P2.2 记忆系统增强
- 优先级: **中高**
- 对标: JackProAi `services/SessionMemory/`, `extractMemories/`, `teamMemorySync/`
- 文件: `src/memory/MemorySystem.h/.cpp` (扩展)
- 工作量: 中 (3-4天)
- 做法:
  1. **自动记忆提取**: 对话结束时，用 LLM 提取值得记住的信息
  2. **记忆分类**: user / feedback / project / reference 四种类型
  3. **文件记忆**: 支持 MEMORY.md 索引 + 独立 .md 文件存储 (与 JackProAi 一致)
  4. **记忆检索**: 对话开始时加载相关记忆到 system prompt
  5. **过期清理**: 项目记忆自动过期 (可配置天数)

#### P2.3 插件系统完善
- 优先级: **中**
- 对标: JackProAi `services/plugins/`
- 文件: `src/plugins/PluginManager.h` (扩展为 .h/.cpp)
- 工作量: 中 (3-4天)
- 做法:
  1. 扫描 `.claude/plugins/` 目录
  2. 每个插件是一个目录，包含 `manifest.json` (名称、版本、命令、tool)
  3. 插件可注册新 Command 和 Tool
  4. 信任验证: 首次加载时提示用户确认
  5. 插件隔离: 插件 tool 在独立沙箱中执行

#### P2.4 Skill 系统完善
- 优先级: **中**
- 对标: JackProAi `skills/`
- 文件: `src/plugins/PluginManager.h` 中的 SkillDirectory (扩展)
- 工作量: 小 (2天)
- 做法:
  1. 扫描 `.claude/skills/` 和项目 `.claude/skills/`
  2. 每个 skill 是一个 .md 文件，包含 frontmatter (name, trigger, description)
  3. SkillTool 执行时将 skill 内容注入为 system message
  4. 支持用户自定义 skill

#### P2.5 补全缺失的 Slash Commands
- 优先级: **中**
- 工作量: 中 (3-5天)
- 按重要性分批:

  **第一批 (常用):**
  - `/review` — 代码审查 (git diff → LLM 分析)
  - `/pr` — 创建 PR (调用 gh CLI)
  - `/config` — 查看/修改配置
  - `/theme` — 切换颜色主题
  - `/memory` — 查看/管理记忆
  - `/tasks` — 查看任务列表
  - `/hooks` — 查看/管理 hooks

  **第二批 (进阶):**
  - `/voice` — 语音模式开关
  - `/vim` — Vim 模式开关
  - `/brief` — 简洁模式开关
  - `/agents` — 查看运行中的 agent
  - `/mcp` — MCP 服务器管理
  - `/plugin` — 插件管理
  - `/share` — 导出对话为可分享格式

  **第三批 (高级):**
  - `/coordinator` — 多 agent 协调模式
  - `/teleport` — 远程会话
  - `/chrome` — 浏览器集成
  - `/desktop` — 桌面集成

---

### P3 — 高级功能 (差异化竞争力)

#### P3.1 Coordinator 多 Agent 协调模式
- 优先级: **中低**
- 对标: JackProAi `coordinator/`
- 新增文件: `src/coordinator/Coordinator.h/.cpp`
- 工作量: 大 (5-7天)
- 做法:
  1. Coordinator 是一个特殊的 QueryEngine，管理多个子 agent
  2. 用户提交任务 → Coordinator 拆分为子任务 → 分配给不同类型的 agent
  3. Agent 间通过 SendMessageTool 通信
  4. Coordinator 汇总结果并呈现给用户
  5. 支持并行执行独立子任务

#### P3.2 Bridge 远程执行完善
- 优先级: **中低**
- 对标: JackProAi `bridge/` (14 文件)
- 文件: `src/bridge/BridgeClient.h/.cpp` (扩展)
- 工作量: 中 (3-4天)
- 做法:
  1. WebSocket 双向通信协议
  2. 远程命令执行 + 结果回传
  3. 文件同步 (增量)
  4. 认证和加密 (TLS)
  5. 断线重连

#### P3.3 Voice 语音引擎
- 优先级: **低**
- 对标: JackProAi `voice/`, `vendor/audio-capture-src/`
- 文件: `src/voice/VoiceEngine.h` (实现)
- 工作量: 大 (5-7天)
- 做法:
  1. 音频采集: Windows WASAPI / Linux PulseAudio
  2. 语音识别: Whisper.cpp 本地推理 或 远程 API
  3. 语音合成: 本地 TTS 或远程 API
  4. VAD (语音活动检测): 自动检测说话开始/结束
  5. 与 QueryEngine 集成: 语音输入 → 文本 → submitMessage

#### P3.4 Sandbox 真实进程隔离
- 优先级: **中低**
- 对标: JackProAi 的 sandbox 执行
- 文件: `src/security/Sandbox.h/.cpp` (扩展)
- 工作量: 中 (3-4天)
- 做法:
  1. Windows: Job Object 限制子进程资源
  2. Linux: seccomp + namespace 隔离
  3. 文件系统: 限制可访问目录 (只允许项目目录 + temp)
  4. 网络: 可选禁止网络访问
  5. 资源限制: CPU 时间、内存、磁盘写入量

#### P3.5 Vim 模式
- 优先级: **低**
- 对标: JackProAi `vim/`
- 新增文件: `src/ui/VimMode.h/.cpp`
- 工作量: 中 (3-4天)
- 做法:
  1. 输入模式切换: Normal / Insert / Visual / Command
  2. 基本 Vim 键绑定: hjkl, dd, yy, p, /, :w, :q
  3. 与 readline/linenoise 集成
  4. 状态栏显示当前模式

---

### P4 — 质量与工程化

#### P4.1 单元测试框架
- 优先级: **高** (应与 P0 同步进行)
- 新增: `tests/` 目录, CMakeLists.txt 添加 test target
- 工作量: 中 (2-3天搭建框架，后续持续补充)
- 做法:
  1. 集成 Google Test (通过 vcpkg)
  2. 为每个 Tool 写基础测试
  3. 为 QueryEngine 写集成测试 (mock APIClient)
  4. 为 StreamParser 写 SSE 解析测试
  5. CI 集成 (GitHub Actions)

#### P4.2 错误处理统一
- 优先级: **中高**
- 工作量: 小 (1-2天)
- 做法:
  1. 定义异常层次: `CloseCrabError` → `APIError` / `ToolError` / `ConfigError`
  2. 所有 Tool::call() 内部 catch 异常，转为 ToolResult::fail()
  3. API 层异常不应导致程序崩溃
  4. 统一日志格式: `[组件] [级别] 消息`

#### P4.3 性能优化
- 优先级: **中**
- 工作量: 中 (持续进行)
- 做法:
  1. **文件读取缓存**: 基于 mtime 的 FileStateCache (避免重复读取同一文件)
  2. **Tool 定义缓存**: buildModelConfig() 中的 tool schema 只构建一次
  3. **消息序列化优化**: 大消息体用 move 语义避免拷贝
  4. **线程池调优**: 根据 CPU 核心数自动设置线程数
  5. **内存池**: 频繁创建的小对象 (ContentBlock, StreamEvent) 用对象池

#### P4.4 跨平台完善
- 优先级: **中**
- 工作量: 中 (3-5天)
- 做法:
  1. Linux 构建: 补全 CMakePresets.json 中 linux preset 的依赖
  2. macOS 构建: 补全 macos preset
  3. 路径处理: 统一用 `std::filesystem::path`，避免硬编码 `\\` 或 `/`
  4. 进程执行: 抽象 `ProcessRunner` 类，封装 Win32 CreateProcess / POSIX fork+exec
  5. 编码处理: 统一 UTF-8，Windows 下正确处理 wchar_t 转换

---

## 实施路线图

```
阶段一 (第1-2周): P0 全部 + P4.1 测试框架
  ├─ P0.1 修复 AgentTool APIClient 传递
  ├─ P0.2 完善 6 个核心 Tool
  ├─ P0.3 API 层健壮性
  └─ P4.1 搭建测试框架

阶段二 (第3-4周): P1 全部
  ├─ P1.1 历史压缩 SnipCompact
  ├─ P1.2 并发 Tool 执行
  ├─ P1.3 完善 tool_use 发送
  ├─ P1.4 终端 UI 增强
  └─ P1.5 Hooks 系统

阶段三 (第5-8周): P2 全部
  ├─ P2.1 补全剩余 Tool 实现
  ├─ P2.2 记忆系统增强
  ├─ P2.3 插件系统
  ├─ P2.4 Skill 系统
  └─ P2.5 补全 Slash Commands

阶段四 (第9-12周): P3 + P4 剩余
  ├─ P3.1 Coordinator 多 Agent 协调
  ├─ P3.2 Bridge 远程执行
  ├─ P3.3 Voice 语音引擎
  ├─ P3.4 Sandbox 进程隔离
  ├─ P3.5 Vim 模式
  ├─ P4.2 错误处理统一
  ├─ P4.3 性能优化
  └─ P4.4 跨平台完善
```

---

## 依赖关系图

```
P0.1 (AgentTool修复) ──→ P1.2 (并发Tool) ──→ P3.1 (Coordinator)
         │
         └──→ P2.1e (LSPTool)

P0.2 (核心Tool) ──→ P2.1a-f (剩余Tool)
         │
         └──→ P4.1 (测试)

P0.3 (API健壮性) ──→ P1.1 (历史压缩) ──→ P1.3 (tool_use发送)
                              │
                              └──→ P2.2 (记忆系统)

P1.4 (终端UI) ──→ P3.5 (Vim模式)

P1.5 (Hooks) ──→ P2.3 (插件系统) ──→ P2.4 (Skill系统)

P2.5 (Commands) ──→ P3.2 (Bridge) ──→ P3.3 (Voice)
```

---

## 新增依赖库 (预计)

| 库 | 用途 | 引入阶段 |
|----|------|---------|
| linenoise / replxx | 终端输入增强 (历史/补全) | P1.4 |
| Google Test | 单元测试 | P4.1 |
| Whisper.cpp | 语音识别 (可选) | P3.3 |
| seccomp (Linux) | 进程沙箱 | P3.4 |

---

## 风险与注意事项

1. **API 兼容性**: 不同 OpenAI-compatible 服务对 tool_use 的支持程度不同 (LM Studio 可能不支持)。需要在 LocalLLMClient 中保留 SKILL: 格式的 fallback 解析。

2. **内存占用**: 42 个 tool 的 JSON Schema 全部发送会占用大量 token。需要实现智能裁剪 — 只发送当前上下文可能用到的 tool。

3. **线程安全**: 并发 tool 执行时，多个 tool 可能同时修改文件系统。FileWriteTool 和 FileEditTool 需要文件级锁。

4. **Windows 特殊处理**: 很多 Unix 工具 (ripgrep, git) 在 Windows 上行为不同。BashTool 需要区分 cmd.exe 和 bash (Git Bash / WSL)。

5. **本地模型限制**: llama.cpp 的 tool_use 支持有限。对于本地模型，可能需要保持 "文本解析 tool 调用" 的方式，而非原生 tool_use。

6. **RAG 与 Memory 的关系**: Unified 独有的 RAG 系统可以与 P2.2 记忆系统结合 — 将记忆向量化存储，实现语义检索记忆。这是超越 JackProAi 的差异化优势。




