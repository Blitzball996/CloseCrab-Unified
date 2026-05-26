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

#include <iostream>
#include <string>
#include <memory>
#include <filesystem>
#include <csignal>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
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
#include "tools/DiscoverSkillsTool/DiscoverSkillsTool.h"
#include "tools/SnipTool/SnipTool.h"
#include "tools/VerifyPlanTool/VerifyPlanTool.h"
#include "tools/WebBrowserTool/WebBrowserTool.h"
#include "tools/MonitorTool/MonitorTool.h"
#include "tools/ReverseTool/ReverseTool.h"
#include "tools/ImageInputTool/ImageInputTool.h"
#include "tools/DebuggerTool/DebuggerTool.h"
#include "tools/SystemInfoTool/SystemInfoTool.h"
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

// Commands
#include "commands/GitCommands.h"
#include "commands/SessionCommands.h"
#include "commands/AdvancedCommands.h"
#include "commands/ExtendedCommands.h"

#include <curl/curl.h>

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

    // Initialize CURL globally BEFORE any threads are created.
    // curl_global_init() is NOT thread-safe — if called implicitly from
    // curl_easy_init() while the Spinner thread is running, it causes a
    // race condition (segfault on Windows).
    curl_global_init(CURL_GLOBAL_ALL);

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
        auto remote = std::make_unique<RemoteAPIClient>(apiKey, apiBaseUrl, apiModel);
        // Set fallback model for 503/529 auto-downgrade
        std::string fallback = config.getString("api.fallback_model", "claude-sonnet-4-20250514");
        if (fallback != apiModel) remote->setFallbackModel(fallback);
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
    cmdRegistry.registerCommand(std::make_unique<RAGCommand>());
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
    toolRegistry.registerTool(std::make_unique<MonitorTool>());
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
    toolRegistry.registerTool(std::make_unique<DiscoverSkillsTool>());
    toolRegistry.registerTool(std::make_unique<SnipTool>());
    toolRegistry.registerTool(std::make_unique<VerifyPlanTool>());
    toolRegistry.registerTool(std::make_unique<WebBrowserTool>());
    toolRegistry.registerTool(std::make_unique<ReverseTool>());
    toolRegistry.registerTool(std::make_unique<ImageInputTool>());
    toolRegistry.registerTool(std::make_unique<DebuggerTool>());
    toolRegistry.registerTool(std::make_unique<SystemInfoTool>());
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
You have tools: Read, Write, Edit, Bash, Glob, Grep, WebSearch, WebFetch, TodoWrite.
IMPORTANT: When given a task, START WORKING IMMEDIATELY using tools. Do NOT just describe what you would do.
Do NOT spawn Agent tools. Handle everything yourself step by step.
For large tasks: use Glob/Grep to explore, Read to understand, Write/Edit to create files, Bash to run commands.)";
    qeConfig.maxTurns = 50;
    qeConfig.verbose = verbose;

    g_queryEngine = std::make_unique<QueryEngine>(qeConfig);
    g_queryEngine->setSessionId(sessionId);

    std::signal(SIGINT, signalHandler);

    // ---- Print banner ----
    std::cout << "\n" << ansi::cyan() << "  CloseCrab-Unified" << ansi::reset()
              << " v0.1.0\n";
    std::cout << "  Model: " << ansi::bold() << appState.currentModel << ansi::reset() << "\n";
    std::cout << "  Type " << ansi::yellow() << "/help" << ansi::reset()
              << " for commands, " << ansi::yellow() << "/quit" << ansi::reset() << " to exit.\n\n";

    // ---- UI components ----
    Spinner spinner;
    InputHistory inputHistory;
    VimInput vimInput;

    // ---- Main loop ----
    CommandContext cmdCtx;
    cmdCtx.queryEngine = g_queryEngine.get();
    cmdCtx.appState = &appState;
    cmdCtx.toolRegistry = &toolRegistry;
    cmdCtx.cwd = cwd;
    cmdCtx.print = [](const std::string& s) { std::cout << s; };

    QueryCallbacks callbacks;
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
        spinner.start(name);
        closecrab::MobileWebSocket::getInstance().sendToolUse(name, input.value("command", input.value("file_path", "")));
    };
    callbacks.onToolResult = [&spinner, &streamState](const std::string& name, const ToolResult& result) {
        spinner.stop();
        streamState = StreamState::WAITING;
        if (result.success) {
            std::cout << " " << ansi::green() << "OK" << ansi::reset();
            if (result.elapsedSeconds > 0.1) {
                std::cout << ansi::dim() << " (" << std::fixed << std::setprecision(1) << result.elapsedSeconds << "s)" << ansi::reset();
            }
            // Show collapsed output for execution tools so user can see progress
            if ((name == "Bash" || name == "PowerShell") && !result.content.empty()) {
                auto collapsed = OutputCollapse::collapse(result.content);
                std::cout << "\n" << ansi::dim() << collapsed.display << ansi::reset();
                if (collapsed.collapsed) {
                    std::cout << ansi::dim() << "  [" << collapsed.totalLines << " total lines]" << ansi::reset();
                }
            }
            std::cout << "\n" << std::flush;
        } else {
            std::cout << " " << ansi::red() << "Error: " << result.error << ansi::reset();
            if (result.elapsedSeconds > 0.1) {
                std::cout << ansi::dim() << " (" << std::fixed << std::setprecision(1) << result.elapsedSeconds << "s)" << ansi::reset();
            }
            std::cout << "\n";
        }
        // After tool result, API will be called again — show waiting
        closecrab::MobileWebSocket::getInstance().sendToolResult(name, result.success, result.elapsedSeconds);
        spinner.start("Waiting for response...");
    };
    callbacks.onComplete = [&spinner, &voiceAccumulator, &streamState, &firstToken]() {
        spinner.stop();
        streamState = StreamState::WAITING;
        firstToken = true;
        std::cout << "\n" << std::flush;
        closecrab::MobileWebSocket::getInstance().sendComplete();
        if (VoiceEngine::getInstance().isEnabled() && !voiceAccumulator.empty()) {
            VoiceEngine::getInstance().speak(voiceAccumulator);
        }
        voiceAccumulator.clear();
    };
    callbacks.onError = [&spinner, &streamState, &firstToken](const std::string& err) {
        spinner.stop();
        streamState = StreamState::WAITING;
        firstToken = true;
        std::cerr << ansi::red() << "Error: " << err << ansi::reset() << "\n";
        closecrab::MobileWebSocket::getInstance().sendError(err);
    };
    // Track session-level permission grants
    bool autoApproveSession = false;
    callbacks.onAskPermission = [&autoApproveSession](const std::string& toolName, const std::string& desc) -> bool {
        // If user chose "accept all" earlier, auto-approve
        if (autoApproveSession) return true;

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
    while (running) {
        // Sync vim mode with appState (toggled by /vim command)
        vimInput.setEnabled(appState.vimMode);

        // Show context token estimate when conversation is long
        std::string tokenHint;
        if (g_queryEngine->getMessages().size() > 20) {
            int estTokens = 0;
            for (const auto& m : g_queryEngine->getMessages())
                estTokens += (int)m.getText().size() / 4;
            tokenHint = ansi::gray() + "[~" + std::to_string(estTokens / 1000) + "k tok] " + ansi::reset();
        }

        std::cout << vimInput.getModeIndicator() << tokenHint
                  << ansi::cyan() << "> " << ansi::reset() << std::flush;
        std::string input = getUserInput();

        if (input.empty()) continue;
        if (std::cin.eof()) break;

        // Multi-line input: backslash continuation
        while (!input.empty() && input.back() == '\\') {
            input.pop_back(); // remove trailing backslash
            input += '\n';
            std::cout << ansi::cyan() << ". " << ansi::reset() << std::flush;
            std::string cont = getUserInput();
            if (std::cin.eof()) break;
            input += cont;
        }

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
        try {
            g_queryEngine->submitMessage(input, callbacks);
        } catch (const std::exception& e) {
            spinner.stop();
            std::cerr << ansi::red() << "Fatal error: " << e.what() << ansi::reset() << "\n";
            spdlog::error("submitMessage crashed: {}", e.what());
        } catch (...) {
            spinner.stop();
            std::cerr << ansi::red() << "Fatal unknown error in submitMessage" << ansi::reset() << "\n";
            spdlog::error("submitMessage crashed with unknown exception");
        }
    }

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
