#pragma once

#include <algorithm>
#include <string>

namespace Utils {
static inline char foldChar(char c) {
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));

    switch (c) {
        case '[':
            return '{';
        case ']':
            return '}';
        case '\\':
            return '|';
        case '~':
            return '^';
        default:
            return c;
    }
}

static inline std::string foldCase(const std::string &name) {
    std::string folded(name);
    std::transform(folded.begin(), folded.end(), folded.begin(), foldChar);
    return folded;
}
} // namespace Utils
