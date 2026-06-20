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

        // Defensive: the model may emit malformed shapes — `questions` as a bare
        // string, or each question as a string instead of {question, options}.
        // Calling .value() on a JSON string throws type_error.306, which used to
        // surface as "Tool AskUserQuestion threw exception". Normalize everything
        // to objects first.
        if (!input.contains("questions")) {
            return ToolResult::fail("AskUserQuestion: missing required 'questions' field");
        }

        nlohmann::json questions = input["questions"];
        if (questions.is_string()) {
            // Single bare-string question → wrap as one object
            questions = nlohmann::json::array({ nlohmann::json{{"question", questions.get<std::string>()}} });
        } else if (questions.is_object()) {
            // Single question object (not wrapped in an array)
            questions = nlohmann::json::array({ questions });
        } else if (!questions.is_array()) {
            return ToolResult::fail("AskUserQuestion: 'questions' must be an array, object, or string");
        }

        for (const auto& qRaw : questions) {
            // Normalize each question: string → {question: <string>}
            nlohmann::json q;
            if (qRaw.is_string()) {
                q = nlohmann::json{{"question", qRaw.get<std::string>()}};
            } else if (qRaw.is_object()) {
                q = qRaw;
            } else {
                continue;  // skip non-string/non-object entries
            }

            std::string question = q.value("question", "");
            if (question.empty()) continue;
            std::cout << "\n\033[33m" << question << "\033[0m\n";

            if (q.contains("options") && q["options"].is_array() && !q["options"].empty()) {
                // Use the SAME interactive selector as the permission prompt
                // (KeyboardSelector: arrow keys + raw _getch). The old
                // std::getline path conflicted with the ReadConsoleW-based main
                // input and the spinner thread — that's why typed input landed
                // before the colon and was capped at one char.
                std::vector<std::string> labels;
                std::vector<std::string> optionLabels;  // parallel: resolved label per option
                for (const auto& opt : q["options"]) {
                    // options entries may also be bare strings
                    std::string label, desc;
                    if (opt.is_string()) {
                        label = opt.get<std::string>();
                    } else if (opt.is_object()) {
                        label = opt.value("label", "");
                        desc = opt.value("description", "");
                    }
                    optionLabels.push_back(label);
                    labels.push_back(desc.empty() ? label : (label + " — " + desc));
                }

                // allowCustom=true gives a "[Type response...]" entry → index -1
                // with the typed text (the "Other" path). enableShortcuts=false so
                // a free-text answer starting with y/n/a isn't hijacked.
                SelectorResult sel = KeyboardSelector::select(labels, 0, true, false);

                if (sel.index >= 0 && sel.index < (int)optionLabels.size()) {
                    answers[question] = optionLabels[sel.index];
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
