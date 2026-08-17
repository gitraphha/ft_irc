#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace Utils {
static inline unsigned char toLower(unsigned char c) {
    return std::tolower(c);
}

static inline std::string foldCase(const std::string &name) {
    std::string folded(name);
    std::transform(folded.begin(), folded.end(), folded.begin(), toLower);
    return folded;
}
} // namespace Utils
