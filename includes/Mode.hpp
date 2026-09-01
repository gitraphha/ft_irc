#pragma once

#include <string>

/*
 * Channel Modes available
 *
 * i: Set/remove Invite-only channel
 * t: Set/remove the restrictions of the TOPIC command to channel operators
 * k: Set/remove the channel key (password)
 * o: Give/take channel operator privilege
 * l: Set/remove the user limit to channel
 * B: 
 */

namespace Mode {

// Channel Modes
enum ChannelMode {
    InviteOnly = 'i',  // flag
    OpOnlyTopic = 't', // flag
    Key = 'k',         // flag arg
    UserLimit = 'l',   // flag arg
    Bot = 'B'          // flag - bonus implementation
};

// Channel User Modes
enum UserMode {
    ChannelFounder = 'q',  // arg
    ChannelAdmin = 'a',    // arg
    ChannelOperator = 'o', // arg
    Halfop = 'h',          // arg
    Voice = 'v',           // arg
    NoMode = 0
};

// ChannelUserModes holds the list of all user modes that can be applied to a
// user in a channel in descending order of precedence
static const int    channelUserModes[] = {ChannelFounder, ChannelAdmin,
                                          ChannelOperator, Halfop, Voice};

static const size_t totalUserModes =
    sizeof(channelUserModes) / sizeof(*channelUserModes);

inline std::string channelModePrefix(int m) {
    switch (m) {
        case ChannelFounder:
            return "~";
        case ChannelAdmin:
            return "&";
        case ChannelOperator:
            return "@";
        case Halfop:
            return "%";
        case Voice:
            return "+";
        default:
            return "";
    }
}

inline bool hasPrivsOver(int kickerMode, int targetMode) {
    switch (kickerMode) {
        case ChannelFounder:
            return true;
        case ChannelAdmin:
        case ChannelOperator:
            return targetMode != ChannelFounder && targetMode != ChannelAdmin;
        case Halfop:
            return targetMode == Voice || targetMode == NoMode;
        default:
            return false;
    }
}

} // namespace Mode
