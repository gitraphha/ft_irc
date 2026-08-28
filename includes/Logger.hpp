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
    enum logMode { lmInfo, lmWarn, lmError, lmExcept };

    Logger(std::string const &prefix) {
        prefix_ = prefix;
    }

    void logMsg(std::string msg, logMode const lm) const {
        std::ostream *os = &std::cout;
        if (lm == Logger::lmError || lm == Logger::lmExcept)
            os = &std::cerr;

        switch (lm) {
            case Logger::lmInfo:
                msg.insert(0, " [INFO]: ");
                *os << CGreen;
                break;
            case Logger::lmWarn:
                msg.insert(0, " [WARN]: ");
                *os << CYellow;
                break;
            case Logger::lmError:
                msg.insert(0, " [ERROR]: ");
                *os << CRed;
                break;
            case Logger::lmExcept:
                msg.insert(0, " [EXCEPTION]: ");
                *os << CMgnta;
                break;
        }
        *os << nowStr_() << " " << prefix_ << msg << CReset << std::endl;
    }
};
