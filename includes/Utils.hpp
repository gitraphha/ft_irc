#pragma once

#include <algorithm>
#include <string>
#include <vector>

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

static inline std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::istringstream       iss(s);
    std::string              token;
    while (std::getline(iss, token, delim))
        out.push_back(token);
    return out;
}

} // namespace Utils
