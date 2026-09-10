#include "ModeSet.hpp"

#include <climits>

ModeSet::ModeSet() : modes_() {
    modes_[0] = 0;
    modes_[1] = 0;
}

void ModeSet::indexAndMask_(int mode, uint32_t &index, uint32_t &mask) {
    static const int bitsPerInt = sizeof(uint32_t) * CHAR_BIT; // 4 * 8 = 32
    uint32_t         position = static_cast<uint32_t>(mode) - MIN_MODE;
    index = position / bitsPerInt;
    uint32_t bit = position % bitsPerInt;
    mask = 1U << bit;
}

bool ModeSet::hasMode(int mode) const {
    if (mode < MIN_MODE || mode > MAX_MODE)
        return false;
    uint32_t index, mask;
    indexAndMask_(mode, index, mask);
    return (modes_[index] & mask) != 0;
}

int ModeSet::highestUserMode() const {
    size_t total = Mode::totalChannelUserModes;
    for (size_t i = 0; i < total; i++) {
        int m = Mode::channelUserModes[i];
        if (hasMode(m))
            return m;
    }
    return Mode::NoMode;
}

// returns true if the mode was changed.
bool ModeSet::setMode(int mode, bool add) {
    if (mode < MIN_MODE || mode > MAX_MODE)
        return false;
    uint32_t index, mask;
    indexAndMask_(mode, index, mask);
    uint32_t current = modes_[index];
    uint32_t desired = add ? (current | mask) : (current & (~mask));
    if (current == desired)
        return false;
    modes_[index] = desired;
    return true;
}

ModeSet &ModeSet::operator+=(const std::string &modes) {
    std::string::const_iterator it = modes.begin();
    for (; it != modes.end(); it++) {
        setMode(*it, true);
    }
    return *this;
}

ModeSet &ModeSet::operator-=(const std::string &modes) {
    std::string::const_iterator it = modes.begin();
    for (; it != modes.end(); it++) {
        setMode(*it, false);
    }
    return *this;
}

ModeSet &ModeSet::operator+=(int mode) {
    setMode(mode, true);
    return *this;
}

ModeSet &ModeSet::operator-=(int mode) {
    setMode(mode, false);
    return *this;
}
