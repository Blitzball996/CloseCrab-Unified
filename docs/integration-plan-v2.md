# CloseCrab-Unified 第二轮深度整合计划

> 基于 claude-code-source v2.1.88 (TypeScript/Bun, 2034文件) 与 CloseCrab-Unified (C++17, 109文件) 的全量源码逐文件对比
> 生成日期: 2026-04-09
> 前置: 第一轮整合 (基于 JackProAi-claudecode3.1) 已全部完成

---

## 一、对比概览

| 维度 | CloseCrab-Unified (当前) | claude-code-source v2.1.88 |
|------|-------------------------|---------------------------|
| 语言 | C++17 | TypeScript (Bun) |
| 源文件数 | 109 | 2,034 |
| Tool 数量 | 42 | 48+ |
| Command 数量 | ~40 | 90+ |
| API Provider | 3 (local/anthropic/openai) | 1 (Anthropic, 含 OAuth) |
| UI 框架 | ANSI 终端 (Spinner/Markdown/Table) | React/Ink (100+ 组件) |
| 状态管理 | AppState 单例 | Zustand Store + Selectors |
| 传输层 | 无抽象 | SSE/WebSocket/Hybrid Transport |
| 认证 | API Key | OAuth 2.0 + PKCE + Keychain |
| 分析 | 无 | Datadog + 1P + GrowthBook |
| 压缩策略 | 单一 HistoryCompactor | 5 策略 (auto/micro/snip/reactive/collapse) |
| 独有功能 | RAG/SSD/Voice/本地LLM | OAuth/Analytics/Transport/Daemon/Proactive |

---

## 二、已完成 (第一轮整合成果)

所有 P0-P4 + Extra 项目均已完成，包括:
- QueryEngine 多轮 tool loop + 并发执行
- 42 Tool 完整实现 + Hooks 系统
- 40 Slash Command + Vim 模式
- HistoryCompactor + SnipCompact
- API 重试/退避/错误分类
- 记忆系统 (SQLite + FileMemoryManager)
- 插件/Skill 系统
- Coordinator 多 Agent 协调
- Bridge 远程执行 + Voice 引擎 + Sandbox
- 单元测试 + 性能优化 + 跨平台

---

## 三、逐模块对比与新增功能清单

### 3.1 QueryEngine — 核心引擎差异

| 功能 | CloseCrab 现状 | claude-code-source 新增 | 优先级 |
|------|---------------|------------------------|--------|
| 压缩策略 | 单一 HistoryCompactor | autoCompact + microCompact + snipCompact + reactiveCompact + contextCollapse 五策略联动 | ✅ P1 |
| Token 预算 | 无 | createBudgetTracker() + checkTokenBudget() + taskBudget 三层预算控制 | ✅ P1 |
| 错误恢复 | 基础 try-catch | MAX_OUTPUT_TOKENS 恢复循环 + FallbackTriggeredError + isPromptTooLong 检测 | ✅ P1 |
| Tool 执行 | std::async 并行 | StreamingToolExecutor 编排器 + applyToolResultBudget() 结果裁剪 | ✅ P2 |
| Hook 扩展 | PreToolUse/PostToolUse | + PostSampling + StopFailure + StopHooks (含 duration 指标) | ✅ P2 |
| 记忆预取 | 无 | startRelevantMemoryPrefetch() + filterDuplicateMemoryAttachments() | ✅ P2 |
| Skill 发现 | 无 | startSkillDiscoveryPrefetch() + discoveredSkillNames 追踪 | ✅ P3 |
| 分析埋点 | 无 | logEvent() + queryCheckpoint() + headlessProfilerCheckpoint() | ✅ P3 |
| 特性门控 | 无 | feature() 编译期门控 (REACTIVE_COMPACT, CONTEXT_COLLAPSE, HISTORY_SNIP 等) | ✅ P3 |
| 查询配置 | 运行时参数 | buildQueryConfig() 不可变配置 + QuerySource 来源追踪 | ✅ P3 |

**关键新增方法 (需移植):**
- `autocompact()` — 智能自动压缩，含恢复循环
- `microcompact()` — 细粒度增量压缩
- `reactiveCompact()` — 响应式压缩 (token 超限时触发)
- `contextCollapse()` — 上下文折叠 (保留关键信息，折叠冗余)
- `createBudgetTracker()` / `checkTokenBudget()` — token 预算追踪与强制执行
- `StreamingToolExecutor.runTools()` — 流式 tool 编排
- `applyToolResultBudget()` — 单条 tool result 大小限制
- `executePostSamplingHooks()` / `handleStopHooks()` — 扩展 hook 生命周期

### 3.2 Message 系统差异

| 功能 | CloseCrab 现状 | claude-code-source 新增 | 优先级 |
|------|---------------|------------------------|--------|
| System 消息类型 | 3 种 (COMPACT_BOUNDARY, API_ERROR, LOCAL_COMMAND) | 16+ 种判别联合类型 (见下表) | ✅ P1 |
| 消息元数据 | 基础 flags | origin, permissionMode, mcpMeta, imagePasteIds, sourceToolAssistantUUID | ✅ P2 |
| 归一化消息 | 无 | NormalizedUserMessage / NormalizedAssistantMessage (API 发送前归一化) | ✅ P2 |
| 渲染消息 | 无 | GroupedToolUseMessage + CollapsedReadSearchGroup (UI 折叠) | ✅ P3 |
| 进度消息 | 无 | ProgressMessage<P> 泛型 + toolUseID/parentToolUseID 层级追踪 | ✅ P2 |
| 压缩元数据 | 无 | CompactMetadata (trigger, preTokens, messagesSummarized, preservedSegment) | ✅ P1 |

**新增 System 消息类型 (16 种):**
1. `SystemInformationalMessage` — info/warning/error/suggestion 四级
2. `SystemAPIErrorMessage` — API 错误 + 重试追踪
3. `SystemPermissionRetryMessage` — 权限重试 + 命令列表
4. `SystemBridgeStatusMessage` — Bridge 状态 + 升级提示
5. `SystemScheduledTaskFireMessage` — 定时任务触发通知
6. `SystemStopHookSummaryMessage` — Stop hook 执行摘要
7. `SystemTurnDurationMessage` — 轮次耗时 + 预算指标
8. `SystemAwaySummaryMessage` — 离开模式摘要
9. `SystemMemorySavedMessage` — 记忆保存通知
10. `SystemAgentsKilledMessage` — Agent 终止通知
11. `SystemApiMetricsMessage` — API 性能指标 (TTFT, OTPs)
12. `SystemLocalCommandMessage` — 本地命令输出
13. `SystemCompactBoundaryMessage` — 压缩边界 + 元数据
14. `SystemMicrocompactBoundaryMessage` — 微压缩边界
15. `SystemThinkingMessage` — 思考块标记
16. `SystemFileSnapshotMessage` — 文件快照 + 内容

### 3.3 Tool 系统差异

#### 3.3.1 新增 Tool (claude-code-source 有, CloseCrab 无)

| Tool | 功能 | 优先级 |
|------|------|--------|
| BriefTool | 附件/文件上传管理 | ✅ P3 |
| DiscoverSkillsTool | 运行时动态 Skill 发现 | ✅ P2 |
| ReviewArtifactTool | 生成物审查/验证 | ✅ P3 |
| SendUserFileTool | 向用户发送文件 | ✅ P3 |
| SnipTool | 代码片段管理/历史裁剪 | ✅ P2 |
| TerminalCaptureTool | 终端输出捕获 | ✅ P3 |
| VerifyPlanExecutionTool | 计划执行验证 | ✅ P2 |
| MonitorTool | 监控工具 (stub) | ✅ P4 |
| OverflowTestTool | 溢出测试 | ✅ P4 |
| TungstenTool | Tungsten 集成 | ✅ P4 |
| WorkflowTool | 工作流执行 | ✅ P3 |
| WebBrowserTool | 浏览器自动化 (Playwright) | ✅ P2 |

#### 3.3.2 Tool 架构差异

| 方面 | CloseCrab 现状 | claude-code-source 新增 | 优先级 |
|------|---------------|------------------------|--------|
| Schema 系统 | nlohmann::json 运行时 | Zod 编译期类型安全 + lazySchema() | ✅ P3 |
| 执行模型 | 同步阻塞 | async/await + Promise | 已有 (std::async) |
| 权限系统 | ALLOWED/DENIED/ASK_USER | + 规则引擎 + 拒绝追踪 + 多工作目录权限 | ✅ P2 |
| 结果类型 | ToolResult{success,content,data,error} | + newMessages 注入 + contextModifier 回调 + mcpMeta | ✅ P2 |
| 延迟加载 | 无 | shouldDefer + alwaysLoad + ToolSearch 发现 | ✅ P3 |
| 中断行为 | 无 | interruptBehavior() ('cancel'/'block') | ✅ P2 |
| 搜索分类 | 无 | isSearchOrReadCommand() 用于 UI 折叠 | ✅ P3 |
| 进度追踪 | string 回调 | 泛型 ToolCallProgress<P> 结构化进度 | ✅ P2 |
| 上下文 | ToolContext 基础 | + fileHistory + attribution + contentReplacement + nestedMemory + queryChain | ✅ P2 |

### 3.4 Command 系统差异

#### 3.4.1 新增 Command (claude-code-source 有, CloseCrab 无, 约 50+ 个)

**高优先级 (核心功能):**

| Command | 功能 | 优先级 |
|---------|------|--------|
| /login, /logout | OAuth 认证登录/登出 | ✅ P1 |
| /config | 查看/修改 settings.json | ✅ P1 |
| /model | 运行时切换模型 | ✅ P1 |
| /cost | 费用追踪与显示 | ✅ P1 |
| /permissions | 权限管理 | ✅ P1 |
| /status | 系统状态总览 | ✅ P1 |
| /compact | 手动触发压缩 (已有但需增强) | ✅ P1 |
| /clear | 清空会话 | ✅ P1 |
| /help | 分类帮助 (已有但需增强) | ✅ P1 |

**中优先级 (增强功能):**

| Command | 功能 | 优先级 |
|---------|------|--------|
| /bridge | 远程控制对话框 | ✅ P2 |
| /buddy | 伙伴模式 | ✅ P2 |
| /fork | 分叉会话 | ✅ P2 |
| /peers | 对等会话管理 | ✅ P2 |
| /workflows | 工作流脚本执行 | ✅ P2 |
| /security-review | 安全审查 | ✅ P2 |
| /sandbox-toggle | 沙箱模式切换 | ✅ P2 |
| /remote-setup, /remote-env | 远程环境配置 | ✅ P2 |
| /keybindings | 键绑定自定义 | ✅ P2 |
| /privacy-settings | 隐私设置 | ✅ P2 |
| /rate-limit-options | 速率限制选项 | ✅ P2 |
| /commit-push-pr | 一键提交+推送+PR | ✅ P2 |
| /release-notes | 生成发布说明 | ✅ P2 |
| /stats | 统计信息 | ✅ P2 |

**低优先级 (辅助/内部):**

| Command | 功能 | 优先级 |
|---------|------|--------|
| /assistant | 守护进程查看器 | ✅ P3 |
| /desktop, /mobile, /chrome | 平台集成 | ✅ P3 |
| /teleport | 会话迁移 | ✅ P3 |
| /onboarding | 首次使用引导 | ✅ P3 |
| /feedback | 发送反馈 | ✅ P3 |
| /upgrade | 升级 CLI | ✅ P3 |
| /color | 颜色设置 | ✅ P3 |
| /stickers | 贴纸 | ✅ P4 |
| /ctx_viz | 上下文可视化 | ✅ P3 |
| /debug-tool-call | 调试 tool 调用 | ✅ P3 |
| /heapdump | 内存转储 | ✅ P4 |
| /perf-issue | 性能问题 | ✅ P3 |
| /ant-trace | 追踪调试 | ✅ P4 |
| /good-claude | 正反馈 | ✅ P4 |
| /btw | 顺便说一下 | ✅ P4 |
| /break-cache | 缓存清除 | ✅ P3 |
| /backfill-sessions | 回填会话 | ✅ P4 |
| /mock-limits | 模拟限制 | ✅ P4 |
| /thinkback-play | 思考回放 | ✅ P3 |
| /install-github-app | GitHub App 安装 | ✅ P3 |
| /install-slack-app | Slack App 安装 | ✅ P3 |
| /oauth-refresh | OAuth 刷新 | ✅ P2 |
| /reset-limits | 重置限制 | ✅ P3 |
| /extra-usage | 额外用量 | ✅ P3 |

#### 3.4.2 Command 架构差异

| 方面 | CloseCrab 现状 | claude-code-source 新增 | 优先级 |
|------|---------------|------------------------|--------|
| 命令类型 | 统一 Command 抽象类 | 判别联合: prompt/local/jsx 三种类型 | ✅ P2 |
| 注册方式 | 单例 CommandRegistry | 记忆化 getCommands() + 动态加载 | ✅ P3 |
| 输出方式 | print 回调 | JSX 组件 + 流式回调 | ✅ P3 |
| 可用性 | isEnabled()/isHidden() | availability 数组 (claude-ai/console) + isCommandEnabled() | ✅ P2 |
| 特性门控 | 无 | feature() 编译期门控 | ✅ P3 |

### 3.5 服务层差异 (全新子系统)

claude-code-source 包含 24+ 个服务模块，CloseCrab 仅有 ~16 个。以下为逐项对比:

#### 3.5.1 压缩服务 (compact/)

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| autoCompact | 无 | 智能自动压缩 + 恢复循环 | ✅ P1 |
| reactiveCompact | 无 | 响应式压缩 (token 超限触发) | ✅ P1 |
| microCompact | 无 | 细粒度增量压缩 | ✅ P1 |
| cachedMicrocompact | 无 | 缓存微压缩 (避免重复计算) | ✅ P2 |
| apiMicrocompact | 无 | API 端微压缩 | ✅ P2 |
| sessionMemoryCompact | 无 | 会话记忆压缩 (结合 SessionMemory) | ✅ P2 |
| snipCompact | 已有 SnipCompact | snipProjection 投影 + grouping 分组 | ✅ P2 |
| postCompactCleanup | 无 | 压缩后清理 (移除孤立引用) | ✅ P2 |
| compactWarningHook | 无 | 压缩警告钩子 (通知用户) | ✅ P3 |
| cachedMCConfig / timeBasedMCConfig | 无 | 微压缩配置缓存 + 时间策略 | ✅ P3 |

#### 3.5.2 上下文折叠 (contextCollapse/)

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| operations | 无 | 折叠操作 (保留关键信息，折叠冗余 tool result) | ✅ P1 |
| persist | 无 | 折叠状态持久化 (跨 compact 保留) | ✅ P2 |

#### 3.5.3 记忆服务

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| SessionMemory | FileMemoryManager 已整合 | 周期性后台摘要 (forked subagent) | ✅ P2 |
| extractMemories | 已整合 | 查询循环结束时自动提取 | 已完成 |
| autoDream | 无 | 后台记忆整合 (时间门控 + 会话计数阈值) | ✅ P3 |
| teamMemorySync | 无 | 团队记忆同步 (per-repo delta + ETag + secret scanning) | ✅ P2 |

#### 3.5.4 Skill 搜索服务 (skillSearch/)

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| remoteSkillLoader | 无 | 远程 Skill 加载 (从 registry 拉取) | ✅ P2 |
| localSearch | 无 | 本地 Skill 搜索 (目录扫描 + 匹配) | ✅ P2 |
| prefetch | 无 | Skill 预取 (会话启动时) | ✅ P3 |
| remoteSkillState | 无 | 远程 Skill 状态追踪 | ✅ P3 |

#### 3.5.5 分析与监控

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| analytics (Datadog + 1P) | 无 | 事件日志 + 路由 + killswitch | ✅ P3 |
| GrowthBook feature gates | 无 | 远程特性门控 | ✅ P3 |
| diagnosticTracking | 无 | 诊断追踪 | ✅ P3 |
| tokenEstimation | 无 | Token 估算 (不调 API 的快速估算) | ✅ P2 |

#### 3.5.6 工具执行服务 (tools/)

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| StreamingToolExecutor | std::async 并行 | 流式编排器 + 结果裁剪 | ✅ P2 |
| toolOrchestration | 无 | 工具编排 (依赖图 + 并行度控制) | ✅ P2 |
| toolHooks | PreToolUse/PostToolUse | + PostSampling + StopFailure 扩展 | ✅ P2 |

#### 3.5.7 LSP 服务 (lsp/)

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| LSPServerManager | 无 (LSPClient 单实例) | 多 LSP 服务器管理 (按文件扩展名路由) | ✅ P2 |
| LSPServerInstance | 无 | 单个 LSP 服务器实例生命周期 | ✅ P2 |
| LSPDiagnosticRegistry | 无 | 诊断注册表 (跨服务器聚合) | ✅ P2 |
| passiveFeedback | 无 | 被动反馈 (LSP 诊断 → 上下文注入) | ✅ P3 |

#### 3.5.8 其他服务

| 服务 | CloseCrab 现状 | claude-code-source | 优先级 |
|------|---------------|-------------------|--------|
| OAuth 2.0 (PKCE) | 无 | 浏览器回调 + token 刷新 + Keychain | ✅ P3 |
| settingsSync | 无 | 跨设备设置同步 (增量上传/下载) | ✅ P3 |
| remoteManagedSettings | 无 | 远程管理设置 + 安全检查 | ✅ P3 |
| PromptSuggestion | 无 | 提示词建议 + 推测 | ✅ P3 |
| MagicDocs | 无 | 智能文档生成 | ✅ P3 |
| AgentSummary | 无 | Agent 执行摘要 (coordinator 模式周期性摘要) | ✅ P3 |
| toolUseSummary | 无 | Tool 使用统计摘要 | ✅ P3 |
| tips | 无 | 使用提示系统 (注册 + 调度 + 历史) | ✅ P4 |
| awaySummary | 无 | 离开模式摘要 | ✅ P3 |
| sessionTranscript | 无 | 会话转录管理 | ✅ P3 |
| notifier | 无 | 通知服务 (系统通知) | ✅ P3 |
| preventSleep | 无 | 防止系统休眠 (长任务时) | ✅ P3 |
| rateLimitMessages | 无 | 速率限制消息格式化 | ✅ P2 |
| voice (STT 侧) | 仅 TTS 可用 | voiceKeyterms + voiceStreamSTT 流式语音识别 | ✅ P3 |
| mcpServerApproval | 无 | MCP 服务器审批 UI | ✅ P3 |

#### 3.5.9 MCP 服务增强

| 子模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|--------|---------------|-------------------|--------|
| InProcessTransport | 无 | 进程内 MCP 传输 | ✅ P3 |
| SdkControlTransport | 无 | SDK 控制传输 | ✅ P3 |
| elicitationHandler | 无 | MCP 引出处理 (交互式参数收集) | ✅ P2 |
| officialRegistry | 无 | 官方 MCP 注册表 (服务器发现) | ✅ P2 |
| channelPermissions | 无 | MCP 通道权限管理 | ✅ P2 |
| envExpansion | 无 | 环境变量展开 (MCP 配置中) | ✅ P3 |

### 3.6 状态管理差异

| 方面 | CloseCrab 现状 | claude-code-source | 优先级 |
|------|---------------|-------------------|--------|
| 状态模型 | AppState 单例 (直接成员访问) | Zustand Store + Selectors (响应式) | ✅ P3 |
| 变更通知 | 无 | onChangeAppState 监听器 + 自动触发 | ✅ P3 |
| 选择器 | 无 | selectors.ts (派生状态计算) | ✅ P3 |
| 团队视图 | 无 | teammateViewHelpers (多 Agent 状态聚合) | ✅ P3 |

### 3.7 Bridge/传输层差异

| 方面 | CloseCrab 现状 | claude-code-source | 优先级 |
|------|---------------|-------------------|--------|
| 传输抽象 | BridgeClient 单一 WebSocket | v1 HybridTransport (WS+POST) / v2 SSETransport+CCRClient | ✅ P2 |
| 会话管理 | 无 | sessionRunner + createSession + codeSessionApi | ✅ P2 |
| 对等会话 | 无 | peerSessions (多会话并行) | ✅ P3 |
| JWT 刷新 | 无 | jwtUtils (自动 token 刷新调度) | ✅ P2 |
| 可信设备 | 无 | trustedDevice (设备注册 + 验证) | ✅ P3 |
| 容量唤醒 | 无 | capacityWake (空闲唤醒管理) | ✅ P3 |
| 刷新门控 | 无 | flushGate (消息批量发送控制) | ✅ P3 |
| 权限回调 | 无 | bridgePermissionCallbacks (远程权限确认) | ✅ P2 |
| 附件处理 | 无 | inboundAttachments (远程文件传输) | ✅ P3 |
| Webhook 清洗 | 无 | webhookSanitizer (安全过滤) | ✅ P3 |

### 3.8 工具层关键差异 (utils/)

claude-code-source 有 563 个工具文件，CloseCrab 约 20 个。关键差距:

| 工具模块 | CloseCrab 现状 | claude-code-source | 优先级 |
|----------|---------------|-------------------|--------|
| bash/ (AST 解析) | ProcessRunner 简单执行 | Bash AST 解析器 + shell 补全 + 引号处理 | ✅ P2 |
| git/ | 基础 git 命令 | 完整 git 工具集 (worktree, stash, rebase 等) | ✅ P2 |
| github/ | gh CLI 调用 | GitHub API 集成 (PR, Issue, Actions) | ✅ P2 |
| hooks/ | PreToolUse/PostToolUse | + post-sampling + stop hooks + duration 指标 | ✅ P2 |
| memory/ | FileMemoryManager | 记忆管理工具集 (过滤、去重、预取) | ✅ P2 |
| messages/ | 基础消息构造 | 消息创建 + 操作 + 归一化 | ✅ P2 |
| model/ | 3 provider 切换 | 模型 provider 工具集 (能力检测、token 计数) | ✅ P2 |
| permissions/ | PermissionEngine | 文件系统访问控制 + 多工作目录权限 | ✅ P2 |
| settings/ | SettingsManager | 设置管理 + 缓存 + 合并策略 | ✅ P3 |
| shell/ | 无 | Shell 工具集 (环境检测、路径解析) | ✅ P3 |
| sandbox/ | ProcessSandbox | 沙箱工具集 (资源限制、网络隔离) | ✅ P3 |
| secureStorage/ | 无 | 安全存储 (Keychain/Credential Manager) | ✅ P3 |
| task/ | TaskTool 系列 | 任务工具集 (依赖图、进度追踪) | ✅ P3 |
| telemetry/ | 无 | 遥测工具集 | ✅ P4 |
| deepLink/ | 无 | 深度链接 (从浏览器/IDE 打开会话) | ✅ P4 |
| computerUse/ | 无 | 计算机使用 (屏幕截图、鼠标键盘控制) | ✅ P4 |

---

## 四、实施路线图

### Phase 1: 核心引擎增强 (P1, 建议 1-2 周)

```
1. 压缩策略联动
   - 实现 autoCompact + reactiveCompact + microCompact
   - 实现 contextCollapse 操作 + 持久化
   - 集成 CompactMetadata 追踪
   - 增强现有 SnipCompact (加入 snipProjection + grouping)

2. Token 预算系统
   - 实现 BudgetTracker (createBudgetTracker / checkTokenBudget / taskBudget)
   - 实现 tokenEstimation 快速估算
   - 集成 applyToolResultBudget 结果裁剪

3. Message 系统扩展
   - 实现 16 种 System 消息类型 (判别联合)
   - 实现 CompactMetadata 元数据
   - 实现 NormalizedMessage 归一化

4. 错误恢复增强
   - 实现 MAX_OUTPUT_TOKENS 恢复循环
   - 实现 FallbackTriggeredError + isPromptTooLong 检测
   - 增强 API 错误分类 (rateLimitMessages)
```

### Phase 2: 工具与服务增强 (P2, 建议 2-3 周)

```
1. Tool 系统
   - 实现 StreamingToolExecutor 编排器
   - 实现 ToolCallProgress<P> 结构化进度
   - 实现 interruptBehavior ('cancel'/'block')
   - 实现 newMessages 注入 + contextModifier 回调
   - 增强权限系统 (规则引擎 + 拒绝追踪 + 多工作目录)
   - 实现 ToolContext 扩展 (fileHistory, attribution, queryChain)

2. 新增 Tool
   - DiscoverSkillsTool (运行时 Skill 发现)
   - SnipTool (代码片段管理)
   - VerifyPlanExecutionTool (计划验证)
   - WebBrowserTool (Playwright 浏览器自动化)

3. 服务层
   - skillSearch 服务 (远程加载 + 本地搜索 + 预取)
   - teamMemorySync (团队记忆同步 + secret scanning)
   - LSPServerManager (多 LSP 服务器管理)
   - MCP 增强 (elicitationHandler + officialRegistry + channelPermissions)
   - tokenEstimation 服务

4. Hook 扩展
   - PostSampling hook
   - StopFailure hook
   - StopHooks (含 duration 指标)

5. Bridge 传输层增强
   - 传输抽象 (v1/v2 切换)
   - JWT 自动刷新
   - 权限回调
```

### Phase 3: 辅助功能 (P3, 按需)

```
1. 新增 Command (~50 个，按 3.4.1 表格优先级)
2. 状态管理响应式改造
3. autoDream 后台记忆整合
4. AgentSummary / PromptSuggestion / MagicDocs
5. OAuth 2.0 完整流程
6. 安全存储 (Keychain)
7. 通知服务 / 防休眠
8. 会话转录管理
```

### Phase 4: 平台与生态 (P4, 长期)

```
1. 分析/遥测 (Datadog 替代方案)
2. Feature Flags (本地实现)
3. IDE 插件协议
4. 深度链接
5. 计算机使用 (屏幕截图/鼠标控制)
```

---

## 五、CloseCrab-Unified 假实现 / 未完成功能清单

以下功能在代码中存在但实际为 stub 或 placeholder:

| 文件 | 行号 | 问题 | 严重程度 |
|------|------|------|---------|
| `src/tools/LSPTool/LSPTool.h` | 26-38 | **完全假实现**: `call()` 不连接任何 LSP 服务器，仅返回 "LSP server not connected" 字符串 | ✅ 已修复 |
| `src/voice/VoiceEngine.h` | 48-58 | **STT 未实现**: `startListening()` 设置 `listening_=true` 并记录日志，但从未实际捕获音频或调用 Whisper.cpp。TTS (speak) 正常工作 | ✅ 已修复 |
| `src/ssd/SSDExpertStreamer.cpp` | 879-892 | **GGUF 解析为 placeholder**: `loadFromGGUF()` 将所有 expert offset 和 size 设为 0，需要用 packed expert 文件替代 | ✅ 已修复 |
| `src/commands/ExtendedCommands.h` | 356-368 | **Coordinator 命令使用 workaround**: 不直接调用 Coordinator API，而是构造 prompt 通过 QueryEngine 提交 | ✅ 已修复 |
| `src/main.cpp` | 360-365 | **Dummy API 客户端**: 无配置时回退到空 API key 的 RemoteAPIClient，任何实际调用都会失败 | 低 (设计如此) |

### 建议修复优先级:

1. **LSPTool** — 应实现真正的 LSP 连接，参考 claude-code-source 的 LSPServerManager 多服务器架构
2. **VoiceEngine STT** — 需集成 Whisper.cpp 音频捕获管线 (WASAPI/PulseAudio → Whisper → callback)
3. **SSDExpertStreamer GGUF** — 实现完整的 GGUF tensor info 解析，提取 expert offset/size
4. **Coordinator 命令** — 重构为直接调用 Coordinator::decompose() API，而非通过 prompt workaround
