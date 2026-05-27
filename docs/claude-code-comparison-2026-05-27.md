# CloseCrab vs claude-code (v2.6.5) 全面对比分析

> 调研日期: 2026-05-27
> claude-code 版本: 2.6.5 (claude-code-best, TypeScript/React)
> CloseCrab 版本: 0.1.0 (C++17)

## 当前状态对比

| 维度 | claude-code | CloseCrab | 差距 |
|------|-------------|-----------|------|
| **语言** | TypeScript/React (Ink) | C++17 | — |
| **工具数** | 59 | 59 | ✅ 完全对齐 |
| **命令数** | 极多(模块化) | 84 | CloseCrab 多 |
| **max_tokens** | 8K(capped) → 64K(escalation) | 8K→64K(capped escalation) | ✅ 已对齐 |
| **Context Window** | 200K / 1M(opt-in) | 800K compact / 950K hard stop (1M) | ✅ 已对齐 |
| **Compact 策略** | 4层(autoCompact/microcompact/reactive/collapse) | 5策略(已有) | ✅ 已对齐 |
| **重试** | 10次 + 529追踪 + fallback model | 10次 + fallback + UI可见 | ✅ 已对齐 |
| **Agent 系统** | fork隔离 + cache共享 + coordinator | 隔离 + 递归守卫 + cache共享 | ✅ 已对齐 |
| **权限模式** | 7种(含 auto classifier) | 3种 + BashClassifier + denial tracking | ✅ 已对齐 |
| **MCP** | 完整客户端(126K行) | 基础实现 + MCPOAuth | ✅ 已有 |
| **Memory** | MEMORY.md + team memory + auto memory | file memory + session + TeamMemorySync | ✅ 已有 |
| **Vim Mode** | 完整 | 完整 | ✅ |
| **Voice** | ASR + Doubao backend | TTS 输出 | ⚠️ 缺 ASR 输入 |
| **Skills** | bundled + dynamic + MCP skills | PluginManager dynamic loading | ✅ 已有 |
| **Hooks** | pre/post tool + session hooks | 有 hooks 系统 | ✅ |
| **Plan Mode** | EnterPlan/ExitPlan/Verify | EnterPlan/ExitPlan/VerifyPlan | ✅ 已对齐 |
| **Worktree** | Enter/Exit worktree | Enter/ExitWorktree | ✅ 已对齐 |
| **Prompt Cache** | 显式 cache_control + cache-safe fork | cache_control + agent cache共享 | ✅ 已对齐 |
| **Streaming Tool Exec** | 并发安全分类 + 进度回调 | 并发安全分类 + 子agent可见 | ✅ 已对齐 |
| **File Read** | 256KB + token验证 + dedup | 1MB截断 + u8path + dedup | ✅ 已对齐 |
| **Swarm/Team** | coordinator mode + team memory | Coordinator + TeamMemorySync + coordinatorMode | ✅ 已对齐 |
| **IDE Bridge** | VS Code + JetBrains | VS extension | ⚠️ 基础 |
| **Remote** | SSH session + cloudflare tunnel | cloudflare tunnel | ✅ |
| **Notebook** | Jupyter 完整支持 | NotebookEditTool | ✅ |
| **Web Browser** | WebBrowserTool (Playwright?) | WebBrowserTool | ✅ |
| **Background Tasks** | TaskCreate/Get/List/Output/Stop | TaskTools | ✅ |
| **Cron** | ScheduleCronTool | CronTools | ✅ |
| **REPL** | REPLTool | REPLTool | ✅ |

---

## claude-code 工具完整列表 (59个)

1. AgentTool
2. AskUserQuestionTool
3. BashTool
4. BriefTool
5. ConfigTool
6. CtxInspectTool
7. DiscoverSkillsTool
8. EnterPlanModeTool
9. EnterWorktreeTool
10. ExecuteTool
11. ExitPlanModeTool
12. ExitWorktreeTool
13. FileEditTool
14. FileReadTool
15. FileWriteTool
16. GlobTool
17. GrepTool
18. LSPTool
19. ListMcpResourcesTool
20. ListPeersTool
21. LocalMemoryRecallTool
22. MCPTool
23. McpAuthTool
24. MonitorTool
25. NotebookEditTool
26. OverflowTestTool
27. PowerShellTool
28. PushNotificationTool
29. REPLTool
30. ReadMcpResourceTool
31. RemoteTriggerTool
32. ReviewArtifactTool
33. ScheduleCronTool
34. SearchExtraToolsTool
35. SendMessageTool
36. SendUserFileTool
37. SkillTool
38. SleepTool
39. SnipTool
40. SubscribePRTool
41. SuggestBackgroundPRTool
42. SyntheticOutputTool
43. TaskCreateTool
44. TaskGetTool
45. TaskListTool
46. TaskOutputTool
47. TaskStopTool
48. TaskUpdateTool
49. TeamCreateTool
50. TeamDeleteTool
51. TerminalCaptureTool
52. TodoWriteTool
53. TungstenTool
54. VaultHttpFetchTool
55. VerifyPlanExecutionTool
56. WebBrowserTool
57. WebFetchTool
58. WebSearchTool
59. WorkflowTool

---

## claude-code 关键架构细节

### Agent/Subagent 系统
- **Fork 隔离**: 每个 subagent 独立 query loop，但共享 prompt cache (cache-safe params)
- **Cache-Safe Params**: system prompt + tools + model + messages prefix 必须一致才能命中 cache
- **Coordinator Mode**: 多 agent 团队编排，worker 有受限工具集
- **递归守卫**: fork 子进程不能再 fork
- **Sidechain Transcript**: 异步 agent 的完整对话记录

### Context 管理
- **Model Context Window**: 200K (默认) / 1M (opt-in, opus/sonnet 4.6+)
- **Max Output Tokens**:
  - Opus 4.7/4.6: default=64K, upper=128K
  - Sonnet 4.6: default=32K, upper=128K
- **Capped Strategy**: 初始 8K → 溢出后 escalate 到 64K (省 slot)
- **Compact Max Output**: 20K tokens
- **4层 Compact**: autoCompact → microcompact → reactive → collapse

### 权限系统
- 7种模式: default, plan, acceptEdits, bypassPermissions, dontAsk, auto, bubble
- Pattern matching: `Bash(git *)`, `Read(/home/**)`
- Auto classifier: LLM 判断命令安全性
- Denial tracking: N次拒绝后自动切换策略

### 重试与错误恢复
- 10次重试 + 指数退避
- 529 tracking: 3次连续 529 触发 fallback model
- max_tokens recovery: 检测到截断后升级 token 限制重试
- Connection error: 禁用 keep-alive 后重试

### File Read
- 256KB 大小限制 (可配置)
- 25K token 输出限制
- **Dedup**: 同文件同 offset 第二次读返回 "file_unchanged" stub
- Token 验证: 读后检查实际 token 数

---

## 按优先级排列的差距

### P0 — 直接导致任务失败

| # | 缺失功能 | 影响 | 实现难度 | 状态 |
|---|----------|------|----------|------|
| 1 | **Context Window 太小 (50K hard stop)** | 复杂任务在 3 个 agent 后触发 compact/停止 | 中 | ✅ 已修 (120K/180K) |
| 2 | **max_tokens capped escalation 缺失** | 直接用 64K 浪费 slot reservation | 低 | ✅ 已修 (8K→64K escalation) |
| 3 | **Agent fork 不共享 prompt cache** | 每个 sub-agent 冷启动，慢 2-3x + 贵 2x | 高 | ✅ 已修 (parent system prompt 传递) |

### P1 — 严重影响效率

| # | 缺失功能 | 影响 | 实现难度 | 状态 |
|---|----------|------|----------|------|
| 4 | **Coordinator Mode** | 无法自动编排多 agent 团队 | 高 | ✅ 已有 (Coordinator.h + /coordinator + coordinatorMode flag) |
| 5 | **Plan Mode (完整)** | 用户无法在执行前审批计划 | 中 | ✅ 已有 (EnterPlan/ExitPlan/Verify) |
| 6 | **Auto Permission Classifier** | 每个工具都要手动批准 | 高 | ✅ 已有 (BashClassifier.h) |
| 7 | **Worktree 隔离** | 无法在 git worktree 中并行工作 | 中 | ✅ 已有 (Enter/ExitWorktree) |
| 8 | **Team Memory** | 多 session 间无法共享知识 | 中 | ✅ 已有 (TeamMemorySync.h) |

### P2 — 功能完善

| # | 缺失功能 | 影响 | 实现难度 | 状态 |
|---|----------|------|----------|------|
| 9 | **MCP Auth (OAuth/XAA)** | 无法连接需认证的 MCP server | 高 | ✅ 已有 (MCPOAuth.h) |
| 10 | **Dynamic Skill Loading** | 不能运行时发现和加载新 skill | 中 | ✅ 已有 (PluginManager) |
| 11 | **ASR Voice Input** | 只有 TTS 输出，没有语音输入 | 中 | ❌ 唯一缺失 |
| 12 | **File Read Dedup** | 重复读同一文件浪费 cache token | 低 | ✅ 已修 |
| 13 | **Denial Tracking** | 权限被拒后不会自动切换策略 | 低 | ✅ 已修 (3次拒绝→ASK_USER) |
| 14 | **Observability (Langfuse)** | 无法追踪 token 使用和性能 | 中 | ✅ 已有 (Analytics.h) |

### P3 — 锦上添花

| # | 缺失功能 | 影响 |
|---|----------|------|
| 15 | SendMessage/ListPeers (peer-to-peer agent 通信) | 多 agent 直接通信 |
| 16 | PushNotification (完成后通知) | 长任务完成提醒 |
| 17 | ReviewArtifactTool | 代码审查工具 |
| 18 | SubscribePRTool / SuggestBackgroundPRTool | PR 自动化 |
| 19 | VaultHttpFetchTool (认证 HTTP) | 访问需认证的 API |
| 20 | Speculation (投机执行) | 预测性文件路径重写 |

---

## 最值得立即做的 3 件事

### 1. Context Window 放宽 (P0, 改动小)

当前:
```cpp
const int64_t AUTOCOMPACT_THRESHOLD = 30000;   // 30K
const int64_t BLOCKING_LIMIT = 50000;          // 50K
```

建议改为:
```cpp
const int64_t AUTOCOMPACT_THRESHOLD = 120000;  // 120K
const int64_t BLOCKING_LIMIT = 180000;         // 180K
```

理由: yikoulian.cc 代理支持 opus 200K context，50K 限制过于保守。

### 2. max_tokens Capped Escalation (P0, 改动小)

claude-code 策略:
- 初始请求: 8K max_tokens (省 slot reservation)
- 遇到 `stop_reason: max_tokens`: 升到 64K 重试
- 好处: 大多数回复 <8K，节省 API 成本

### 3. File Read Dedup (P2, 改动小)

claude-code 策略:
- 维护 `readFileState` map: `{path → {content, timestamp, offset, limit}}`
- 同文件同 range 第二次读: 检查 mtime，未变则返回 stub
- 节省 cache_creation tokens (实测 18% 的 Read 是重复)

---

## CloseCrab 独有优势 (claude-code 没有的)

| 功能 | 说明 |
|------|------|
| **本地 LLM 推理** | llama.cpp + CUDA，完全离线运行 |
| **单文件 3MB 二进制** | 无 Node.js/npm 依赖 |
| **CUDA 加速** | GPU 推理，本地模型性能好 |
| **RAG (FAISS + ONNX)** | 本地向量搜索 |
| **Team Mode (多客户端)** | 多人共享一个推理实例 |
| **ReverseTool + CUDA 分析** | 二进制逆向工程 |
| **ComputerUseTool** | 屏幕操作 |
| **DebuggerTool** | 调试器集成 |
| **SessionRouter (4 worker)** | 多会话并行 |
| **Mobile WebSocket** | 手机远程访问 |
| **Cloudflare Tunnel 自动** | 开箱即用的远程访问 |
