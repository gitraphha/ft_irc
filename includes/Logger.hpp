#pragma once

#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

enum logMode { lmInfo, lmError };

class Logger {
    std::string prefix_;

    std::string nowStr_() const {
        time_t     now = time(0);
        struct tm *timeinfo = localtime(&now);
        char       buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return std::string(buffer);
    }

  public:
    Logger(std::string const &prefix) {
        prefix_ = prefix;
    }

    void logMsg(std::string const &msg, logMode const lm) const {
        if (lm == (logMode)lmInfo)
            std::cout << nowStr_() << " " << prefix_ << "[INFO] : " << msg
                      << std::endl;
        if (lm == (logMode)lmError)
            std::cerr << nowStr_() << " " << prefix_ << "[ERROR]: " << msg
                      << std::endl;
    }
};
