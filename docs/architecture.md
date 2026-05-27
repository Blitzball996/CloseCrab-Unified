# CloseCrab-Unified Architecture

---

## 1. System Overview

```mermaid
graph TB
    subgraph Entry["Entry Points"]
        CLI["CLI Terminal"]
        HTTP["HTTP :8080"]
        WS["WebSocket :9001"]
        SSE["SSE :8081"]
    end

    subgraph Core["Core Engine"]
        QE["QueryEngine<br/>Multi-turn tool loop"]
        MSG["Message System<br/>6 types"]
        STATE["AppState<br/>Session + Cost + Modes"]
    end

    subgraph API["API Layer"]
        LOCAL["LocalLLMClient<br/>llama.cpp"]
        REMOTE["RemoteAPIClient<br/>Anthropic /v1/messages"]
        OAI["OpenAICompatClient<br/>/v1/chat/completions"]
    end

    subgraph Tools["42 Tools"]
        T_FILE["File: Read Write Edit Glob Grep"]
        T_EXEC["Exec: Bash PowerShell REPL"]
        T_AI["AI: Agent Skill"]
        T_MCP["MCP: Tool Resources Auth"]
        T_TASK["Task: Create Update Get List Stop"]
        T_WEB["Web: Search Fetch"]
        T_WF["Workflow: Plan Worktree Cron"]
    end

    subgraph Services["Services"]
        PERM["Permission Engine<br/>allow/deny/ask rules"]
        RAG["RAG<br/>FAISS + ONNX"]
        SSD_S["SSD Streaming<br/>MoE experts"]
        MCP_S["MCP Client<br/>JSON-RPC stdio"]
        PLUGIN["Plugin Manager<br/>Skill Directory"]
        AGENT["Agent Manager<br/>5 types"]
    end

    subgraph Storage["Storage"]
        DB[("SQLite<br/>Sessions + Memory")]
        CONFIG["config.yaml"]
        SETTINGS[".claude/settings.json"]
    end

    Entry --> QE
    QE --> API
    QE --> Tools
    QE --> PERM
    QE --> MSG
    QE --> STATE

    LOCAL --> RAG
    LOCAL --> SSD_S
    Tools --> MCP_S
    Tools --> AGENT
    Tools --> PLUGIN

    STATE --> DB
    QE --> CONFIG
    PERM --> SETTINGS

    style Core fill:#e8eaf6
    style API fill:#e3f2fd
    style Tools fill:#e8f5e9
    style Services fill:#fff3e0
    style Storage fill:#f5f5f5
```

---

## 2. Request Processing Flow

```mermaid
flowchart TD
    INPUT["User Input"] --> CMD{"Starts with /?"}
    CMD -->|Yes| EXEC_CMD["CommandRegistry.execute()"]
    CMD -->|No| QE["QueryEngine.submitMessage()"]

    QE --> BUILD["Build System Prompt<br/>+ CLAUDE.md<br/>+ Tool definitions"]
    BUILD --> API_CALL["APIClient.streamChat()<br/>SSE streaming"]

    API_CALL --> EVENTS{"Stream Events"}
    EVENTS -->|TEXT| DISPLAY["Print to terminal"]
    EVENTS -->|THINKING| THINK["Show thinking (dimmed)"]
    EVENTS -->|TOOL_USE| TOOL_FLOW["Tool Execution"]
    EVENTS -->|STOP| CHECK{"Stop reason?"}

    TOOL_FLOW --> VALIDATE["validateInput()"]
    VALIDATE --> PERM_CHECK["PermissionEngine.check()"]
    PERM_CHECK -->|ALLOWED| CALL["Tool.call()"]
    PERM_CHECK -->|ASK| ASK["Ask user Y/N"]
    PERM_CHECK -->|DENIED| DENY["Return error"]
    ASK -->|Y| CALL
    ASK -->|N| DENY
    CALL --> RESULT["Add tool result to messages"]
    DENY --> RESULT

    CHECK -->|end_turn| DONE["Save to memory"]
    CHECK -->|tool_use| RESULT
    RESULT --> CONTINUE{"Local model?"}
    CONTINUE -->|Yes| SUMMARY["One summary turn, then stop"]
    CONTINUE -->|No| API_CALL
    SUMMARY --> DONE

    style INPUT fill:#e3f2fd
    style DONE fill:#c8e6c9
    style DENY fill:#ffcdd2
```

---

## 3. API Client Selection

```mermaid
flowchart LR
    CONFIG["config.yaml<br/>provider: ?"] --> SWITCH{"provider"}

    SWITCH -->|local| LOCAL["LocalLLMClient<br/>llama.cpp + CUDA<br/>Qwen ChatML format<br/>SKILL: tool parsing"]
    SWITCH -->|anthropic| REMOTE["RemoteAPIClient<br/>POST /v1/messages<br/>SSE streaming<br/>Native tool_use"]
    SWITCH -->|openai| OAI["OpenAICompatClient<br/>POST /v1/chat/completions<br/>SSE streaming<br/>Function calling"]

    LOCAL --> ENGINE["LLMEngine<br/>GPU inference"]
    REMOTE --> CURL1["libcurl<br/>x-api-key header"]
    OAI --> CURL2["libcurl<br/>Bearer token"]

    style LOCAL fill:#e8f5e9
    style REMOTE fill:#e3f2fd
    style OAI fill:#fff3e0
```

---

## 4. Configuration Priority

```mermaid
flowchart TB
    CLI["CLI Arguments<br/>--provider --api-key"] --> |highest| FINAL["Final Config"]
    ENV["Environment Variables<br/>ANTHROPIC_AUTH_TOKEN"] --> |medium| FINAL
    YAML["config/config.yaml<br/>provider: api:"] --> |lowest| FINAL

    FINAL --> INIT["Initialize APIClient"]

    style CLI fill:#c8e6c9
    style FINAL fill:#e8eaf6
```

---

## 5. Permission System

```mermaid
flowchart TD
    TOOL["Tool call request"] --> DENY_R{"Deny rules match?"}
    DENY_R -->|Yes| BLOCKED["DENIED"]
    DENY_R -->|No| ALLOW_R{"Allow rules match?"}
    ALLOW_R -->|Yes| PASS["ALLOWED"]
    ALLOW_R -->|No| MODE{"Permission mode?"}

    MODE -->|bypass| PASS
    MODE -->|auto| AUTO{"Read-only?"}
    MODE -->|default| DEFAULT{"Read-only?"}

    AUTO -->|Yes| PASS
    AUTO -->|No| DESTRUCT{"Destructive?"}
    DESTRUCT -->|Yes| ASK["Ask user"]
    DESTRUCT -->|No| PASS

    DEFAULT -->|Yes| PASS
    DEFAULT -->|No| ASK

    ASK -->|Allow| PASS
    ASK -->|Deny| BLOCKED
    ASK -->|Always allow| SAVE_A["Save allow rule"] --> PASS
    ASK -->|Always deny| SAVE_D["Save deny rule"] --> BLOCKED

    PASS --> LOG["Audit log"]
    BLOCKED --> LOG

    style PASS fill:#c8e6c9
    style BLOCKED fill:#ffcdd2
```

---

## 6. Multi-Agent System

```mermaid
flowchart TD
    PARENT["Parent QueryEngine"] --> AGENT_TOOL["AgentTool.call()"]
    AGENT_TOOL --> TYPE{"Agent type?"}

    TYPE -->|general-purpose| GP["All tools available"]
    TYPE -->|explore| EXP["Read-only: Glob Grep Read"]
    TYPE -->|plan| PLAN["Read-only + AskUser"]
    TYPE -->|verification| VER["Read + Bash (testing)"]
    TYPE -->|code-guide| CG["Read + Search"]

    GP --> SPAWN["Spawn thread<br/>New QueryEngine"]
    EXP --> SPAWN
    PLAN --> SPAWN
    VER --> SPAWN
    CG --> SPAWN

    SPAWN --> EXEC["Execute prompt"]
    EXEC --> RESULT["Return result to parent"]

    style PARENT fill:#e8eaf6
    style SPAWN fill:#e3f2fd
```

---

## 7. MCP Protocol

```mermaid
sequenceDiagram
    participant App as CloseCrab
    participant MCP as MCP Server (stdio)

    App->>MCP: initialize {protocolVersion, capabilities}
    MCP-->>App: {capabilities, serverInfo}
    App->>MCP: notifications/initialized

    App->>MCP: tools/list
    MCP-->>App: {tools: [{name, description, inputSchema}]}

    App->>MCP: tools/call {name, arguments}
    MCP-->>App: {content: [{type: "text", text: "..."}]}

    App->>MCP: resources/list
    MCP-->>App: {resources: [{uri, name, mimeType}]}

    App->>MCP: resources/read {uri}
    MCP-->>App: {contents: [{text: "..."}]}
```

---

## 8. RAG Pipeline

```mermaid
flowchart LR
    subgraph Ingest["Document Ingestion"]
        DOC["Text files"] --> CHUNK["Chunk (500 chars)"]
        CHUNK --> EMBED["EmbeddingEngine<br/>bge-small-zh ONNX"]
        EMBED --> STORE["FAISS Index + SQLite"]
    end

    subgraph Query["Search"]
        Q["User query"] --> Q_EMBED["Embed query"]
        Q_EMBED --> SEARCH["FAISS top-K search"]
        SEARCH --> RERANK["RerankerEngine<br/>bge-reranker ONNX"]
        RERANK --> RESULTS["Top-5 documents"]
    end

    RESULTS --> PROMPT["Inject into system prompt"]

    style Ingest fill:#e3f2fd
    style Query fill:#e8f5e9
```

---

## 9. SSD Expert Streaming (MoE)

```mermaid
flowchart TD
    TOKEN["Process token"] --> ROUTER["Router: select top-K experts"]
    ROUTER --> GPU_HIT{"GPU cache hit?"}

    GPU_HIT -->|Yes| USE["Use cached weights"]
    GPU_HIT -->|No| RAM_HIT{"RAM LRU hit?"}

    RAM_HIT -->|Yes| UPLOAD["Upload to GPU"]
    RAM_HIT -->|No| SSD["Read from NVMe SSD"]

    SSD --> RAM["Store in RAM LRU"]
    RAM --> UPLOAD
    UPLOAD --> USE

    USE --> FFN["FFN computation"]
    FFN --> PREFETCH{"Prefetch enabled?"}
    PREFETCH -->|Yes| PREDICT["Predict next layer experts"]
    PREDICT --> ASYNC["Async preload"]

    style USE fill:#c8e6c9
    style SSD fill:#ffe0b2
```

---

## 10. Module Dependency Graph

```mermaid
graph TD
    MAIN["main.cpp"] --> QE["QueryEngine"]
    MAIN --> CMD["CommandRegistry"]
    MAIN --> TOOL_REG["ToolRegistry"]
    MAIN --> PERM["PermissionEngine"]
    MAIN --> SETTINGS["SettingsManager"]
    MAIN --> MCP_MGR["MCPServerManager"]
    MAIN --> SKILL_DIR["SkillDirectory"]
    MAIN --> PLUGIN_MGR["PluginManager"]

    QE --> API_CLIENT["APIClient"]
    QE --> TOOL_REG
    QE --> PERM
    QE --> MEMORY["MemorySystem"]
    QE --> STATE["AppState"]

    API_CLIENT --> LOCAL_LLM["LocalLLMClient"]
    API_CLIENT --> REMOTE_API["RemoteAPIClient"]
    API_CLIENT --> OAI_API["OpenAICompatClient"]

    LOCAL_LLM --> LLM_ENGINE["LLMEngine"]
    LLM_ENGINE --> SSD_STREAM["SSDExpertStreamer"]

    REMOTE_API --> STREAM_PARSER["StreamParser"]
    REMOTE_API --> CURL["libcurl"]
    OAI_API --> STREAM_PARSER
    OAI_API --> CURL

    TOOL_REG --> TOOLS["42 Tools"]
    TOOLS --> AGENT_MGR["AgentManager"]
    TOOLS --> MCP_MGR
    TOOLS --> GIT["GitManager"]
    TOOLS --> LSP["LSPClient"]

    MEMORY --> SQLITE["SQLite"]
    SETTINGS --> JSON["nlohmann/json"]

    style MAIN fill:#e8eaf6
    style QE fill:#e3f2fd
    style TOOLS fill:#e8f5e9
```

---

## 11. Tool Execution Detail

```mermaid
sequenceDiagram
    participant User
    participant QE as QueryEngine
    participant API as APIClient
    participant TR as ToolRegistry
    participant PE as PermissionEngine
    participant Tool as Tool.call()

    User->>QE: "Create hello.txt with Hello World"
    QE->>API: streamChat(messages, systemPrompt)
    API-->>QE: TOOL_USE {name:"Write", input:{file_path, content}}

    QE->>TR: getTool("Write")
    TR-->>QE: WriteFileTool*

    QE->>QE: validateInput(input)
    QE->>PE: check("Write", action, isReadOnly=false)
    PE-->>QE: ASK_USER

    QE->>User: "Allow Write? [y/N]"
    User-->>QE: "y"

    QE->>Tool: call(ctx, {file_path, content})
    Tool-->>QE: ToolResult{success, "File written"}

    QE->>QE: Add tool result to messages
    Note over QE: Local model: one summary turn then stop
    Note over QE: Remote API: continue multi-turn loop

    QE->>API: streamChat(messages + tool_result)
    API-->>QE: "Done! Created hello.txt"
    QE-->>User: "Done! Created hello.txt"
```

---

## 12. Streaming Architecture

```mermaid
flowchart LR
    subgraph Local["Local Model"]
        LLM["LLMEngine<br/>llama.cpp"] -->|token callback| FILTER["Filter special tokens<br/>im_start im_end"]
        FILTER --> PARSE["Parse SKILL: format"]
    end

    subgraph Remote["Remote API"]
        CURL_R["libcurl"] -->|SSE chunks| SSE_PARSE["StreamParser<br/>event: data:"]
        SSE_PARSE --> JSON_PARSE["Parse JSON events<br/>content_block_delta<br/>tool_use"]
    end

    PARSE --> EVENTS["StreamEvent"]
    JSON_PARSE --> EVENTS

    EVENTS --> QE_HANDLER["QueryEngine<br/>event handler"]
    QE_HANDLER -->|EVT_TEXT| PRINT["Print to terminal"]
    QE_HANDLER -->|EVT_TOOL_USE| EXEC["Execute tool"]
    QE_HANDLER -->|EVT_STOP| DONE["End turn"]

    style Local fill:#e8f5e9
    style Remote fill:#e3f2fd
```

---

## 13. File Structure

```
CloseCrab-Unified/
├── src/
│   ├── main.cpp                    # Entry, init, main loop
│   ├── core/
│   │   ├── QueryEngine.h/.cpp      # Core: multi-turn tool loop
│   │   ├── Message.h/.cpp          # 6 message types
│   │   ├── AppState.h              # Global state
│   │   └── CostTracker.h           # Token cost tracking
│   ├── api/
│   │   ├── APIClient.h             # Abstract interface
│   │   ├── LocalLLMClient.h/.cpp   # llama.cpp wrapper
│   │   ├── RemoteAPIClient.h/.cpp  # Anthropic API
│   │   ├── OpenAICompatClient.h/.cpp # OpenAI format
│   │   └── StreamParser.h/.cpp     # SSE parser
│   ├── tools/                      # 42 tools (header-only)
│   ├── commands/                   # 36 commands
│   ├── agents/AgentManager.h/.cpp  # 5 agent types
│   ├── mcp/MCPClient.h/.cpp        # MCP JSON-RPC client
│   ├── plugins/PluginManager.h     # Plugin + skill loader
│   ├── permissions/PermissionEngine.h/.cpp
│   ├── rag/                        # FAISS + ONNX
│   ├── ssd/SSDExpertStreamer.h/.cpp # MoE streaming
│   ├── llm/LLMEngine.h/.cpp        # llama.cpp engine
│   ├── network/                    # HTTP, WS, SSE servers
│   ├── git/GitManager.h            # Git CLI wrapper
│   ├── lsp/LSPClient.h/.cpp        # LSP protocol
│   ├── bridge/BridgeClient.h/.cpp  # Remote execution (HTTP + reconnect)
│   ├── voice/VoiceEngine.h         # Voice TTS (SAPI/say/espeak)
│   ├── coordinator/Coordinator.h   # Multi-agent task decomposition
│   ├── hooks/HookManager.h         # PreToolUse/PostToolUse event hooks
│   ├── ui/TerminalUI.h             # Spinner, Markdown renderer, Table formatter
│   ├── ui/VimMode.h                # Vim Normal/Insert/Command modes
│   └── utils/ProcessRunner.h       # Cross-platform process execution
├── config/config.yaml              # Main configuration
├── installer.iss                   # Inno Setup installer
├── run.bat                         # Launch script
├── download_model.bat              # Model downloader
├── icons/                          # App icons
└── docs/                           # Documentation
```

---

## 14. New Components (v0.2.0)

### 14.1 HistoryCompactor

```mermaid
graph LR
    QE["QueryEngine<br/>submitMessage loop"] -->|每轮检查| HC["HistoryCompactor"]
    HC -->|estimateTokens| API["APIClient.countTokens"]
    HC -->|超过75%阈值| COMPACT["performCompaction"]
    COMPACT -->|远程API| LLM_SUM["LLM 摘要"]
    COMPACT -->|本地模型| LOCAL_SUM["统计摘要"]
    COMPACT --> NEW_MSGS["压缩后消息列表<br/>= 摘要 + 最近N条"]
```

- 自动触发：每轮 API 调用前检查 token 总量
- 阈值：`maxContextTokens * 0.75`（默认 128k * 0.75 = 96k）
- 保留最近 10 条消息，旧消息压缩为 1 条 SYSTEM 摘要
- 不会在 tool_use/tool_result 对中间切割

### 14.2 HookManager

```mermaid
sequenceDiagram
    participant QE as QueryEngine
    participant HM as HookManager
    participant Shell as Shell Command
    participant Tool as Tool

    QE->>HM: fire(PRE_TOOL_USE, "Bash")
    HM->>Shell: HOOK_TOOL=Bash HOOK_EVENT=PreToolUse <command>
    Shell-->>HM: exit code
    alt exit code != 0
        HM-->>QE: blocked=true
        QE->>QE: 跳过 tool 执行
    else exit code == 0
        QE->>Tool: call()
        Tool-->>QE: ToolResult
        QE->>HM: fire(POST_TOOL_USE, "Bash")
    end
```

### 14.3 Coordinator

```mermaid
graph TB
    USER["用户任务"] --> COORD["Coordinator"]
    COORD -->|LLM 分解| SUBTASKS["子任务列表<br/>(JSON array)"]
    SUBTASKS --> A1["Agent: explore"]
    SUBTASKS --> A2["Agent: plan"]
    SUBTASKS --> A3["Agent: general-purpose"]
    A1 -->|并行执行| RESULTS["结果收集"]
    A2 --> RESULTS
    A3 --> RESULTS
    RESULTS -->|LLM 综合| SYNTHESIS["最终回答"]
```

### 14.4 FileMemoryManager

```mermaid
graph LR
    subgraph ".claude/memory/"
        INDEX["MEMORY.md<br/>(索引)"]
        M1["user_role.md"]
        M2["feedback_testing.md"]
        M3["project_auth.md"]
    end
    INDEX -->|加载到| SP["System Prompt"]
    QE["QueryEngine.buildSystemPrompt()"] --> SP
```

每个 .md 文件包含 YAML frontmatter（name, description, type）+ 正文内容。

### 14.5 ProcessSandbox

```
Windows:
  CreateJobObject → SetInformationJobObject(内存/CPU限制) → AssignProcessToJobObject

Linux:
  fork → setrlimit(RLIMIT_AS, RLIMIT_CPU, RLIMIT_FSIZE) → exec
```

### 14.6 Terminal UI 组件

| 组件 | 文件 | 功能 |
|------|------|------|
| Spinner | ui/TerminalUI.h | Tool 执行时旋转动画 |
| MarkdownRenderer | ui/TerminalUI.h | 代码块/标题/粗体 ANSI 着色 |
| TableFormatter | ui/TerminalUI.h | /status, /cost 对齐表格 |
| InputHistory | ui/TerminalUI.h | 输入历史记录 |
| VimInput | ui/VimMode.h | Normal/Insert/Command 模式切换 |

### 14.7 API Error & Retry

```mermaid
graph TD
    API_CALL["API 调用"] --> CURL["CURL 执行"]
    CURL -->|成功| PARSE["解析 SSE"]
    CURL -->|失败| CLASSIFY["classifyHttpStatus"]
    CLASSIFY -->|401/403| AUTH["AuthError<br/>不重试"]
    CLASSIFY -->|429| RATE["RateLimitError<br/>重试 (>=2s)"]
    CLASSIFY -->|500/502/503| SERVER["ServerError<br/>重试"]
    CLASSIFY -->|529| OVERLOAD["OverloadedError<br/>重试"]
    RATE -->|withRetry| BACKOFF["指数退避<br/>1s→2s→4s"]
    SERVER --> BACKOFF
    OVERLOAD --> BACKOFF
    BACKOFF -->|最多3次| API_CALL
```
