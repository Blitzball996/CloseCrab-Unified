#include "moe/MoETokenizer.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    spdlog::set_level(spdlog::level::off);
    closecrab::moe::MoETokenizer tokenizer;
    if (!tokenizer.loadFromJson(argv[1])) return 3;
    std::string line;
    while (std::getline(std::cin, line)) {
        auto text = nlohmann::json::parse(line).get<std::string>();
        std::cout << nlohmann::json(tokenizer.encode(text, false)).dump() << std::endl;
    }
}
