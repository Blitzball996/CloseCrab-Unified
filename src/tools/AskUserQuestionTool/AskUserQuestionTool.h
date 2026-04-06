#pragma once

#include "../Tool.h"
#include <iostream>

namespace closecrab {

class AskUserQuestionTool : public Tool {
public:
    std::string getName() const override { return "AskUserQuestion"; }
    std::string getDescription() const override {
        return "Ask the user a question with optional multiple-choice options.";
    }
    std::string getCategory() const override { return "interaction"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"questions", {
                    {"type", "array"},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"question", {{"type", "string"}}},
                            {"options", {
                                {"type", "array"},
                                {"items", {
                                    {"type", "object"},
                                    {"properties", {
                                        {"label", {{"type", "string"}}},
                                        {"description", {{"type", "string"}}}
                                    }}
                                }}
                            }}
                        }}
                    }}
                }}
            }},
            {"required", {"questions"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        nlohmann::json answers = nlohmann::json::object();

        for (const auto& q : input["questions"]) {
            std::string question = q.value("question", "");
            std::cout << "\n\033[33m" << question << "\033[0m\n";

            if (q.contains("options") && q["options"].is_array()) {
                int i = 1;
                for (const auto& opt : q["options"]) {
                    std::cout << "  " << i << ". " << opt.value("label", "")
                              << " - " << opt.value("description", "") << "\n";
                    i++;
                }
                std::cout << "  " << i << ". Other (type your answer)\n";
                std::cout << "\033[33mChoice: \033[0m";

                std::string choice;
                std::getline(std::cin, choice);

                int idx = 0;
                try { idx = std::stoi(choice); } catch (...) {}

                if (idx >= 1 && idx <= (int)q["options"].size()) {
                    answers[question] = q["options"][idx - 1].value("label", choice);
                } else {
                    answers[question] = choice;
                }
            } else {
                std::cout << "\033[33mAnswer: \033[0m";
                std::string answer;
                std::getline(std::cin, answer);
                answers[question] = answer;
            }
        }

        return ToolResult::ok(answers.dump(2), answers);
    }
};

} // namespace closecrab
