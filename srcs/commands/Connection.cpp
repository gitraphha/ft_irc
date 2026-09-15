/*
 * Connection upkeep
 *
 *   QUIT [<message>]        RFC 2812 3.1.7
 *   PING <token>            RFC 2812 3.7.2
 *   PONG <token>            RFC 2812 3.7.3
 *
 * QUIT tells everyone sharing a channel with the client ":nick!user@host QUIT
 * :<message>" (once each), sends "ERROR :Closing link" to the client and
 * returns true - the agreed signal for "close this connection once pending
 * output is flushed". The loop then parts the client from its channels and
 * frees it; nothing else to do here.
 *
 * PING answers "PONG <server> :<token>" so the client knows the link is alive;
 * it is usable before registration because irssi pings while connecting. PONG
 * is the client's answer to a PING from us - we do not send any yet (no timer
 * in the loop), so it is accepted silently.
 *
 * PING is registered with minParams 0 so a bare PING gets 409 ERR_NOORIGIN
 * instead of the generic 461.
 */

#include "Client.hpp"
#include "Handlers.hpp"
#include "Reply.hpp"
#include "Server.hpp"

#include <string>
#include <vector>

bool Handlers::quit(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    const std::string reason =
        params.empty() || params[0].empty() ? "Client Quit" : params[0];
    if (client.isRegistered())
        server.getChannelManager().broadcastToPeers(
            client, Reply::fromClient(client.getPrefix(), "QUIT", "", reason));
    client.sendClosingMsg("Quit: " + reason);
    return true;
}

bool Handlers::ping(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    if (params.empty() || params[0].empty()) {
        client.sendReply(Reply::reply(Reply::ERR_NOORIGIN));
        return false;
    }
    client.send(Reply::fromClient(server.getServerName(), "PONG",
                                  server.getServerName(), params[0]));
    return false;
}

bool Handlers::pong(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}
