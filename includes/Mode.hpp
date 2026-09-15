#pragma once

#include "Utils.hpp"

#include <string>
#include <vector>

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
    UserLimit = 'l'    // flag arg
};

// SupportedChannelModes are the channel modes that we support.
static const int supportedChannelModes[] = {InviteOnly, Key, OpOnlyTopic,
                                            UserLimit};

static const size_t totalChannelModes =
    sizeof(supportedChannelModes) / sizeof(supportedChannelModes[0]);

// Channel User Modes
enum ChannelUserMode {
    ChannelFounder = 'q',  // arg
    ChannelAdmin = 'a',    // arg
    ChannelOperator = 'o', // arg
    Halfop = 'h',          // arg
    Voice = 'v',           // arg
    NoMode = 0
};

// ChannelUserModes holds the list of all user modes that can be applied to a
// user in a channel in descending order of precedence
static const int channelUserModes[] = {ChannelFounder, ChannelAdmin,
                                       ChannelOperator, Halfop, Voice};

static const size_t totalChannelUserModes =
    sizeof(channelUserModes) / sizeof(channelUserModes[0]);

inline std::string toString(int m) {
    return std::string(1, static_cast<char>(m));
}

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

/*
Returns true if the `client` can grant/affect the `target`.

Rank:  Founder > Admin > Operator > Halfop > Voice > NoMode
  - Founders outrank everyone.
  - Admin/Operator may act on anyone below them, but NOT on a Founder or on
  another Admin.
  - Halfop may only act on Voice / the unprivileged.
  - Voice and the unprivileged may act on nobody.
*/
inline bool channelUserModeHasPrivsOver(int clientMode, int targetMode) {
    switch (clientMode) {
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

// = means no sign, meaning a query
enum ModeOp { Add = '+', Remove = '-', List = '=' };

// ModeChange is a single mode changing
struct ModeChange {
    int         mode;
    ModeOp      op;
    std::string arg;

    ModeChange(int m, ModeOp o, const std::string &a = "")
        : mode(m), op(o), arg(a) {}
};

// ModeChanges are a collection of 'ModeChange's
typedef std::vector<Mode::ModeChange> ModeChanges;

/*
Returns whether the client (whose highest channel mode is `issuerHighest`) may
apply the given `change`

For channel user mode (q, a, o, h, v)
  - List is always allowed (a quey, not a change)
  - self removal of a mode is always allowed
  - else the requester must outrank the target's mode
For flag modes (i, t, k, l)
  - List is always allowed, adding/removing requires channel operator rank
*/
inline bool hasPrivs(const std::string &clientCaseFolded, int issuerHighest,
                     const ModeChange &change) {
    switch (change.mode) {
        case ChannelFounder:
        case ChannelAdmin:
        case ChannelOperator:
        case Halfop:
        case Voice:
            if (change.op == List)
                return true;
            if (Utils::foldCase(change.arg) == clientCaseFolded) {
                if (change.op == Remove)
                    return true;
            }
            return channelUserModeHasPrivsOver(issuerHighest, change.mode);
        default:
            return change.op == List ||
                   channelUserModeHasPrivsOver(issuerHighest, ChannelOperator);
    }
}

// create string like `+itl` instead of `+i +t +l`
inline std::string serialiseModeChanges(const ModeChanges &changes) {
    std::string out;
    if (changes.empty())
        return out;
    char op = static_cast<char>(changes[0].op);
    out += op;
    ModeChanges::const_iterator it = changes.begin();
    for (; it != changes.end(); it++) {
        char c = static_cast<char>(it->op);
        if (c != op) {
            op = c;
            out += c;
        }
        out += static_cast<char>(it->mode);
    }
    return out;
}

/*
Parses the params and returns ModeChanges (vector of ModeChange).

  - `k` is type B, requiring a parameter for both add and remove
  - `l` is type C, requiring parameter only when setting

Note: allows `-k` with or without a param
*/
inline ModeChanges
parseChannelModeChanges(const std::vector<std::string> &params,
                        std::string                    &unknown) {
    ModeOp op = List;
    unknown = "";
    ModeChanges changes;

    std::string::const_iterator it = params[0].begin();
    size_t                      idx = 1;
    for (; it != params[0].end(); it++) {
        if (*it == '-' || *it == '+' || *it == '=') {
            op = ModeOp(*it);
            continue;
        }
        switch (*it) {
            case InviteOnly:
            case OpOnlyTopic:
                changes.push_back(ModeChange(*it, op, ""));
                break;
            case UserLimit:
                if (op == Remove) {
                    changes.push_back(ModeChange(*it, op, ""));
                    continue;
                }
            case ChannelOperator:
            case Key:
                if (idx >= params.size())
                    changes.push_back(ModeChange(*it, op, ""));
                else
                    changes.push_back(ModeChange(*it, op, params.at(idx++)));
                break;
            default:
                unknown.push_back(*it);
        }
    }
    return changes;
}
} // namespace Mode
