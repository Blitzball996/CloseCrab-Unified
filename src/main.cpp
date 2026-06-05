#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>   // _NSGetExecutablePath
#elif defined(__linux__)
#include <unistd.h>        // readlink
#endif

// Version string — injected by CMake (target_compile_definitions). Fallback keeps
// non-CMake builds compiling; CMake is the single source of truth.
#ifndef CLOSECRAB_VERSION
#define CLOSECRAB_VERSION "dev"
#endif

#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <CLI/CLI.hpp>

#include "core/QueryEngine.h"
#include "core/AppState.h"
#include "core/SessionManager.h"
#include "core/SessionRouter.h"
#include "core/ThreadPool.h"
#include "memory/MemorySystem.h"
#include "llm/LLMEngine.h"
#include "config/Config.h"
#include "config/SettingsManager.h"
#include "tools/ToolRegistry.h"
#include "commands/CommandRegistry.h"
#include "permissions/PermissionEngine.h"
#include "api/APIClient.h"
#include "api/LocalLLMClient.h"
#include "api/RemoteAPIClient.h"
#include "api/OpenAICompatClient.h"

// Tools
// FileReadTool: when built with USE_ENHANCED_FILE_READ (default ON), the
// enhanced variant adds native image (stb), PDF, Jupyter notebook, security
// checks and smart error hints. USE_ASYNC_FILE_READ swaps in the thread-pool
// batch variant. Both fall back to the base tool when the flags are off.
#if defined(CLOSECRAB_ASYNC_FILE_READ)
#include "tools/FileReadTool/FileReadTool_Async.h"
#elif defined(CLOSECRAB_ENHANCED_FILE_READ)
#include "tools/FileReadTool/FileReadTool_Enhanced.h"
#else
#include "tools/FileReadTool/FileReadTool.h"
#endif
#include "tools/FileWriteTool/FileWriteTool.h"
#include "tools/FileEditTool/FileEditTool.h"
#include "tools/GlobTool/GlobTool.h"
#include "tools/GrepTool/GrepTool.h"
#include "tools/BashTool/BashTool.h"
#include "tools/AskUserQuestionTool/AskUserQuestionTool.h"
#include "tools/TodoWriteTool/TodoWriteTool.h"
#include "tools/TaskTools/TaskTools.h"
#include "tools/WebTools/WebTools.h"
#include "tools/MiscTools/MiscTools.h"
#include "tools/AgentTool/AgentTool.h"
#include "tools/ToolSearchTool/ToolSearchTool.h"
#include "agents/AgentManager.h"
#include "services/AgentProgress.h"
#include "tools/WorkflowTools/WorkflowTools.h"
#include "tools/CronTools/CronTools.h"
#include "tools/MCPTools/MCPTools.h"
#include "tools/SkillTool/SkillTool.h"
#include "tools/NotebookEditTool/NotebookEditTool.h"
#include "tools/LSPTool/LSPTool.h"
#include "tools/PowerShellTool/PowerShellTool.h"
#include "tools/REPLTool/REPLTool.h"
#include "tools/TeamTools/TeamTools.h"
#include "tools/ConfigTool/ConfigTool.h"
#include "tools/SystemTools/SystemTools.h"
#include "tools/DiscoverSkillsTool/DiscoverSkillsTool.h"
#include "tools/SnipTool/SnipTool.h"
#include "tools/VerifyPlanTool/VerifyPlanTool.h"
#include "tools/WebBrowserTool/WebBrowserTool.h"
#include "tools/MonitorTool/MonitorTool.h"
#include "tools/ReverseTool/ReverseTool.h"
#include "tools/ImageInputTool/ImageInputTool.h"
#include "tools/DebuggerTool/DebuggerTool.h"
#include "tools/SystemInfoTool/SystemInfoTool.h"
#include "tools/ExtraTools/ExtraTools.h"
#include "mcp/MCPClient.h"
#include "plugins/PluginManager.h"
#include "core/CostTracker.h"
#include "core/PolicyLimits.h"
#include "git/GitManager.h"
#include "voice/VoiceEngine.h"
#include "hooks/HookManager.h"
#include "memory/MemoryExtractor.h"
#include "ui/TerminalUI.h"
#include "ui/VimMode.h"
#include "network/HttpServer.h"
#include "network/MobileWebSocket.h"
#include "ui/KeyboardSelector.h"
#include "ui/OutputCollapse.h"
#include "utils/SyntaxHighlight.h"

// Commands
#include "commands/GitCommands.h"
#include "commands/SessionCommands.h"
#include "services/SessionAutoSave.h"
#include "commands/AdvancedCommands.h"
#include "commands/ExtendedCommands.h"

#include <curl/curl.h>
#include "license/LicenseGate.h"
#include "ui/ConsoleInputGuard.h"
#include "ui/PosixTty.h"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>

namespace fs = std::filesystem;
using namespace closecrab;

static std::unique_ptr<QueryEngine> g_queryEngine;

// Console-input ownership is now tracked by the shared ConsoleInputGuard
// (ui/ConsoleInputGuard.h): consoleInputBusy() is true while ANY interactive
// prompt (permission prompt, AskUserQuestion — both via KeyboardSelector) is
// reading the console. The per-turn key-watcher checks it and yields so two
// threads never call _getch() on the same console (that races and hard-crashes
// — the "typing during AI generation" 闪退). See KeyboardSelector::select.

static void signalHandler(int) {
    if (g_queryEngine) g_queryEngine->interrupt();
}

// Register built-in commands
static void registerBuiltinCommands(CommandRegistry& reg) {
    reg.registerCommand(std::make_unique<SimpleCommand>(
        "help", "Show available commands",
        [](const std::string&, CommandContext& ctx) {
            auto& reg = CommandRegistry::getInstance();
            ctx.print(reg.getHelpText());
            return CommandResult::ok();
        }
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "quit", "Exit the program",
        [](const std::string&, CommandContext&) {
            return CommandResult::quit();
        },
        std::vector<std::string>{"exit", "q"}
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "clear", "Clear conversation history",
        [](const std::string&, CommandContext& ctx) {
            if (ctx.queryEngine) ctx.queryEngine->clearMessages();
            ctx.print("Conversation cleared.\n");
            return CommandResult::ok();
        }
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "model", "Show or change current model",
        [](const std::string& args, CommandContext& ctx) {
            if (args.empty()) {
                ctx.print("Current model: " + ctx.appState->currentModel + "\n");
            } else {
                ctx.appState->currentModel = args;
                ctx.print("Model set to: " + args + "\n");
            }
            return CommandResult::ok();
        }
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "status", "Show session status",
        [](const std::string&, CommandContext& ctx) {
            TableFormatter table;
            table.addRow({"Property", "Value"});
            table.addRow({"Session", ctx.queryEngine->getSessionId()});
            table.addRow({"Model", ctx.appState->currentModel});
            table.addRow({"Messages", std::to_string(ctx.queryEngine->getMessages().size())});

            std::ostringstream cost;
            cost << std::fixed << std::setprecision(4) << "$" << ctx.appState->getTotalCost();
            table.addRow({"Cost", cost.str()});
            table.addRow({"Permissions", PermissionEngine::getInstance().getModeName()});
            table.addRow({"Plan mode", ctx.appState->planMode ? "ON" : "OFF"});
            table.addRow({"Thinking", ctx.appState->thinkingConfig.enabled ? "ON" : "OFF"});
            ctx.print(table.render());
            return CommandResult::ok();
        }
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "permissions", "Show or change permission mode (default/auto/bypass)",
        [](const std::string& args, CommandContext& ctx) {
            auto& pe = PermissionEngine::getInstance();
            if (args.empty()) {
                ctx.print("Permission mode: " + pe.getModeName() + "\n");
            } else if (args == "default") {
                pe.setMode(PermissionMode::DEFAULT);
                ctx.print("Permission mode set to: default\n");
            } else if (args == "auto") {
                pe.setMode(PermissionMode::AUTO);
                ctx.print("Permission mode set to: auto\n");
            } else if (args == "bypass") {
                pe.setMode(PermissionMode::BYPASS);
                ctx.print("Permission mode set to: bypass\n");
            } else {
                ctx.print("Usage: /permissions [default|auto|bypass]\n");
            }
            return CommandResult::ok();
        }
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "tools", "List available tools",
        [](const std::string&, CommandContext& ctx) {
            ctx.print(ctx.toolRegistry->toSystemPromptDescription());
            return CommandResult::ok();
        },
        std::vector<std::string>{"skills"}
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "cost", "Show API cost summary",
        [](const std::string&, CommandContext& ctx) {
            auto& tracker = CostTracker::getInstance();

            TableFormatter table;
            table.addRow({"Model", "Input", "Output", "Cost"});

            std::lock_guard<std::mutex> lock(ctx.appState->usageMutex);
            for (const auto& [model, usage] : ctx.appState->modelUsage) {
                std::ostringstream cost;
                cost << std::fixed << std::setprecision(4) << "$" << usage.costUSD;
                table.addRow({
                    model,
                    std::to_string(usage.inputTokens) + " tok",
                    std::to_string(usage.outputTokens) + " tok",
                    cost.str()
                });
            }

            std::ostringstream total;
            total << std::fixed << std::setprecision(4) << "$" << tracker.getTotalCost();
            table.addRow({"TOTAL", "", "", total.str()});

            ctx.print(table.render());

            // Show context usage estimate
            int msgTokens = 0;
            for (const auto& m : ctx.queryEngine->getMessages()) {
                msgTokens += (int)m.getText().size() / 4;
            }
            ctx.print("\nContext: ~" + std::to_string(msgTokens) + " tokens in " +
                      std::to_string(ctx.queryEngine->getMessages().size()) + " messages\n");

            return CommandResult::ok();
        }
    ));

    reg.registerCommand(std::make_unique<SimpleCommand>(
        "audit", "Show permission audit log",
        [](const std::string&, CommandContext& ctx) {
            auto log = PermissionEngine::getInstance().getAuditLog();
            if (log.empty()) { ctx.print("No audit entries.\n"); }
            else { for (const auto& e : log) ctx.print(e + "\n"); }
            return CommandResult::ok();
        }
    ));
}

#ifdef _WIN32
static std::string getUserInput() {
    std::string line;
    wchar_t buf[65536];
    DWORD read;
    if (ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), buf, 65535, &read, nullptr)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, buf, read, nullptr, 0, nullptr, nullptr);
        std::string utf8(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, buf, read, &utf8[0], len, nullptr, nullptr);
        // Trim trailing newline
        while (!utf8.empty() && (utf8.back() == '\n' || utf8.back() == '\r')) utf8.pop_back();
        return utf8;
    }
    std::getline(std::cin, line);
    return line;
}
#else
static std::string getUserInput() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}
#endif

// ---- Per-turn console input mode ------------------------------------------
// While an AI turn is streaming we put the console into "raw" input mode: no
// echo, no line buffering. Combined with the Esc-only key-watcher this is what
// makes "during generation you can ONLY press Esc" actually hold — typed keys
// neither appear on screen nor pile up in the input buffer to leak into the
// next prompt (or race the streaming output and crash). Restored after the turn.
#ifdef _WIN32
static DWORD g_savedConsoleMode = 0;
static bool  g_consoleModeSaved = false;
static void enterTurnInputMode() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hIn, &mode)) {
        g_savedConsoleMode = mode;
        g_consoleModeSaved = true;
        // Drop echo + line input; keep processed input so Ctrl-C still works.
        SetConsoleMode(hIn, mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
    }
    FlushConsoleInputBuffer(hIn);  // discard anything typed just before the turn
}
static void leaveTurnInputMode() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    FlushConsoleInputBuffer(hIn);  // discard anything typed DURING the turn
    if (g_consoleModeSaved) {
        SetConsoleMode(hIn, g_savedConsoleMode);
        g_consoleModeSaved = false;
    }
}
#else
// POSIX: put the terminal in raw mode for the turn so the Esc-watcher can read
// keys immediately and typed chars don't echo/leak. PosixTty restores cooked
// mode (and flushes pending input) on leave. Only the main thread calls these.
static void enterTurnInputMode() { closecrab::PosixTty::instance().enterRaw(); }
static void leaveTurnInputMode() { closecrab::PosixTty::instance().leaveRaw(); }
#endif

// Load CLAUDE.md from project root
static std::string loadClaudeMd(const std::string& projectRoot) {
    fs::path p = fs::path(projectRoot) / "CLAUDE.md";
    if (!fs::exists(p)) return "";
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// ---- Cross-platform config resolution -------------------------------------
// Directory containing the running executable (NOT the cwd). Used so a globally
// installed binary finds its sibling config/ folder on Windows.
static fs::path getExecutableDir() {
    try {
#if defined(_WIN32)
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) return fs::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
        char buf[4096];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return fs::canonical(fs::path(buf)).parent_path();
#elif defined(__linux__)
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; return fs::path(buf).parent_path(); }
#endif
    } catch (...) {}
    return fs::current_path();
}

// User home directory (~). On macOS/Linux this is where ~/.crab lives.
static fs::path getHomeDir() {
#if defined(_WIN32)
    if (const char* up = std::getenv("USERPROFILE")) return fs::path(up);
    const char* hd = std::getenv("HOMEDRIVE");
    const char* hp = std::getenv("HOMEPATH");
    if (hd && hp) return fs::path(std::string(hd) + hp);
#else
    if (const char* h = std::getenv("HOME")) return fs::path(h);
#endif
    return fs::current_path();
}

// Default config.yaml shipped when none exists yet. Mirrors config/config.yaml.
// IMPORTANT: provider defaults to "anthropic" so a fresh install talks to the
// hosted API out of the box (a brand-new machine has no local .gguf model).
static const char* kDefaultConfigYaml = R"YAML(# CloseCrab-Unified Configuration (auto-generated on first run)

server:
  port: 9001
  host: "127.0.0.1"

database:
  path: "data/closecrab.db"

logging:
  level: "info"

# Provider: local, anthropic, openai
provider: "anthropic"

api:
  base_url: "https://yikoulian.cc/"
  api_key: ""
  model: "claude-opus-4-7"
  fallback_model: "claude-sonnet-4-20250514"
  cache_ttl_minutes: 5

# HTTP Proxy (optional). Also reads env: CLOSECRAB_PROXY, https_proxy, HTTPS_PROXY.
# Set CLOSECRAB_NO_PROXY=1 to force a direct connection ignoring proxy env vars.
proxy: ""

llm:
  model_path: "models/deepseek-moe-16b-chat.Q4_K_M.gguf"
  max_tokens: 4096
  temperature: 0.7

rag:
  embedding_model_path: "models/bge-small-zh/onnx/model_quantized.onnx"
  embedding_tokenizer_path: "models/bge-small-zh/tokenizer.json"
  reranker_model_path: "models/bge-reranker-base/onnx/model_uint8.onnx"
  reranker_tokenizer_path: "models/bge-reranker-base/tokenizer.json"
)YAML";

// The platform-canonical config location — also where we auto-create on first run.
//   Windows      : <exe_dir>/config/config.yaml   (sibling of the installed exe)
//   macOS/Linux  : ~/.crab/config.yaml            (same home as ~/.crab/settings.json)
static fs::path defaultConfigPath() {
#if defined(_WIN32)
    return getExecutableDir() / "config" / "config.yaml";
#else
    return getHomeDir() / ".crab" / "config.yaml";
#endif
}

// The single directory that holds ALL app-internal files (config, data/, logs,
// trace.log, crash.log). The process chdir's here at startup so the many relative
// paths scattered through the codebase ("data/...", "trace.log", "closecrab.log")
// all land in ONE place instead of wherever the user happened to launch from.
//   Windows      : <exe_dir>            (everything beside the installed exe)
//   macOS/Linux  : ~/.crab             (one tidy home folder)
//   Dev/portable : the launch cwd, IF it already has config/config.yaml — keeps
//                  source-checkout and portable-folder runs behaving as before.
static fs::path appDataDir(const fs::path& userCwd) {
    std::error_code ec;
    if (fs::exists(userCwd / "config" / "config.yaml", ec)) return userCwd;
#if defined(_WIN32)
    return getExecutableDir();
#else
    return getHomeDir() / ".crab";
#endif
}

// Resolve which config.yaml to load, creating a default if none is found.
// Search order (when the user did NOT pass -c/--config):
//   1. ./config/config.yaml             — running from a dev/source checkout
//   2. platform-canonical location      — global install (see defaultConfigPath)
//   3. <exe_dir>/config/config.yaml     — fallback bundle next to the binary (all OS)
//   4. /usr/local/etc/closecrab/...     — macOS/Linux .pkg install location
// If none exist, write kDefaultConfigYaml to the platform-canonical location.
static std::string resolveConfigPath() {
    std::vector<fs::path> candidates;
    candidates.push_back(fs::path("config") / "config.yaml");      // cwd (dev)
    candidates.push_back(defaultConfigPath());                      // canonical
    candidates.push_back(getExecutableDir() / "config" / "config.yaml");
#if !defined(_WIN32)
    candidates.push_back(fs::path("/usr/local/etc/closecrab/config.yaml"));
#endif
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) return c.string();
    }
    // None found → create the canonical default so the app is usable immediately.
    fs::path target = defaultConfigPath();
    try {
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target);
        if (out.is_open()) {
            out << kDefaultConfigYaml;
            out.close();
            spdlog::info("No config found — created default at: {}", target.string());
            return target.string();
        }
        spdlog::warn("Could not write default config to {} — using built-in defaults", target.string());
    } catch (const std::exception& e) {
        spdlog::warn("Config auto-create failed ({}) — using built-in defaults", e.what());
    }
    return target.string();  // load() will fail gracefully and fall back to getters' defaults
}


int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Crash handler: write diagnostic info to crash.log before dying.
    // This captures the exact reason for "闪退" that's otherwise invisible.
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        FILE* f = fopen("crash.log", "a");
        if (f) {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            fprintf(f, "\n=== CRASH at %s", ctime(&t));
            fprintf(f, "Exception code: 0x%08lX\n", ep->ExceptionRecord->ExceptionCode);
            fprintf(f, "Address: 0x%p\n", ep->ExceptionRecord->ExceptionAddress);
            fprintf(f, "Fault address: 0x%p\n", (void*)ep->ExceptionRecord->ExceptionInformation[1]);
            fprintf(f, "Access type: %s\n", ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" : "WRITE");
            fprintf(f, "RIP: 0x%p\n", (void*)ep->ContextRecord->Rip);
            fprintf(f, "RSP: 0x%p\n", (void*)ep->ContextRecord->Rsp);
            fprintf(f, "RBP: 0x%p\n", (void*)ep->ContextRecord->Rbp);
            fflush(f);
            fclose(f);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });

    // Also catch std::terminate (from uncaught exceptions in threads)
    std::set_terminate([]() {
        FILE* f = fopen("crash.log", "a");
        if (f) {
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            fprintf(f, "\n=== TERMINATE at %s", ctime(&t));
            try { std::rethrow_exception(std::current_exception()); }
            catch (const std::exception& e) { fprintf(f, "Exception: %s\n", e.what()); }
            catch (...) { fprintf(f, "Unknown exception\n"); }
            fflush(f);
            fclose(f);
        }
        std::abort();
    });
#endif

    // Initialize CURL globally BEFORE any threads are created.
    // curl_global_init() is NOT thread-safe — if called implicitly from
    // curl_easy_init() while the Spinner thread is running, it causes a
    // race condition (segfault on Windows).
    curl_global_init(CURL_GLOBAL_ALL);

    // ---- CLI arguments ----
    CLI::App app{"CloseCrab-Unified - Local AI Assistant"};
    std::string configPath;   // empty = auto-resolve; set only when user passes -c
    std::string modelPath;
    std::string apiKey;
    std::string apiBaseUrl;
    std::string apiModel;
    std::string provider;
    bool verbose = false;

    app.add_option("-c,--config", configPath, "Config file path");
    app.add_option("-m,--model", modelPath, "Local model path override");
    app.add_option("--api-key", apiKey, "API key");
    app.add_option("--api-url", apiBaseUrl, "API base URL");
    app.add_option("--api-model", apiModel, "API model name");
    app.add_option("--provider", provider, "Provider: local, anthropic, openai");
    app.add_flag("-v,--verbose", verbose, "Verbose logging");
    // License / activation (序列号在线激活, see docs/序列号格式规范.md)
    std::string activateKey;
    std::string activateOfflineBlob;
    bool deactivate = false;
    bool licenseStatus = false;
    bool showDeviceId = false;
    app.add_option("--activate", activateKey, "Activate with a license key (CCST/CCPR-...) and exit");
    app.add_option("--activate-offline", activateOfflineBlob,
                   "Activate offline with a signed code (token|sig|edition) and exit");
    app.add_flag("--device-id", showDeviceId, "Print this machine's Device ID (for offline activation) and exit");
    app.add_flag("--deactivate", deactivate, "Remove local activation and exit");
    app.add_flag("--license-status", licenseStatus, "Show license / trial status and exit");
    CLI11_PARSE(app, argc, argv);

    // ---- License commands (handle and exit before loading the app) ----
    if (showDeviceId) {
        std::cout << closecrab::LicenseGate::deviceId() << std::endl;
        return 0;
    }
    if (!activateKey.empty()) {
        auto res = closecrab::LicenseGate::activate(activateKey);
        std::cout << res.message << std::endl;
        return res.ok ? 0 : 1;
    }
    if (!activateOfflineBlob.empty()) {
        auto res = closecrab::LicenseGate::activateOffline(activateOfflineBlob);
        std::cout << res.message << std::endl;
        return res.ok ? 0 : 1;
    }
    if (deactivate) {
        closecrab::LicenseGate::deactivate();
        std::cout << "已清除本机激活信息。" << std::endl;
        return 0;
    }
    if (licenseStatus) {
        closecrab::LicenseGate::printStatus();
        return 0;
    }
    // ---- License gate: block / start trial countdown for unactivated copies ----
    if (!closecrab::LicenseGate::enforceAtStartup()) {
        return 1;
    }

    // ---- Consolidate all app-internal files into ONE directory --------------
    // Capture the user's launch directory FIRST (this stays the AI's project root
    // — file tools operate here via ctx.cwd, NOT the process cwd). Then chdir the
    // PROCESS into the app data dir so every relative path the app writes
    // (config/, data/, closecrab.log, trace.log, crash.log) lands in one place.
    std::string userProjectCwd = fs::current_path().string();
    {
        std::error_code ec;
        fs::path appDir = appDataDir(userProjectCwd);
        fs::create_directories(appDir, ec);
        fs::current_path(appDir, ec);   // portable chdir; AI still uses userProjectCwd
        if (ec) {
            // If chdir fails, stay put — files scatter but the app still runs.
            spdlog::warn("Could not switch to app dir {} ({})", appDir.string(), ec.message());
        }
    }

    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);

    // File logging: write all logs to closecrab.log so crash diagnostics survive
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("closecrab.log", true);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("multi", spdlog::sinks_init_list{console_sink, file_sink});
        logger->set_level(verbose ? spdlog::level::debug : spdlog::level::info);
        logger->flush_on(spdlog::level::warn); // Flush on warnings (ensures crash context is saved)
        spdlog::set_default_logger(logger);
    } catch (...) {
        // Fall back to console-only if file logging fails
    }

    // ---- Load config ----
    // If the user didn't pass -c/--config, resolve a per-OS location and
    // auto-create a default there on first run (Windows: <exe>/config/config.yaml,
    // macOS/Linux: ~/.crab/config.yaml).
    if (configPath.empty()) configPath = resolveConfigPath();
    auto& config = Config::getInstance();
    config.load(configPath);

    // The AI's project root = where the user launched (NOT the app data dir we
    // chdir'd into above). All file tools resolve paths against this via ctx.cwd.
    std::string cwd = userProjectCwd;

    // ---- Load settings.json ----
    auto& settings = SettingsManager::getInstance();
    settings.load(cwd);

    // ---- Initialize AppState ----
    AppState appState;
    appState.originalCwd = cwd;
    appState.projectRoot = cwd;
    appState.verbose = verbose;
    appState.configPath = configPath;   // so /api and the banner show the real path
    appState.claudeMdContent = loadClaudeMd(cwd);

    // ---- Initialize components ----
    SessionManager sessionMgr(config.getString("database.path", "data/closecrab.db"));
    MemorySystem memory(config.getString("database.path", "data/closecrab.db"));

    // ---- Create API client ----
    std::unique_ptr<LLMEngine> llmEngine;
    std::unique_ptr<APIClient> apiClient;

    // Priority: CLI args > env vars > config.yaml
    // 1. Read from config.yaml first
    if (provider.empty()) provider = config.getString("provider", "local");
    if (apiKey.empty()) apiKey = config.getString("api.api_key", "");
    if (apiBaseUrl.empty()) apiBaseUrl = config.getString("api.base_url", "");
    if (apiModel.empty()) apiModel = config.getString("api.model", "");

    // 2. Override with env vars
    if (apiKey.empty()) {
        const char* envKey = std::getenv("ANTHROPIC_AUTH_TOKEN");
        if (envKey) apiKey = envKey;
    }
    if (apiBaseUrl.empty()) {
        const char* envUrl = std::getenv("ANTHROPIC_BASE_URL");
        if (envUrl) apiBaseUrl = envUrl;
    }
    if (apiModel.empty()) {
        const char* envModel = std::getenv("ANTHROPIC_MODEL");
        if (envModel) apiModel = envModel;
    }
    {
        const char* envProvider = std::getenv("CLAUDE_LOCAL_PROVIDER");
        if (envProvider && !std::string(envProvider).empty()) provider = envProvider;
    }

    if (provider == "anthropic") {
        if (apiBaseUrl.empty()) apiBaseUrl = "https://api.anthropic.com";
        if (apiModel.empty()) apiModel = "claude-sonnet-4-20250514";
        auto remote = std::make_unique<RemoteAPIClient>(apiKey, apiBaseUrl, apiModel);
        // Set fallback model for 503/529 auto-downgrade
        std::string fallback = config.getString("api.fallback_model", "claude-sonnet-4-20250514");
        if (fallback != apiModel) remote->setFallbackModel(fallback);
        // Effective prompt-cache TTL (minutes). Default 5 = measured yikoulian cap
        // (it ignores ttl:1h). Set api.cache_ttl_minutes: 60 for the official
        // Anthropic API to actually get 1h cache survival. Drives cache_control
        // ttl + P1 time-based microcompact gap threshold.
        int cacheTtl = config.getInt("api.cache_ttl_minutes", 5);
        remote->setCacheTtlMinutes(cacheTtl);
        appState.currentModel = apiModel;
        spdlog::info("Using Anthropic API: {} ({}, fallback={})", apiBaseUrl, apiModel, fallback);
        apiClient = std::move(remote);
    } else if (provider == "openai" || provider == "lmstudio" || provider == "siliconflow") {
        if (apiBaseUrl.empty()) apiBaseUrl = "http://127.0.0.1:1234";
        if (apiModel.empty()) apiModel = "default";
        auto oai = std::make_unique<OpenAICompatClient>(apiKey, apiBaseUrl, apiModel);
        appState.currentModel = apiModel;
        spdlog::info("Using OpenAI-compatible API: {} ({})", apiBaseUrl, apiModel);
        apiClient = std::move(oai);
    } else {
        // Local LLM
        std::string mpath = modelPath.empty()
            ? config.getString("llm.model_path", "")
            : modelPath;
        if (!mpath.empty() && fs::exists(mpath)) {
            int cpuMoe = config.getInt("gpu.cpu_moe", 0);
            llmEngine = std::make_unique<LLMEngine>(mpath, cpuMoe);
            apiClient = std::make_unique<LocalLLMClient>(llmEngine.get());
            appState.currentModel = "local:" + fs::path(mpath).filename().string();
            spdlog::info("Using local LLM: {}", mpath);
        } else {
            spdlog::warn("No model path specified and no API configured.");
            spdlog::warn("Use --provider anthropic/openai or --model <path>");
            // Fall back to a dummy that errors
            apiClient = std::make_unique<RemoteAPIClient>("", "http://localhost", "none");
            appState.currentModel = "none";
        }
    }

    // ---- Initialize registries ----
    auto& toolRegistry = ToolRegistry::getInstance();
    auto& cmdRegistry = CommandRegistry::getInstance();
    auto& permEngine = PermissionEngine::getInstance();

    // Load permission rules from settings
    auto permRules = settings.getPermissionRules();
    if (!permRules.empty()) permEngine.loadRules(permRules);

    // Register built-in commands
    registerBuiltinCommands(cmdRegistry);

    // Register extended commands
    // Git
    cmdRegistry.registerCommand(std::make_unique<CommitCommand>());
    cmdRegistry.registerCommand(std::make_unique<DiffCommand>());
    cmdRegistry.registerCommand(std::make_unique<BranchCommand>());
    cmdRegistry.registerCommand(std::make_unique<LogCommand>());
    cmdRegistry.registerCommand(std::make_unique<PushCommand>());
    cmdRegistry.registerCommand(std::make_unique<PullCommand>());
    cmdRegistry.registerCommand(std::make_unique<StashCommand>());
    cmdRegistry.registerCommand(std::make_unique<UndoCommand>());
    // Session
    cmdRegistry.registerCommand(std::make_unique<SessionCommand>());
    cmdRegistry.registerCommand(std::make_unique<NewSessionCommand>());
    cmdRegistry.registerCommand(std::make_unique<ResumeCommand>());
    cmdRegistry.registerCommand(std::make_unique<HistoryCommand>());
    cmdRegistry.registerCommand(std::make_unique<ExportCommand>());
    cmdRegistry.registerCommand(std::make_unique<CompactCommand>());
    cmdRegistry.registerCommand(std::make_unique<ContextCommand>());
    cmdRegistry.registerCommand(std::make_unique<EnvCommand>());
    cmdRegistry.registerCommand(std::make_unique<VersionCommand>());
    cmdRegistry.registerCommand(std::make_unique<FastCommand>());
    cmdRegistry.registerCommand(std::make_unique<ThinkingCommand>());
    // Advanced
#ifdef CLOSECRAB_HAS_ONNX
    cmdRegistry.registerCommand(std::make_unique<RAGCommand>());
#endif
    cmdRegistry.registerCommand(std::make_unique<SSDCommand>());
    cmdRegistry.registerCommand(std::make_unique<SandboxCommand>());
    cmdRegistry.registerCommand(std::make_unique<PlanCommand>());
    cmdRegistry.registerCommand(std::make_unique<DoctorCommand>());
    cmdRegistry.registerCommand(std::make_unique<InitCommand>());
    cmdRegistry.registerCommand(std::make_unique<AddDirCommand>());
    cmdRegistry.registerCommand(std::make_unique<FilesCommand>());
    cmdRegistry.registerCommand(std::make_unique<ProviderCommand>());
    cmdRegistry.registerCommand(std::make_unique<ApiConfigCommand>());
    cmdRegistry.registerCommand(std::make_unique<ReloadCommand>());
    cmdRegistry.registerCommand(std::make_unique<BtwCommand>());
    // Extended commands (P2.5)
    cmdRegistry.registerCommand(std::make_unique<ReviewCommand>());
    cmdRegistry.registerCommand(std::make_unique<HooksCommand>());
    cmdRegistry.registerCommand(std::make_unique<MemoryCommand>());
    cmdRegistry.registerCommand(std::make_unique<TasksCommand>());
    cmdRegistry.registerCommand(std::make_unique<AgentsCommand>());
    cmdRegistry.registerCommand(std::make_unique<McpCommand>());
    // Extended commands batch 2
    cmdRegistry.registerCommand(std::make_unique<BriefCommand>());
    cmdRegistry.registerCommand(std::make_unique<PluginCommand>());
    cmdRegistry.registerCommand(std::make_unique<PrCommand>());
    cmdRegistry.registerCommand(std::make_unique<ShareCommand>());
    cmdRegistry.registerCommand(std::make_unique<SkillsCommand>());
    cmdRegistry.registerCommand(std::make_unique<CoordinatorCommand>());
    cmdRegistry.registerCommand(std::make_unique<VimCommand>());
    cmdRegistry.registerCommand(std::make_unique<VoiceCommand>());
    cmdRegistry.registerCommand(std::make_unique<ThemeCommand>());
    // Batch 3: high-value gap commands
    cmdRegistry.registerCommand(std::make_unique<IssueCommand>());
    cmdRegistry.registerCommand(std::make_unique<RenameCommand>());
    cmdRegistry.registerCommand(std::make_unique<CopyCommand>());
    cmdRegistry.registerCommand(std::make_unique<SummaryCommand>());
    cmdRegistry.registerCommand(std::make_unique<UsageCommand>());
    cmdRegistry.registerCommand(std::make_unique<EffortCommand>());
    cmdRegistry.registerCommand(std::make_unique<TagCommand>());
    cmdRegistry.registerCommand(std::make_unique<RewindCommand>());
    cmdRegistry.registerCommand(std::make_unique<PrCommentsCommand>());
    // Batch 4: medium-value gap commands
    cmdRegistry.registerCommand(std::make_unique<ThinkbackCommand>());
    cmdRegistry.registerCommand(std::make_unique<OutputStyleCommand>());
    cmdRegistry.registerCommand(std::make_unique<AutofixPrCommand>());
    cmdRegistry.registerCommand(std::make_unique<BughunterCommand>());
    cmdRegistry.registerCommand(std::make_unique<PassesCommand>());
    // v2 new commands
    cmdRegistry.registerCommand(std::make_unique<ConfigCommand>());
    cmdRegistry.registerCommand(std::make_unique<ModelCommand>());
    cmdRegistry.registerCommand(std::make_unique<CostCommand>());
    cmdRegistry.registerCommand(std::make_unique<PermissionsCommand>());
    cmdRegistry.registerCommand(std::make_unique<StatusCommand>());
    cmdRegistry.registerCommand(std::make_unique<ClearCommand>());
    cmdRegistry.registerCommand(std::make_unique<ForkCommand>());
    cmdRegistry.registerCommand(std::make_unique<SecurityReviewCommand>());
    cmdRegistry.registerCommand(std::make_unique<SandboxToggleCommand>());
    cmdRegistry.registerCommand(std::make_unique<KeybindingsCommand>());
    cmdRegistry.registerCommand(std::make_unique<PrivacySettingsCommand>());
    cmdRegistry.registerCommand(std::make_unique<RateLimitOptionsCommand>());
    cmdRegistry.registerCommand(std::make_unique<CommitPushPrCommand>());
    cmdRegistry.registerCommand(std::make_unique<ReleaseNotesCommand>());
    cmdRegistry.registerCommand(std::make_unique<StatsCommand>());
    cmdRegistry.registerCommand(std::make_unique<BridgeCommand>());
    cmdRegistry.registerCommand(std::make_unique<BuddyCommand>());
    cmdRegistry.registerCommand(std::make_unique<PeersCommand>());
    cmdRegistry.registerCommand(std::make_unique<WorkflowsCommand>());
    cmdRegistry.registerCommand(std::make_unique<OauthRefreshCommand>());
    spdlog::info("Registered {} commands", cmdRegistry.getCommandNames().size());
#if defined(CLOSECRAB_ASYNC_FILE_READ)
    toolRegistry.registerTool(std::make_unique<FileReadToolAsync>());
#elif defined(CLOSECRAB_ENHANCED_FILE_READ)
    toolRegistry.registerTool(std::make_unique<FileReadToolEnhanced>());
#else
    toolRegistry.registerTool(std::make_unique<FileReadTool>());
#endif
    toolRegistry.registerTool(std::make_unique<FileWriteTool>());
    toolRegistry.registerTool(std::make_unique<FileEditTool>());
    toolRegistry.registerTool(std::make_unique<GlobTool>());
    toolRegistry.registerTool(std::make_unique<GrepTool>());
    toolRegistry.registerTool(std::make_unique<BashTool>());
    toolRegistry.registerTool(std::make_unique<AskUserQuestionTool>());
    toolRegistry.registerTool(std::make_unique<TodoWriteTool>());
    toolRegistry.registerTool(std::make_unique<TaskCreateTool>());
    toolRegistry.registerTool(std::make_unique<TaskUpdateTool>());
    toolRegistry.registerTool(std::make_unique<TaskGetTool>());
    toolRegistry.registerTool(std::make_unique<TaskListTool>());
    toolRegistry.registerTool(std::make_unique<WebSearchTool>());
    toolRegistry.registerTool(std::make_unique<WebFetchTool>());
    toolRegistry.registerTool(std::make_unique<SleepTool>());
    toolRegistry.registerTool(std::make_unique<SendMessageTool>());
    toolRegistry.registerTool(std::make_unique<AgentTool>());
    toolRegistry.registerTool(std::make_unique<ToolSearchTool>());
    toolRegistry.registerTool(std::make_unique<EnterPlanModeTool>());
    toolRegistry.registerTool(std::make_unique<ExitPlanModeTool>());
    toolRegistry.registerTool(std::make_unique<EnterWorktreeTool>());
    toolRegistry.registerTool(std::make_unique<ExitWorktreeTool>());
    toolRegistry.registerTool(std::make_unique<CronCreateTool>());
    toolRegistry.registerTool(std::make_unique<CronDeleteTool>());
    toolRegistry.registerTool(std::make_unique<CronListTool>());
    toolRegistry.registerTool(std::make_unique<MCPTool>());
    toolRegistry.registerTool(std::make_unique<ListMcpResourcesTool>());
    toolRegistry.registerTool(std::make_unique<ReadMcpResourceTool>());
    toolRegistry.registerTool(std::make_unique<SkillTool>());
    toolRegistry.registerTool(std::make_unique<NotebookEditTool>());
    toolRegistry.registerTool(std::make_unique<LSPTool>());
    toolRegistry.registerTool(std::make_unique<PowerShellTool>());
    toolRegistry.registerTool(std::make_unique<MonitorTool>());
    toolRegistry.registerTool(std::make_unique<REPLTool>());
    toolRegistry.registerTool(std::make_unique<TeamCreateTool>());
    toolRegistry.registerTool(std::make_unique<TeamDeleteTool>());
    toolRegistry.registerTool(std::make_unique<ConfigTool>());
    toolRegistry.registerTool(std::make_unique<TaskStopTool>());
    toolRegistry.registerTool(std::make_unique<TaskOutputTool>());
    toolRegistry.registerTool(std::make_unique<RemoteTriggerTool>());
    // ToolSearchTool already registered above (defer/discover version)
    toolRegistry.registerTool(std::make_unique<McpAuthTool>());
    toolRegistry.registerTool(std::make_unique<BriefTool>());
    toolRegistry.registerTool(std::make_unique<SyntheticOutputTool>());
    toolRegistry.registerTool(std::make_unique<DiscoverSkillsTool>());
    toolRegistry.registerTool(std::make_unique<SnipTool>());
    toolRegistry.registerTool(std::make_unique<VerifyPlanTool>());
    toolRegistry.registerTool(std::make_unique<WebBrowserTool>());
    toolRegistry.registerTool(std::make_unique<ReverseTool>());
    toolRegistry.registerTool(std::make_unique<ImageInputTool>());
    toolRegistry.registerTool(std::make_unique<DebuggerTool>());
    toolRegistry.registerTool(std::make_unique<SystemInfoTool>());
    // P3 tools (claude-code parity)
    toolRegistry.registerTool(std::make_unique<CtxInspectTool>());
    toolRegistry.registerTool(std::make_unique<ListPeersTool>());
    toolRegistry.registerTool(std::make_unique<LocalMemoryRecallTool>());
    toolRegistry.registerTool(std::make_unique<PushNotificationTool>());
    toolRegistry.registerTool(std::make_unique<ReviewArtifactTool>());
    toolRegistry.registerTool(std::make_unique<SendUserFileTool>());
    toolRegistry.registerTool(std::make_unique<SubscribePRTool>());
    toolRegistry.registerTool(std::make_unique<VaultHttpFetchTool>());
    spdlog::info("Registered {} tools", toolRegistry.getToolNames().size());

    // Load MCP servers from settings
    auto mcpConfig = settings.getMcpServers();
    if (!mcpConfig.empty()) {
        MCPServerManager::getInstance().loadFromSettings(mcpConfig);
    }

    // Load hooks from settings
    auto hooksConfig = settings.getHooks();
    if (!hooksConfig.empty() && hooksConfig.is_array()) {
        HookManager::getInstance().loadFromSettings(hooksConfig);
    }

    // Load skills and plugins
    SkillDirectory::getInstance().loadFromDirectory(cwd);
    PluginManager::getInstance().loadFromDirectory(cwd);

    // ---- Create session ----
    std::string sessionId = sessionMgr.createSession("default");
    appState.sessionId = sessionId;

    // ---- Create QueryEngine ----
    QueryEngineConfig qeConfig;
    qeConfig.cwd = cwd;
    qeConfig.apiClient = apiClient.get();
    qeConfig.toolRegistry = &toolRegistry;
    qeConfig.commandRegistry = &cmdRegistry;
    qeConfig.permissionEngine = &permEngine;
    qeConfig.memorySystem = &memory;
    qeConfig.appState = &appState;
    qeConfig.systemPrompt = R"(You are CloseCrab, a powerful AI coding assistant. Respond in the user's language.
You have tools available for reading/writing files, running commands, searching code, and browsing the web.
When given a task, first understand what's needed. Ask clarifying questions if the request is ambiguous.
Then work step by step using your tools to complete the task.)";
    qeConfig.maxTurns = 50;
    qeConfig.verbose = verbose;

    g_queryEngine = std::make_unique<QueryEngine>(qeConfig);
    g_queryEngine->setSessionId(sessionId);

    std::signal(SIGINT, signalHandler);

    // ---- Print banner ----
    std::cout << "\n" << ansi::cyan() << "  CloseCrab-Unified" << ansi::reset()
              << " v" << CLOSECRAB_VERSION << "\n";
    std::cout << "  Model: " << ansi::bold() << appState.currentModel << ansi::reset() << "\n";
    // Show WHERE the config lives so users can edit provider / API key / relay URL.
    // (macOS/Linux: ~/.crab/config.yaml — otherwise hard to find.)
    std::cout << "  Config: " << ansi::dim() << configPath << ansi::reset() << "\n";
    std::cout << "  Type " << ansi::yellow() << "/help" << ansi::reset()
              << " for commands, " << ansi::yellow() << "/api" << ansi::reset()
              << " to set API key/URL, " << ansi::yellow() << "/quit" << ansi::reset() << " to exit.\n\n";

    // First-run hint: if a hosted provider is selected but no API key is set,
    // tell the user exactly how to configure it (the most common "it won't
    // connect" cause for a fresh install).
    if ((provider == "anthropic" || provider == "openai") && apiKey.empty()) {
        std::cout << ansi::yellow()
                  << "  ⚠ No API key configured.\n" << ansi::reset()
                  << "    Run " << ansi::yellow() << "/api key=sk-xxxx" << ansi::reset()
                  << " and " << ansi::yellow() << "/api url=https://your-relay.com" << ansi::reset()
                  << "\n    (or edit " << configPath << "), then restart.\n\n";
    }

    // ---- UI components ----
    // Spinner MUST be heap-allocated. Its thread accesses members via `this`
    // pointer; when on the stack, deep call chains (submitMessage → processToolUse
    // → tool->call) cause the spinner thread's std::string operations to corrupt
    // adjacent stack frames → deterministic ACCESS_VIOLATION 0x000003E800000003.
    auto spinnerPtr = std::make_unique<Spinner>();
    auto& spinner = *spinnerPtr;
    InputHistory inputHistory;
    VimInput vimInput;

    // Sub-agent activity sink (task B): print what each sub-agent is doing,
    // prefixed with its id, so multi-agent work isn't an opaque spinner.
    // Serialized by its own mutex; sub-agents run on worker threads.
    AgentActivitySink::getInstance().setHandler(
        [](const std::string& agentId, const std::string& line) {
            std::lock_guard<std::mutex> lock(getStdoutMutex());
            std::cout << "\r" << std::string(60, ' ') << "\r"
                      << ansi::cyan() << "  [" << agentId << "]" << ansi::reset()
                      << " " << ansi::dim() << line << ansi::reset() << "\n" << std::flush;
            closecrab::MobileWebSocket::getInstance().sendText(
                "[" + agentId + "] " + line + "\n");
        });

    // ---- Main loop ----
    CommandContext cmdCtx;
    cmdCtx.queryEngine = g_queryEngine.get();
    cmdCtx.appState = &appState;
    cmdCtx.toolRegistry = &toolRegistry;
    cmdCtx.cwd = cwd;
    cmdCtx.print = [](const std::string& s) { std::cout << s; };

    // HEAP-ALLOCATED callbacks to isolate from stack corruption.
    // The crash (fault 0x000003E800000003) was caused by an adjacent stack
    // variable overflowing into the callbacks struct, corrupting the
    // std::function's internal pointer. Heap allocation prevents this.
    auto callbacksPtr = std::make_unique<QueryCallbacks>();
    auto& callbacks = *callbacksPtr;
    // State tracking for status display
    enum class StreamState { WAITING, THINKING, RESPONDING, TOOL };
    StreamState streamState = StreamState::WAITING;
    bool firstToken = true;

    std::string voiceAccumulator; // Accumulate text for TTS
    bool inCodeBlock = false; // Track code block state for highlighting
    callbacks.onText = [&voiceAccumulator, &spinner, &streamState, &firstToken, &inCodeBlock](const std::string& text) {
        if (firstToken || streamState != StreamState::RESPONDING) {
            spinner.stop();
            streamState = StreamState::RESPONDING;
            firstToken = false;
        }
        // Real-time code block detection for syntax highlighting
        std::string output;
        for (size_t i = 0; i < text.size(); i++) {
            if (i + 2 < text.size() && text[i] == '`' && text[i+1] == '`' && text[i+2] == '`') {
                if (!inCodeBlock) {
                    inCodeBlock = true;
                    output += "\033[48;5;236m"; // Dark background
                } else {
                    inCodeBlock = false;
                    output += "\033[0m";
                }
                i += 2; // Skip the ```
                continue;
            }
            output += text[i];
        }
        std::cout << output << std::flush;
        closecrab::MobileWebSocket::getInstance().sendText(text);
        voiceAccumulator += text;
    };
    callbacks.onThinking = [&spinner, &streamState](const std::string& text) {
        if (streamState != StreamState::THINKING) {
            spinner.stop();
            spinner.start("Thinking...");
            streamState = StreamState::THINKING;
        }
        // Don't print thinking text by default (spinner shows the state)
        // Uncomment below to show thinking content:
        // std::cout << ansi::dim() << text << ansi::reset() << std::flush;
    };
    callbacks.onToolUse = [&spinner, &streamState](const std::string& name, const nlohmann::json& input) {
        spinner.stop();
        streamState = StreamState::TOOL;
        std::cout << "\n" << ansi::yellow() << "  [" << name << "]" << ansi::reset() << " ";
        // Show what the tool is doing
        if (name == "Bash" || name == "PowerShell") {
            std::string cmd = input.value("command", "");
            if (cmd.size() > 120) cmd = cmd.substr(0, 120) + "...";
            std::cout << ansi::dim() << cmd << ansi::reset();
        } else if (name == "Write" || name == "Edit" || name == "Read") {
            std::string path = input.value("file_path", "");
            std::cout << ansi::dim() << path << ansi::reset();
        } else if (name == "Glob") {
            std::cout << ansi::dim() << input.value("pattern", "") << ansi::reset();
        } else if (name == "Grep") {
            std::cout << ansi::dim() << input.value("pattern", "") << ansi::reset();
        }
        std::cout << std::flush;
        // Don't run the spinner for tools that take over the console:
        //  - Agent: streams its own sub-agent activity lines (task B).
        //  - AskUserQuestion: reads the console via KeyboardSelector (raw printf,
        //    not the stdout mutex); a running spinner worker would write cout
        //    concurrently with the selector's draw = the typing-during-turn crash.
        if (name != "Agent" && name != "AskUserQuestion") {
            spinner.start(name);
        } else {
            std::cout << "\n";
        }
        closecrab::MobileWebSocket::getInstance().sendToolUse(name, input.value("command", input.value("file_path", "")));
    };
    callbacks.onToolResult = [&spinner, &streamState](const std::string& name, const ToolResult& result) {
        spinner.stop();
        streamState = StreamState::WAITING;
        if (result.success) {
            std::string summary;
            if (name == "Read") {
                std::string path = result.data.is_object() ? result.data.value("filePath", "") : "";
                int lines = result.data.is_object() ? result.data.value("numLines", 0) : 0;
                std::string fname;
                if (!path.empty()) {
                    size_t slash = path.find_last_of("/\\");
                    fname = (slash == std::string::npos) ? path : path.substr(slash + 1);
                }
                summary = fname + " (" + std::to_string(lines) + " lines)";
            } else if (name == "Write") {
                summary = result.content;
            } else if (name == "Edit") {
                summary = result.content;
            } else if (name == "Glob") {
                int count = result.data.is_object() ? result.data.value("numFiles", 0) : 0;
                if (count > 0) summary = std::to_string(count) + " files matched";
                else summary = result.content.substr(0, std::min((size_t)80, result.content.size()));
            } else if (name == "Grep") {
                int matches = result.data.is_object() ? result.data.value("numMatches", 0) : 0;
                int files = result.data.is_object() ? result.data.value("numFiles", 0) : 0;
                if (matches > 0) summary = std::to_string(matches) + " matches in " + std::to_string(files) + " files";
                else summary = result.content.substr(0, std::min((size_t)80, result.content.size()));
            } else if (name == "Agent") {
                summary = result.content.substr(0, std::min((size_t)60, result.content.size()));
            } else if (name == "Bash" || name == "PowerShell") {
                summary = "";
            } else {
                summary = result.content.substr(0, std::min((size_t)80, result.content.size()));
            }

            std::cout << " " << ansi::green() << "OK" << ansi::reset();
            if (!summary.empty()) {
                std::cout << " " << ansi::dim() << summary << ansi::reset();
            }
            if (result.elapsedSeconds > 0.1) {
                std::cout << ansi::dim() << " (" << std::fixed << std::setprecision(1) << result.elapsedSeconds << "s)" << ansi::reset();
            }
            if ((name == "Bash" || name == "PowerShell") && !result.content.empty()) {
                auto collapsed = OutputCollapse::collapse(result.content);
                std::cout << "\n" << ansi::dim() << collapsed.display << ansi::reset();
                if (collapsed.collapsed) {
                    std::cout << ansi::dim() << "  [" << collapsed.totalLines << " total lines]" << ansi::reset();
                }
            }
            // 3.1/3.2: render the Edit/Write diff with syntax highlighting so the
            // user SEES what changed (green +, red -, dim context) AND the code is
            // colored. The diff lives in result.data only.
            if ((name == "Edit" || name == "Write") && result.data.is_object()
                && result.data.contains("diff") && result.data["diff"].is_array()) {
                // Language from file extension for highlighting.
                std::string fp = result.data.value("filePath", "");
                std::string ext;
                { size_t dot = fp.find_last_of('.'); if (dot != std::string::npos) ext = fp.substr(dot + 1); }
                std::string lang = SyntaxHighlight::langFromHint(ext);
                bool blk = false;
                std::cout << "\n";
                for (const auto& d : result.data["diff"]) {
                    std::string op = d.value("op", " ");
                    std::string text = d.value("text", "");
                    std::string hl = lang.empty() ? text : SyntaxHighlight::line(text, lang, blk);
                    if (op == "+")      std::cout << ansi::green() << "  + " << ansi::reset() << hl << "\n";
                    else if (op == "-") std::cout << ansi::red()   << "  - " << ansi::reset()
                                                  << ansi::red() << text << ansi::reset() << "\n";
                    else                std::cout << ansi::dim()   << "    " << text << ansi::reset() << "\n";
                }
                std::cout << std::flush;
            }
            std::cout << "\n" << std::flush;
        } else {
            std::cout << " " << ansi::red() << "Error: " << result.error << ansi::reset() << "\n";
        }
        closecrab::MobileWebSocket::getInstance().sendToolResult(name, result.success, result.elapsedSeconds);
        spinner.start("Waiting for response...");
    };
    callbacks.onComplete = [&spinner, &voiceAccumulator, &streamState, &firstToken, &sessionMgr, &sessionId]() {
        spinner.stop();
        streamState = StreamState::WAITING;
        firstToken = true;
        std::cout << "\n" << std::flush;
        closecrab::MobileWebSocket::getInstance().sendComplete();
        if (VoiceEngine::getInstance().isEnabled() && !voiceAccumulator.empty()) {
            VoiceEngine::getInstance().speak(voiceAccumulator);
        }
        voiceAccumulator.clear();
        // JackProAi: persistSession → recordTranscript(messages) after each turn
        try {
            if (g_queryEngine) {
                SessionAutoSave::save(&sessionMgr, sessionId, g_queryEngine->getMessages());
            }
        } catch (...) {
            spdlog::warn("SessionAutoSave failed (non-fatal)");
        }
    };
    callbacks.onError = [&spinner, &streamState, &firstToken](const std::string& err) {
        spinner.stop();
        streamState = StreamState::WAITING;
        firstToken = true;
        std::cerr << ansi::red() << "Error: " << err << ansi::reset() << "\n";
        closecrab::MobileWebSocket::getInstance().sendError(err);
    };
    // Retry status (task C): update the spinner so the user sees progress
    // instead of a frozen wheel during 503/network retries.
    callbacks.onRetry = [&spinner](int attempt, int maxAttempts, int delayMs, const std::string& reason) {
        if (attempt == 0) {
            // Streaming progress update (not a retry): show what's being generated
            // delayMs carries the accumulated bytes count
            int kb = delayMs / 1024;
            spinner.setMessage(reason + " (" + std::to_string(kb) + "KB)");
        } else {
            double secs = delayMs / 1000.0;
            std::ostringstream msg;
            msg << "Retrying " << attempt << "/" << maxAttempts << " in "
                << std::fixed << std::setprecision(1) << secs << "s (" << reason << ")";
            spinner.setMessage(msg.str());
            closecrab::MobileWebSocket::getInstance().sendText("[" + msg.str() + "]\n");
        }
    };
    // Track session-level permission grants
    bool autoApproveSession = false;
    callbacks.onAskPermission = [&autoApproveSession, &spinner](const std::string& toolName, const std::string& desc) -> bool {
        // If user chose "accept all" earlier, auto-approve
        if (autoApproveSession) return true;

        // This callback reads the console (KeyboardSelector) on the streaming
        // thread. Stop the spinner (so its worker isn't writing). KeyboardSelector
        // now claims the ConsoleInputGuard internally, so the key-watcher yields
        // automatically — no manual flag needed here.
        spinner.stop();

        std::cout << ansi::yellow() << "  Allow " << toolName
                  << ansi::reset() << " (" << desc << ")?\n";

        std::vector<std::string> options = {"Allow", "Deny", "Allow All"};
        SelectorResult sel = KeyboardSelector::select(options, 0, true);

        if (sel.index == -1) {
            // User typed custom text — treat as feedback and deny for safety
            std::cout << ansi::dim() << "  Feedback: " << sel.customText << ansi::reset() << "\n";
            std::cout << ansi::red() << "  Denied (custom response)." << ansi::reset() << "\n";
            return false;
        }
        if (sel.index == 2) { // Allow All
            autoApproveSession = true;
            std::cout << ansi::green() << "  Auto-approving all tools for this session." << ansi::reset() << "\n";
            return true;
        }
        return (sel.index == 0); // Allow=true, Deny or Escape=false
    };

    // Start HTTP server for mobile remote control and API
    HttpServer httpServer(9001);
    httpServer.onChat([&apiClient](const std::string& message, const std::string& sessionId) -> std::string {
        if (!apiClient) return "No API client configured";
        try {
            std::string result;
            std::vector<Message> msgs;
            msgs.push_back(Message::makeUser(message));
            ModelConfig cfg;
            cfg.maxTokens = 4096;
            cfg.stream = true;
            apiClient->streamChat(msgs, "You are CloseCrab AI. Be concise.", cfg,
                [&](const StreamEvent& event) {
                    if (event.type == StreamEvent::EVT_TEXT) result += event.content;
                });
            return result.empty() ? "No response from AI" : result;
        } catch (const std::exception& e) {
            return std::string("Error: ") + e.what();
        }
    });
    httpServer.start();
    spdlog::info("HTTP server started on port 9001 (mobile: http://localhost:9001/mobile)");

    // Start WebSocket server for mobile remote control
    auto& mobileWs = closecrab::MobileWebSocket::getInstance();
    mobileWs.start(9002);

    // === Team Mode: SessionRouter for multi-client parallel inference ===
    closecrab::SessionRouter sessionRouter(qeConfig, 4);
    sessionRouter.setClientCallback([&mobileWs](const std::string& clientId,
                                                 const std::string& event,
                                                 const nlohmann::json& data) {
        nlohmann::json msg = data;
        msg["type"] = event;
        msg["clientId"] = clientId;
        mobileWs.sendToClient(clientId, msg);
    });

    mobileWs.setMessageHandler([&sessionRouter](const std::string& clientId,
                                                 const std::string& type,
                                                 const nlohmann::json& data) {
        if (type == "register") {
            std::string username = data.value("username", "anonymous");
            std::string cwd = data.value("cwd", "");
            sessionRouter.registerClient(username, cwd);
        } else if (type == "chat") {
            std::string message = data.value("message", "");
            if (!message.empty()) {
                sessionRouter.submitRequest(clientId, message);
            }
        } else if (type == "abort") {
            sessionRouter.abortClient(clientId);
        }
    });

    // Auto-start cloudflared tunnel for remote mobile access
    std::string tunnelUrl;
    {
        std::vector<std::string> cloudflaredPaths = {
            "cloudflared.exe",
            "extensions/mobile-web/cloudflared.exe",
            "../extensions/mobile-web/cloudflared.exe",
            "../../extensions/mobile-web/cloudflared.exe"
        };
        std::string cloudflaredPath;
        for (const auto& p : cloudflaredPaths) {
            if (fs::exists(p)) { cloudflaredPath = p; break; }
        }

        if (!cloudflaredPath.empty()) {
            // Start cloudflared and capture URL via temp file
            std::string logFile = (fs::current_path() / "data" / "tunnel.log").string();
            fs::create_directories("data");
            std::string absPath = fs::absolute(cloudflaredPath).string();
            std::string cmd = absPath + " tunnel --url http://localhost:9001 > " + logFile + " 2>&1";
#ifdef _WIN32
            STARTUPINFOA si = {}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            std::string fullCmd = "cmd /c " + cmd;
            std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
            cmdBuf.push_back('\0');
            CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
#endif

            // Wait for URL to appear in log (max 12 seconds)
            for (int i = 0; i < 24; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::ifstream f(logFile);
                if (f.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(f)), {});
                    size_t pos = content.find("https://");
                    size_t end = content.find(".trycloudflare.com");
                    if (pos != std::string::npos && end != std::string::npos) {
                        tunnelUrl = content.substr(pos, end - pos + 18);
                        break;
                    }
                }
            }
        }

        if (!tunnelUrl.empty()) {
            std::cout << "\n" << ansi::green() << "  Remote access ready!" << ansi::reset() << "\n";
            std::cout << "  " << ansi::cyan() << tunnelUrl << "/mobile" << ansi::reset() << "\n\n";
        } else {
            std::cout << ansi::dim() << "  Local only: http://localhost:9001/mobile" << ansi::reset() << "\n";
        }
    }

    bool running = true;
    // NOTE: type-ahead (queuing input typed WHILE a turn runs) is DISABLED — it
    // crashed mid-turn on Windows. These remain only so the (now always-empty)
    // drain code below is a harmless no-op. Type at the prompt between turns.
    std::deque<std::string> queuedInputs;
    std::mutex queueMtx;

    // ---- Persistent key-watcher thread (Esc-abort ONLY) ----
    // LESSON FROM commit eb6540c: on Windows/MSVC, creating+joining a std::thread
    // per turn corrupts the CRT heap. So — like the Spinner — this is ONE thread
    // that lives for the whole session, armed/disarmed per turn via an atomic.
    // It does the ABSOLUTE MINIMUM: read a key, and if it's Esc, set an atomic to
    // abort. It NEVER allocates (no line buffer), NEVER writes std::cout, and
    // NEVER touches the spinner. Type-ahead was removed because collecting typed
    // keys (heap push_back) + streaming output concurrently hard-crashed; every
    // key now takes the same trivial no-heap path that Esc always did safely.
    std::atomic<bool> kwTurnActive{false}; // armed only while a turn is in flight
    std::atomic<bool> kwAborted{false};    // set when Esc was pressed this turn
    std::atomic<bool> kwShutdown{false};   // set once at program exit
    std::thread keyWatcher([&]() {
#ifdef _WIN32
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        while (!kwShutdown.load()) {
            if (!kwTurnActive.load()) {
                // Idle between turns: the main thread owns the console
                // (ReadConsoleW), so we must NOT read input here.
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (consoleInputBusy()) {
                // An interactive prompt (KeyboardSelector: permission prompt or
                // AskUserQuestion) is reading the console on another thread — yield
                // input ownership so we don't double-read (that races and crashes).
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            // Read WIDE input records (NOT _getch). _getch is byte-oriented ANSI:
            // an IME-composed Chinese char arrives as a multi-byte UTF-8 sequence
            // whose lead byte (0xE4..) was misread as an arrow/function prefix,
            // desyncing the stream and crashing (闪退). ReadConsoleInputW gives one
            // record per event; we act ONLY on Esc (key-down) and DISCARD every
            // other event — ASCII, IME/Unicode chars, key-up, mouse, focus, resize.
            DWORD avail = 0;
            if (!GetNumberOfConsoleInputEvents(hIn, &avail) || avail == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            INPUT_RECORD recs[32];
            DWORD got = 0;
            if (!ReadConsoleInputW(hIn, recs, 32, &got) || got == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            for (DWORD i = 0; i < got; i++) {
                if (recs[i].EventType == KEY_EVENT &&
                    recs[i].Event.KeyEvent.bKeyDown &&
                    recs[i].Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
                    if (g_queryEngine) g_queryEngine->interrupt();
                    kwAborted = true;
                }
                // All other records are read and discarded (type-ahead disabled;
                // IME wide chars consumed safely, never reinterpreted as bytes).
            }
        }
#else
        // POSIX (macOS/Linux): mirror the Windows discipline EXACTLY to avoid the
        // double-read crash class. We read stdin ONLY when (a) a turn is active and
        // (b) no selector/prompt owns the console (consoleInputBusy). The terminal
        // is in raw mode for the turn (enterTurnInputMode → PosixTty), so read() is
        // non-blocking (VMIN=0). We act ONLY on a lone Esc; every other byte —
        // including the multi-byte UTF-8 of an IME-composed CJK char and arrow
        // escape sequences — is consumed and DISCARDED (type-ahead disabled). No
        // heap allocation, no std::cout, no spinner touch on this path.
        while (!kwShutdown.load()) {
            if (!kwTurnActive.load()) {
                // Idle between turns: the main thread owns stdin (std::getline in
                // cooked mode). We must NOT read here.
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (consoleInputBusy()) {
                // A KeyboardSelector prompt (permission / AskUserQuestion) is
                // reading stdin on the streaming thread — yield so we don't
                // double-read the same fd (races and crashes).
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            int lastChar = 0;
            int key = closecrab::posixReadKey(20, lastChar);  // 20ms block, then loop
            if (key == closecrab::PK_ESC) {
                if (g_queryEngine) g_queryEngine->interrupt();
                kwAborted = true;
            }
            // PK_NONE (timeout), PK_ENTER, arrows, PK_OTHER → all ignored/discarded.
        }
#endif
    });

    while (running) {
        // Sync vim mode with appState (toggled by /vim command)
        vimInput.setEnabled(appState.vimMode);

        // Show context token estimate when conversation is long
        std::string tokenHint;
        if (g_queryEngine->getMessages().size() > 20) {
            int estTokens = 0;
            for (const auto& m : g_queryEngine->getMessages()) {
                // Count ALL content (text + tool results), not just getText()
                // which only returns TEXT blocks and shows misleading "~0k tok"
                // after agent turns that are mostly tool_result content.
                auto j = m.toApiJson();
                std::string serialized = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                estTokens += (int)serialized.size() / 4;
            }
            tokenHint = ansi::gray() + "[~" + std::to_string(estTokens / 1000) + "k tok] " + ansi::reset();
        }

        std::cout << vimInput.getModeIndicator() << tokenHint
                  << ansi::cyan() << "> " << ansi::reset() << std::flush;

        // Bug B: if the user queued input during the previous turn, process that
        // first instead of blocking on the console.
        std::string input;
        {
            std::lock_guard<std::mutex> lk(queueMtx);
            if (!queuedInputs.empty()) {
                input = queuedInputs.front();
                queuedInputs.pop_front();
            }
        }
        if (!input.empty()) {
            std::cout << ansi::dim() << "[queued] " << ansi::reset() << input << "\n";
        } else {

        // Voice input: if ASR is active and has a transcript, use it instead of keyboard
        auto& voiceEng = VoiceEngine::getInstance();
        if (voiceEng.isListening()) {
            std::string transcript = voiceEng.getLastTranscript();
            if (!transcript.empty()) {
                std::cout << ansi::magenta() << "[voice] " << ansi::reset() << transcript << "\n";
                input = transcript;
            } else {
                input = getUserInput();
            }
        } else {
            input = getUserInput();
        }

        if (input.empty()) {
            if (std::cin.eof()) break;
            continue;
        }

        // Multi-line input: backslash continuation
        while (!input.empty() && input.back() == '\\') {
            input.pop_back(); // remove trailing backslash
            input += '\n';
            std::cout << ansi::cyan() << ". " << ansi::reset() << std::flush;
            std::string cont = getUserInput();
            if (std::cin.eof()) break;
            input += cont;
        }
        } // end: not-queued console-input branch

        // Vim mode processing
        if (vimInput.isEnabled()) {
            auto vr = vimInput.processLine(input);
            if (vr.shouldQuit) { running = false; break; }
            if (vr.handled && vr.text.empty()) continue;
            if (!vr.text.empty()) input = vr.text;
        }

        inputHistory.add(input);

        // Shell escape: ! prefix runs command directly
        if (input.size() > 1 && input[0] == '!') {
            std::string shellCmd = input.substr(1);
            while (!shellCmd.empty() && shellCmd[0] == ' ') shellCmd.erase(0, 1);
            if (!shellCmd.empty()) {
                std::system(shellCmd.c_str());
            }
            continue;
        }

        // Check if it's a command
        if (CommandRegistry::isCommand(input)) {
            auto [cmdName, cmdArgs] = CommandRegistry::parseCommand(input);
            CommandResult result;
            if (cmdRegistry.executeCommand(cmdName, cmdArgs, cmdCtx, result)) {
                if (!result.output.empty()) std::cout << result.output;
                if (!result.shouldContinue) { running = false; break; }
            } else {
                std::cout << "Unknown command: /" << cmdName << ". Type /help for available commands.\n";
            }
            continue;
        }

        // Regular message — send to QueryEngine
        streamState = StreamState::WAITING;
        firstToken = true;
        spinner.start("Waiting for response...");

        // Arm the persistent key-watcher for this turn (Esc-abort only).
        // No thread is created here — see keyWatcher declared before the loop.
        kwAborted = false;
        // Raw input mode FIRST (no echo / no line buffering), then arm the
        // watcher. Now any typing during generation is inert except Esc.
        // Cross-platform: Windows uses the console API, POSIX uses termios
        // (PosixTty) — both implemented in enterTurnInputMode().
        enterTurnInputMode();
        kwTurnActive = true;

        try {
            g_queryEngine->submitMessage(input, callbacks);
        } catch (const std::exception& e) {
            spinner.stop();
            // Don't say "Fatal" — it scares users into thinking the program crashed.
            // claude-code just shows the error and returns to the prompt.
            std::cerr << ansi::red() << "Error: " << e.what() << ansi::reset() << "\n";
            std::cerr << ansi::dim() << "(Returned to prompt. You can retry or try a different approach.)" << ansi::reset() << "\n";
            spdlog::error("submitMessage exception: {}", e.what());
        } catch (...) {
            spinner.stop();
            std::cerr << ansi::red() << "Error: unexpected exception" << ansi::reset() << "\n";
            std::cerr << ansi::dim() << "(Returned to prompt.)" << ansi::reset() << "\n";
            spdlog::error("submitMessage unknown exception");
        }

        // Disarm the watcher before reading the next prompt. It is NOT joined per
        // turn (that would reintroduce the eb6540c heap-corruption crash) — it just
        // stops calling _getch, so the main thread alone owns the console and the
        // std::cout below. All watcher-related display happens HERE.
        kwTurnActive = false;
        // Flush keystrokes typed during the turn, then restore cooked mode for the
        // next prompt. Cross-platform (Windows console API / POSIX termios).
        leaveTurnInputMode();
        spinner.stop();
        if (kwAborted.load()) {
            std::cout << "\n" << ansi::yellow()
                      << "  [Stopped by Esc. Returned to prompt.]"
                      << ansi::reset() << "\n" << std::flush;
        }
        {
            std::lock_guard<std::mutex> lk(queueMtx);
            if (!queuedInputs.empty()) {
                std::cout << ansi::dim() << "  ["
                          << queuedInputs.size()
                          << " queued message(s) will run next]" << ansi::reset()
                          << "\n" << std::flush;
            }
        }
    }

    // Shut down the persistent key-watcher (single join for the whole session —
    // mirrors the Spinner's lifecycle; never per-turn, per eb6540c).
    kwShutdown = true;
    if (keyWatcher.joinable()) keyWatcher.join();

    // ---- Cleanup ----
    // Auto-extract memories from conversation
    if (g_queryEngine && g_queryEngine->getMessages().size() >= 6 && apiClient && !apiClient->isLocal()) {
        spdlog::info("Extracting memories from conversation...");
        int extracted = MemoryExtractor::extractAndSave(
            g_queryEngine->getMessages(), apiClient.get(), cwd);
        if (extracted > 0) {
            std::cout << ansi::dim() << "Saved " << extracted << " memories." << ansi::reset() << "\n";
        }
    }

    // Save session messages for /resume
    if (g_queryEngine && !g_queryEngine->getMessages().empty()) {
        auto msgJson = g_queryEngine->serializeMessages();
        sessionMgr.updateContext(sessionId, msgJson.dump());
        spdlog::info("Saved {} messages to session {}", g_queryEngine->getMessages().size(), sessionId);
    }

    // Save permission rules
    permEngine.saveRules();
    settings.setPermissionRules(permEngine.saveRules());
    settings.save();

    g_queryEngine.reset();

    // Disconnect MCP servers
    MCPServerManager::getInstance().disconnectAll();

#ifdef _WIN32
    WSACleanup();
#endif

    curl_global_cleanup();

    std::cout << "Goodbye!\n";
    return 0;
}
