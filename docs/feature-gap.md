# CloseCrab-Unified 未整合功能清单

> 基于 JackProAi-claudecode3.1 (95 命令, 42 工具, 21 服务) 与 CloseCrab-Unified (51 命令, 42 工具) 的逐项对比
> 生成日期: 2026-04-07

## 总览

| 类别 | JackProAi | CloseCrab | 差距 |
|------|-----------|-----------|------|
| 命令 | 95 | 66 | 29 个未整合（大部分为内部/平台特定） |
| 工具 | 42 | 42 | 0（全部已整合） |
| 服务模块 | 21 | ~16 | 5 个未整合（需云端基础设施） |

**工具层面已 100% 整合**，差距主要在命令和服务模块。

---

## 一、未整合的命令 (44 个)

### 高价值（建议整合）

| 命令 | 功能 | 难度 |
|------|------|------|
| `/issue` | 创建/查看 GitHub Issue (gh CLI) | 低 |
| `/rename` | 重命名当前会话 | 低 |
| `/copy` | 复制最后回复到剪贴板 | 低 |
| `/summary` | 生成对话摘要 | 低 |
| `/usage` | 详细 token 使用统计 | 低 |
| `/effort` | 设置推理努力程度 (low/medium/high) | 低 |
| `/thinkback` | 回放 AI 的思考过程 | 中 |
| `/keybindings` | 查看/自定义快捷键 | 中 |
| `/output-style` | 切换输出风格 (markdown/plain/json) | 中 |
| `/color` | 自定义颜色配置 | 低 |
| `/tag` | 给会话打标签 | 低 |
| `/feedback` | 提交反馈 | 低 |
| `/rewind` | 撤销最后 N 轮对话 | 中 |
| `/autofix-pr` | 自动修复 PR 中的问题 | 中 |
| `/pr_comments` | 查看 PR 评论 | 低 |

### 中等价值（按需整合）

| 命令 | 功能 | 难度 |
|------|------|------|
| `/login` `/logout` | OAuth 认证流程 | 高 |
| `/oauth-refresh` | 刷新 OAuth token | 高 |
| `/privacy-settings` | 隐私设置管理 | 中 |
| `/rate-limit-options` | 限流策略配置 | 中 |
| `/release-notes` | 显示版本更新日志 | 低 |
| `/upgrade` | 自动升级到最新版 | 高 |
| `/stats` | 详细使用统计 | 中 |
| `/passes` | 多轮自动执行 | 中 |
| `/bughunter` | 自动 bug 搜索模式 | 高 |
| `/sandbox-toggle` | 快速切换沙箱模式 | 低 |
| `/reload-plugins` | 热重载插件 | 中 |

### 低价值 / 平台特定（可跳过）

| 命令 | 功能 | 原因 |
|------|------|------|
| `/chrome` | 浏览器集成 | 需要 Chrome DevTools Protocol |
| `/desktop` | 桌面应用集成 | 需要 Electron/Tauri |
| `/mobile` | 移动端集成 | 需要移动端 app |
| `/ide` | IDE 集成 | 需要 VS Code/JetBrains 插件 |
| `/teleport` | 远程会话传送 | 需要云端基础设施 |
| `/stickers` | 贴纸/表情 | 装饰性功能 |
| `/install-github-app` | 安装 GitHub App | 需要 OAuth 基础设施 |
| `/install-slack-app` | 安装 Slack App | 需要 OAuth 基础设施 |
| `/ant-trace` | Anthropic 内部追踪 | 内部调试工具 |
| `/heapdump` | 内存转储 | Node.js 特有 |
| `/debug-tool-call` | 调试 tool 调用 | 开发者工具 |
| `/mock-limits` | 模拟限制 | 测试工具 |
| `/reset-limits` | 重置限制 | 测试工具 |
| `/break-cache` | 清除缓存 | 调试工具 |
| `/ctx_viz` | 上下文可视化 | 调试工具 |
| `/btw` | 随机提示 | 装饰性功能 |
| `/good-claude` | 表扬 AI | 装饰性功能 |
| `/thinkback-play` | 回放思考动画 | 依赖 thinkback |
| `/backfill-sessions` | 回填历史会话 | 数据迁移工具 |
| `/remote-env` `/remote-setup` | 远程环境配置 | 需要云端基础设施 |
| `/terminalSetup` | 终端初始化 | Node.js 特有 |
| `/onboarding` | 新手引导 | UI 密集型 |
| `/extra-usage` | 额外用量信息 | Anthropic 特有 |
| `/perf-issue` | 性能问题报告 | 内部工具 |

---

## 二、未整合的服务模块 (8 个)

| 服务 | 功能 | 整合价值 | 难度 |
|------|------|---------|------|
| **teamMemorySync** | 团队记忆同步（多人共享记忆） | 高 | 高 |
| **SessionMemory** | 高级会话记忆管理（自动提取、分类、过期） | 高 | 中 |
| **extractMemories** | 从对话中自动提取值得记住的信息 | 高 | 中 |
| **analytics** | 使用分析（Datadog、GrowthBook feature flags） | 中 | 高 |
| **policyLimits** | 策略限制引擎（token 预算、费用上限） | 中 | 中 |
| **settingsSync** | 设置同步（跨设备） | 中 | 高 |
| **oauth** | OAuth 2.0 认证流程 | 中 | 高 |
| **tips** | 使用提示系统 | 低 | 低 |
| **AgentSummary** | Agent 执行摘要 | 低 | 低 |
| **MagicDocs** | 智能文档生成 | 低 | 中 |
| **PromptSuggestion** | 提示词建议 | 低 | 中 |
| **autoDream** | 自动梦境（后台任务） | 低 | 中 |
| **toolUseSummary** | Tool 使用统计摘要 | 低 | 低 |
| **remoteManagedSettings** | 远程管理设置 | 低 | 高 |

---

## 三、架构级差距

### 已整合的核心架构

- [x] QueryEngine 多轮对话循环
- [x] 3 种 API Provider (local/anthropic/openai)
- [x] 42 个 Tool 全部整合
- [x] 权限系统 (default/auto/bypass)
- [x] MCP 协议客户端
- [x] 多 Agent 系统 (5 种类型)
- [x] Coordinator 多 Agent 协调
- [x] 历史压缩 (SnipCompact)
- [x] 并发 Tool 执行
- [x] API 重试 + 错误分类
- [x] Hooks 系统
- [x] 文件记忆系统
- [x] 插件 + Skill 系统
- [x] Vim 模式
- [x] 语音输出
- [x] 会话持久化
- [x] 进程沙箱
- [x] 终端 UI (Spinner/Markdown/Table)
- [x] Cron 调度器
- [x] Bridge 远程执行
- [x] CostTracker 费用追踪
- [x] FileStateCache 文件缓存

### 未整合的架构特性

| 特性 | 说明 | 影响 |
|------|------|------|
| **Tool Search + defer_loading** | Anthropic beta API 延迟加载 tool | 需要官方 API，中转站不支持 |
| **Prompt Caching** | `cache_control: ephemeral` 缓存 system prompt | 需要 Anthropic API 支持 |
| **React/Ink 终端 UI** | 富交互组件（选择框、进度条、布局） | C++ 无等价库，需自行实现 |
| **OAuth 2.0 完整流程** | 浏览器回调、token 刷新、安全存储 | 需要 HTTP server + 浏览器交互 |
| **Feature Flags** | GrowthBook 远程功能开关 | 需要云端服务 |
| **遥测/分析** | Datadog 集成、使用统计上报 | 需要云端服务 |
| **IDE 插件协议** | VS Code / JetBrains 扩展通信 | 需要各 IDE 的插件 SDK |
| **团队协作基础设施** | 共享记忆、设置同步、远程管理 | 需要后端服务器 |

---

## 四、建议整合优先级

### 第一批（低成本高价值，1-2 天） — DONE

```
/issue        — gh issue create/view                    ✅
/rename       — 重命名会话                              ✅
/copy         — 复制到剪贴板                            ✅
/summary      — 对话摘要                                ✅
/usage        — 详细 token 统计                         ✅
/effort       — 推理努力程度                            ✅
/color        — 颜色配置                                (merged into /theme)
/tag          — 会话标签                                ✅
/rewind       — 撤销对话                                ✅
/pr_comments  — PR 评论                                 ✅
```

### 第二批（中等成本，3-5 天） — DONE

```
/thinkback           — 思考回放                         ✅
/output-style        — 输出风格切换                     ✅
/autofix-pr          — 自动修复 PR                      ✅
/bughunter           — 自动 bug 搜索                    ✅
/passes              — 多轮自动执行                     ✅
extractMemories 服务  — 自动记忆提取                    ✅
SessionMemory 服务    — 高级会话记忆                    (integrated into FileMemoryManager)
policyLimits 服务     — 策略限制引擎                    ✅
```

### 第三批（高成本，按需）

```
OAuth 2.0 完整流程
团队记忆同步
设置同步
IDE 插件协议
分析/遥测
```

---

## 五、不建议整合的功能

以下功能是 Anthropic 内部工具、Node.js 特有、或需要云端基础设施，不适合 C++ 本地工具：

- `/ant-trace`, `/heapdump`, `/debug-tool-call`, `/mock-limits`, `/ctx_viz` — 内部调试
- `/chrome`, `/desktop`, `/mobile`, `/ide` — 需要各平台 SDK
- `/teleport`, `/remote-env`, `/remote-setup` — 需要云端基础设施
- `/install-github-app`, `/install-slack-app` — 需要 OAuth 基础设施
- `/stickers`, `/btw`, `/good-claude` — 装饰性功能
- analytics, remoteManagedSettings, settingsSync — 需要云端服务
