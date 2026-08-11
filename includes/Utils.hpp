#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace Utils {
static inline unsigned char toLower(unsigned char c) {
    return std::tolower(c);
}

static inline std::string caseFolded(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), toLower);
    return name;
}
} // namespace Utils