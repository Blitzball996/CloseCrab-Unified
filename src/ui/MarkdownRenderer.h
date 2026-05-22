#pragma once
#include <string>
#include <regex>
#include <sstream>

namespace closecrab {

class MarkdownRenderer {
public:
    static std::string render(const std::string& text) {
        std::string result;
        std::istringstream stream(text);
        std::string line;
        bool inCodeBlock = false;
        std::string codeLanguage;

        while (std::getline(stream, line)) {
            if (line.substr(0, 3) == "```") {
                if (!inCodeBlock) {
                    inCodeBlock = true;
                    codeLanguage = line.substr(3);
                    result += "\033[48;5;236m"; // Dark background for code
                    if (!codeLanguage.empty()) {
                        result += "\033[90m[" + codeLanguage + "]\033[0m\033[48;5;236m\n";
                    } else {
                        result += "\n";
                    }
                } else {
                    inCodeBlock = false;
                    result += "\033[0m\n";
                }
                continue;
            }

            if (inCodeBlock) {
                result += "  " + line + "\n";
                continue;
            }

            // Headers
            if (line.substr(0, 3) == "###") {
                result += "\033[1;36m" + line.substr(4) + "\033[0m\n";
            } else if (line.substr(0, 2) == "##") {
                result += "\033[1;33m" + line.substr(3) + "\033[0m\n";
            } else if (line.size() > 0 && line[0] == '#') {
                result += "\033[1;32m" + line.substr(2) + "\033[0m\n";
            }
            // Bold
            else if (line.find("**") != std::string::npos) {
                std::string rendered = line;
                size_t pos = 0;
                while ((pos = rendered.find("**", pos)) != std::string::npos) {
                    size_t end = rendered.find("**", pos + 2);
                    if (end == std::string::npos) break;
                    std::string bold = rendered.substr(pos + 2, end - pos - 2);
                    rendered = rendered.substr(0, pos) + "\033[1m" + bold + "\033[0m" + rendered.substr(end + 2);
                    pos = pos + bold.size() + 8;
                }
                result += rendered + "\n";
            }
            // Bullet points
            else if (line.size() > 2 && (line.substr(0, 2) == "- " || line.substr(0, 2) == "* ")) {
                result += "  \033[33m•\033[0m " + line.substr(2) + "\n";
            }
            // Numbered list
            else if (line.size() > 2 && std::isdigit(line[0]) && line[1] == '.') {
                result += "  " + line + "\n";
            }
            // Inline code
            else if (line.find('`') != std::string::npos) {
                std::string rendered = line;
                size_t pos = 0;
                while ((pos = rendered.find('`', pos)) != std::string::npos) {
                    size_t end = rendered.find('`', pos + 1);
                    if (end == std::string::npos) break;
                    std::string code = rendered.substr(pos + 1, end - pos - 1);
                    rendered = rendered.substr(0, pos) + "\033[36m" + code + "\033[0m" + rendered.substr(end + 1);
                    pos = pos + code.size() + 8;
                }
                result += rendered + "\n";
            }
            else {
                result += line + "\n";
            }
        }

        if (inCodeBlock) result += "\033[0m";
        return result;
    }
};

} // namespace closecrab
