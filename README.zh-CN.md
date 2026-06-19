<div align="center">

[English](README.md) | **中文**

</div>

# CloseCrab-Unified — 本地 AI 编程助手

一个用 C++17 编写的本地优先 AI 编程助手。可以在你自己的 GPU 上运行大语言模型，也可以连接 Claude、OpenAI 等远程 API — 只需改一行配置。AI 拥有 59 个工具、84 个命令、多智能体协作、Coordinator Mode、Team Mode（多客户端并行推理）、语音输入输出（TTS+ASR）、百万 token 上下文窗口，全部受权限系统保护。单文件可执行程序，约 3.2MB。

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CUDA](https://img.shields.io/badge/CUDA-12.x-green.svg)](https://developer.nvidia.com/cuda-toolkit)
[![Windows](https://img.shields.io/badge/Platform-Windows%20|%20Linux%20|%20macOS-0078d7.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Tools](https://img.shields.io/badge/Tools-59-orange.svg)](#tools-59)
[![Team Mode](https://img.shields.io/badge/Team%20Mode-Parallel-ff69b4.svg)](#team-mode团队模式)
[![Skills](https://img.shields.io/badge/Skills-11-purple.svg)](#skills)
[![Context](https://img.shields.io/badge/Context-1M%20tokens-blueviolet.svg)](#)

---

## 这是什么？

大多数 AI 编程工具需要联网。CloseCrab-Unified 给你选择权：在本地 GPU 上运行模型（零网络依赖），或者连接 Claude/OpenAI/任何兼容 API。无论哪种方式，AI 都能使用同一套 59 个工具。

它融合了两个项目：**CloseCrab**（C++ 本地推理引擎，带 RAG 和 MoE 流式加载）和 **JackProAi-claudecode**（TypeScript CLI，40+ 工具和 95 个命令）。最终产出一个 C++ 单文件可执行程序，~170 个源文件编译为 ~3.2MB，包含 59 个工具、84 个命令、30+ 个服务模块。

### 为什么用它？

- **隐私**：本地模式下所有数据留在你的机器上，零网络依赖
- **灵活**：改一行配置就能在本地和远程模型之间切换
- **真工具**：AI 不只是聊天 — 它能读文件、写代码、跑测试、搜索网络
- **智能**：多 Agent 协调、历史压缩、记忆系统、Hooks 自动化
- **可扩展**：59 个内置工具 + 插件系统 + MCP 协议 + 技能目录
- **Team Mode**：多客户端并行推理，内置排行榜、成就系统和共享知识库
- **快**：C++17，CUDA GPU 加速，无 Python 运行时

---

## 0.4.0 新增（503 / "处理着就忘了在做什么" 根治 + MCP 工具摊开）

本次彻底修掉"卡 503 一会会就忘了之前在做什么"的真因，并把 MCP server 工具变成一等公民。所有 503 修复均对齐 JackProAi 真实的上下文管理源码（`services/compact/`）。

**503 "健忘"循环——真因与修复。** 触发它的从来不是供应商随机宕机，而是 CloseCrab 自己的上下文管理。深入排查问题时，模型会狂读文件，tool_result 不断堆积，请求涨到约 16~20 万 tokens。中转因请求过大回 503，而 CloseCrab 误以为"503 = 太大了"，每次重试就压得更狠（L1→L9），最后把整段对话从 302 条总结成 11 条，毁掉 96% 的上下文。你输入"继续"时，模型已经完全不知道刚才在干嘛。

- **发送前阈值现在跟随真实上下文窗口** —— 主动压缩阈值原本写死 800K，是模型真实上限 200K 的约 4 倍，所以它在请求撑爆之前从不触发。现在按 JackProAi 的方式解析（`getContextWindowForModel` / `getAutoCompactThreshold`）：`环境变量 CLOSECRAB_MAX_CONTEXT_TOKENS` > `config api.context_window` > 模型 ID 含 `[1m]` → 1M > 默认 200K，再算 `有效窗口 = 窗口 − 20K 输出预留`、`阈值 = 有效窗口 − 13K`。现在约 16.7 万就压缩——在请求被拒之前。可在 `config.yaml` 设 `api.context_window` 钉死你中转的真实上限。
- **503 = "等待"，不是"砍上下文"** —— 错误体含"供应商暂时不可用 / overloaded / service_unavailable"且请求本身不大时，判定为临时宕机：指数退避重试、**保留上下文**、不压缩。只有请求真的过大（或明确的超限错误）才升级压缩。对齐 JackProAi 的 `Hw6`（overloaded → 纯重试）与 `fX4`（真超限 → 调整）。
- **外科式压缩，不再核爆** —— 真需要裁剪时，改用 micro-compaction（只清**旧的 tool_result 内容**，保留每条消息和最近的结果），而不是 302→11 总结。任务上下文得以幸存，"继续"才能真的继续。对齐 JackProAi 的 `microCompact.ts`。
- **压缩熔断器** —— 连续 3 次压缩都释放不出空间后，停止对 API 的徒劳重试（JackProAi `MAX_CONSECUTIVE_AUTOCOMPACT_FAILURES = 3`）；下一轮成功后重置。

**MCP 工具现在是一等公民。** 每个已连接的 MCP server 工具都注册成独立工具（`mcp__<server>__<tool>`），带 server 真实的名称、描述和 schema，模型可直接调用如 `mcp__codebase-memory__search_graph`，不再走那个笼统的代理。已用 codebase-memory-mcp 经 stdio 验证（摊开 14 个工具）。

---

## 0.3.9 新增（闪退修复、崩溃安全保存与按项目隔离会话）

本次更新终结"用着用着疯狂闪退"，避免异常退出丢进度，并把会话历史按项目隔离。

- **修复流式回调闪退（`0xE06D7363`）** —— 最关键的一个。OpenAI 兼容客户端的 libcurl 写回调没有任何异常防护，于是解析/回调链里抛出的任何 C++ 异常（503/504 转成的抛出、坏 UTF-8、JSON 错误、超大工具结果 OOM）会**穿过 libcurl 的 C 栈帧**展开——这是未定义行为，在 MSVC 上表现为 `0xE06D7363` 直接撞顶层处理器、杀掉进程。这就是它"用着用着"崩（跑了几轮、某次流式响应触发）而不是一启动就崩的原因。回调现在标记 `noexcept` 并加 try/catch：出错时返回偏短的字节数让 curl 干净中止（`CURLE_WRITE_ERROR`），交给已有的重试/错误逻辑接管，而不是炸掉进程。（`RemoteAPIClient` 早先就有这道防护；偏偏实际对接中转用的 OpenAI 兼容路径被漏了。）
- **远程遥控的 worker 线程闪退** —— 手机/网页的 `SessionRouter` worker 裸调 `submitMessage`、无 try/catch，所以用手机/浏览器遥控时同类异常会经 `std::terminate` 崩进程。现在 worker 捕获一切异常、把错误回报给客户端并保持存活；并用作用域守卫清掉每客户端的"生成中"标志，抛异常的一轮不再让客户端卡在"生成中"。
- **崩溃安全保存进度** —— 此前点窗口 X、`taskkill`、Ctrl-C、注销、关机都会完全跳过退出时的保存，丢掉当前对话。现在用 `SetConsoleCtrlHandler` 在进程死前 flush transcript。assistant 回合（文本 + 工具调用）也会在工具执行**之前**就写入 JSONL transcript，于是中途崩溃最多丢正在执行的那一个工具结果，不会丢整轮。
- **按项目隔离会话** —— transcript 现在按项目目录分桶（`data/transcripts/<哈希>__<项目>/`），不再共用一个扁平目录。`/resume` 和默认会话复用只看当前项目的历史，换项目不再翻出 CloseCrab 自己的会话。同项目无视斜杠风格/大小写/尾斜杠都解析到同一桶；旧的扁平 transcript 经 legacy 回退仍可加载。

---

## 0.3.8 新增（503/504 韧性与会话恢复）

> **0.3.8 热修复：** 0.3.7 启动即崩溃（`-c` 短 flag 重复，导致 CLI 解析在 `main` 运行前就抛异常）。0.3.8 已修复——请用 `--continue`（无短写形式）。下面所有功能都是 0.3.7 引入的，现在才真正可运行。

本次更新彻底打断"对话一长就反复 503/504"的循环，并修复会话恢复：

- **不再 503/504 死循环** -- 真凶是：重试前的压缩只作用在请求的*副本*上，从未落到持久化的历史，所以每次重试（以及你的下一条消息）都重发同样的超大请求、同样失败。现在压缩状态跨重试保留并在每次重建请求时重新套用，且对服务端错误（`503`/`504`/`500`/`502`）都触发，不再只针对网络超时。
- **504 显式处理** -- 网关超时现在归类为可重试的服务端错误（它通常意味着请求太大、代理来不及处理）。
- **服务端上下文管理默认开启** -- 输入超过阈值后，由服务端清理旧的读类工具结果（`Bash`/`Glob`/`Grep`/`Read`/`WebFetch`/`WebSearch`），不扰动已缓存的前缀。若代理不支持该字段会自动剥离并重试；设 `CLOSECRAB_API_CLEAR_TOOL_RESULTS=0` 可关闭。
- **重试用尽后自动压缩** -- 若所有重试都因服务端错误失败，会自动压缩一次会话，让你的*下一条*消息从更小的上下文开始，而不是继续 503。
- **`/resume` 真正显示历史** -- 之前它把消息读进内存却只打印"已恢复 N 条消息"，过往对话从不显示。现在会把恢复的对话重新渲染到屏幕。
- **transcript 保留工具调用** -- 会话记录现在存完整的 content blocks（`tool_use`/`tool_result`/`thinking`）而非扁平文本，恢复的会话更完整。旧 transcript 仍可加载。
- **会话连续性** -- 启动时复用最近一次会话的 ID（transcript 跨运行持续增长），但默认不把历史灌入上下文。新增 flag：`--continue` 加载历史进上下文，`--new` 强制全新会话。
- **版本号 banner 修复** -- 程序内版本（banner / `/version`）一直停在旧号，因为 `CMakeLists.txt` 落后于 installer/tag；现已修正这个单一来源。

---

## 0.3.1 新增（许可证与 macOS/Linux 持久化）

本次更新添加离线许可证激活，改进跨平台持久化：

- **离线激活** -- 许可证激活在首次在线激活后可离线工作
- **macOS/Linux 许可证持久化** -- macOS 和 Linux 上许可证现在重启后保持（之前仅 Windows）
- **Windows 安装器** -- 完整的 Windows 安装器包含运行时 DLL 和图标
- **SSL 线程安全** -- 修复 macOS 上的 libcurl SSL 连接错误（"用着用着SSL connect error"）通过添加 `CURLOPT_NOSIGNAL` 保证线程安全
- **合并连续用户消息** -- 多条连续用户消息（中间无助手回复）自动合并
- **Pause Turn 处理** -- 处理流式 API 响应中的 `pause_turn`（修复无限"等待响应"）
- **CI 修复** -- 内置 ChineseSimplified Inno Setup 语言文件、跟踪安装器图标到 git

---

## 0.2.1 新增（可靠性与成本）

本次更新加固了 agent 主循环，并大幅降低多轮会话的 API 成本：

- **写前必读保护**：Write/Edit 会拒绝覆盖模型还没读过的文件，并通过 mtime + 内容哈希检测外部改动。写失败时不再走"删文件重建"的危险路径，避免误删你的文件。
- **危险 `rm` 拦截**：`rm`/`rmdir`/`del` 指向受保护路径（盘符根、家目录、根级目录、通配符）时**强制确认**——即使开了自动批准也绕不过。
- **Windows shell 引号修复**：`node -e "..."`、`sed -i "..."`、`python -c "..."` 在 Git Bash 下不再炸。命令通过单引号 `eval` 包裹 + 正确的 Windows argv 转义执行；`>nul` 自动改写为 `/dev/null`。
- **工作目录持久化**：`cd subdir` 现在会带到下一条 Bash 调用（通过 `pwd -P` 捕获）。
- **Prompt 缓存优化**：系统提示拆成稳定（可缓存）前缀 + 动态尾部；旧工具结果按确定性方式压缩，使消息前缀字节稳定。恢复了 prompt-cache 命中，多轮会话成本显著下降。
- **模型 fallback 链**：连续 `503`/过载错误后，客户端自动从主模型（如 Opus）切换到配置的 fallback（如 Sonnet）。在 `config.yaml` 设置 `api.fallback_model`。

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
closecrab.exe
```

### 方法 3：命令行参数

```bash
closecrab.exe --provider anthropic --api-key sk-xxx --api-url https://api.anthropic.com --api-model claude-sonnet-4-20250514
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

## 命令参考 (83 个)

### 会话

| 命令 | 说明 |
|------|------|
| `/help` | 显示所有命令（按分类） |
| `/quit` `/exit` | 退出程序 |
| `/clear` | 清空对话历史 |
| `/new` | 开始新会话 |
| `/resume` | 恢复上次保存的会话 |
| `/rename <名称>` | 重命名当前会话 |
| `/tag <标签>` | 给会话打标签 |
| `/history [N]` | 显示最近 N 条对话 |
| `/export [文件]` | 导出对话到文件 |
| `/share` | 导出对话为可分享的 Markdown |
| `/copy` | 复制最后回复到剪贴板 |
| `/compact` | 压缩对话历史（节省上下文） |
| `/rewind [N]` | 撤销最后 N 轮对话（默认 2） |
| `/summary` | LLM 生成对话摘要 |
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
| `/usage` | 详细 token 使用统计（按模型 + 上下文 + 耗时） |
| `/fast` | 切换快速模式 |
| `/brief` | 切换简洁输出模式 |
| `/thinking [on/off/N]` | 切换扩展思考模式 |
| `/effort [low/medium/high]` | 设置推理努力程度 |
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
| `/pr_comments [N]` | 查看 PR 评论 |
| `/issue [命令]` | GitHub Issue 管理（list/view/create） |
| `/autofix-pr [N]` | 自动修复 PR 问题（读评论+检查→修复） |

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
| `/usage` | 详细 token 使用统计 |

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
| `/effort [low/medium/high]` | 设置推理努力程度 |
| `/output-style [格式]` | 切换输出风格（markdown/plain/json） |
| `/thinkback` | 回放 AI 的思考过程 |
| `/bughunter [路径]` | 自动 bug 搜索模式 |
| `/passes N <任务>` | 多轮自动执行任务 |
| `/rename <名称>` | 重命名会话 |
| `/tag <标签>` | 给会话打标签 |
| `/copy` | 复制最后回复到剪贴板 |
| `/rewind [N]` | 撤销最后 N 轮对话 |
| `/summary` | 生成对话摘要 |
| `/add-dir [路径]` | 添加目录到上下文 |
| `/files [路径]` | 列出目录文件 |
| `/reload` | 重新加载配置 |

### v0.2.0 新增命令

| 命令 | 说明 |
|------|------|
| `/config` | 查看/修改 settings.json |
| `/model [名称]` | 运行时切换模型 |
| `/cost` | 费用追踪（按模型分列） |
| `/permissions [模式]` | 权限管理（default/auto/bypass） |
| `/status` | 系统状态总览 |
| `/clear` | 清空对话历史 |
| `/fork` | 分叉当前会话 |
| `/security-review` | 安全审查（LLM 分析代码变更） |
| `/sandbox-toggle` | 切换沙箱模式 |
| `/keybindings` | 查看快捷键 |
| `/privacy-settings` | 隐私设置 |
| `/rate-limit-options` | 速率限制配置 |
| `/commit-push-pr` | 一键 git add + commit + push + PR |
| `/release-notes` | 从 git log 生成发布说明 |
| `/stats` | 详细统计（消息数、工具调用、token、耗时） |
| `/bridge` | Bridge 远程控制 |
| `/buddy` | 伙伴模式（每步确认） |
| `/peers` | 对等会话管理 |
| `/workflows` | 运行 .crab/workflows/ 脚本 |
| `/oauth-refresh` | OAuth token 刷新 |

---

## 工具 (51 个)

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
| 发现/工作流 | DiscoverSkills（技能发现）, Snip（片段管理/历史裁剪）, VerifyPlanExecution（计划验证）, WebBrowser（浏览器自动化） |

---

## 逆向工程

内置逆向工程能力，支持 CUDA 加速：

| 功能 | 说明 |
|------|------|
| Capstone 反汇编 | 多架构反汇编（x86, x64, ARM, AArch64, MIPS） |
| CUDA 加速二进制分析 | GPU 加速的模式匹配和熵计算 |
| PE/ELF 解析 | Windows PE 和 Linux ELF 二进制文件的结构化解析 |
| 熵分析 | 段级熵计算，用于检测加壳/加密 |
| 加密检测 | 识别二进制文件中的加密常量和算法签名 |

---

## 核心特性

### 历史压缩（5 策略联动）

五种压缩策略协同工作：**AutoCompact**（75% 阈值，LLM 摘要）、**MicroCompact**（60% 阈值，截断大 tool result）、**ReactiveCompact**（95% 紧急压缩）、**ContextCollapse**（折叠连续 read/search 结果）、**EnhancedSnip**（相关性评分分组删除）。HistoryCompactor 按优先级编排策略。可用 `/compact` 手动触发。

### 并发 Tool 执行

LLM 一次返回多个 tool_use 时，StreamingToolExecutor 并行执行，支持并发度限制、预算裁剪和中断处理。

### API 重试与错误分类

所有 API 调用自动分类错误类型（AUTH/RATE_LIMIT/OVERLOADED/SERVER/NETWORK/INVALID_REQUEST），可重试错误（429/500/502/503/529）自动指数退避重试（1s/2s/4s），最多 3 次。不可重试错误（401/400）立即报错。

### 错误恢复

自动从常见 API 故障恢复：MAX_OUTPUT_TOKENS 触发重新压缩并重试，prompt 过长触发激进压缩，主模型失败可回退到备用模型。最多 3 次恢复尝试。

### Token 预算系统

三层预算控制：每查询预算、每任务预算、每 tool result 预算。超出预算的 tool result 自动截断并附加大小说明。

### Hooks 系统

在 `.crab/settings.json` 中配置事件钩子：

```json
{
  "hooks": [
    {"event": "PreToolUse", "matcher": "Bash", "command": "echo 'About to run Bash'"},
    {"event": "PostToolUse", "matcher": ".*", "command": "echo 'Tool finished'"}
  ]
}
```

Hook 返回非零退出码会阻止 tool 执行。支持 `HOOK_TOOL` 和 `HOOK_EVENT` 环境变量。支持 4 种事件：`PreToolUse`、`PostToolUse`、`PostSampling`（API 响应后）、`StopFailure`（stop hook 失败时）。Hook 结果包含执行耗时。

### 文件记忆系统

在 `.crab/memory/` 目录中存储持久化记忆，每个记忆是一个带 YAML frontmatter 的 .md 文件：

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

### v0.2.0 新增服务

| 服务 | 功能 |
|------|------|
| 5 策略压缩 | AutoCompact + MicroCompact + ReactiveCompact + ContextCollapse + EnhancedSnip |
| Token 预算 | BudgetTracker 三层预算 + TokenEstimator 快速估算 |
| 错误恢复 | ErrorRecovery（max_tokens/prompt_too_long/fallback） |
| StreamingToolExecutor | 流式 tool 编排 + 预算裁剪 |
| LSPServerManager | 多 LSP 服务器管理（按扩展名路由） |
| SkillSearchService | 本地 + 远程技能搜索 + 预取 |
| TeamMemorySync | 团队记忆同步 + secret scanning |
| FeatureFlags | 本地特性门控 |
| AnalyticsService | 本地文件日志分析 |
| NotifierService | 跨平台系统通知 |
| PreventSleepService | 长任务防休眠 |
| AutoDreamService | 后台记忆整合 |
| PromptSuggestionService | 提示词建议 |
| SessionTranscriptService | 会话转录导出 |

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

AI 回复完成后自动朗读，长文本自动截断到 500 字符。v0.2.0 新增 STT（语音转文字）：Windows 用 PowerShell Speech Recognition，Linux/macOS 用 whisper.cpp CLI。

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

## Team Mode（团队模式）

Team Mode 允许多个开发者同时连接到一台 CloseCrab 服务器，各自拥有独立的对话历史和完整工具访问权限。

### 工作原理

- **多客户端连接** -- 多台手机、笔记本或 PC 通过 CloseCrab-Web 连接到同一台服务器。每个客户端分配唯一 ID。
- **独立对话历史** -- 每个连接的客户端维护自己的对话上下文，互不干扰。
- **真正的并行推理** -- API 模式下，请求以并发 HTTP 调用方式分发；本地模式下，利用 llama.cpp 的 `n_parallel` 槽位同时为多个客户端推理。
- **游戏化** -- 内置排行榜追踪编码统计（代码行数、工具使用次数、修复 bug 数）。开发者达成里程碑时解锁成就。
- **共享知识库** -- 团队 Q&A 自动索引并可搜索。一个开发者解决了问题，解决方案对整个团队可见。
- **会话持久化** -- 协作会话可保存和恢复。即使服务器重启，也能从上次中断处继续。

### 快速开始

```bash
# 启动 CloseCrab 服务器（Team Mode 自动在端口 9002 启用）
closecrab --config config/config.yaml

# 从多台手机/PC 通过 CloseCrab-Web 连接
# 每个客户端获得唯一 ID、独立历史，并出现在排行榜上
```

### 配置

在 `config/config.yaml` 中添加：

```yaml
team:
  enabled: true
  port: 9002                    # Team Mode WebSocket 端口
  max_clients: 8                # 最大同时连接数
  leaderboard: true             # 启用游戏化排行榜
  shared_knowledge: true        # 启用共享 Q&A 知识库
  session_persistence: true     # 保存/恢复协作会话
```

---

## 架构

详见 [docs/architecture.md](docs/architecture.md)。

```
src/
├── main.cpp              # 入口，组件初始化，主循环
├── core/                 # QueryEngine, Message, AppState, HistoryCompactor, FileStateCache, CostTracker
├── api/                  # LocalLLM, Anthropic, OpenAI API 客户端, APIError, StreamParser
├── tools/                # 51 个工具实现（每个工具一个目录）
├── commands/             # 83 个斜杠命令（Git/Session/Advanced/Extended）
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
├── services/             # 14 个服务模块
└── utils/                # 日志, 字符串(UTF-8), UUID, ProcessRunner
```

### 新增服务模块

| 模块 | 文件 | 功能 |
|------|------|------|
| MemoryExtractor | memory/MemoryExtractor.h | 退出时自动从对话提取记忆 |
| PolicyLimits | core/PolicyLimits.h | 策略限制引擎（turn/cost/token 上限） |
| FileStateCache | core/FileStateCache.h | 基于 mtime 的文件读取缓存 |
| HistoryCompactor | core/HistoryCompactor.h | 自动历史压缩（75% 阈值） |
| HookManager | hooks/HookManager.h | PreToolUse/PostToolUse 事件钩子 |
| ProcessSandbox | security/ProcessSandbox.h | OS 级进程资源限制 |
| WebCache | tools/WebTools/WebTools.h | 15 分钟 TTL URL 缓存 |
| CronScheduler | tools/CronTools/CronTools.h | 5 字段 cron 表达式解析 + 调度 |
| 5 策略压缩 | core/CompactStrategies.h | AutoCompact + MicroCompact + ReactiveCompact + ContextCollapse + EnhancedSnip |
| Token 预算 | core/BudgetTracker.h | BudgetTracker 三层预算 + TokenEstimator 快速估算 |
| 错误恢复 | core/ErrorRecovery.h | ErrorRecovery（max_tokens/prompt_too_long/fallback） |
| StreamingToolExecutor | core/StreamingToolExecutor.h | 流式 tool 编排 + 预算裁剪 |
| LSPServerManager | lsp/LSPServerManager.h | 多 LSP 服务器管理（按扩展名路由） |
| SkillSearchService | services/SkillSearchService.h | 本地 + 远程技能搜索 + 预取 |
| TeamMemorySync | services/TeamMemorySync.h | 团队记忆同步 + secret scanning |
| FeatureFlags | services/FeatureFlags.h | 本地特性门控 |
| AnalyticsService | services/AnalyticsService.h | 本地文件日志分析 |
| NotifierService | services/NotifierService.h | 跨平台系统通知 |
| PreventSleepService | services/PreventSleepService.h | 长任务防休眠 |
| AutoDreamService | services/AutoDreamService.h | 后台记忆整合 |
| PromptSuggestionService | services/PromptSuggestionService.h | 提示词建议 |
| SessionTranscriptService | services/SessionTranscriptService.h | 会话转录导出 |

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

### .crab/settings.json

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
out\build\x64-release\closecrab.exe
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
| MCP 服务器断开 | 检查 `.crab/settings.json` 配置，用 `/mcp` 查看状态 |
| `/doctor` 显示工具 "not found" | 安装缺失工具（git, rg, node, python, gh）并确保在 PATH 中 |
| CUDA 内存不足 | 减小 `llm.max_tokens` 或使用更小的量化模型 |

---

## 许可证

MIT



