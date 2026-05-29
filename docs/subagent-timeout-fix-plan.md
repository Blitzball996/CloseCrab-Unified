# Plan: Fix Sub-Agent Timeout — 三项目对比方案

## 问题

子 agent 执行翻译任务时反复超时（300s），具体表现：
- 反复读同一文件（sameToolStreak 触发）
- proxy 504 重试累计耗时过长
- 最终 300s 硬超时强制终止

## 三项目架构对比

| 维度 | claude-code | JackProAi | CloseCrab (当前) |
|------|-------------|-----------|-----------------|
| **语言/线程模型** | TypeScript/单线程 async | TypeScript/单线程 async | C++/多线程 |
| **子 agent maxTurns** | 按用途: 1/5/20/无限 | `FORK_AGENT.maxTurns=200` | 硬编码 20 |
| **硬超时** | 无（靠 maxTurns 自然结束） | 24h session timeout + watchdog | 300s `wait_for` |
| **工具结果上限** | `DEFAULT_MAX_RESULT_SIZE_CHARS=50_000` 超过持久化到磁盘，模型只看 2KB preview | 同 claude-code（同一套代码） | `toolResultBudget=800 tokens` (~3.2KB) 直接截断 |
| **每消息聚合上限** | `MAX_TOOL_RESULTS_PER_MESSAGE_CHARS=200_000` | 同 | 无 |
| **重试策略** | 10次通用，529限3次，unattended模式无限重试 | 同 claude-code + `MAX_529_RETRIES=3` | 10次指数退避，无区分 |
| **系统提示** | 共享父 prompt（CacheSafeParams），子 agent 无额外写入限制 | 同 + `renderedSystemPrompt` 直传避免重渲染 | 继承父完整 prompt 含 "50行Write限制" |
| **递归防护** | `isInForkChild()` 检查 `FORK_BOILERPLATE_TAG` | 同 | `allowSubagents=false` |
| **并发控制** | 单线程天然串行 | `SPAWN_SESSIONS_DEFAULT=32` | `AgentSemaphore(4)` |
| **prompt cache** | `CacheSafeParams` 确保 fork 共享父 cache | 同 + `lastCacheSafeParams` 槽位 | 父 systemPrompt 直传（已实现） |

## 功能细节对比 + 优缺点

### 子 agent 生命周期管理

| | claude-code | JackProAi | CloseCrab |
|--|-------------|-----------|-----------|
| **创建方式** | `runForkedAgent()` async generator | 同 | `std::async` + `AgentManager` |
| **结束条件** | maxTurns 到达 / 模型 end_turn / abort | 同 + session watchdog | 300s 硬超时 / maxTurns / interrupted |
| **结果传递** | generator yield messages | 同 | `future.get()` 阻塞等待 |
| **取消机制** | `AbortController` 级联（父 abort → 子 abort） | 同 | `interrupted` atomic flag |
| **优点** | 灵活，无资源泄漏 | 同 + 多 session 隔离 | 真并行（多核利用） |
| **缺点** | 单线程无法真并行 | 同 | 硬超时太激进，线程管理复杂 |

### 工具结果处理策略

| | claude-code | JackProAi | CloseCrab |
|--|-------------|-----------|-----------|
| **小结果 (<50KB)** | 原样保留 | 同 | 原样保留（但子 agent 被截到 3.2KB） |
| **大结果 (>50KB)** | 持久化到磁盘，返回 2KB preview + 文件路径 | 同 | `OutputPersistence::persistIfNeeded` 已实现，但子 agent 的 800 token budget 先截断了 |
| **每 turn 聚合** | 200KB 上限，超过的按大小排序持久化 | 同 | 无聚合限制 |
| **优点** | 模型能看到足够上下文，不会反复读 | 同 | — |
| **缺点** | 磁盘 IO 开销 | 同 | 子 agent 截断太狠导致反复读 |
| **最佳** | ✅ claude-code/JackProAi | | |

### 重试与错误恢复

| | claude-code | JackProAi | CloseCrab |
|--|-------------|-----------|-----------|
| **通用重试** | 10 次，指数退避 500ms base | 同 | 10 次，500ms base，cap 30s |
| **529/容量错误** | 限 3 次 | 同 `MAX_529_RETRIES=3` | 不区分，一律 10 次 |
| **504 网关超时** | 归类为 server_error，正常重试 | 同 | 正常重试 10 次 |
| **unattended 模式** | 429/529 无限重试 + heartbeat | 同 + `PERSISTENT_MAX_BACKOFF_MS=5min` | 无此模式 |
| **idle watchdog** | 30s 无数据 → 回退非流式 | 同 | 已移除（曾导致 crash） |
| **优点** | 区分错误类型，容量错误快速失败 | 同 + unattended 长期运行 | 简单直接 |
| **缺点** | 复杂 | 更复杂 | 504 重试 10 次浪费 120s+ |
| **最佳** | | ✅ JackProAi（最完整） | |

### 系统提示与 prompt cache

| | claude-code | JackProAi | CloseCrab |
|--|-------------|-----------|-----------|
| **cache 共享** | `CacheSafeParams` 传递完整 prompt+tools+messages 前缀 | 同 + `renderedSystemPrompt` 避免重渲染 | 父 systemPrompt 字符串直传 |
| **子 agent 限制** | 无额外限制（50行规则不在 system prompt 中） | 同 | 继承 "50行Write限制" 导致多次 Read→Edit |
| **cache hit 监控** | `tengu_fork_agent_query` 事件记录 hit rate | 同 | 无监控 |
| **优点** | cache hit 高，子 agent 无不必要限制 | 同 + 监控 | 简单 |
| **缺点** | 需要精确对齐参数 | 同 | 子 agent 被不相关规则拖慢 |
| **最佳** | | ✅ JackProAi（有监控） | |

### 并发与资源控制

| | claude-code | JackProAi | CloseCrab |
|--|-------------|-----------|-----------|
| **并发模型** | 单线程 event loop，子 agent 串行 | 多 session（32 并发），单 session 内串行 | 多线程，4 并发子 agent |
| **API 并发** | 1（单线程） | 1 per session，多 session 可并发 | 无限制（4 agent 同时请求） |
| **资源隔离** | `createSubagentContext()` 克隆状态 | 同 + worktree 文件隔离 | 共享 messages_ 指针 |
| **优点** | 无竞争 | 真正隔离 | 真并行快 |
| **缺点** | 无法利用多核 | 复杂 | 多 agent 同时请求可能打爆 proxy |
| **最佳** | | ✅ JackProAi（隔离最好） | |

## 总结：谁最好

**JackProAi > claude-code > CloseCrab**

JackProAi 本质是 claude-code 的增强版（加了 bridge 多 session + worktree 隔离 + unattended retry）。claude-code 是基础版但已经很完善。CloseCrab 作为 C++ 重写，在子 agent 管理上有几个关键参数设置不当（toolResultBudget 太小、硬超时太短、继承了不该继承的 prompt 规则）。

**CloseCrab 的优势**：真多线程并行（C++ 独有），编译为单二进制无依赖。
**CloseCrab 的劣势**：子 agent 参数调优不足，缺少 JackProAi 的错误分类和 unattended 模式。

## 修复方案

### 1. 工具结果预算对齐（最关键修复）
**参考**: JackProAi `DEFAULT_MAX_RESULT_SIZE_CHARS=50_000` + 磁盘持久化
**文件**: `src/agents/AgentManager.cpp:131`
**改动**: 删除 `qeConfig.tokenBudget.toolResultBudget = 800;`（使用默认 100000）

这是子 agent 反复读文件的根因——800 token 截断后模型看不到完整内容。

### 2. 超时 300s → 无硬超时（靠 maxTurns 控制）
**参考**: JackProAi 无硬超时，靠 `maxTurns=200` 自然结束
**文件**: `src/agents/AgentManager.cpp:207`
**改动**: `wait_for(std::chrono::seconds(300))` → `wait_for(std::chrono::seconds(1800))` (30分钟安全网)

不能完全去掉超时（C++ 线程需要安全网），但 30 分钟足够任何合理任务完成。

### 3. 重试区分 529/503 vs 其他错误
**参考**: JackProAi `MAX_529_RETRIES=3`
**文件**: `src/api/RemoteAPIClient.cpp:361-470`
**改动**: 504/503 错误最多重试 3 次（不是 10 次），其他网络错误保持 10 次

### 4. 子 agent 系统提示去掉 Write 行数限制
**参考**: JackProAi/claude-code 子 agent 无此限制（这是 CLI UI 层规则）
**文件**: `src/core/QueryEngine.cpp` (buildSystemPrompt 函数)
**改动**: 当 `config_.allowSubagents == false`（即当前是子 agent）时，跳过 "IMPORTANT: If the content to write exceeds 150 lines..." 段落

### 5. maxTurns 保持 20（不改）
JackProAi 用 200 是因为无硬超时。CloseCrab 有 30 分钟安全网，20 turns 足够。如果 20 turns 内完不成说明任务拆分有问题，应该由主 agent 重新拆分。

### 6. API 并发信号量（可选，观察后决定）
如果修复 1-4 后 504 仍频繁，再加全局 API 并发限制。暂不实施。

## 修改文件清单

1. `src/agents/AgentManager.cpp` — 删除 toolResultBudget=800, timeout 300→1800
2. `src/api/RemoteAPIClient.cpp` — 504/503 重试限 3 次
3. `src/core/QueryEngine.cpp` — 子 agent 跳过 50 行 Write 限制

## 验证

1. 用需求.txt 多语言翻译任务测试
2. 确认子 agent 不再反复读同一文件（trace.log 无 sameToolStreak 警告）
3. 确认 504 重试不超过 3 次
4. 确认子 agent 能一次性写完大 JSON 文件

---

## Phase 2（超时问题解决后）：发挥 C++ 性能优势

> 先解决超时问题，稳定后再实施以下优化。

### 当前瓶颈分析

CloseCrab 是 C++ 多线程但目前性能优势未体现，原因：
- 子 agent 内部串行（call API → wait → tool → call API），瓶颈在 API 响应（几秒~几十秒）
- 并行工具执行快（本地 IO），但结果要串行发回 API
- C++ 多线程在 IO-bound 场景下优势有限

### 真正发挥 C++ 的方向

| 优化 | 预期收益 | 复杂度 | 状态 |
|------|---------|--------|------|
| ✅ **多线程 grep/glob** — 内置 regex + ThreadPool，rg 不可用时自动启用 | 大项目 grep 从秒级→毫秒级 | 低 | **已完成** commit 6b6b2e9 |
| ✅ **mmap 文件缓存** — LRU 50文件/100MB，mtime 校验 | 重复读同一文件零开销 | 低 | **已完成** commit 6b6b2e9 |
| ✅ **流式 idle 预计算** — API 等待期间预加载 CWD 文件 | 减少工具调用延迟 | 中 | **已完成** commit 6b6b2e9 |
| **本地 LLM 推理** — llama.cpp 简单任务零 API 调用 | 简单任务零延迟零成本 | 低（已有 LocalLLMClient） | 待实施 |
| ✅ **预测性工具执行** — 解析 streaming input_json_delta 提前读文件 | 减少 ~500ms/tool | 高 | **已完成** commit 6b6b2e9 |
| **本地 RAG 加速** — embedding+reranker 用 ONNX/CUDA | 比 TS 快 10-50x | 中（已有 ONNX 集成） | 待实施 |

### CloseCrab 独有优势（TS 项目做不到）

1. **真并行子 agent**：多个 agent 同时跑不同任务，不是 JS 的伪并发
2. **CUDA 加速**：本地 embedding/reranker/LLM 全部 GPU 加速
3. **零 GC 停顿**：无垃圾回收，长时间运行不会卡顿
4. **单二进制分发**：无 node_modules，启动 <100ms
5. **系统级集成**：直接调用 Win32 API、ASIO、进程管理，无 FFI 开销
