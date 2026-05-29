#pragma once

#include "../Tool.h"
#include "../../ui/KeyboardSelector.h"
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

            if (q.contains("options") && q["options"].is_array() && !q["options"].empty()) {
                // Use the SAME interactive selector as the permission prompt
                // (KeyboardSelector: arrow keys + raw _getch). The old
                // std::getline path conflicted with the ReadConsoleW-based main
                // input and the spinner thread — that's why typed input landed
                // before the colon and was capped at one char.
                std::vector<std::string> labels;
                for (const auto& opt : q["options"]) {
                    std::string label = opt.value("label", "");
                    std::string desc = opt.value("description", "");
                    labels.push_back(desc.empty() ? label : (label + " — " + desc));
                }

                // allowCustom=true gives a "[Type response...]" entry → index -1
                // with the typed text (the "Other" path). enableShortcuts=false so
                // a free-text answer starting with y/n/a isn't hijacked.
                SelectorResult sel = KeyboardSelector::select(labels, 0, true, false);

                if (sel.index >= 0 && sel.index < (int)q["options"].size()) {
                    answers[question] = q["options"][sel.index].value("label", "");
                } else {
                    // Custom typed answer (or escape fallback)
                    answers[question] = sel.customText;
                }
            } else {
                // Free-text question: full-line custom input via the selector's
                // text path (consistent console handling).
                SelectorResult sel = KeyboardSelector::select({}, 0, true, false);
                answers[question] = sel.customText;
            }
        }

        return ToolResult::ok(answers.dump(2), answers);
    }
};

} // namespace closecrab
