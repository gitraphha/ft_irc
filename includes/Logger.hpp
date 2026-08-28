#pragma once

#include "colours.hpp"

#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

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
    enum logMode { lmInfo, lmWarn, lmError };

    Logger(std::string const &prefix) {
        prefix_ = prefix;
    }

    void logMsg(std::string const &msg, logMode const lm) const {
        if (lm == Logger::lmInfo)
            std::cout << nowStr_() << " " << prefix_ << CGreen
                      << " [INFO] : " << msg << CReset << std::endl;
        if (lm == Logger::lmWarn)
            std::cout << nowStr_() << " " << prefix_ << CYellow
                      << " [WARN] : " << msg << CReset << std::endl;
        if (lm == Logger::lmError)
            std::cerr << nowStr_() << " " << prefix_ << CRed
                      << "[ERROR]: " << msg << CReset << std::endl;
    }
};
