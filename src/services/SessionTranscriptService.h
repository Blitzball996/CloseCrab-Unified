#pragma once

#include "../core/Message.h"
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace closecrab {
namespace fs = std::filesystem;

class SessionTranscriptService {
public:
    static SessionTranscriptService& getInstance() {
        static SessionTranscriptService instance;
        return instance;
    }

    bool save(const std::string& sessionId,
              const std::vector<Message>& messages,
              const std::string& outputPath) {
        std::lock_guard<std::mutex> lock(mutex_);

        try {
            fs::path outDir = fs::path(outputPath).parent_path();
            if (!outDir.empty() && !fs::exists(outDir)) {
                fs::create_directories(outDir);
            }

            std::ofstream out(outputPath);
            if (!out.is_open()) {
                spdlog::error("SessionTranscript: cannot open {} for writing", outputPath);
                return false;
            }

            out << "# Session Transcript: " << sessionId << "\n\n";

            for (const auto& msg : messages) {
                std::string roleHeader;
                switch (msg.role) {
                    case MessageRole::USER:      roleHeader = "## User"; break;
                    case MessageRole::ASSISTANT:  roleHeader = "## Assistant"; break;
                    case MessageRole::SYSTEM:     roleHeader = "## System"; break;
                }
                out << roleHeader << "\n\n";

                for (const auto& block : msg.content) {
                    switch (block.type) {
                        case ContentBlockType::TEXT:
                        case ContentBlockType::THINKING:
                            out << block.text << "\n\n";
                            break;
                        case ContentBlockType::TOOL_USE:
                            out << "```tool_use: " << block.toolName << "\n";
                            out << block.toolInput.dump(2) << "\n";
                            out << "```\n\n";
                            break;
                        case ContentBlockType::TOOL_RESULT:
                            out << "```tool_result" << (block.isError ? " [ERROR]" : "") << "\n";
                            out << block.toolResult.dump(2) << "\n";
                            out << "```\n\n";
                            break;
                        case ContentBlockType::IMAGE:
                            out << "[Image: " << block.mediaType << "]\n\n";
                            break;
                    }
                }
            }

            spdlog::info("SessionTranscript: saved {} messages to {}", messages.size(), outputPath);
            return true;

        } catch (const std::exception& e) {
            spdlog::error("SessionTranscript: save failed: {}", e.what());
            return false;
        }
    }

    std::vector<Message> load(const std::string& inputPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Message> messages;

        try {
            std::ifstream in(inputPath);
            if (!in.is_open()) {
                spdlog::error("SessionTranscript: cannot open {} for reading", inputPath);
                return messages;
            }

            std::string line;
            std::string currentRole;
            std::string currentText;
            bool inCodeBlock = false;
            std::string codeBlockContent;
            std::string codeBlockType;

            auto flushMessage = [&]() {
                if (currentRole.empty()) return;
                Message msg;
                if (currentRole == "User") {
                    msg = Message::makeUser(currentText);
                } else if (currentRole == "Assistant") {
                    msg = Message::makeAssistant(currentText);
                } else {
                    msg = Message::makeSystem(SystemSubtype::INFORMATIONAL, currentText);
                }
                if (!currentText.empty() || !msg.content.empty()) {
                    messages.push_back(std::move(msg));
                }
                currentText.clear();
            };

            while (std::getline(in, line)) {
                // Skip the title line
                if (line.rfind("# Session Transcript:", 0) == 0) continue;

                // Check for role headers
                if (line == "## User" || line == "## Assistant" || line == "## System") {
                    if (!inCodeBlock) {
                        flushMessage();
                        currentRole = line.substr(3); // Remove "## "
                    }
                    continue;
                }

                // Handle code blocks
                if (line.rfind("```", 0) == 0) {
                    if (!inCodeBlock) {
                        inCodeBlock = true;
                        codeBlockType = line.substr(3);
                        codeBlockContent.clear();
                    } else {
                        inCodeBlock = false;
                        // Code block content is part of the message text
                        currentText += "```" + codeBlockType + "\n" + codeBlockContent + "```\n";
                    }
                    continue;
                }

                if (inCodeBlock) {
                    if (!codeBlockContent.empty()) codeBlockContent += "\n";
                    codeBlockContent += line;
                } else {
                    if (!line.empty()) {
                        if (!currentText.empty()) currentText += "\n";
                        currentText += line;
                    }
                }
            }

            // Flush last message
            flushMessage();

            spdlog::info("SessionTranscript: loaded {} messages from {}", messages.size(), inputPath);

        } catch (const std::exception& e) {
            spdlog::error("SessionTranscript: load failed: {}", e.what());
        }

        return messages;
    }

private:
    SessionTranscriptService() = default;
    mutable std::mutex mutex_;
};

} // namespace closecrab
