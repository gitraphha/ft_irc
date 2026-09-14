#include "Logger.hpp"

#include "colours.hpp" // for CGreen, CMgnta, CRed, CReset, CYellow

#include <iostream> // for basic_ostream, operator<<, basic_ios, cerr, cout

/*
   Log strings in four categories (severity) in different colours
   to stdout/stderr respectively.
*/
void Logger::logMsg(std::string msg, logMode const lm) const {
    std::ostream *os = &std::cout;
    if (lm == Logger::lmError || lm == Logger::lmExcept)
        os = &std::cerr;

    switch (lm) {
        case Logger::lmInfo:
            msg.insert(0, " [INFO  ]: ");
            *os << CGreen;
            break;
        case Logger::lmWarn:
            msg.insert(0, " [WARN  ]: ");
            *os << CYellow;
            break;
        case Logger::lmError:
            msg.insert(0, " [ERROR ]: ");
            *os << CRed;
            break;
        case Logger::lmExcept:
            msg.insert(0, " [EXCEPT]: ");
            *os << CMgnta;
            break;
    }
    *os << nowStr() << " " << prefix_ << msg << CReset << std::endl;
}
