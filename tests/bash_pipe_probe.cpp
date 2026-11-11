#include "tools/BashTool/BashTool.h"
#include <iostream>

int main() {
    closecrab::BashTool tool;
    closecrab::ToolContext context;
    context.localMoE = true;
    auto check = [&](const char* command, int timeout, const char* expected) {
        auto start = std::chrono::steady_clock::now();
        auto result = tool.call(context, {{"command", command}, {"timeout", timeout}});
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << command << ": " << seconds << "s " << result.content << '\n';
        return result.content.find(expected) != std::string::npos && seconds < 4.0;
    };
    if (!check("printf pipe-output", 5000, "pipe-output")) return 1;
    if (!check("sleep 8 & printf detached", 5000, "detached")) return 2;
    if (!check("sleep 8", 1000, "timed out")) return 3;
    std::cout << "PASS: output, detached child, timeout\n";
}
