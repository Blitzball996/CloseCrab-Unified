#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>

namespace closecrab {

inline std::string generateUUID() {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 15);
    std::uniform_int_distribution<uint32_t> dist2(8, 11);

    const char* hex = "0123456789abcdef";
    std::string uuid(36, '-');
    // 8-4-4-4-12
    int pos[] = {0,1,2,3,4,5,6,7, 9,10,11,12, 14,15,16,17, 19,20,21,22, 24,25,26,27,28,29,30,31,32,33,34,35};
    for (int i : pos) {
        uuid[i] = hex[dist(gen)];
    }
    uuid[14] = '4'; // version 4
    uuid[19] = hex[dist2(gen)]; // variant
    return uuid;
}

} // namespace closecrab
