# CloseCrab 营销推广计划

## 一、项目定位

**一句话介绍：**
> CloseCrab — 3MB 本地优先 AI 编程助手，51 个工具，GPU 加速，手机远程控制。

**核心差异化（vs Cursor / Claude Code / Aider）：**

| 维度 | CloseCrab | Cursor | Claude Code | Aider |
|------|-----------|--------|-------------|-------|
| 隐私 | 本地推理，代码不离开机器 | 云端 | 云端 | 云端为主 |
| 体积 | 单二进制 ~3MB | 500MB+ Electron | CLI 但需 Node | Python 环境 |
| GPU | CUDA + ONNX + llama.cpp | 无本地推理 | 无 | 无 |
| 手机控制 | CloseCrab-Web | 无 | 无 | 无 |
| 价格 | 免费开源 MIT | $20/月 | API 费用 | API 费用 |
| 工具数 | 51 个 | ~20 | ~15 | ~10 |
| 语言 | C++17 原生 | TypeScript | TypeScript | Python |

---

## 二、目标受众

1. **隐私敏感的开发者** — 不想把代码发到云端
2. **本地 LLM 玩家** — r/LocalLLaMA 社区，已有 GPU 和模型
3. **C++ 爱好者** — 欣赏原生性能、单二进制
4. **远程开发者** — 需要手机控制开发环境
5. **Cursor/Copilot 付费用户** — 寻找免费替代

---

## 三、平台投放策略

### 3.1 Hacker News (Show HN)

**最佳发帖时间：** 美东时间周二-周四上午 8-10 点（北京时间晚 8-10 点）

**标题（选一个）：**

```
Show HN: CloseCrab – A 3MB C++ AI coding assistant with 51 tools, runs LLMs on your GPU
Show HN: CloseCrab – Local-first AI coding assistant, single C++17 binary, 51 tools
Show HN: I built a local-first alternative to Cursor in C++17 with GPU acceleration
```

**正文：**

```
Hi HN,

I built CloseCrab, a terminal-based AI coding assistant in C++17. Single binary (~3MB),
runs LLMs locally via llama.cpp with CUDA acceleration, or connects to Claude/OpenAI APIs.

Key differences from Cursor/Copilot/Aider:
- Local-first: your code never leaves your machine
- 51 tools: file ops, shell, git, web search, multi-agent, RAG
- Single binary, no Node/Python/Docker runtime needed
- GPU-accelerated inference with ONNX + CUDA
- Phone remote control via companion web app (CloseCrab-Web)

Tech stack: C++17, llama.cpp, FAISS (vector search), ONNX Runtime, WebSocket.

GitHub: https://github.com/Blitzball996/CloseCrab-Unified
Mobile UI: https://github.com/Blitzball996/CloseCrab-Web

Would love feedback on the tool design and local inference experience.
```

---

### 3.2 Reddit r/LocalLLaMA

**标题：**
```
CloseCrab: Local-first AI coding assistant in C++17 — runs any GGUF model on your GPU, 51 built-in tools
```

**正文：**

```
Hey everyone,

I've been working on CloseCrab, a coding assistant that runs LLMs locally
via llama.cpp. It's a single C++17 binary with 51 tools (file editing,
shell execution, git, web search, RAG with FAISS, multi-agent mode).

What makes it different:
- Loads any GGUF model directly, no server needed
- CUDA GPU acceleration out of the box
- RAG with FAISS vector search for codebase understanding
- MoE expert streaming from SSD for large models
- Can also connect to Claude/OpenAI APIs with one config change
- Companion mobile web app for remote control

I'm running it with Qwen2.5-Coder-32B on a 3090 and it handles most
coding tasks well. The tool-use loop is similar to Claude Code but
everything stays local.

Download: https://github.com/Blitzball996/CloseCrab-Unified/releases
Source: https://github.com/Blitzball996/CloseCrab-Unified

Happy to answer questions about the architecture or local inference setup.
```

---

### 3.3 Reddit r/programming

**标题：**
```
CloseCrab: A terminal AI coding assistant written in C++17 — single 3MB binary, 51 tools, no runtime dependencies
```

**正文：**

```
I built a terminal-based AI coding assistant from scratch in C++17.
The goal was: one binary, no dependencies, real tools.

Stats:
- ~160 source files, ~3MB compiled binary
- 51 tools (file I/O, shell, git, search, web, agents)
- 83 commands
- Runs LLMs locally (llama.cpp + CUDA) or via API
- RAG with FAISS for codebase search
- WebSocket server for remote control

It's essentially Cursor/Claude Code but as a native terminal app with
local inference support. MIT licensed.

The interesting engineering bits:
- Concurrent tool execution with thread pool
- Token budgeting and history compression
- SSD expert streaming for MoE models
- Plugin system with MCP protocol support

https://github.com/Blitzball996/CloseCrab-Unified

Feedback welcome, especially on the tool design.
```

---

### 3.4 V2EX (创造节点)

**标题：**
```
开源了一个本地优先的 AI 编程助手 CloseCrab，C++17 单二进制，51 个工具，GPU 加速
```

**正文：**

```
做了一个终端 AI 编程助手，类似 Cursor / Claude Code，但：

1. 本地优先 — 代码不出你的机器，用 llama.cpp 跑本地模型
2. 单二进制 3MB — 不需要 Node/Python/Docker
3. GPU 加速 — CUDA + ONNX Runtime
4. 51 个工具 — 文件操作、Shell、Git、搜索、RAG、多 Agent
5. 手机控制 — 配套 Web 前端，手机随时操控

技术栈：C++17, llama.cpp, FAISS, ONNX Runtime, WebSocket

可以加载任何 GGUF 模型本地推理，也可以一行配置切换到 Claude/OpenAI API。

GitHub: https://github.com/Blitzball996/CloseCrab-Unified
手机端: https://github.com/Blitzball996/CloseCrab-Web

MIT 开源，欢迎 Star 和反馈。

目前在 3090 上跑 Qwen2.5-Coder-32B 效果不错，日常编程任务基本够用。
```

---

### 3.5 Twitter/X Thread

```
🧵 I built a local-first AI coding assistant in C++17.

Single binary. 51 tools. GPU-accelerated. Control from your phone.

Here's why and how: ↓

1/ The problem: Cursor costs $20/mo, Claude Code sends your code to the cloud,
Aider needs Python. I wanted something native, private, and fast.

2/ CloseCrab is a ~3MB terminal app. It runs LLMs locally via llama.cpp
with CUDA, or connects to any API. One config change to switch.

3/ 51 built-in tools: file editing, shell execution, git operations,
web search, code search with RAG (FAISS), multi-agent collaboration.

4/ The killer feature nobody else has: a companion mobile web app.
Control your coding assistant from your phone via Tailscale/ZeroTier.
Mini-games while you wait for responses. Yes, really.

5/ Tech: C++17, no runtime deps, concurrent tool execution,
token budgeting, history compression, SSD expert streaming for MoE models.

6/ It's MIT licensed and free forever.

⭐ https://github.com/Blitzball996/CloseCrab-Unified
📱 https://github.com/Blitzball996/CloseCrab-Web

Try it. Break it. Tell me what sucks.
```

---

### 3.6 掘金 / 少数派

**标题：**
```
我用 C++17 写了个本地 AI 编程助手，单文件 3MB，51 个工具，可以手机远程控制
```

**文章大纲：**

1. 为什么做这个（Cursor 贵、代码隐私、想要原生性能）
2. 功能演示（配 GIF/截图）
3. 架构设计（简要）
4. 和 Cursor/Claude Code/Aider 的对比表格
5. 手机控制功能展示
6. 如何安装使用
7. 未来计划
8. GitHub 链接

---

## 四、SEO 和 GitHub 优化

### 4.1 GitHub Topics

**CloseCrab-Unified:**
```
ai-coding-assistant, local-llm, cpp, cuda, terminal, coding-agent,
llama-cpp, rag, developer-tools, cursor-alternative
```

**CloseCrab-Web:**
```
mobile, remote-control, terminal, web-ui, ai-coding, nodejs, websocket
```

### 4.2 GitHub Description（仓库简介）

**CloseCrab-Unified:**
```
Local-first AI coding assistant. Single C++17 binary, 51 tools, GPU-accelerated LLM inference, RAG, multi-agent. Privacy-first alternative to Cursor.
```

**CloseCrab-Web:**
```
Mobile web interface for CloseCrab — control your AI coding assistant from your phone via Tailscale/ZeroTier.
```

### 4.3 Social Preview

制作一张 1280x640 的 Open Graph 图片，包含：
- CloseCrab logo
- 标语 "Local-first AI Coding Assistant"
- 关键数字：51 Tools | 3MB | GPU Accelerated

---

## 五、Awesome Lists 提交

提交到以下列表（每个列表提一个 PR）：

- [awesome-ai-tools](https://github.com/mahseema/awesome-ai-tools)
- [awesome-llm](https://github.com/Hannibal046/Awesome-LLM)
- [awesome-developer-tools](https://github.com/moimikey/awesome-devtools)
- [awesome-cpp](https://github.com/fffaraz/awesome-cpp)
- [awesome-selfhosted](https://github.com/awesome-selfhosted/awesome-selfhosted)

---

## 六、时间线

| 时间 | 动作 | 优先级 |
|------|------|--------|
| 第 1 天 | 设置 GitHub Topics + Description + Social Preview | 高 |
| 第 1 天 | 录制 30 秒 Demo GIF（用 VHS 或 asciinema） | 高 |
| 第 2 天 | 发 Hacker News Show HN | 高 |
| 第 2 天 | 发 Reddit r/LocalLLaMA | 高 |
| 第 3 天 | 发 V2EX + 掘金 | 中 |
| 第 3 天 | 发 Twitter thread | 中 |
| 第 4-5 天 | 提交 Awesome Lists PR | 中 |
| 第 7 天 | 发 Reddit r/programming | 中 |
| 第 14 天 | Product Hunt 发布 | 低 |
| 持续 | 回复评论、收集反馈、迭代 | 高 |

---

## 七、关键指标

- GitHub Stars 增长
- README 页面访问量（Insights → Traffic）
- Release 下载次数
- Issue/PR 数量（社区活跃度）
- 各平台帖子的 upvote/评论数

---

## 八、注意事项

1. **Demo GIF 是最重要的** — 没有视觉展示，90% 的人不会往下看
2. **Hacker News 标题不要用 emoji** — 会被降权
3. **Reddit 不要同一天发多个 sub** — 会被标记为 spam
4. **V2EX 不要过度营销** — 技术内容为主，链接放最后
5. **回复每一条评论** — 早期互动率决定帖子排名
