#pragma once

/*
 * Parser turns one raw IRC line into a command name and a list of parameters,
 * following the message grammar of RFC 2812 section 2.3.1.
 *
 * Parse splits line into its command name and parameters. On success, cmd holds
 * the command name folded to upper case, params holds every parameter in order,
 * and parse returns true.
 */

#include <cctype>
#include <string>
#include <vector>

namespace Parser {
inline char toUpperChar(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

inline std::string upperCase(const std::string &s) {
    std::string folded(s);
    for (std::string::size_type i = 0; i < folded.size(); i++)
        folded[i] = toUpperChar(folded[i]);
    return folded;
}

inline bool parse(const std::string &line, std::string &cmd,
                  std::vector<std::string> &params) {
    cmd.clear();
    params.clear();

    std::string::size_type pos = 0;
    if (pos < line.size() && line[pos] == ':') {
        std::string::size_type space = line.find(' ', pos);
        if (space == std::string::npos)
            return false;
        pos = space + 1;
    }
    pos = line.find_first_not_of(' ', pos);
    if (pos == std::string::npos)
        return false;
    std::string::size_type end = line.find(' ', pos);
    if (end == std::string::npos)
        end = line.size();
    cmd = upperCase(line.substr(pos, end - pos));
    pos = end;
    while (pos < line.size()) {
        pos = line.find_first_not_of(' ', pos);
        if (pos == std::string::npos)
            break;
        if (line[pos] == ':') {
            params.push_back(line.substr(pos + 1));
            break;
        }
        end = line.find(' ', pos);
        if (end == std::string::npos)
            end = line.size();
        params.push_back(line.substr(pos, end - pos));
        pos = end;
    }
    return !cmd.empty();
}
} // namespace Parser
