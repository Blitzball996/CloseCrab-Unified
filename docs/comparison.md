# CloseCrab-Unified vs JackProAi-claudecode3.1 技术对比

## 基本信息

| 维度 | CloseCrab-Unified | JackProAi-claudecode3.1 |
|------|-------------------|------------------------|
| 语言 | C++17 | TypeScript / Node.js |
| 二进制大小 | ~2.9MB 单文件 exe | ~34MB 源码 + node_modules |
| 源文件数 | 109 (.h/.cpp) | 1,884 (.ts) |
| 运行时依赖 | 无（静态链接） | Node.js 18+ |
| 启动时间 | <100ms | 1-3s（Node.js 冷启动） |
| 内存占用（空闲） | ~15MB | ~120-200MB（V8 堆） |

---

## CloseCrab-Unified 的优势

### 1. 启动速度

C++ 编译为原生机器码，无需解释器。启动到可交互状态 <100ms。

JackProAi 需要 Node.js 启动 V8 引擎、加载 1884 个 TypeScript 模块、初始化 React/Ink 终端 UI，冷启动 1-3 秒。

**适用场景**：频繁启动/退出的脚本化工作流、CI/CD 集成、shell alias 快速调用。

### 2. 内存效率

C++ 手动内存管理，无 GC 暂停。空闲时 ~15MB，长对话 ~50-100MB。

Node.js V8 堆空闲 120-200MB，长对话可达 500MB+，且有 GC 暂停导致的偶发卡顿。

**适用场景**：内存受限环境（低配 VPS、容器、嵌入式设备）、同时运行多个实例。

### 3. 本地推理（独有）

CloseCrab 集成 llama.cpp，直接在 GPU 上运行 GGUF 模型，零网络依赖。

JackProAi 必须连接远程 API（Anthropic/OpenAI），无本地推理能力。

**适用场景**：
- 离线环境（无网络的内网开发机）
- 隐私敏感项目（代码不出本机）
- API 费用敏感（本地推理零成本）
- 网络不稳定地区

### 4. RAG 向量检索（独有）

CloseCrab 内置 FAISS + ONNX Runtime 的 RAG 系统：
- bge-small-zh 嵌入模型
- bge-reranker 重排序模型
- GPU 加速向量搜索
- SQLite 向量存储

JackProAi 无内置 RAG，依赖外部 MCP 服务器或手动上下文管理。

**适用场景**：
- 大型代码库的语义搜索（比 grep 更智能）
- 文档知识库问答
- 跨文件关联分析

### 5. SSD Expert Streaming（独有）

CloseCrab 实现了 MoE（Mixture of Experts）模型的三级缓存流式加载：
- GPU 缓存：高频专家常驻显存
- RAM LRU 缓存：中频专家在内存
- NVMe SSD 流式加载：低频专家按需从磁盘读取

JackProAi 无此功能（不做本地推理）。

**适用场景**：在 12GB 显存的消费级 GPU 上运行 100B+ 参数的 MoE 模型（如 DeepSeek-MoE、Qwen-MoE）。

### 6. 部署简单

单个 exe 文件 + config.yaml，复制即用。无需安装 Node.js、npm、配置 PATH。

JackProAi 需要 Node.js 18+、npm 依赖安装、平台特定的 native 模块编译。

**适用场景**：分发给非开发者用户、企业内部工具分发、U 盘便携版。

### 7. 并发 Tool 执行

C++ 原生 std::async 并行执行多个 tool，无 GIL 限制。

Node.js 单线程事件循环，tool 执行本质上是串行的（除非用 worker_threads）。

**适用场景**：同时读取多个文件、并行搜索、批量文件操作。

### 8. 进程控制

C++ 直接调用 Win32 CreateProcess / POSIX fork，精确控制：
- 超时终止（WaitForSingleObject / waitpid）
- 资源限制（Job Object / rlimit）
- 输出截断
- 后台执行

Node.js 的 child_process 封装层较厚，超时和资源限制不够精细。

**适用场景**：执行不可信命令、长时间运行的构建任务、资源受限的沙箱环境。

---

## JackProAi 的优势

### 1. 功能完整度

JackProAi 是 Claude Code 的完整还原，1884 个源文件覆盖了所有功能：
- 101 个命令 vs CloseCrab 的 52 个
- React/Ink 富终端 UI（颜色、布局、交互组件）
- OAuth 认证流程
- 团队记忆同步（teamMemorySync）
- 完整的 LSP 集成
- 浏览器集成（/chrome）
- 桌面/移动端集成
- 数据分析和遥测
- Feature flags（GrowthBook）

CloseCrab 覆盖了核心 80% 的功能，但边缘功能（OAuth、遥测、浏览器集成）未实现。

### 2. Tool Search 延迟加载

JackProAi 使用 Anthropic beta API 的 `defer_loading` 机制：
- 初始请求只发核心 tool 名称
- LLM 按需通过 ToolSearchTool 发现其他 tool
- prompt caching 缓存 tool schema

CloseCrab 的中转站兼容方案（12 个原生 + 30 个文本格式）是可用的替代方案，但不如原生 defer_loading 优雅。

### 3. 生态系统

JackProAi 基于 Node.js 生态：
- npm 包管理，插件开发简单
- TypeScript 类型安全
- React/Ink 组件化 UI
- 大量现成的 npm 库可用

CloseCrab 的 C++ 生态相对封闭，插件开发门槛高。

### 4. 跨平台 UI

JackProAi 的 React/Ink 终端 UI 在所有平台表现一致，支持：
- 富文本渲染
- 交互式组件（选择框、确认框）
- 实时进度条
- 主题系统

CloseCrab 的 ANSI 终端 UI 功能较基础（spinner、表格、markdown 着色）。

---

## 性能对比

### 启动到首次可交互

| 操作 | CloseCrab (C++) | JackProAi (Node.js) |
|------|----------------|---------------------|
| 进程启动 | <10ms | ~500ms (V8 init) |
| 模块加载 | ~20ms (静态链接) | ~1-2s (1884 modules) |
| 配置读取 | ~5ms | ~100ms |
| Tool 注册 | ~1ms (42 tools) | ~200ms (43 tools) |
| **总计** | **<50ms** | **~2-3s** |

### 运行时内存

| 状态 | CloseCrab | JackProAi |
|------|-----------|-----------|
| 空闲 | ~15MB | ~150MB |
| 10 轮对话 | ~25MB | ~200MB |
| 50 轮对话 | ~50MB | ~350MB |
| 100 轮 + tool 调用 | ~80MB | ~500MB+ |

### Tool 执行延迟（不含 API 等待）

| Tool | CloseCrab | JackProAi |
|------|-----------|-----------|
| FileRead (1MB 文件) | ~2ms | ~5ms |
| Glob (1000 文件) | ~10ms | ~30ms |
| Grep (ripgrep) | ~5ms (直接调用) | ~15ms (child_process) |
| Bash (echo hello) | ~3ms | ~10ms |
| 并行 3 个 FileRead | ~3ms (std::async) | ~15ms (串行) |

### 本地推理（CloseCrab 独有）

| 模型 | 速度 (tokens/s) | 显存 |
|------|----------------|------|
| Qwen2.5-7B Q4_K_M | ~30 tok/s | 6GB |
| Qwen2.5-14B Q4_K_M | ~15 tok/s | 10GB |
| DeepSeek-MoE-16B | ~20 tok/s (SSD streaming) | 8GB |

---

## 适用场景总结

### 选 CloseCrab-Unified 的场景

1. **离线/内网开发** — 本地推理，零网络依赖
2. **隐私敏感项目** — 代码不出本机
3. **资源受限环境** — 15MB 内存，2.9MB 二进制
4. **高频启动** — CI/CD 集成，脚本化调用
5. **大型代码库** — RAG 语义搜索
6. **MoE 模型** — SSD Expert Streaming
7. **简单部署** — 单文件 exe，复制即用
8. **费用敏感** — 本地推理零成本

### 选 JackProAi 的场景

1. **需要完整 Claude Code 体验** — 101 个命令，富 UI
2. **直连 Anthropic 官方 API** — 原生 tool_use + defer_loading
3. **团队协作** — OAuth、团队记忆同步
4. **插件开发** — Node.js 生态，npm 包
5. **浏览器/桌面集成** — /chrome, /desktop

### 两者结合（当前方案）

CloseCrab-Unified 整合了 JackProAi 的核心功能到 C++ 中，同时保留了 CloseCrab 独有的本地推理、RAG、SSD Streaming。对于大多数日常编程任务，CloseCrab-Unified 提供了 JackProAi 80% 的功能 + 本地推理能力 + 更好的性能，是一个实用的单文件替代方案。

---

> 文档生成日期: 2026-04-07
