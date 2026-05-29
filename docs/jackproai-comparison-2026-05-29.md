# JackProAi vs CloseCrab 深度对比表（v3/v3.1 全项）

> 目的：把 `fix-plan-2026-05-29-v3.md` 和 `fix-plan-2026-05-29-v3.1.md` 提到的每一项，逐项对照 JackProAi 的真实实现、CloseCrab 当前实现、我们刚改的实现，并列优缺点。后续再改代码时，以本表为依据。
>
> 调研基准：
> - JackProAi：`G:/CMakePJ/JackProAi-claudecode3.1/restored-src/src/`
> - CloseCrab：`G:/CMakePJ/CloseCrab-Unified/src/`
> - 当前已改：§4、§6、§10 部分、§5 部分。

---

## 总览表

| 项 | JackProAi 实现 | CloseCrab 原实现 | 我刚改的实现 | 与 JackProAi 一致度 | 结论 |
|---|---|---|---|---|---|
| §1 缓存断点 | system 静/动态拆分；message 1 个断点；tools 前缀稳定 | system/tools/message 都打；system 未拆；tools 动态变 | 未改 | 未实现 | 应按 JackProAi 模式改，但要先解决 tools cache_control 标记位置真实性 |
| §2 tools 稳定 | tool schema session-cache；defer_loading 是 overlay；deferred 从 cache hash 排除 | discovered tools 动态扩 tools 数组 | 未改 | 未实现 | CloseCrab 应避免每轮改变 tools 数组 |
| §3 API microcompact | A：服务端 context_management；B：本地确定性 replacement state | 每轮 in-place 改写 tool_result | 未改 | 未实现 | 必须改，当前是缓存杀手 |
| §4 BashTool prompt | 完整 Bash prompt 里强制用 Read/Edit/Grep/Glob，不用 cat/sed/find 等 | BashTool 描述很短 | 已在 getDescription 加禁用清单 | 模式一致，位置简化 | 可保留，后续可补完整 prompt |
| §5 rm 拦截 | validate path → dangerous removal check → behavior:'ask'，无 MUST_ASK 枚举 | 只靠 PermissionEngine；auto-approve 可绕过 | 新加 MUST_ASK_USER + 简化 path parser | 功能近似，机制不同 | 当前不够 JackProAi 化，建议后续重构到 PermissionEngine 流程 |
| §6 readFileState | FileStateCache 存 content/timestamp/offset/limit/isPartialView；写前检查未读/partial/mtime | 无写前必读 | 已加 readFileState + 写前检查；刚修正 isPartialView | 基本一致，但少 content/offset/limit | 可用；要进一步补 path normalize 和 content fallback |
| §7 tool_result 持久化 | 单结果 50K、每消息 200K；ContentReplacementState 保字节一致 | AgentTool >2KB 持久化；Bash 截断 | 未改 | 部分已有 | 应统一 ToolResultStorage |
| §8 fallback | withRetry 连续 529≥3 → FallbackTriggeredError → query.ts 切模型重试 | 无 fallback | 未改 | 未实现 | 可后做 |
| §9 防循环 | 无 sameToolStreak；靠 maxTurns/task_budget/prompt/microcompact | sameToolStreak 有，但阈值松 | 未改 | CloseCrab 自有增强 | 不必硬照 JackProAi；调低阈值即可 |
| §10 shell spawn | spawn + cwd + env + shell snapshot + quoteShellCommand + output fd + cwd tracking | CreateProcessA 硬编码 bash；无 cwd；无 env | 加 cwd/env/bash 自动发现；未做 shell quote/snapshot/cwd tracking | 部分一致 | 目前只是粗糙接近，后续要补 quoting 与 cwd persistence |

---

## §1 缓存断点策略（prompt cache）

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| system 拆分 | `constants/prompts.ts:560-575`：static sections 在 `SYSTEM_PROMPT_DYNAMIC_BOUNDARY` 前；dynamic sections 在后。`utils/api.ts:321-405` 的 `splitSysPromptPrefix` 根据 boundary 分块，静态块可带 `cache_control scope:'global'`，动态块不缓存。 | `RemoteAPIClient.cpp:35-40`：整个 `systemPrompt` 一个 block 带 cache_control。`QueryEngine.cpp:60-176` 里 cwd/model/mode 直接拼进 system。 | 未改。 | JackProAi 能防 session-specific guidance、MCP、cwd、mode 变化污染静态 cache key。 | CloseCrab 现在一旦 mode/model/cwd 变，system 整体 cache 失效。 |
| message 断点 | `claude.ts:addCacheBreakpoints`：注释明确"Exactly one message-level cache_control marker per request"，标在最后 message；fork `skipCacheWrite` 时标倒数第二。 | `RemoteAPIClient.cpp:83-88`：最后 message 最后 block 打 cache_control。 | 未改。 | CloseCrab 这一点接近 JackProAi。 | CloseCrab 没跳过 assistant thinking/tool_use，也没处理 fork skipCacheWrite。 |
| tools 断点 | 之前我判断工具 cache_control 在 `toolSchemas` 最后一个，但本次深挖 `claude.ts:1235-1244` 未看到 `cacheControl` 传入；`utils/api.ts:129` 支持参数但调用处未传。说明还原源码里**工具 cache_control 可能由其他路径/feature 注入，不能断言**。 | `RemoteAPIClient.cpp:93-100` 明确 `tools.back()["cache_control"]`。 | 未改。 | CloseCrab 简单直接。 | 若 tools 数组动态变，标在 back() 会导致缓存不稳。 |
| 结论 | JackProAi 的关键不是"三处断点"，而是**静态前缀稳定 + 动态内容后移 + 同请求 message 断点唯一**。 | CloseCrab 最大问题是 system 未拆、tools 动态变。 | 未改。 |  | 下一步应先拆 system，再处理 tools 稳定。 |

**建议**：先不要照 v3 写死"tools cache_control 在 toolSchemas.back"，因为本次深挖没在调用链里找到明确传 `cacheControl`。应先做稳妥的：system 静/动态拆分 + messages 单断点 + tools 数组固定化。

---

## §2 tools 数组稳定性 / defer_loading

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| tool schema 缓存 | `utils/api.ts:136-180`：`toolToAPISchema` 用 `getToolSchemaCache()`，base schema 只算一次，避免 GrowthBook/tool.prompt() 中途变导致工具数组字节 churn。 | 每轮从 ToolRegistry 生成 JSON schema，没有稳定缓存层。 | 未改。 | JackProAi 防止工具描述/strict/eager_input_streaming 中途变导致 cache miss。 | CloseCrab 工具描述若动态变化会破 cache。 |
| defer_loading | `utils/api.ts:211-260`：deferLoading/cacheControl 是 per-request overlay，不污染 base cache；`CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS` 时剥掉非标准字段。 | `QueryEngine.cpp:210-280` 用 `discovered` 从消息历史里扫描 `<tool_reference name="X"/>` 后动态 push 完整 schema。 | 未改。 | JackProAi 的工具发现不必改变基础 schema 缓存。 | CloseCrab 的动态扩 tools 会让 tools 数组每轮不同。 |
| cache hash 排除 | `claude.ts:1461-1467`：prompt cache break detection 过滤 defer_loading 工具，注释说明 API 会 strip deferred tools，所以它们不应影响 cache key。 | 无等价检测。 | 未改。 | JackProAi 能判断 cache break 来源。 | CloseCrab 无法知道 tools 变化是否破 cache。 |
| 结论 | JackProAi 是"稳定 base schema + overlay defer_loading"。 | CloseCrab 是"发现一个加一个完整 schema"。 | 未改。 |  | CloseCrab 不建议继续动态扩 tools。 |

**建议方案**：
1. 短期：直接全量发所有工具，保证 tools 数组固定，立刻换缓存命中。
2. 长期：做 JackProAi 式 schema cache + defer_loading overlay，但要先确认中转是否接受 defer_loading。

---

## §3 + §7 microcompact / tool_result 持久化

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| API-side microcompact | `services/compact/apiMicrocompact.ts`：构造 `context_management.edits`，`clear_tool_uses_20250919`，trigger 180K，target 40K，清理 Bash/Glob/Grep/FileRead/WebFetch/WebSearch。 | `RemoteAPIClient.cpp:48-81`：客户端每轮 `msgs.dump()` 超 200KB 就直接改旧 `tool_result.content`。 | 未改。 | JackProAi 不改本地 messages 字节，服务端清理，cache key 稳。 | 依赖 beta 和 provider 支持；第三方中转可能剥字段。 |
| 本地持久化阈值 | `constants/toolLimits.ts`：单结果默认 50K；同一 user message aggregate 200K。 | AgentTool 自己 >2KB 持久化；Bash 100KB 截断；Read/Glob/Grep 不统一。 | 未改。 | JackProAi 有统一预算，不让单轮并行工具堆 400K。 | CloseCrab 分散硬编码，行为不一致。 |
| Replacement state | `toolResultStorage.ts:390-397`：`ContentReplacementState={seenIds,replacements}`；line 769 起 `enforceToolResultBudget` 对 fresh/frozen/mustReapply 分区，同一 tool_use_id 的替换决策永远固定。 | 无跨轮 replacement state；每轮重新按大小决定清理哪条。 | 未改。 | JackProAi byte-identical，保 cache。 | CloseCrab 当前是 cache killer。 |
| 结论 | JackProAi 有两条互补路径：服务端清理 + 本地确定性落盘。 | CloseCrab 是非确定性 in-place 清理。 | 未改。 |  | 这是缓存成本核心。 |

**建议方案**：在中转支持未确认前，先做本地确定性 B 路：`ContentReplacementState` + 统一 `ToolResultStorage`。不要再每轮 in-place 改旧消息。

---

## §4 BashTool prompt 结构

| 维度 | JackProAi | CloseCrab 原实现 | 我刚改的 | 优点 | 缺点/风险 |
|---|---|---|---|---|---|
| Bash prompt 位置 | `tools/BashTool/prompt.ts:getSimplePrompt()`，作为工具 prompt 进入 API schema description。包含完整 bash 用法、sandbox、git、sleep、background、工具替代清单。 | `BashTool.h:getDescription()` 只有一句。 | 在 `getDescription()` 加工具替代清单 + Windows/Git Bash 注意。 | 快速有效，让模型少跑 sed/findstr/cat。 | 比 JackProAi 少很多细节：sandbox、sleep、git safety、working dir persistence。 |
| 禁用清单 | JackProAi 明确：File search 用 Glob；Content search 用 Grep；Read 用 Read；Edit 用 Edit；Write 用 Write；Communication 直接输出。 | 无。 | 已加。 | 模式一致。 | 我额外加了 Windows findstr/cmd 警告，这是针对本项目事故的合理扩展。 |
| 结论 | 模式一致，深度不足。 | 原来太弱。 | 可保留。 |  | 后续可把 Bash prompt 拆成专门 prompt 函数，不塞 getDescription 一行里。 |

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
