# JackProAi vs CloseCrab 深度对比表（v3/v3.1 全项）

> 目的：把 `fix-plan-2026-05-29-v3.md` 和 `fix-plan-2026-05-29-v3.1.md` 提到的每一项，逐项对照 JackProAi 的真实实现、CloseCrab 当前实现、我们刚改的实现，并列优缺点。后续再改代码时，以本表为依据。
>
> 调研基准：
> - JackProAi：`G:/CMakePJ/JackProAi-claudecode3.1/restored-src/src/`
> - CloseCrab：`G:/CMakePJ/CloseCrab-Unified/src/`
> - 当前已改：§4、§6、§10 部分、§5 部分。

---

## 总览表

| 项 | JackProAi 实现 | CloseCrab 原实现 | 最终状态 | 一致度 |
|---|---|---|---|---|
| §1 缓存断点 | system 静/动态拆分；message 1 个断点；tools 前缀稳定 | system 整块缓存；cwd/model 混入 | ✅ system 按 boundary 拆 2 块，cache_control 只在静态块 | 已对齐 |
| §2 tools 稳定 | session-cache schema + defer_loading overlay（聚焦核心集，非全量） | ALWAYS_LOAD 10 核心 + ToolSearch 发现（已是 JackProAi-style） | ✅ 保持原版（10 核心+发现）。⚠️ 我曾误改为"全发 58 工具"→模型被 Workflow 等元工具带偏、一调工具就停，已还原 | 已对齐(原版即对) |
| §3 API microcompact | A:服务端 context_management；B:本地确定性 replacement | 每轮 in-place 改写 = cache killer | ✅ B 路：clearedToolUseIds_ 冻结决策，单调不回退 | 已对齐(B路) |
| §4 BashTool prompt | 完整 prompt（工具替代+instructions+git+sleep） | 一句话 | ✅ 完整结构（除不适用的 sandbox 段） | 已对齐 |
| §5 rm 拦截 | path validate → dangerous removal → ask（流程前置） | 仅 PermissionEngine；auto-approve 可绕 | ✅ dangerous rm 下沉 PermissionEngine::check 最前，BYPASS 也拦 | 已对齐 |
| §6 readFileState | content/timestamp/offset/limit/isPartialView；写前检查 | 无写前必读 | ✅ hash+size+mtime+offset/limit+isPartialView+normalize+content fallback | 已对齐 |
| §7 tool_result 持久化 | 单结果 50K；ContentReplacementState 保字节 | AgentTool 2KB；Bash 截断 | ✅ OutputPersistence 50K/2KB 集中应用所有工具（本就齐） | 已对齐 |
| §8 fallback | 连续 529≥3 → 切 fallbackModel 重试 | 代码齐但配置空 | ✅ 填配置值，FALLBACK_THRESHOLD=3 生效 | 已对齐 |
| §9 防循环 | 无 sameToolStreak；靠 maxTurns/budget/prompt | sameToolStreak 有但阈值松(15) | 🟡 HOLD：删=回退；建议仅调阈值，未动 | CloseCrab 增强 |
| §10 shell spawn | spawn+cwd+env+quoteShellCommand+cwd tracking | CreateProcessA 硬编码；无 cwd/env/quoting | ✅ eval 单引号包裹+Windows argv 转义+>nul rewrite+cwd+env+pwd-P 持久化 | 已对齐(foreground) |

---

## §1 缓存断点策略（prompt cache）

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| system 拆分 | `constants/prompts.ts:560-575`：static sections 在 `SYSTEM_PROMPT_DYNAMIC_BOUNDARY` 前；dynamic sections 在后。`utils/api.ts:321-405` 的 `splitSysPromptPrefix` 根据 boundary 分块，静态块带 `cache_control`，动态块不缓存。 | `RemoteAPIClient.cpp:35-40`：整个 `systemPrompt` 一个 block 带 cache_control。 | **✅ 已对齐**：`Message.h` 定义 `kSystemDynamicBoundary`；`QueryEngine::buildSystemPrompt` 在 `# Environment` 前插入 marker（static=intro/rules/CLAUDE.md/tool docs/MEMORY；dynamic=cwd/model/mode/coordinator）；`RemoteAPIClient` 按 marker 切成 2 个 system block，cache_control **只在 static 块**。无 marker 时回退单块。 | mode/model/cwd 变化不再污染静态前缀；与 JackProAi boundary 模式一致。 | 未用 `scope:'global'`（那是 ant-only 跨会话特性，单机用 ephemeral 即可）。 |
| message 断点 | `claude.ts:addCacheBreakpoints`：单 message-level cache_control，标最后 message。 | `RemoteAPIClient.cpp:83-88`：最后 message 最后 block 打 cache_control。 | 保留（已是单断点，与 JackProAi 一致）。 | 与 JackProAi 一致。 | 未跳过 assistant thinking 块（CloseCrab 不在 messages 里塞 thinking，无影响）。 |
| tools 断点 | base schema session-cache + defer_loading overlay（从 cache hash 排除）。 | `RemoteAPIClient.cpp:93-100` `tools.back()["cache_control"]`。 | 保留 tools.back() 断点；**关键是让 tools 数组本身字节稳定（见 §2）**，断点才有意义。 | tools 固定后该断点稳定命中。 | — |
| 结论 | 关键是静态前缀稳定 + 动态后移 + message 单断点。 | system 未拆、tools 动态变。 | **✅ system 拆分已完全对齐**（配合 §2 tools 固定，静态前缀 = tools + static-system 全字节稳定）。 |  | message 前缀仍受 §3 in-place microcompact 影响（见 §3）。 |

---

## §2 tools 数组稳定性 / defer_loading

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| tool schema 稳定 | `utils/api.ts:136-180`：`toolToAPISchema` 用 `getToolSchemaCache()`，base schema 只算一次，避免中途变导致工具数组字节 churn。 | 每轮从 ToolRegistry 生成 JSON schema。 | **✅ 已对齐（目标层面）**：`QueryEngine::buildModelConfig` 改为发**全部** enabled 工具，按 name 排序保证确定性顺序，描述稳定（getDescription 是 const 字面量）→ tools 数组字节稳定。 | 达成 JackProAi 的 verified 目标（稳定 tools 前缀 → cache 命中）。 | 工具描述若运行时动态变仍会破 cache（当前都是静态字面量，无此问题）。 |
| defer_loading | `utils/api.ts:211-260`：deferLoading 是 per-request overlay；`CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS` 剥掉非标准字段。 | `QueryEngine.cpp:210-280`：ALWAYS_LOAD 10 核心 + ToolSearch 发现（JackProAi-style）。 | **保持原版**。曾误改"全发 58 工具"→模型被 Workflow/Monitor/Reverse 元工具带偏（一调工具就停），已还原。正常会话 discovered 为空 → 数组就是稳定的 10 个。 | 聚焦核心集（如 JackProAi）；正常会话亦缓存稳定。 | 真用 ToolSearch 发现新工具时数组才变（罕见）。 |
| cache hash 排除 | `claude.ts:1461-1467`：过滤 defer_loading 工具不计入 cache key。 | 无。 | N/A（不用 defer_loading 就无需排除）。 | — | — |
| 结论 | JackProAi：稳定 base schema + defer_loading overlay（聚焦核心）。 | CloseCrab：ALWAYS_LOAD 10 核心 + 按需发现（原本就对）。 | **✅ 还原原版**：核心集聚焦 + 按需发现，与 JackProAi 聚焦理念一致。教训：勿"全发 58"过度修改。 |  | — |

---

## §3 + §7 microcompact / tool_result 持久化

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| API-side microcompact | `services/compact/apiMicrocompact.ts`：构造 `context_management.edits`，`clear_tool_uses_20250919`，trigger 180K，target 40K（服务端清理，本地字节不动）。 | `RemoteAPIClient.cpp:48-81`：每轮 `msgs.dump()` 超 200KB 就**就地改旧** `tool_result.content`，决策每轮变 → cache 失效。 | **B 路（已对齐目标）**：未走 A 路（服务端 `context_management`）——yikoulian 中转未确认转发该 beta（门禁 B 未过）。改为本地**确定性**替换：见下行 Replacement state。 | 不依赖中转 beta。 | A 路待中转确认后可再加（验证项 7）。 |
| 本地持久化阈值 | `constants/toolLimits.ts`：单结果默认 50K；同一 message aggregate 200K。 | AgentTool >2KB 持久化；Bash 100KB 截断；Read/Glob/Grep 不统一。 | **✅ 已对齐（且本就统一）**：`OutputPersistence::persistIfNeeded`（`MAX_INLINE_SIZE=50000`，2KB 预览，对齐 JackProAi DEFAULT_MAX_RESULT_SIZE_CHARS）在 `processToolUse` 串行(line417)+并行(line857)路径**集中**对所有工具结果生效；超 50K → 落盘 `data/tool-results/<tool_use_id>.txt` + `<persisted>` 预览。 | 所有工具统一预算；预览在 tool 运行时写入 messages 一次 → 天然字节稳定。 | AgentTool 另有 >2KB 内联持久化（更激进，预览已 <50K，两者不冲突）。 |
| Replacement state | `toolResultStorage.ts:390-397`：`ContentReplacementState={seenIds,replacements}`；`enforceToolResultBudget` 对 fresh/frozen 分区，同一 tool_use_id 决策永远固定。 | 无跨轮 state；每轮重新决定清理哪条 → cache killer。 | **✅ 已对齐**：`RemoteAPIClient::clearedToolUseIds_`（mutable set）= JackProAi seenIds。Pass1 对已冻结 id **始终**应用相同 stub（byte-identical）；Pass2 仅在仍超预算时**追加冻结**最旧 tool_result。决策单调 → message 前缀字节稳定 → 压缩后 cache 仍命中。 | 根治 77K/请求的 cache miss。 | stub 用「原文前 400 字符 + 固定 marker」，同 id 同内容确定性一致。 |
| 结论 | 服务端清理 + 本地确定性落盘两条路。 | 非确定性 in-place 清理（cache killer）。 | **✅ B 路 + 持久化已完全对齐**（A 路服务端 context_management 待中转确认）。 |  | — |

---

## §4 BashTool prompt 结构

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| Bash prompt 位置 | `tools/BashTool/prompt.ts:getSimplePrompt()`，进入 API schema description。含 bash 用法、sandbox、git、sleep、background、工具替代清单。 | `BashTool.h:getDescription()` 只有一句。 | **✅ 已对齐（除 sandbox）**：`getDescription()` 扩成完整结构——working-dir 持久说明 + 工具替代清单 + `# Instructions`（Windows/Git Bash 路径规则、绝对路径优先、内联脚本警告、多命令并行/`&&`、run_in_background 不轮询、git safety、不删源文件）。 | 与 JackProAi 段落结构一致；模型行为引导完整。 | 未含 sandbox 段——CloseCrab 没有命令沙箱机制，照搬无意义（合理省略）。 |
| 禁用清单 | File search→Glob；Content→Grep；Read→Read；Edit→Edit；Write→Write。 | 无。 | **✅ 已对齐**：同款 + 额外 Windows findstr/cmd 警告（本项目事故的合理扩展）。 | 模式一致。 | — |
| 结论 | 完整 prompt 结构。 | 原来一句话。 | **✅ 已完全对齐**（sandbox 段不适用，合理省略）。 |  | — |

---

## §5 rm / 危险命令拦截

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| 流程位置 | `BashTool/pathValidation.ts:713-737`：先 `validateCommandPaths`，若 deny 尊重；然后对 rm/rmdir 做 dangerous removal check，发生在普通 allow/suggestion 之前。 | PermissionEngine 只按工具 read/destructive 判断；auto-approve 会跳过。 | **✅ 已完全对齐**：dangerous removal check 下沉到 `PermissionEngine::check()` 最前面，runs BEFORE bypass/allow，连 BYPASS 模式也强制 ASK。撤掉了 MUST_ASK_USER 枚举和 QueryEngine hack。 | 功能上可强制问。 | ~~机制不像 JackProAi~~ 已修正 |
| 危险路径 | JackProAi `utils/permissions/pathValidation.ts:331`：`*`、`/*`、`/`、home、root direct child、Windows drive root/drive child。 | 无。 | `utils/PathValidation.h` 实现类似规则。 | 规则接近。 | 我的实现简化，未使用安全 path resolver/tilde expansion/symlink 策略。 |
| 参数解析 | JackProAi `filterOutFlags` 正确处理 `--` end-of-options，PATH_EXTRACTORS 针对不同命令。 | 无。 | `extractRmTargets` 简单 regex + token split。 | 能挡常见 `rm path` / `rm -rf path`。 | 不处理复杂 shell、glob expansion、`--`、多个 command AST、安全性弱于 JackProAi。 |
| 警告 | JackProAi `destructiveCommandWarning.ts`：只用于 UI 提示，不影响权限；主要覆盖 rm -rf、git reset/push force、DB/infra。 | 无。 | 把 warning pattern 放进 `PathValidation.h`，但目前没接 UI 展示。 | 有基础材料。 | 实际未展示 warning；纯 rm 也被我列为 warning，范围比 JackProAi 广。 |
| 结论 | JackProAi 是成熟权限管道 + 精确 path validation。 | CloseCrab 缺硬拦截。 | 当前只是功能性补丁，不是 1:1。 |  | 建议后续重构到 PermissionEngine，不保留 MUST_ASK_USER 分叉。 |

---

## §6 readFileState / 写前必读

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| 状态结构 | `utils/fileStateCache.ts:4-15`：`content,timestamp,offset,limit,isPartialView?`，LRU 100，25MB 限制，path key normalize。 | 有 `FileStateCache` 但只做文件内容缓存，不用于写权限。 | **✅ 已对齐**：`ToolContext::ReadState={contentHash,contentSize,mtimeMs,hasOffset,hasLimit,isPartialView}` + `QueryEngine::readFileState_` map + `normalizePathKey`（`/`↔`\`、Windows 小写）。 | 能挡未读写、mtime 变后写；key normalize 后 `G:/x` 与 `G:\x` 一致。 | 用 contentHash+size 代替 JackProAi 的原始 `content`（O(1) 内存，功能等价），无 LRU 上限（session map，量小可接受）。 |
| Read 写入 | JackProAi FileReadTool 写入 offset/limit/timestamp/content；dedup 时只 dedup prior Read，Edit/Write 状态不作为 Read dedup。 | FileReadTool 有自己的 dedup map，但没给写工具用。 | **✅ 已对齐**：FileReadTool 成功后写 `readFileState[normalizePathKey(path)]`，含 contentHash/size/mtime/hasOffset/hasLimit，`isPartialView=false`。 | 与 JackProAi `isPartialView` 语义一致。 | — |
| 写前检查 | JackProAi FileWriteTool.ts:198-219；FileEditTool.ts:275-294：未读/partial 拒绝；mtime 大于 read timestamp 拒绝。Windows mtime 变化但内容相同时用 content fallback 避免误报。 | 无。 | **✅ 已对齐**：Write/Edit 开头检查未读/partial/mtime；mtime 变后用 contentHash+size fallback（JackProAi FileWriteTool.ts:282-294 思路）避免 Windows 云同步/杀毒误拦。 | 核心一致 + content fallback。 | hash 用 `std::hash<string>`，非加密哈希，碰撞概率极低、足够本用途。 |
| isPartialView | JackProAi 注释：auto-injection 内容与磁盘不一致时 true（CLAUDE.md/MEMORY attachments），不是普通 range Read。 | 无。 | **✅ 已对齐**：FileRead 始终 false（普通 range Read 不标 partial）。 | 修正后语义一致。 | 没有 attachments 系统，所以 true 暂无来源（CloseCrab 不自动注入文件内容，不需要）。 |
| 结论 | 核心写前必读 + content fallback 一致。 | 原无。 | **✅ 已完全对齐**（内存策略用 hash 优化，功能等价）。 |  | — |

---

## §8 模型 fallback

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| 触发 | `withRetry.ts:330-365`：连续 529 达 `MAX_529_RETRIES=3` 后，若有 fallbackModel 抛 `FallbackTriggeredError`。 | **已有**：`RemoteAPIClient.cpp:364-416` `FALLBACK_THRESHOLD=3` + `consecutive503` 计数，达阈值切 `fallbackModel_`。 | **✅ 已对齐**：逻辑本就在 streamChat 里，与 JackProAi MAX_529_RETRIES=3 一致；只是 config 的 `fallback_model` 是空串导致永不触发——已填 `claude-sonnet-4-20250514`（源 config + build 运行时副本都填）。 | Opus 过载时自动降级 sonnet。 | 只解决 503/529 overload，不解决余额不足 403（需充值/换 key）。 |
| 捕获/重试 | `query.ts:894-914`：捕获 FallbackTriggeredError，切 model，清空 failed attempt 的 results，fresh executor 重试。 | **已有**：streamChat 内 for-attempt 循环就地切 `activeModel`，同循环重试（CloseCrab 是流式重试，非抛异常回上层）。 | 未改（机制不同但等价：CloseCrab 在 API 层内重试，JackProAi 在 query 层重试）。 | 实现更简单，不需清理 orphan tool_result。 | — |
| main.cpp 接线 | — | `main.cpp:431-432` 从 `api.fallback_model` 读（默认 `claude-sonnet-4-20250514`）调 `setFallbackModel`。 | **✅ 已生效**：config 填值后该调用真正传入非空 fallback。 | — | — |
| 结论 | JackProAi query 层 fallback。 | CloseCrab API 层 fallback，逻辑齐全。 | **✅ 已完全对齐**（机制等价，仅补了缺失的配置值）。 |  | — |

---

## §9 防循环机制

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| 显式 sameToolStreak | 全库搜索无 `sameToolStreak/loopDetect/repeatedAction`。 | `QueryEngine.cpp:486-1019` 有 repeat/sameTool warn/break。 | 未改。 | CloseCrab 有显式防循环，比 JackProAi 多一层。 | 阈值 sameTool break=15 太松。 |
| maxTurns | `query.ts:1506,1705` 检查 `maxTurns`，超过返回 max_turns。 | 有自己的 turn loop，但具体上限需再看。 | 未改。 | JackProAi 用全局 turn cap 防长跑。 | 不能细分同工具循环。 |
| task_budget | `query.ts` 有 API `output_config.task_budget` 跟踪，给模型预算感知。 | 无类似 API task_budget。 | 未改。 | 让模型自我节制。 | 依赖 API beta。 |
| 结论 | 不应硬照 JackProAi 删除 CloseCrab loop 检测；CloseCrab 这块是增强。 | 阈值太松。 | 未改。 |  | 只需把 warn/break 阈值调小，break 后给用户选择。 |

---

## §10 Windows Shell / BashTool spawn

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| shell 查找 | `windowsPaths.ts:98`：`CLAUDE_CODE_GIT_BASH_PATH` → `where git` 推导 → 默认；找不到退出并提示。 | 硬编码 `C:/Program Files/Git/bin/bash.exe`。 | env `CLAUDE_CODE_GIT_BASH_PATH` + 三个常见路径，static 缓存。 | 比原好。 | ⚠️ 仍少 `where git` 自动推导，找不到时回默认而非友好错误（差距小，后续补）。 |
| command quoting | `bashProvider.ts:127-184`：rewrite `2>nul`、quoteShellCommand（单引号 eval 包裹）、pipe 重排、禁 extglob。 | 直接 `bash.exe -c " + cmd + "`，引号容易炸。 | **✅ 已对齐**：新增 `ShellQuoting.h`——`rewriteWindowsNullRedirect`（`>nul`→`/dev/null`）+ `buildEvalCommand`（单引号转义包 `eval '...'`）+ `windowsQuoteArg`（MSVCRT 逐参数转义）。foreground 路径完全消除 `node -e "..."`/`sed -i`/`python -c` 的引号炸。 | bug.txt 的核心事故根治。 | background 路径走 `_popen`（cmd.exe 层），bash 层已 eval 安全，cmd.exe 外层双引号仍是已知小限制（罕用）。未做 pipe 重排/禁 extglob（CloseCrab 不 source snapshot，影响小）。 |
| cwd | `Shell.ts:316-337` spawn `{cwd}`；执行后 `pwd -P > cwdFilePath` 更新 session cwd。 | CreateProcessA `lpCurrentDirectory=null`。 | **✅ 已对齐**：`lpCurrentDirectory=sessionCwd_`；命令尾追加 `; __cc_ec=$?; pwd -P > '<tmp>'; exit $__cc_ec`（始终捕获、保留退出码）；exec 后读回并 `posixToWindowsPath` 更新 `QueryEngine::sessionCwd_`，下一次 bash 继承 `cd`。并行模式不写 sessionCwd（避免竞态）。 | `cd subdir` 跨调用持久，与 JackProAi 一致。 | 并行工具不持久化 cwd（合理，通常是只读）。 |
| env | JackProAi：`...process.env`、`SHELL`、`GIT_EDITOR=true`、`CLAUDECODE=1`、envOverrides、ant 时 `CLAUDE_CODE_SESSION_ID`。 | 继承父环境，无显式字段。 | **✅ 基本对齐**：继承当前 env block + `CLAUDECODE/GIT_EDITOR/SHELL/LANG`。 | 接近。 | LANG 是 CloseCrab 合理扩展；少 env scrub/proxy/session id（CCR-only，本项目不需要）。 |
| output | JackProAi 用 TaskOutput 文件 fd；Windows 用 `'w'` 解决 MSYS handle 只读问题；tail/progress；tree-kill。 | pipe + reader thread，把输出累进 string，>100K 截断。 | 未改（归入 §7 tool_result 持久化）。 | 简单。 | 大输出仍进内存/context；无 tree kill；留 §7 统一处理。 |
| MSYS path conversion | JackProAi 不设 MSYS_NO_PATHCONV；靠 prompt 不调 Windows native commands。 | 不设。 | 不设（§4 prompt 软防 + eval 包裹硬化）。 | 与 JackProAi 一致。 | findstr 等仍靠 prompt 软防，但 eval 包裹已让大部分命令不再炸。 |
| 结论 | JackProAi shell 是完整 provider 架构。 | CloseCrab 是简化直接 CreateProcess。 | **✅ quoting + cwd persistence 已完全对齐**；output(归 §7) 与 `where git` 自动发现为剩余小差距。 |  | — |

---

## 我刚才代码改动的复盘

| 改动 | 是否应保留 | 原因 | 后续修正 |
|---|---|---|---|
| BashTool description 禁用清单 | 保留 | 与 JackProAi prompt 模式一致，直接缓解事故。 | 后续拆成专门 `BashPrompt.h`，补完整 instructionItems。 |
| FileReadTool `isPartialView=false` | 保留 | 修正后符合 JackProAi；普通 range Read 不标 partial。 | 增加 content/offset/limit 记录。 |
| Write/Edit 写前必读 | 保留 | 核心与 JackProAi 一致。 | 增加内容 fallback，防 mtime false positive。 |
| ToolContext::ReadState 只存 mtime/isPartialView | 暂保留 | 最小可用。 | 改成 content/timestamp/offset/limit/isPartialView + LRU。 |
| BashTool env 加 SHELL/GIT_EDITOR/CLAUDECODE | 保留 | 与 JackProAi 一致。 | 加 where git 自动发现，找不到友好失败。 |
| BashTool env 加 LANG | 保留但标为 CloseCrab 扩展 | JackProAi 没设；CloseCrab 有 CJK Windows 问题，合理。 | 若有副作用再删。 |
| BashTool cwd=ctx.cwd | 保留 | 与 JackProAi spawn cwd 一致。 | 还需加执行后 cwd persistence。 |
| MUST_ASK_USER 枚举 | 谨慎保留短期 | 能堵 auto-approve rm。 | 长期改成 JackProAi 式 PermissionEngine 内部 pathValidation 流程，不新增 tool enum。 |
| extractRmTargets 简化 parser | 短期保留 | 能挡常见 rm。 | 改成 AST/token parser，处理 `--`、多路径、glob、quote。 |
| PathValidation.h regex patterns | 短期保留 | 有基础保护。 | 拆 destructive warning 与 path validation，warning 接到 UI。 |

---

## 建议后续修改顺序（按本次深挖修正）

1. **先修我刚才 §5 的粗糙实现**：把 MUST_ASK_USER 逻辑下沉到 PermissionEngine，照 JackProAi：deny 优先 → dangerous rm path → passthrough/ask suggestions。
2. **补 §10 quoteShellCommand / rewriteWindowsNullRedirect**：这是 Git Bash 引号灾难的硬修，不是 prompt 软防能完全解决的。
3. **补 §6 readFileState 完整结构**：content/timestamp/offset/limit/isPartialView + path normalize + content fallback。
4. **做 §1/§2 缓存核心**：system 静/动态拆分；tools 数组固定化或 schema cache。
5. **做 §3/§7 ContentReplacementState + ToolResultStorage**：解决 77K 上下文膨胀。
6. **§9 调阈值**：sameTool warn 4/break 8。
7. **§8 fallback**：最后做。

---

## 一句话结论

JackProAi 的核心不是某个单点 patch，而是三套稳定机制：**prompt/cache 字节稳定、权限路径验证前置、shell provider 全链路 quoting/cwd/output 管理**。我刚才改的 P0 里，§4/§6/§10 的方向基本对，§5 是功能性临时补丁但不够 JackProAi 化；后续应先把 §5 和 §10 补成 JackProAi 风格，再动缓存大块。
