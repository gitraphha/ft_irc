/*
 * Messages
 *
 *   PRIVMSG <target>{,<target>} :<text>     RFC 2812 3.3.1
 *   NOTICE  <target>{,<target>} :<text>     RFC 2812 3.3.2
 *
 * NOTICE and PRIVMSG are the same delivery code; the one rule that separates
 * them (3.3.2) is that the server must NEVER send an automatic reply to a
 * NOTICE - no 401/403/404/411/412. That is what stops two bots from answering
 * each other's errors forever. deliver() therefore takes a `silent` flag:
 * NOTICE passes true, PRIVMSG passes false.
 *
 * A channel target (ChannelManager::isChannelName): the sender must be a
 * member (404 otherwise) and every other member receives the line - the
 * sender does not get its own message back. Anything else is a nickname (401
 * if unknown).
 *
 * Registered with minParams 0 so the RFC numerics 411 ("No recipient given")
 * and 412 ("No text to send") can be sent instead of the generic 461.
 */

#include "Channel.hpp"
#include "Client.hpp"
#include "Errors.hpp"
#include "Handlers.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "Utils.hpp"

#include <string>
#include <vector>

namespace {

// One target: a channel (fan out to the other members) or a nick.
void deliverTo(Server &server, Client &from, const std::string &target,
               const std::string &line) {
    ChannelManager &cm = server.getChannelManager();
    if (cm.isChannelName(target)) {
        Channel &channel = cm.at(target); // throws 403
        if (!channel.hasMember(from))
            throw Error::reply(Reply::ERR_CANNOTSENDTOCHAN, target);
        channel.broadcast(line, &from);
        return;
    }
    server.getClientManager().at(target).send(line); // throws 401
}

bool deliver(Server &server, Client &client,
             const std::vector<std::string> &params, const std::string &cmd,
             bool silent) {
    if (params.empty() || params[0].empty()) {
        if (!silent)
            client.sendReply(Reply::reply(Reply::ERR_NORECIPIENT, "",
                                          "No recipient given (" + cmd + ")"));
        return false;
    }
    if (params.size() < 2 || params[1].empty()) {
        if (!silent)
            client.sendReply(Reply::reply(Reply::ERR_NOTEXTTOSEND));
        return false;
    }
    const std::vector<std::string> targets = Utils::split(params[0], ',');
    for (size_t i = 0; i < targets.size(); i++) {
        if (targets[i].empty())
            continue;
        const std::string line =
            Reply::fromClient(client.getPrefix(), cmd, targets[i], params[1]);
        try {
            deliverTo(server, client, targets[i], line);
        } catch (const Error &e) {
            if (!silent && e.hasReply())
                client.sendReply(e.reply());
        }
    }
    return false;
}

} // namespace

bool Handlers::privmsg(Server &server, Client &client,
                       const std::vector<std::string> &params) {
    return deliver(server, client, params, "PRIVMSG", false);
}

bool Handlers::notice(Server &server, Client &client,
                      const std::vector<std::string> &params) {
    return deliver(server, client, params, "NOTICE", true);
}
