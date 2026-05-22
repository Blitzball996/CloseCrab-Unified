# CloseCrab-Unified 功能需求清单

> 对标 JackProAi-claudecode3.1，按优先级排列
> ✅ = 已完成  ⬜ = 待实现

---

## P0 — 严重影响使用体验

| # | 功能 | 状态 | 说明 |
|---|------|------|------|
| 1 | 实时流式输出 | ✅ | BashTool/PowerShell执行时逐行显示输出 |
| 2 | Monitor工具 | ✅ | 监控长时间运行进程(dev server/build)，后台流式 |
| 3 | 智能权限分类器 | ✅ | BashClassifier: SAFE/WRITE/DANGEROUS分类+regex危险模式 |
| 4 | 大输出截断+持久化 | ✅ | >30KB自动存盘，返回预览给LLM |
| 5 | 键盘选择权限提示 | ⬜ | 用上下键选择y/n/a，不需要打字（类似Claude Code的Ink UI） |
| 6 | /model实际切换模型 | ⬜ | 切换后重建APIClient实例，支持运行时换模型 |
| 7 | 历史对话列表+恢复 | ⬜ | /resume列出所有历史对话(时间+摘要)，选择恢复 |

---

## P1 — 影响高级使用

| # | 功能 | 状态 | 说明 |
|---|------|------|------|
| 8 | TaskOutput/TaskStop | ✅ | 查看后台任务输出、停止任务 |
| 9 | Assistant模式 | ⬜ | 长命令(>15s)自动后台化，保持对话响应 |
| 10 | MCP OAuth认证 | ⬜ | 连接需要OAuth的MCP服务器(GitHub/Slack等) |
| 11 | 富文本UI框架 | ⬜ | 折叠工具输出、进度条、彩色diff、表格渲染 |
| 12 | Git操作追踪 | ⬜ | 自动记录git/PR操作，统计commit/push频率 |
| 13 | Prompt缓存优化 | ⬜ | 精细控制cache_control位置，最大化缓存命中 |
| 14 | 工具输出折叠 | ⬜ | 长输出默认折叠，用户可展开查看 |
| 15 | 命令自动补全 | ⬜ | Tab补全命令名和参数 |

---

## P2 — 提升体验

| # | 功能 | 状态 | 说明 |
|---|------|------|------|
| 16 | 远程会话 | ⬜ | WebSocket远程连接，多设备同步对话 |
| 17 | Computer Use | ⬜ | 屏幕截图+鼠标键盘操作(类似Anthropic computer use) |
| 18 | DXT插件格式 | ⬜ | 支持JackProAi的DXT插件包格式 |
| 19 | 自动更新 | ⬜ | 检查新版本并自动下载更新 |
| 20 | Deep Link | ⬜ | URL scheme处理(closecrab://open?file=...) |
| 21 | 设置云同步 | ⬜ | 跨设备同步settings.json和权限规则 |
| 22 | 使用分析/遥测 | ⬜ | 本地统计：工具使用频率、token消耗趋势、常用命令 |
| 23 | 会话搜索 | ⬜ | 搜索历史对话内容(全文检索) |
| 24 | 多窗口/分屏 | ⬜ | 同时运行多个对话(类似tmux split) |

---

## P3 — 锦上添花

| # | 功能 | 状态 | 说明 |
|---|------|------|------|
| 25 | Buddy伴侣动画 | ⬜ | 终端ASCII动画伴侣(JackProAi的CompanionSprite) |
| 26 | 提示建议 | ⬜ | 根据上下文推荐下一步操作(PromptSuggestion) |
| 27 | Magic Docs | ⬜ | 自动生成项目文档(MagicDocs服务) |
| 28 | Auto Dream | ⬜ | 空闲时自动整理记忆/优化上下文(autoDream) |
| 29 | Team Memory Sync | ⬜ | 多人协作时同步记忆和上下文 |
| 30 | Agent Summary | ⬜ | 子代理执行完后自动生成摘要 |
| 31 | Tips系统 | ⬜ | 根据使用习惯显示操作提示 |

---

## P4 — 长期规划

| # | 功能 | 状态 | 说明 |
|---|------|------|------|
| 32 | VS Code扩展 | ⬜ | 作为VS Code插件运行(MCP SDK控制传输) |
| 33 | JetBrains扩展 | ⬜ | IntelliJ/PyCharm/CLion集成 |
| 34 | Web UI | ⬜ | 浏览器界面(类似claude.ai/code) |
| 35 | 移动端 | ⬜ | iOS/Android客户端通过WebSocket连接 |
| 36 | 多模型路由 | ⬜ | 根据任务复杂度自动选择模型(简单用haiku,复杂用opus) |
| 37 | 本地微调 | ⬜ | 基于用户习惯微调本地模型的system prompt |
| 38 | 协作模式 | ⬜ | 多人同时连接同一个CloseCrab实例 |

---

## 已完成的基础功能（之前修复的bug）

| 功能 | 状态 |
|------|------|
| BashTool安全绕过修复 | ✅ |
| GrepTool命令注入修复 | ✅ |
| SessionManager空指针修复 | ✅ |
| MemorySystem空指针修复 | ✅ |
| Config::getBool崩溃修复 | ✅ |
| TokenEstimator UTF-8修复 | ✅ |
| PermissionEngine空pattern修复 | ✅ |
| RemoteAPIClient SSL+调试路径修复 | ✅ |
| PowerShell超时+EncodedCommand | ✅ |
| PowerShell CLIXML过滤 | ✅ |
| 权限提示"a"(approve all)选项 | ✅ |
| UI显示命令内容和输出 | ✅ |
| 安全命令白名单扩展 | ✅ |

---

## 统计

- **总需求**: 38项
- **已完成**: 17项 (45%)
- **P0完成率**: 4/7 (57%)
- **测试覆盖**: 17个项目，14种语言，3675行代码验证通过
