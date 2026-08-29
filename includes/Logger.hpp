#pragma once

#include "colours.hpp"

#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

class Logger {
    std::string        prefix_;

    static std::string nowStr_() {
        time_t     now = time(0);
        struct tm *timeinfo = localtime(&now);
        char       buffer[32];
        strftime(&buffer[0], sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return std::string(&buffer[0]);
    }

  public:
    enum logMode { lmInfo, lmWarn, lmError, lmExcept };

    Logger(std::string const &prefix) : prefix_(prefix) {}

    void logMsg(std::string msg, logMode const lm) const;
};
