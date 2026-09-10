#pragma once

/*
 * RFC 2812 numeric replies: RPL_* (001-395) and ERR_* (401-502)
 * trimmed to what this server implements.
 *
 * Implemented commands:
 *   PASS, NICK, USER, JOIN, PART, PRIVMSG, NOTICE, PING/PONG, QUIT
 *   KICK, INVITE, TOPIC, MODE (i,t,k,o,l), NAMES, OPER
 *   bonus: DCC (file transfer over PRIVMSG/CTCP), bot
 *
 *   - enum Code      : the numeric codes (the numbers must be sent -
 *                      clients identify replies by numeric, not by text).
 *   - text()         : the default trailing text of fixed-text replies.
 *                      Code::ERR_NOSUCHNICK -> "No such nick/channel"
 *   - reply()        : one generic builder, no printf involved.
 *
 * Line layout required:
 *   ":<server> <code> <nick> <params> :<text>\r\n"
 *
 *   Reply::Message m = Reply::reply(Reply::ERR_NOSUCHNICK, "bob");
 *   std::string line = Reply::build(server, nick, m);
 *     // ":irc.local 401 nick bob :No such nick/channel\r\n"
 *
 * Replies whose trailing text is dynamic (TOPIC, WELCOME, NAMREPLY, MOTD,
 * and errors like ERR_UNKNOWNMODE that embed a channel) pass it explicitly:
 *   Reply::reply(Reply::RPL_TOPIC, "#chan", topic);
 *     // "332 #chan :<topic>"
 *   Reply::reply(Reply::RPL_WELCOME, "", "Welcome to the Internet Relay Network nick!u@h");
 *     // "001 :Welcome to the Internet Relay Network nick!u@h"
 *
 * C++98 notes:
 *   - Everything is inline in a header -> no ODR/link problems.
 *   - text() switches on the enum with no default so -Wswitch (via -Wall)
 *     forces every code to have a case: a new numeric without text is a
 *     compile-time error, not a silent "".
 *   - No std::to_string; the numeric code is converted with std::ostringstream.
 */

#include <iomanip>
#include <sstream>
#include <string>

namespace Reply {

// Numeric codes. Each reply's text lives in text(), or is passed by the
// caller when it is dynamic. Format lines follow RFC 2812.
enum Code {
    // registration (sent 001-004 on success; 005 optional for client compat)
    // if client waits for MOTD after 004, send 422 after 004
    RPL_WELCOME =
        001, // :Welcome to the Internet Relay Network <nick>!<user>@<host>
    RPL_YOURHOST = 002, // :Your host is <servername>, running version <ver>
    RPL_CREATED = 003,  // :This server was created <date>
    RPL_MYINFO = 004,   // <servername> <version> <umodes> <chanmodes>
    RPL_ISUPPORT = 005, // tokens, e.g. CHANTYPES=# NICKLEN=30

    // ADMIN command 256/257/258/259/423

    // WHO command needs 315/352
    // WHOIS needs 311/312/318
    // WHOWAS needs 314/369

    // channel queries and ops (JOIN/PART/KICK/INVITE/TOPIC/MODE)
    RPL_CHANNELMODEIS = 324, // <channel> <mode> <mode params>
    RPL_NOTOPIC = 331,       // <channel> :No topic is set
    RPL_TOPIC = 332,         // <channel> :<topic>
    RPL_TOPICWHOTIME = 333,  // <channel> <setnick> <setat>
    RPL_INVITING = 341,      // <channel> <nick>
    RPL_VERSION = 351,       // <version>.<debuglevel> <server> :<comments>
    RPL_NAMREPLY = 353, // "=" <channel> :[prefix]<nick> *( " " [prefix]<nick> )
    RPL_ENDOFNAMES = 366, // <channel> :End of NAMES list

    RPL_YOUREOPER = 381, // :You are now an IRC operator                (OPER)
    RPL_REHASHING = 382, // <config file> :Rehashing                    (REHASH)

    // errors 400-599 (RFC 2812 section 5.2)
    ERR_NOSUCHNICK =
        401, // <nickname> :No such nick/channel       (PRIVMSG, INVITE)
    ERR_NOSUCHSERVER = 402, // <server name> :No such server
    ERR_NOSUCHCHANNEL =
        403, // <channel> :No such channel             (JOIN/PART/KICK/INVITE/TOPIC/MODE/PRIVMSG)
    ERR_CANNOTSENDTOCHAN =
        404, // <channel> :Cannot send to channel      (PRIVMSG)
    ERR_TOOMANYCHANNELS =
        405, // <channel name> :You have joined too many channels (JOIN)
    ERR_NOORIGIN = 409,    // :No origin specified                   (PING/PONG)
    ERR_NORECIPIENT = 411, // :No recipient given (<command>)        (PRIVMSG)
    ERR_NOTEXTTOSEND = 412, // :No text to send                       (PRIVMSG)
    ERR_INPUTTOOLONG = 417, // :Input line was too long
    ERR_UNKNOWNCOMMAND = 421,   // <command> :Unknown command
    ERR_NOMOTD = 422,           // :MOTD File is missing
    ERR_NONICKNAMEGIVEN = 431,  // :No nickname given                     (NICK)
    ERR_ERRONEUSNICKNAME = 432, // <nick> :Erroneous nickname             (NICK)
    ERR_NICKNAMEINUSE = 433,    // <nick> :Nickname is already in use     (NICK)
    ERR_USERNOTINCHANNEL =
        441, // <nick> <channel> :They aren't on that channel (KICK, MODE +o)
    ERR_NOTONCHANNEL = 442,   // <channel> :You're not on that channel
    ERR_USERONCHANNEL = 443,  // <user> <channel> :is already on channel
    ERR_USERSDISABLED = 446,  // :USERS has been disabled                (USERS)
    ERR_NOTREGISTERED = 451,  // :You have not registered
    ERR_NEEDMOREPARAMS = 461, // <command> :Not enough parameters
    ERR_ALREADYREGISTRED = 462, // :Unauthorized command (already registered)
    ERR_PASSWDMISMATCH =
        464,          // :Password incorrect                    (PASS, OPER)
    ERR_KEYSET = 467, // <channel> :Channel key already set
    ERR_CHANNELISFULL = 471, // <channel> :Cannot join channel (+l)
    ERR_UNKNOWNMODE = 472,   // <char> :is unknown mode char to me for <channel>
    ERR_INVITEONLYCHAN = 473, // <channel> :Cannot join channel (+i)
    ERR_BADCHANNELKEY = 475,  // <channel> :Cannot join channel (+k)
    ERR_NOPRIVILEGES = 481,   // :Permission Denied- You're not an IRC operator
    ERR_CHANOPRIVSNEEDED = 482, // <channel> :You're not channel operator
    ERR_NOOPERHOST = 491,       // :No O-lines for your host             (OPER)
    ERR_INVALIDMODEPARAM =
        696 // <target chan/user> <mode char> <parameter> :<description>
};

// text() is the default trailing text of a fixed-text reply, or "" when the
// reply's trailing text is dynamic and must be supplied by the caller.
// No default case: -Wswitch requires every Code to be listed here, so a code
// added to the enum without a text is a compile-time error.
inline const char *text(Code code) {
    switch (code) {
        // fixed-text replies
        case RPL_NOTOPIC:
            return "No topic is set";
        case RPL_ENDOFNAMES:
            return "End of NAMES list";
        case RPL_YOUREOPER:
            return "You are now an IRC operator";
        case RPL_REHASHING:
            return "Rehashing";
        // fixed-text errors
        case ERR_NOSUCHNICK:
            return "No such nick/channel";
        case ERR_NOSUCHSERVER:
            return "No such server";
        case ERR_NOSUCHCHANNEL:
            return "No such channel";
        case ERR_CANNOTSENDTOCHAN:
            return "Cannot send to channel";
        case ERR_TOOMANYCHANNELS:
            return "You have joined too many channels";
        case ERR_NOORIGIN:
            return "No origin specified";
        case ERR_NOTEXTTOSEND:
            return "No text to send";
        case ERR_INPUTTOOLONG:
            return "Input line was too long";
        case ERR_UNKNOWNCOMMAND:
            return "Unknown command";
        case ERR_NOMOTD:
            return "MOTD File is missing";
        case ERR_NONICKNAMEGIVEN:
            return "No nickname given";
        case ERR_ERRONEUSNICKNAME:
            return "Erroneous nickname";
        case ERR_NICKNAMEINUSE:
            return "Nickname is already in use";
        case ERR_USERNOTINCHANNEL:
            return "They aren't on that channel";
        case ERR_NOTONCHANNEL:
            return "You're not on that channel";
        case ERR_USERONCHANNEL:
            return "is already on channel";
        case ERR_USERSDISABLED:
            return "USERS has been disabled";
        case ERR_NOTREGISTERED:
            return "You have not registered";
        case ERR_NEEDMOREPARAMS:
            return "Not enough parameters";
        case ERR_ALREADYREGISTRED:
            return "Unauthorized command (already registered)";
        case ERR_PASSWDMISMATCH:
            return "Password incorrect";
        case ERR_KEYSET:
            return "Channel key already set";
        case ERR_CHANNELISFULL:
            return "Cannot join channel (+l)";
        case ERR_INVITEONLYCHAN:
            return "Cannot join channel (+i)";
        case ERR_BADCHANNELKEY:
            return "Cannot join channel (+k)";
        case ERR_NOPRIVILEGES:
            return "Permission Denied- You're not an IRC operator";
        case ERR_CHANOPRIVSNEEDED:
            return "You're not channel operator";
        case ERR_NOOPERHOST:
            return "No O-lines for your host";
        // dynamic trailing text: caller supplies it via reply(code, params, text)
        case RPL_WELCOME:
        case RPL_YOURHOST:
        case RPL_CREATED:
        case RPL_MYINFO:
        case RPL_ISUPPORT:
        case RPL_CHANNELMODEIS:
        case RPL_TOPIC:
        case RPL_TOPICWHOTIME:
        case RPL_INVITING:
        case RPL_VERSION:
        case RPL_NAMREPLY:
        case ERR_NORECIPIENT:
        case ERR_UNKNOWNMODE:
        case ERR_INVALIDMODEPARAM:
            return "";
    }
    return "";
}

// Message is one reply: the numeric code plus the "<params> :<text>" body.
// It deliberately does not include the receiver nick or the server name;
// build() injects those when the reply is actually sent.
struct Message {
    Code        code;
    std::string params;
    bool        hasTrailing;
    std::string trailing;

    Message() : code(), hasTrailing(false) {}

    Message(Code c, const std::string &p)
        : code(c), params(p), hasTrailing(false) {}

    Message(Code c, const std::string &p, const std::string &t)
        : code(c), params(p), hasTrailing(true), trailing(t) {}
};

// reply() with default text from text().
//   reply(ERR_NOSUCHNICK, "bob") -> code=401, body="bob :No such nick/channel"
//   reply(RPL_MYINFO, "irc.local 1.0 itkol") -> code=4, body="irc.local 1.0 itkol"
inline Message reply(Code code, const std::string &params) {
    const char *t = text(code);
    if (*t != '\0')
        return Message(code, params, t);
    return Message(code, params);
}

/*
reply() with caller-supplied trailing text: the ":" is always sent, even if
trailing is empty.
  reply(RPL_TOPIC, "#chan", topic) -> code=332, body="#chan :<topic>"
  reply(RPL_WELCOME, "", "Welcome ...") -> body " :Welcome ..."
*/
inline Message reply(Code code, const std::string &params,
                     const std::string &trailing) {
    return Message(code, params, trailing);
}

// returns formatted string ( :<prefix> <COMMAND> <params> [ :trailing ] )
inline std::string fromClient(const std::string &prefix, const std::string &cmd,
                              const std::string &params,
                              const std::string &trailing = "") {
    std::string out = ":";
    if (!prefix.empty())
        out += prefix + " ";
    out += cmd;
    if (!params.empty())
        out += " " + params;
    if (!trailing.empty())
        out += " :" + trailing;
    out += "\r\n";
    return out;
}

/*
builds the complete line layout for `msg`, addressed to `nick`:
  ":<server> <code> <nick> <body>\r\n"
The numeric is zero-padded to 3 digits ("001", not "1"). Empty params emit
nothing instead of leaving two spaces before the trailing.
Use "*" for `nick` until the client has registered (pre-001).
*/
inline std::string build(const std::string &server, const std::string &nick,
                         const Message &msg) {
    std::ostringstream ss;
    ss << ":" << server << " " << std::setw(3) << std::setfill('0') << msg.code
       << " " << nick;
    if (!msg.params.empty())
        ss << " " << msg.params;
    if (msg.hasTrailing)
        ss << " :" << msg.trailing;
    ss << "\r\n";
    return ss.str();
}

} // namespace Reply
