/*
 * Connection registration - RFC 2812 section 3.1
 *
 *   PASS <password>                                    3.1.1
 *   NICK <nickname>                                    3.1.2
 *   USER <user> <mode> <unused> <realname>             3.1.3
 *   CAP  LS|LIST|REQ|END      (IRCv3, sent first by irssi/HexChat; we
 *                              advertise no capabilities so the client
 *                              falls back to plain RFC 2812 registration)
 *
 * Flow (NICK and USER in any order; PASS must precede whichever of them
 * completes the pair - a link with no/wrong PASS is closed at that point,
 * the way ergo does it):
 *
 *   PASS ok? -> passAccepted_ = true / false
 *   NICK     -> validate (431/432/433), store, index in ClientManager
 *   USER     -> store user + realname
 *   after every pre-registration command, CommandProcessor::run calls
 *   tryRegister():
 *     nick && user known
 *       && !passAccepted   -> 464 + ERROR, return true (close the link)
 *       && passAccepted    -> registered_ = true, 001 002 003 004 005 422
 *
 * PASS/USER after registration -> 462. NICK after registration renames
 * the client and tells it, and everyone sharing a channel with it,
 * ":old!user@host NICK :new" (once each).
 *
 * All replies go through Client::send() (the per-client output queue); the
 * event loop is responsible for flushing it to the socket.
 */

#include "Registration.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Handlers.hpp"
#include "Parser.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "Utils.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#define NICKLEN 30 // RFC 2812 says 9; modern servers and clients use 30

namespace {

// RFC 2812 2.3.1:
//   nickname = ( letter / special ) *8( letter / digit / special / "-" )
//   special  = %x5B-60 / %x7B-7D  ; "[", "]", "\", "`", "_", "^", "{", "|", "}"
bool isSpecial(char c) {
    return std::strchr("[]\\`_^{|}", c) != 0 && c != '\0';
}

// First char: letter or special. The rest: letter, digit, special or '-'.
bool isValidNick(const std::string &nick) {
    if (nick.empty() || nick.size() > NICKLEN)
        return false;
    unsigned char c = static_cast<unsigned char>(nick[0]);
    if (!std::isalpha(c) && !isSpecial(nick[0]))
        return false;
    for (std::string::size_type i = 1; i < nick.size(); i++) {
        c = static_cast<unsigned char>(nick[i]);
        if (!std::isalnum(c) && !isSpecial(nick[i]) && nick[i] != '-')
            return false;
    }
    return true;
}

void sendWelcome(Server &server, Client &client) {
    const std::string name = server.getServerName();
    const std::string version = server.getServerVersion();
    client.sendReply(Reply::reply(Reply::RPL_WELCOME, "",
                                  "Welcome to the Internet Relay Network " +
                                      client.getPrefix()));
    client.sendReply(
        Reply::reply(Reply::RPL_YOURHOST, "",
                     "Your host is " + name + ", running version " + version));
    client.sendReply(
        Reply::reply(Reply::RPL_CREATED, "",
                     "This server was created " + server.getCreatedAt()));
    // <servername> <version> <available user modes> <available channel modes>
    client.sendReply(
        Reply::reply(Reply::RPL_MYINFO, name + " " + version + "  itkol"));
    client.sendReply(Reply::reply(Reply::RPL_ISUPPORT,
                                  "CHANTYPES=&#+! NICKLEN=30 CHANMODES=,k,l,it "
                                  "PREFIX=(o)@ CASEMAPPING=rfc1459",
                                  "are supported by this server"));
    // No MOTD: irssi (and most clients) consider registration finished only
    // after RPL_ENDOFMOTD (376) or ERR_NOMOTD (422).
    client.sendReply(Reply::reply(Reply::ERR_NOMOTD, ""));
}

} // namespace

// CAP LS 302 is the first thing irssi/HexChat send. Answer with an empty
// capability list so the client sends CAP END and carries on; NAK any REQ.
bool Handlers::cap(Server &server, Client &client,
                   const std::vector<std::string> &params) {
    const std::string sub = Parser::upperCase(params[0]);
    const std::string target = client.getReplyTarget();
    if (sub == "LS" || sub == "LIST")
        // trailing required - create custom string directly
        client.send(":" + server.getServerName() + " CAP " + target + " " +
                    sub + " :\r\n");
    else if (sub == "REQ")
        client.send(Reply::fromClient(server.getServerName(), "CAP",
                                      target + " NAK",
                                      params.size() > 1 ? params[1] : ""));
    // END and anything else: nothing to do
    return false;
}

// PASS <password>
bool Handlers::pass(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    if (client.isRegistered())
        throw Error::reply(Reply::ERR_ALREADYREGISTRED, "");
    if (params[0].empty())
        throw Error::reply(Reply::ERR_NEEDMOREPARAMS, "PASS");
    // A wrong password is not reported here: it is reported (464) and the
    // link closed when NICK+USER complete, see tryRegister().
    client.setPassAccepted(params[0] == server.getPassword());
    return false;
}

// NICK <nickname>
bool Handlers::nick(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    if (params.empty() || params[0].empty())
        throw Error::reply(Reply::ERR_NONICKNAMEGIVEN, "");
    const std::string &nick = params[0];
    if (!isValidNick(nick))
        throw Error::reply(Reply::ERR_ERRONEUSNICKNAME, nick);

    ClientManager &clients = server.getClientManager();
    const bool sameClient = Utils::foldCase(nick) == client.getNickCaseFolded();
    if (!sameClient && clients.hasNick(nick))
        throw Error::reply(Reply::ERR_NICKNAMEINUSE, nick);

    if (client.isRegistered()) {
        // Registered client renaming itself: everyone who can see it - itself
        // and every peer sharing a channel - gets the change, with the OLD
        // prefix.
        const std::string notice =
            Reply::fromClient(client.getPrefix(), "NICK", nick);
        client.send(notice);
        server.getChannelManager().broadcastToPeers(client, notice);
    }
    clients.setNick(client, nick);
    return false;
}

// USER <user> <mode> <unused> <realname>
bool Handlers::user(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    if (client.isRegistered())
        throw Error::reply(Reply::ERR_ALREADYREGISTRED, "");
    if (params[0].empty() || params[3].empty())
        throw Error::reply(Reply::ERR_NEEDMOREPARAMS, "USER");
    client.setUser(params[0], params[3]);
    return false;
}

bool Registration::tryRegister(Server &server, Client &client) {
    if (client.isRegistered() || !client.hasIdentity())
        return false;
    if (!client.hasPassAccepted()) {
        client.sendReply(Reply::reply(Reply::ERR_PASSWDMISMATCH, ""));
        client.sendClosingMsg("Password incorrect");
        return true; // event loop: flush, then close the connection
    }
    client.setRegistered(true);
    sendWelcome(server, client);
    return false;
}
