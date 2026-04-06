# CloseCrab-Unified — 本地 AI 编程助手

一个用 C++ 编写的本地优先 AI 编程助手。可以在你自己的 GPU 上运行大语言模型，也可以连接 Claude、OpenAI 等远程 API — 只需改一行配置。AI 拥有 42 个工具：读写文件、执行命令、搜索代码、多智能体协作，全部受权限系统保护。

[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CUDA](https://img.shields.io/badge/CUDA-12.x-green.svg)](https://developer.nvidia.com/cuda-toolkit)
[![Windows](https://img.shields.io/badge/Platform-Windows-0078d7.svg)](https://www.microsoft.com/)

---

## 这是什么？

大多数 AI 编程工具需要联网。CloseCrab-Unified 给你选择权：在本地 GPU 上运行模型（零网络依赖），或者连接 Claude/OpenAI/任何兼容 API。无论哪种方式，AI 都能使用同一套 42 个工具。

它融合了两个项目：**CloseCrab**（C++ 本地推理引擎，带 RAG 和 MoE 流式加载）和 **JackProAi-claudecode**（TypeScript CLI，40+ 工具和 100+ 命令）。最终产出一个 C++ 单文件可执行程序。

### 为什么用它？

- **隐私**：本地模式下所有数据留在你的机器上
- **灵活**：改一行配置就能在本地和远程模型之间切换
- **真工具**：AI 不只是聊天 — 它能读文件、写代码、跑测试、搜索网络
- **可扩展**：42 个内置工具 + 插件系统 + MCP 协议 + 技能目录
- **快**：C++17，CUDA GPU 加速，无 Python 运行时，单个可执行文件

---

## 快速开始

### 方式 1：安装器

下载 `CloseCrab-Unified_Setup.exe`，安装时选择本地或 API 模式，自动配置。

### 方式 2：手动运行

```bash
cd G:\CMakePJ\CloseCrab-Unified
# 编辑 config/config.yaml（见下文）
.\run.bat
```

---

## 切换本地模式和 API 模式

这是最常用的操作。三种方式任选。

### 方法 1：编辑 config/config.yaml（推荐）

用任何文本编辑器打开 `config/config.yaml`：

```yaml
# ========== 本地模式 ==========
# 在你的 GPU 上运行 GGUF 模型，不需要网络
provider: "local"
llm:
  model_path: "models/qwen2.5-7b-instruct-q4_k_m.gguf"

# ========== API 模式 (Anthropic) ==========
# 使用 Claude API 或兼容的中转站
# provider: "anthropic"
# api:
#   base_url: "https://api.anthropic.com"
#   api_key: "sk-ant-你的密钥"
#   model: "claude-sonnet-4-20250514"

# ========== API 模式 (OpenAI 兼容) ==========
# 支持 OpenAI、LM Studio、SiliconFlow、Ollama、vLLM
# provider: "openai"
# api:
#   base_url: "http://127.0.0.1:1234"
#   api_key: "你的密钥"
#   model: "qwen2.5-7b"
```

切换方法：取消注释你想用的模式，注释掉其他的。重启程序生效。

### 方法 2：环境变量

```bash
set ANTHROPIC_AUTH_TOKEN=sk-你的密钥
set ANTHROPIC_BASE_URL=https://你的中转站.com
set ANTHROPIC_MODEL=claude-opus-4-20250514
closecrab-unified.exe -c config/config.yaml
```

### 方法 3：命令行参数

```bash
closecrab-unified.exe --provider anthropic --api-key sk-xxx --api-url https://api.anthropic.com --api-model claude-sonnet-4-20250514
```

### 优先级

命令行参数 > 环境变量 > config.yaml

### 运行时命令

程序运行中可以用这些命令查看当前配置：

```
/provider    — 查看当前提供商
/api         — 查看 API 配置帮助
/model       — 查看当前模型名
/doctor      — 完整诊断信息
```

---

## 提供商参考

| 提供商 | `provider` | `api.base_url` | `api.model` |
|--------|-----------|----------------|-------------|
| 本地 GGUF | `local` | — | 设置 `llm.model_path` |
| Anthropic Claude | `anthropic` | `https://api.anthropic.com` | `claude-sonnet-4-20250514` |
| Claude 中转站 | `anthropic` | `https://你的中转站.com` | 中转站的模型名 |
| OpenAI | `openai` | `https://api.openai.com` | `gpt-4o` |
| LM Studio | `openai` | `http://127.0.0.1:1234` | 你加载的模型 |
| SiliconFlow | `openai` | `https://api.siliconflow.cn` | `moonshotai/Kimi-K2-Instruct` |
| Ollama | `openai` | `http://127.0.0.1:11434` | `qwen2.5` |

---

## 命令参考

| 命令 | 说明 |
|------|------|
| `/help` | 显示所有命令 |
| `/quit` `/exit` | 退出 |
| `/model [名称]` | 查看/切换模型 |
| `/provider` | 查看提供商 |
| `/api` | API 配置帮助 |
| `/status` | 会话状态 |
| `/doctor` | 诊断信息 |
| `/commit [消息]` | Git 提交 |
| `/diff` | Git 差异 |
| `/branch [名称]` | Git 分支 |
| `/log [N]` | Git 日志 |
| `/push` `/pull` | Git 推送/拉取 |
| `/history [N]` | 对话历史 |
| `/new` | 新会话 |
| `/export [文件]` | 导出对话 |
| `/compact` | 压缩历史 |
| `/rag [命令]` | RAG 管理 |
| `/permissions [模式]` | 权限模式 |
| `/plan` | 计划模式 |
| `/thinking [on/off]` | 思考模式 |
| `/fast` | 快速模式 |
| `/tools` | 工具列表 |
| `/cost` | 费用统计 |
| `/audit` | 审计日志 |

---

## 工具 (42 个)

**文件**: Read, Write, Edit, Glob, Grep, NotebookEdit
**执行**: Bash, PowerShell, REPL
**AI**: Agent (5 种类型), Skill
**MCP**: MCPTool, ListMcpResources, ReadMcpResource, McpAuth
**任务**: TaskCreate/Update/Get/List/Stop/Output
**团队**: TeamCreate, TeamDelete
**工作流**: EnterPlanMode, ExitPlanMode, EnterWorktree, ExitWorktree, CronCreate/Delete/List, Sleep
**网络**: WebSearch, WebFetch
**交互**: AskUserQuestion, SendMessage, TodoWrite
**系统**: Config, LSP, RemoteTrigger, ToolSearch, Brief, SyntheticOutput

---

## 从源码构建

### 环境要求

- Windows 10/11, Visual Studio 2022, CMake 3.20+, CUDA 12.x (可选), vcpkg

### 构建

```bash
vcpkg install curl:x64-windows faiss:x64-windows zlib:x64-windows

cd G:\CMakePJ\CloseCrab-Unified
cmake -B build -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

---

## 故障排查

| 问题 | 解决 |
|------|------|
| "Failed to load config" | 从项目根目录运行，或用 `-c` 指定绝对路径 |
| "No model path" | 在 config.yaml 中设置 `llm.model_path`，用绝对路径 |
| HTTP 503 | 请求太大或模型名错误，检查 API 配置 |
| SSL/CURL 错误 | 程序自动跳过 SSL 验证 |
| 工具不执行 | 本地模型用 SKILL: 格式；远程 API 用原生 tool_use |

---

## 许可证

MIT
