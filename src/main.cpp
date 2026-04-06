#include <iostream>
#include <string>
#include <memory>
#include <filesystem>
#include <csignal>
#include <spdlog/spdlog.h>
#include <CLI/CLI.hpp>

#include "core/QueryEngine.h"
#include "core/AppState.h"
#include "core/SessionManager.h"
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
#include "tools/FileReadTool/FileReadTool.h"
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
#include "agents/AgentManager.h"
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
#include "mcp/MCPClient.h"
#include "plugins/PluginManager.h"
#include "core/CostTracker.h"
#include "git/GitManager.h"
#include "voice/VoiceEngine.h"

// Commands
#include "commands/GitCommands.h"
#include "commands/SessionCommands.h"
#include "commands/AdvancedCommands.h"

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace fs = std::filesystem;
using namespace closecrab;

static std::unique_ptr<QueryEngine> g_queryEngine;

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
            std::string out;
            out += "Session: " + ctx.queryEngine->getSessionId() + "\n";
            out += "Model: " + ctx.appState->currentModel + "\n";
            out += "Cost: $" + std::to_string(ctx.appState->getTotalCost()) + "\n";
            out += "Permission mode: " + PermissionEngine::getInstance().getModeName() + "\n";
            out += "Plan mode: " + std::string(ctx.appState->planMode ? "ON" : "OFF") + "\n";
            ctx.print(out);
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
            std::lock_guard<std::mutex> lock(ctx.appState->usageMutex);
            std::string out = "API Cost Summary:\n";
            for (const auto& [model, usage] : ctx.appState->modelUsage) {
                out += "  " + model + ": " +
                    std::to_string(usage.inputTokens) + " in / " +
                    std::to_string(usage.outputTokens) + " out\n";
            }
            out += "Total: $" + std::to_string(ctx.appState->getTotalCost()) + "\n";
            ctx.print(out);
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

// Load CLAUDE.md from project root
static std::string loadClaudeMd(const std::string& projectRoot) {
    fs::path p = fs::path(projectRoot) / "CLAUDE.md";
    if (!fs::exists(p)) return "";
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // ---- CLI arguments ----
    CLI::App app{"CloseCrab-Unified - Local AI Assistant"};
    std::string configPath = "config/config.yaml";
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
    CLI11_PARSE(app, argc, argv);

    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);

    // ---- Load config ----
    auto& config = Config::getInstance();
    config.load(configPath);

    std::string cwd = fs::current_path().string();

    // ---- Load settings.json ----
    auto& settings = SettingsManager::getInstance();
    settings.load(cwd);

    // ---- Initialize AppState ----
    AppState appState;
    appState.originalCwd = cwd;
    appState.projectRoot = cwd;
    appState.verbose = verbose;
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
        apiClient = std::make_unique<RemoteAPIClient>(apiKey, apiBaseUrl, apiModel);
        appState.currentModel = apiModel;
        spdlog::info("Using Anthropic API: {} ({})", apiBaseUrl, apiModel);
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
    // Session
    cmdRegistry.registerCommand(std::make_unique<SessionCommand>());
    cmdRegistry.registerCommand(std::make_unique<NewSessionCommand>());
    cmdRegistry.registerCommand(std::make_unique<HistoryCommand>());
    cmdRegistry.registerCommand(std::make_unique<ExportCommand>());
    cmdRegistry.registerCommand(std::make_unique<CompactCommand>());
    cmdRegistry.registerCommand(std::make_unique<ContextCommand>());
    cmdRegistry.registerCommand(std::make_unique<EnvCommand>());
    cmdRegistry.registerCommand(std::make_unique<VersionCommand>());
    cmdRegistry.registerCommand(std::make_unique<FastCommand>());
    cmdRegistry.registerCommand(std::make_unique<ThinkingCommand>());
    // Advanced
    cmdRegistry.registerCommand(std::make_unique<RAGCommand>());
    cmdRegistry.registerCommand(std::make_unique<SSDCommand>());
    cmdRegistry.registerCommand(std::make_unique<SandboxCommand>());
    cmdRegistry.registerCommand(std::make_unique<PlanCommand>());
    cmdRegistry.registerCommand(std::make_unique<DoctorCommand>());
    cmdRegistry.registerCommand(std::make_unique<AddDirCommand>());
    cmdRegistry.registerCommand(std::make_unique<FilesCommand>());
    cmdRegistry.registerCommand(std::make_unique<ProviderCommand>());
    cmdRegistry.registerCommand(std::make_unique<ApiConfigCommand>());
    cmdRegistry.registerCommand(std::make_unique<ReloadCommand>());
    spdlog::info("Registered {} commands", cmdRegistry.getCommandNames().size());
    toolRegistry.registerTool(std::make_unique<FileReadTool>());
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
    toolRegistry.registerTool(std::make_unique<REPLTool>());
    toolRegistry.registerTool(std::make_unique<TeamCreateTool>());
    toolRegistry.registerTool(std::make_unique<TeamDeleteTool>());
    toolRegistry.registerTool(std::make_unique<ConfigTool>());
    toolRegistry.registerTool(std::make_unique<TaskStopTool>());
    toolRegistry.registerTool(std::make_unique<TaskOutputTool>());
    toolRegistry.registerTool(std::make_unique<RemoteTriggerTool>());
    toolRegistry.registerTool(std::make_unique<ToolSearchTool>());
    toolRegistry.registerTool(std::make_unique<McpAuthTool>());
    toolRegistry.registerTool(std::make_unique<BriefTool>());
    toolRegistry.registerTool(std::make_unique<SyntheticOutputTool>());
    spdlog::info("Registered {} tools", toolRegistry.getToolNames().size());

    // Load MCP servers from settings
    auto mcpConfig = settings.getMcpServers();
    if (!mcpConfig.empty()) {
        MCPServerManager::getInstance().loadFromSettings(mcpConfig);
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
    qeConfig.systemPrompt = R"(You are CloseCrab-Unified, a powerful local AI coding assistant.
You are NOT Claude, NOT Kiro, NOT any other AI. Your name is CloseCrab.
Respond in the same language as the user. Be concise and helpful.

When the user asks you to perform actions (create/read/edit files, run commands, search), use tools.
When the user asks a question, answer directly.)";
    qeConfig.maxTurns = 50;
    qeConfig.verbose = verbose;

    g_queryEngine = std::make_unique<QueryEngine>(qeConfig);
    g_queryEngine->setSessionId(sessionId);

    std::signal(SIGINT, signalHandler);

    // ---- Print banner ----
    std::cout << "\n  CloseCrab-Unified v0.1.0\n";
    std::cout << "  Model: " << appState.currentModel << "\n";
    std::cout << "  Type /help for commands, /quit to exit.\n\n";

    // ---- Main loop ----
    CommandContext cmdCtx;
    cmdCtx.queryEngine = g_queryEngine.get();
    cmdCtx.appState = &appState;
    cmdCtx.toolRegistry = &toolRegistry;
    cmdCtx.cwd = cwd;
    cmdCtx.print = [](const std::string& s) { std::cout << s; };

    QueryCallbacks callbacks;
    callbacks.onText = [](const std::string& text) { std::cout << text << std::flush; };
    callbacks.onThinking = [](const std::string& text) {
        std::cout << "\033[2m" << text << "\033[0m" << std::flush;
    };
    callbacks.onToolUse = [](const std::string& name, const nlohmann::json& input) {
        std::cout << "\n\033[33m[Tool: " << name << "]\033[0m\n" << std::flush;
    };
    callbacks.onToolResult = [](const std::string& name, const ToolResult& result) {
        if (result.success) {
            std::cout << "\033[32m[" << name << " OK]\033[0m\n" << std::flush;
        } else {
            std::cout << "\033[31m[" << name << " Error: " << result.error << "]\033[0m\n";
        }
    };
    callbacks.onComplete = []() { std::cout << "\n" << std::flush; };
    callbacks.onError = [](const std::string& err) {
        std::cerr << "\033[31mError: " << err << "\033[0m\n";
    };
    callbacks.onAskPermission = [](const std::string& toolName, const std::string& desc) -> bool {
        std::cout << "\033[33mAllow " << toolName << " (" << desc << ")? [y/N]: \033[0m";
        std::string answer;
        std::getline(std::cin, answer);
        return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
    };

    bool running = true;
    while (running) {
        std::cout << "\033[36m> \033[0m" << std::flush;
        std::string input = getUserInput();

        if (input.empty()) continue;
        if (std::cin.eof()) break;

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
        g_queryEngine->submitMessage(input, callbacks);
    }

    // ---- Cleanup ----
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

    std::cout << "Goodbye!\n";
    return 0;
}
