# CloseCrab-Unified × CodePilot GUI 整合计划

> 目标: CloseCrab-Unified.exe 启动即弹出图形界面窗口，拥有 CodePilot 完整的 GUI 和功能
> 基于 CodePilot v0.48.0 (Electron + Next.js 16 + React 19) 与 CloseCrab-Unified (C++17)
> 生成日期: 2026-04-10
> 前置: 第一轮 (JackProAi) 和第二轮 (claude-code-source) 整合均已完成

---

## 一、整合目标

用户双击 `CloseCrab-Unified.exe` 后:
1. 弹出一个原生窗口，内嵌 WebView 渲染 CodePilot 的完整 GUI
2. 左侧边栏: 会话列表 + 导航 (Skills/MCP/Gallery/Bridge)
3. 中间主区域: 聊天界面 (消息流/工具执行/权限确认/代码高亮)
4. 右侧面板: 文件树/Git/终端/仪表盘/助手
5. 顶栏: 会话标题 + Git 分支 + 面板切换按钮
6. 所有 CodePilot 功能完整可用: 17+ Provider、分屏、图片生成、IM Bridge 等
7. 同时保留终端模式 (通过 `--cli` 参数启动)

---

## 二、架构方案

```
┌─────────────────────────────────────────────────────┐
│                 CloseCrab-Unified.exe                │
│                                                     │
│  ┌──────────────┐    ┌───────────────────────────┐  │
│  │  C++ 后端     │    │  WebView2 窗口             │  │
│  │              │    │                           │  │
│  │  httplib     │◄──►│  CodePilot 前端            │  │
│  │  :9001       │SSE │  (React + Tailwind)       │  │
│  │              │REST│                           │  │
│  │  QueryEngine │    │  静态文件从 exe 内嵌       │  │
│  │  46 Tools    │    │  资源或 webui/ 目录加载    │  │
│  │  81 Commands │    │                           │  │
│  │  SQLite      │    │  localhost:9001/api/*      │  │
│  │  RAG/LLM     │    │  localhost:9001/           │  │
│  └──────────────┘    └───────────────────────────┘  │
│                                                     │
│  启动流程:                                           │
│  1. main() 启动 httplib 服务器 (端口 9001)            │
│  2. 注册所有 /api/* REST 路由                         │
│  3. 挂载 webui/ 静态文件服务                          │
│  4. 创建 WebView2 窗口，导航到 http://127.0.0.1:9001 │
│  5. 窗口关闭时退出进程                                │
└─────────────────────────────────────────────────────┘
```

### 为什么选 WebView2 而不是 Qt/wxWidgets/Dear ImGui

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| WebView2 (Edge) | 直接复用 CodePilot 的 React 前端; Windows 10+ 自带; 体积小 (~2MB SDK) | 仅 Windows (Linux 用 WebKitGTK) | ✅ 选择 |
| Qt | 跨平台; 原生控件 | 需要完全重写 UI (~100 组件); 体积大 (~50MB); 许可证复杂 | ❌ |
| Dear ImGui | 轻量; C++ 原生 | 不适合复杂 Web 风格 UI; 需重写所有组件 | ❌ |
| 内嵌 Electron | 功能完整 | 体积巨大 (~150MB); 与 C++ 后端通信复杂 | ❌ |

---

## 三、前端改造 — 从 Electron+Next.js 到纯静态 SPA

### 3.1 核心思路

CodePilot 当前架构: `Electron → Next.js Server (Node.js) → React 前端`

改造后架构: `C++ httplib 服务器 → 静态 React SPA 前端`

需要做的:
1. 将 Next.js App Router 改为纯客户端 SPA (去掉 SSR/API Routes)
2. 所有 API 调用指向 C++ 后端 `http://127.0.0.1:9001/api/*`
3. 用 Vite 或 Next.js `output: 'export'` 构建为纯静态文件 (HTML/JS/CSS)
4. 静态文件由 C++ httplib 服务或嵌入 exe 资源

### 3.2 前端文件改造清单

#### 3.2.1 需要保留并改造的 (核心 UI)

| 目录/文件 | 文件数 | 改造内容 |
|-----------|--------|---------|
| src/components/ui/ | 27 | Radix UI 基础组件 — 原样保留 |
| src/components/chat/ | ~15 | 聊天 UI — 去掉 Next.js router 依赖，改用 React Router |
| src/components/ai-elements/ | ~12 | AI 响应渲染 (代码块/工具/推理) — 原样保留 |
| src/components/layout/ | ~10 | AppShell/侧边栏/顶栏/面板 — 去掉 Next.js Link，改用 React Router |
| src/components/settings/ | ~6 | 设置面板 — API 调用改为 fetch C++ 后端 |
| src/components/bridge/ | ~6 | IM Bridge UI — 原样保留 |
| src/components/plugins/ | ~4 | MCP/插件管理 — 原样保留 |
| src/components/gallery/ | ~4 | 画廊 — 原样保留 |
| src/components/project/ | ~3 | 文件树 — 原样保留 |
| src/components/git/ | ~4 | Git 面板 — 原样保留 |
| src/components/terminal/ | ~2 | 终端 — WebSocket 连接 C++ 后端 |
| src/components/assistant/ | ~3 | 助手工作区 — 原样保留 |
| src/hooks/ | ~30 | React Hooks — 去掉 Electron IPC 调用，改为 HTTP fetch |
| src/types/ | 1 | 类型定义 — 原样保留 |
| src/i18n/ | 2 | 国际化 — 原样保留 |
| src/lib/db.ts | 1 | **删除** — 数据库操作全部由 C++ 后端处理 |
| src/lib/claude-client.ts | 1 | **删除** — SDK 调用由 C++ 后端处理 |
| src/lib/stream-session-manager.ts | 1 | 改造: 消费 C++ 后端的 SSE 流 |
| src/lib/files.ts | 1 | **删除** — 文件操作由 C++ 后端 API 处理 |
| src/lib/bridge/ | ~10 | **删除** — Bridge 逻辑移到 C++ 后端 |

#### 3.2.2 需要删除的 (后端逻辑，由 C++ 替代)

| 目录/文件 | 原因 |
|-----------|------|
| src/app/api/ (52+ 文件) | Next.js API Routes → 由 C++ httplib 路由替代 |
| src/lib/db.ts | SQLite 操作 → C++ sqlite3 |
| src/lib/claude-client.ts | Claude SDK → C++ QueryEngine |
| src/lib/provider-resolver.ts | Provider 解析 → C++ ProviderResolver |
| src/lib/image-generator.ts | 图片生成 → C++ ImageGenerator |
| src/lib/job-executor.ts | 批量任务 → C++ JobExecutor |
| src/lib/error-classifier.ts | 错误分类 → C++ ErrorClassifier |
| src/lib/provider-doctor.ts | Provider 诊断 → C++ ProviderDoctor |
| src/lib/context-assembler.ts | 上下文组装 → C++ ContextAssembler |
| src/lib/context-compressor.ts | 上下文压缩 → C++ HistoryCompactor |
| src/lib/task-scheduler.ts | 任务调度 → C++ CronScheduler |
| src/lib/bridge/ (全部) | Bridge 逻辑 → C++ Bridge 模块 |
| src/lib/channels/ (全部) | 频道插件 → C++ ChannelAdapter |
| electron/ (全部) | Electron 主进程 → C++ main() + WebView2 |

#### 3.2.3 构建方式改造

```
当前 (CodePilot):
  Next.js build → .next/standalone/ (需要 Node.js 运行时)

改造后:
  方案 A: Next.js output: 'export' → out/ (纯静态 HTML/JS/CSS)
  方案 B: 迁移到 Vite + React Router → dist/ (纯静态)

推荐方案 B (Vite):
  - 更轻量，无 SSR 开销
  - 构建速度快
  - 输出纯静态文件，httplib 直接 serve
  - 不依赖 Node.js 运行时
```

### 3.3 前端与 C++ 后端通信协议

```
前端 (WebView2 中的 React SPA)
    │
    ├── REST API (fetch)
    │   GET/POST/PATCH/DELETE http://127.0.0.1:9001/api/*
    │   Content-Type: application/json
    │
    ├── SSE (EventSource)
    │   POST http://127.0.0.1:9001/api/chat → text/event-stream
    │   事件类型: text, tool_use, tool_result, tool_output,
    │            status, result, permission_request, error, done
    │
    └── WebSocket (终端)
        ws://127.0.0.1:9001/ws/terminal
        双向: 命令输入 ↔ 输出流
```

---

## 四、C++ 后端新增 — 为 GUI 提供完整 API

### 4.1 HTTP API 路由 (替代 Next.js API Routes)

C++ httplib 服务器需实现 CodePilot 的全部 52+ 端点:

#### 4.1.1 聊天 API

| 端点 | 方法 | 功能 | C++ 实现 |
|------|------|------|---------|
| /api/chat | POST | 流式对话 (SSE) | QueryEngine::submitMessage() → SSE 事件流 |
| /api/chat/messages | POST | 保存消息 | SessionManager::saveMessage() |
| /api/chat/messages | PUT | 更新消息 | SessionManager::updateMessage() |
| /api/chat/sessions | GET | 列出会话 | SessionManager::listSessions() |
| /api/chat/sessions | POST | 创建会话 | SessionManager::createSession() |
| /api/chat/sessions/:id | GET | 获取会话 | SessionManager::getSession() |
| /api/chat/sessions/:id | PATCH | 更新会话 | SessionManager::updateSession() |
| /api/chat/sessions/:id | DELETE | 删除会话 | SessionManager::deleteSession() |
| /api/chat/sessions/:id/messages | GET | 加载历史消息 | SessionManager::getMessages() |
| /api/chat/interrupt | POST | 中断流式 | StreamSessionManager::interrupt() |
| /api/chat/rewind | POST | 回溯 | RewindManager::rewindTo() |
| /api/chat/mode | PATCH | 切换模式 | AppState::setMode() |
| /api/chat/model | PATCH | 切换模型 | AppState::setModel() |
| /api/chat/permission | PATCH | 切换权限 | PermissionEngine::setMode() |

#### 4.1.2 Provider API

| 端点 | 方法 | 功能 | C++ 实现 |
|------|------|------|---------|
| /api/providers | GET | 列出 Provider | ProviderResolver::listProviders() |
| /api/providers | POST | 创建 Provider | ProviderResolver::addProvider() |
| /api/providers/:id | PATCH | 更新 Provider | ProviderResolver::updateProvider() |
| /api/providers/:id | DELETE | 删除 Provider | ProviderResolver::removeProvider() |
| /api/providers/models | GET | 可用模型列表 | ProviderResolver::listModels() |
| /api/providers/options | GET | Provider 选项 | ProviderResolver::getOptions() |
| /api/providers/:id/diagnose | POST | 诊断 | ProviderDoctor::diagnose() |

#### 4.1.3 文件/Git/设置 API

| 端点 | 方法 | 功能 | C++ 实现 |
|------|------|------|---------|
| /api/files/tree | GET | 文件树 | std::filesystem 递归遍历 |
| /api/files/preview | GET | 文件预览 | 读取文件 + 语法检测 |
| /api/files/browse | GET | 目录浏览 | std::filesystem::directory_iterator |
| /api/git/status | GET | Git 状态 | GitWrapper::status() |
| /api/git/branches | GET | 分支列表 | GitWrapper::branches() |
| /api/git/log | GET | 提交历史 | GitWrapper::log() |
| /api/git/worktrees | GET | Worktree 列表 | GitWrapper::worktrees() |
| /api/settings | GET | 读取设置 | SettingsManager::getAll() |
| /api/settings | POST | 更新设置 | SettingsManager::update() |
| /api/setup | GET | 检查初始化状态 | 检查配置/Provider 是否就绪 |

#### 4.1.4 Bridge/MCP/媒体 API

| 端点 | 方法 | 功能 | C++ 实现 |
|------|------|------|---------|
| /api/bridge | GET | Bridge 状态 | BridgeManager::getStatus() |
| /api/bridge/settings | POST | Bridge 配置 | BridgeManager::configure() |
| /api/bridge/channels | GET | 频道列表 | BridgeManager::listChannels() |
| /api/plugins/mcp | GET | MCP 服务器列表 | MCPClient::listServers() |
| /api/plugins/mcp | POST | 添加 MCP 服务器 | MCPClient::addServer() |
| /api/plugins/mcp/:id | DELETE | 删除 MCP 服务器 | MCPClient::removeServer() |
| /api/media/generate | POST | 生成图片 | ImageGenerator::generate() |
| /api/media/gallery | GET | 画廊列表 | MediaStore::list() |
| /api/media/:id | GET | 媒体详情 | MediaStore::get() |
| /api/media/:id | DELETE | 删除媒体 | MediaStore::remove() |
| /api/media/:id/favorite | PUT | 收藏切换 | MediaStore::toggleFavorite() |

#### 4.1.5 静态文件服务

```cpp
// httplib 挂载静态文件
server.set_mount_point("/", "webui/");  // React SPA 静态文件
server.set_mount_point("/assets", "webui/assets/");

// SPA fallback: 所有非 /api/* 路径返回 index.html
server.set_error_handler([](const auto& req, auto& res) {
    if (!req.path.starts_with("/api/")) {
        res.set_content(read_file("webui/index.html"), "text/html");
        res.status = 200;
    }
});
```

### 4.2 WebView2 窗口管理

```
新增文件:
  src/gui/WebViewWindow.h/.cpp     — WebView2 窗口创建与管理
  src/gui/TrayIcon.h/.cpp          — 系统托盘图标 (最小化到托盘)
  src/gui/NativeDialogs.h/.cpp     — 原生对话框 (文件选择/消息框)

修改文件:
  src/main.cpp                     — 增加 GUI 启动模式
  CMakeLists.txt                   — 链接 WebView2 SDK
```

**WebView2 窗口核心逻辑:**

```cpp
// src/gui/WebViewWindow.h 概要
class WebViewWindow {
public:
    void create(int width = 1280, int height = 860);
    void navigate(const std::string& url);
    void setTitle(const std::string& title);
    void minimize();
    void maximize();
    void close();

    // C++ ↔ JS 双向通信 (用于原生功能)
    void postMessage(const std::string& json);
    void onMessage(std::function<void(const std::string&)> callback);

private:
    ICoreWebView2* webview_ = nullptr;
    HWND hwnd_ = nullptr;
};
```

**main.cpp 启动流程改造:**

```cpp
int main(int argc, char* argv[]) {
    // 解析参数
    bool cliMode = hasFlag(argc, argv, "--cli");

    // 启动 HTTP 服务器 (两种模式都需要)
    HTTPServer server;
    registerAllAPIRoutes(server);       // 注册 /api/* 路由
    if (!cliMode) {
        server.mountStaticFiles("webui/"); // GUI 模式挂载前端
    }
    server.startAsync(9001);

    if (cliMode) {
        // 终端模式 (原有逻辑)
        runTerminalREPL();
    } else {
        // GUI 模式
        WebViewWindow window;
        window.create(1280, 860);
        window.navigate("http://127.0.0.1:9001");
        window.runMessageLoop();  // 阻塞直到窗口关闭
    }

    server.stop();
    return 0;
}
```

### 4.3 WebView2 ↔ C++ 原生功能桥接

某些功能需要原生能力，无法通过 HTTP API 实现:

| 功能 | 实现方式 |
|------|---------|
| 原生文件选择对话框 | WebView2 postMessage → C++ GetOpenFileName → 返回路径 |
| 系统托盘 | C++ Shell_NotifyIcon (无需前端参与) |
| 窗口最小化/最大化/关闭 | WebView2 postMessage → C++ ShowWindow |
| 系统通知 | C++ Shell_NotifyIcon 气泡通知 |
| 剪贴板 | WebView2 navigator.clipboard (浏览器原生) |
| 拖拽文件 | WebView2 drag-and-drop 事件 → C++ 处理 |
| 自动更新 | C++ WinHTTP 下载 + 替换 exe |

```javascript
// 前端调用原生功能示例
window.chrome.webview.postMessage({
    type: 'native:openFolder',
    id: 'req-123'
});

window.chrome.webview.addEventListener('message', (e) => {
    if (e.data.id === 'req-123') {
        // e.data.path = "G:\\MyProject"
        setWorkingDirectory(e.data.path);
    }
});
```

<!-- PLACEHOLDER_SECTION_5 -->


