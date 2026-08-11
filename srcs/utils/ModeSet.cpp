#include "ModeSet.hpp"

ModeSet::ModeSet() {
    modeSet[0] = 0;
    modeSet[1] = 0;
}

bool ModeSet::has(int mode) const {
    if (mode < MIN_MODE || mode > MAX_MODE)
        return false;
    uint32_t index, mask;
    indexAndMask(mode, index, mask);
    return (modeSet[index] & mask) != 0;
}

bool ModeSet::hasAtLeast(const ModeSet &modes) const {
    for (uint32_t i = MIN_MODE; i <= MAX_MODE; i++) {
        if (modes.has(i) && !has(i))
            return false;
    }
    return true;
}

// set `mode` to be on or off, return whether the value actually changed
bool ModeSet::set(int mode, bool on) {
    if (mode < MIN_MODE || mode > MAX_MODE)
        return false;
    uint32_t index, mask;
    indexAndMask(mode, index, mask);
    uint32_t desired;
    uint32_t current = modeSet[index];
    if (on)
        desired = current | mask;
    else
        desired = current & (~mask);
    if (current == desired)
        return false;
    modeSet[index] = desired;
    return true;
}

ModeSet &ModeSet::operator+=(const std::string &modes) {
    std::string::const_iterator it = modes.begin();
    for (; it != modes.end(); it++) {
        set(*it, true);
    }
    return *this;
}

ModeSet &ModeSet::operator-=(const std::string &modes) {
    std::string::const_iterator it = modes.begin();
    for (; it != modes.end(); it++) {
        set(*it, false);
    }
    return *this;
}

ModeSet &ModeSet::operator+=(int mode) {
    set(mode, true);
    return *this;
}

ModeSet &ModeSet::operator-=(int mode) {
    set(mode, false);
    return *this;
}
