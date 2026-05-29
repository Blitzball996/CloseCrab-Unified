# CloseCrab 极致省钱计划 — 对照 JackProAi

> **实施状态(2026-05-29 已落地)**:P0/P1/P2/P3 全部实现并编译通过、烟测正常。
> - ✅ **P0 1h 缓存 TTL** — 三处 cache_control 共用 `{type:ephemeral, ttl:"1h"}`(`RemoteAPIClient.cpp` `kCacheControl`)。带 ttl:1h 的请求被中转接受(HTTP 200)。
> - ✅ **P1 时间触发 microcompact** — `RemoteAPIClient` 记 `lastRequestEpochMs_`,间隔 >60min 时发请求前冻结清理旧 tool 结果、保留最近 5 条(对齐 `timeBasedMCConfig` keepRecent=5)。
> - ✅ **P2 子 agent 共享父缓存** — `ModelConfig.skipCacheWrite`(子 agent=true),message 断点挪到倒数第二条(对齐 `addCacheBreakpoints` markerIndex=length-2)。
> - ✅ **P3 服务端 context_management** — env 门控 `CLOSECRAB_API_CLEAR_TOOL_RESULTS`(默认关,对齐 JackProAi ant-only + USE_API_CLEAR_TOOL_RESULTS),开启时发 `clear_tool_uses_20250919`(trigger 180K/target 40K)。
>
> **待人工验证**:P0 的 1h TTL 是否被中转后端真正延长(语法已接受)。方法:发一轮 → 等 6 分钟 → 再发,看第二轮 `cache_read_input_tokens` 是否仍 >0(5min 默认会清零,1h 则命中)。

---

> 调研基准:`JackProAi-claudecode3.1/restored-src/src/`(已验证的参考实现)与 `CloseCrab-Unified/src/`(当前状态,含本次会话 §1/§3/§7 修复)。
>
> **前提澄清**:CloseCrab 的 `RemoteAPIClient.cpp:319` 已经在发送这些 beta 头:
> `extended-cache-ttl-2025-04-11`、`context-management-2025-06-27`、`tool-search-tool-2025-10-19`、`prompt-caching-scope-2026-01-05`、`advanced-tool-use-2025-11-20`。
> 说明这些高级省钱特性的**通道已开**,只是代码没真正用上。这是好消息——大部分优化只是"接上已有的能力",不是从零造。

---

## 省钱机制全表对照

| # | 省钱机制 | JackProAi 做法 | CloseCrab 现状 | 省钱量级 | 落地难度 |
|---|---|---|---|---|---|
| 1 | **1h 缓存 TTL** | `getCacheControl` → `ttl:'1h'`(`should1hCacheTTL`)。缓存命中窗口 1 小时 | ❌ 只发 `{type:ephemeral}` = **5 分钟**。但 `extended-cache-ttl` 头已发 | **极高** | 1 行 |
| 2 | **system 静/动态拆分** | `splitSysPromptPrefix` + boundary,静态块带 cache_control | ✅ 已做(§1) | 高 | 已完成 |
| 3 | **tools 前缀稳定** | session-cache schema + defer_loading(从 cache hash 排除) | ✅ 10 核心 + ToolSearch 发现(正常会话稳定) | 高 | 已完成 |
| 4 | **确定性 tool_result 压缩** | `ContentReplacementState.seenIds` 冻结决策 | ✅ 已做(§3 `clearedToolUseIds_`) | 高 | 已完成 |
| 5 | **大结果落盘** | `toolResultStorage` 单结果 50K / 每消息 200K | ✅ 已做(§7 OutputPersistence 50K) | 中 | 已完成 |
| 6 | **时间触发 microcompact** | `tengu_slate_heron`:距上条 assistant > 60min(缓存必过期)→ 发请求前先清旧 tool 结果,缩小"反正要重写"的前缀 | ❌ 无 | **高**(慢节奏会话) | 中 |
| 7 | **autoCompact 摘要** | 上下文窗口 - 13K buffer 时触发对话摘要 | ✅ 有(800K 阈值 + HistoryCompactor),但 buffer 较粗 | 中 | 已有,可调优 |
| 8 | **子 agent 共享父缓存** | fork `skipCacheWrite`:断点挪到倒数第二条,子 agent 读父缓存、不写自己的尾 | ❌ AgentTool 独立 context,不共享 | **高**(多 agent 任务) | 中高 |
| 9 | **服务端 context_management** | `clear_tool_uses_20250919`(trigger 180K/target 40K),服务端清理,本地零字节改动 | ❌ 走本地 B 路(§3)。但 `context-management` 头已发 | 中(与 §3 重叠) | 中 |
| 10 | **capped max_tokens** | 起步 8K 输出上限,需要时升到 64K(省 slot 预留) | ⚠️ 直接 64K(注释说升级已禁用) | 低(影响限流非$) | 低 |
| 11 | **read 去重** | 同 range + mtime 未变 → 返回 stub | ✅ 有(FileReadTool dedup) | 中 | 已完成 |
| 12 | **非前台 529 直接放弃** | 后台/摘要/分类查询 529 不重试(避免中转放大计费) | ⚠️ 统一重试 | 低中 | 低 |
| 13 | **beta 头粘滞** | header 一旦发出整会话保持,避免中途 toggle 破缓存(~50-70K/次) | ✅ 头固定不变(无中途 toggle) | 中 | 天然满足 |

**小结**:本次会话已把 #2/#3/#4/#5 这些"大头"补齐(CloseCrab 之前是 cache killer)。剩下能再榨出钱的,按优先级是 **#1(1h TTL)> #6(时间触发清理)> #8(子 agent 共享缓存)> #9(服务端清理)**。

---

## CloseCrab 极致省钱行动计划(按性价比排序)

### P0 — 1h 缓存 TTL(最高性价比,1 行)

**依据**:JackProAi `getCacheControl` 在 ant/订阅用户 + 允许的 querySource 下加 `ttl:'1h'`。CloseCrab 已发 `extended-cache-ttl-2025-04-11` 头,通道是通的。

**改法**:`RemoteAPIClient.cpp` 把所有 `{"type","ephemeral"}` 改成 `{"type","ephemeral"},{"ttl","1h"}`(system 静态块、tools.back、last message 三处)。

**收益**:5 分钟 → 1 小时缓存存活。用户思考/走神超过 5 分钟,原来缓存就过期、下轮按创建价($10.6/1M)全量重写;改后 1 小时内都按读取价($0.85/1M)命中。**慢节奏会话省 5-10×**。

**风险**:需确认 yikoulian 中转真的尊重 1h TTL(发了头不代表中转后端落实)。**验证**:打印响应 `usage.cache_read_input_tokens`,间隔 6-10 分钟发两轮,看第二轮是否仍命中。

---

### P1 — 时间触发 microcompact(配合 1h TTL)

**依据**:JackProAi `timeBasedMCConfig.ts` —— 距上条 assistant 消息超过阈值(默认 60min,= 缓存必然过期)时,发请求前先清掉旧 tool 结果(保留最近 5 条),缩小"反正要重写"的前缀。

**改法**:在 `QueryEngine` 记录 `lastAssistantTimestamp_`;发请求前若 `now - last > TTL`(用 5min 若没上 1h TTL,用 60min 若上了),触发一次 `clearedToolUseIds_` 把老 tool 结果全冻结清理。复用 §3 已有的 `clearedToolUseIds_` 机制,只是改触发条件(从"超 200KB"加一条"超时")。

**收益**:缓存反正要重写时,把重写量从 77K 压到 ~20K。**冷启动/慢会话省 60-70% 重写成本**。

---

### P2 — 子 agent 共享父 prompt 缓存

**依据**:JackProAi `addCacheBreakpoints` 的 `skipCacheWrite`:fork 时把 cache_control 断点挪到倒数第二条消息,使子 agent **读**父缓存而不**写**自己的尾。CloseCrab 当前 `AgentTool` 把 `ctx.systemPrompt` 传给子 agent(已部分共享 system),但每个子 agent 仍独立写缓存。

**改法**:子 agent 请求时 system + tools 前缀与父一致(已大致满足),并在 `buildRequestBody` 增加 `skipCacheWrite` 参数:为真时断点放倒数第二条。需要 QueryEngine 标记"这是子 agent 请求"。

**收益**:多 agent 任务(探索+写文件并行)中,N 个子 agent 不再各写一份 77K 缓存,只读父缓存。**多 agent 场景省 (N-1)× 的缓存创建费**。

---

### P3 — 服务端 context_management(A 路,可选)

**依据**:JackProAi `apiMicrocompact.ts` 发 `context_management.edits`(`clear_tool_uses_20250919`,trigger 180K/target 40K),服务端清理,本地 messages 零字节改动 → 前缀更稳。CloseCrab 已发 `context-management-2025-06-27` 头。

**改法**:`buildRequestBody` 加 `body["context_management"]`,照搬 apiMicrocompact JSON 结构。与 §3 本地 B 路二选一(A 路更优,本地不动字节)。

**收益**:比 §3 本地清理更干净(本地不改字节,前缀绝对稳)。**与 §3 部分重叠,增量收益中等**。**前提**:实测中转真的执行 context_management(对比开/关时的 usage)。

---

### P4 — 其余小项

- **capped max_tokens 8K 起步**:`QueryEngine.cpp:193` 恢复 8K 起步 + 命中 max_tokens 才升 64K。影响限流 slot 而非直接$,优先级低。
- **后台查询 529 不重试**:摘要/标题/分类这类非前台调用,529 直接放弃,避免中转重试放大计费。
- **autoCompact buffer 调优**:对齐 JackProAi 的 13K buffer 逻辑。

---

## 验证方法(每改一项都要量化)

```
# 每个请求打印 usage,看缓存命中:
#   cache_read_input_tokens     ← 命中读取(便宜)
#   cache_creation_input_tokens ← 创建写入(贵 12×)
# 目标:第二轮起 cache_read 稳定在 ~60K+,cache_creation 降到增量(5-10K)

# 1h TTL 验证:发一轮 → 等 6 分钟 → 再发一轮
#   未改:第二轮 cache_read=0(5min 过期)
#   改后:第二轮 cache_read≈60K(1h 内命中)

# 时间触发清理验证:等 >TTL 再发,看 bodySize 是否缩小
```

---

## 一句话给老板版

> CloseCrab 经本次修复已堵住"每轮重写 77K 缓存"的烧钱黑洞(§1/§3/§7),和 JackProAi 在大头上拉平。再想**极致省钱**,最高性价比是 **P0 加 1h 缓存 TTL(1 行,beta 头已发)**——慢节奏会话直接再省 5-10×;其次是 P1 时间触发清理和 P2 子 agent 共享缓存。CloseCrab 还有 JackProAi 没有的**本地模式 = $0 API 费**这张王牌。
