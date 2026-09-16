#pragma once

#include <ctime>  // for localtime, strftime, time, time_t
#include <string> // for basic_string, string

#define DBGLOG

class Logger {
    std::string prefix_;

  public:
    static std::string nowStr() {
        time_t     now = time(0);
        struct tm *timeinfo = localtime(&now);
        char       buffer[32]; // NOLINT
        strftime(&buffer[0], sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return std::string(&buffer[0]);
    }

    enum logMode { lmInfo, lmWarn, lmError, lmExcept, lmIn, lmOut };

    Logger(std::string const &prefix) : prefix_(prefix) {}

    void logMsg(std::string msg, logMode const lm) const;
};
