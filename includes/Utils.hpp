#pragma once

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <sstream>
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

static inline int toPosInt(const std::string &s) {
    if (s.empty())
        return -1;
    char *ptr = NULL;
    long  val = strtol(s.c_str(), &ptr, 10);
    if (*ptr || val <= 0 || val > INT_MAX)
        return -1;
    return static_cast<int>(val);
}

// joins 3 strings with space character. optional usage for 2 strings
static inline std::string join3(const std::string &a, const std::string &b,
                                const std::string &c = "") {
    return a + " " + b + " " + (c.empty() ? "" : " " + c);
}

} // namespace Utils
