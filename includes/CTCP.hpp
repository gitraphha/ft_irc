#ifdef BONUS

#pragma once

#include <string>

namespace CTCP {
static const char        SOH = '\x01';
static const std::string strSOH = "\x01";

static inline bool isCTCP(const std::string &body) {
    return !body.empty() && body[0] == SOH;
}

// strip starting SOF (\x01), strip ending SOH if present
static inline std::string extractCTCP(const std::string &body) {
    std::string out = body.substr(1);
    if (!out.empty() && out[out.size() - 1] == SOH)
        out.erase(out.size() - 1);
    return out;
}

// Builds "\x01<payload>\x01". Used by bot
static inline std::string buildCTCP(const std::string payload) {
    std::string out = strSOH + payload + SOH;
    return out;
}

} // namespace CTCP

#endif
