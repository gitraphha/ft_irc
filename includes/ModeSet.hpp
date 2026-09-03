#pragma once

#define MIN_MODE 65
#define MAX_MODE 122

#include "Mode.hpp"

#include <stdint.h> // uint32_t
#include <string>

/*
 * ModeSet holds a set of modes.
 *
 * Valid modes go from 65 ('A') to 122 ('z'), making at most 58 possible values.
 * Subtract 65 from the mode and use that bit of the uint32_t to represent it.
*/
class ModeSet {
    uint32_t modes_[2];

    static void indexAndMask_(int mode, uint32_t &index, uint32_t &mask) {
        uint32_t position = (uint32_t)mode - MIN_MODE;
        index = position / 32;
        uint32_t bit = position % 32;
        mask = 1U << bit;
    }

  public:
    ModeSet();

    bool has(int mode) const;
    bool hasAtLeast(const ModeSet &modes) const;
    int  highestUserMode() const;

    bool set(int mode, bool on);

    ModeSet &operator+=(const std::string &modes);
    ModeSet &operator-=(const std::string &modes);

    ModeSet &operator+=(int mode);
    ModeSet &operator-=(int mode);
};
