#include "Handlers.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Server.hpp"
#include "Utils.hpp"

bool Handlers::cap(Server &server, Client &client,
                   std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::invite(Server &server, Client &client,
                      std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

/*
JOIN <channel> *( "," <channel> ) [ <key> *( "," <key> ) ]
     / "0"

The JOIN command is used by a user to request to start listening to the
specific channel.

Once a user has joined a channel, he receives information about all commands his
server receives affecting the channel. This includes JOIN, MODE, KICK, PART, QUIT
and of course PRIVMSG/NOTICE.

Note that this message accepts a special argument ("0"), which is a special
request to leave all channels the user is currently a member of.

If a JOIN is successful, the user receives a JOIN message as
confirmation and is then sent the channel's topic (using RPL_TOPIC) and
the list of users who are on the channel (using RPL_NAMREPLY), which
MUST include the user joining.
*/
bool join(Server &server, Client &client, std::vector<std::string> &params) {
    ChannelManager &cm = server.getChannelManager();
    if (params.size() == 1 && params.at(0) == "0") {
        cm.partAll(client);
        return false;
    }
    std::vector<std::string> names = Utils::split(params[0], ',');
    std::vector<std::string> keys = params.size() > 1
                                        ? Utils::split(params[1], ',')
                                        : std::vector<std::string>();
    const std::string       &serverName = server.getServerName();
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i].empty())
            continue;
        try {
            const std::string &key = i < keys.size() ? keys[i] : "";
            cm.join(client, names[i], key);
            Channel &ch = cm.at(names[i]);
            ch.broadcast(
                Reply::fromClient(client.getPrefix(), "JOIN", names[i]));
            //send topic
            if (ch.hasTopic())
                client.sendReply(
                    serverName,
                    Reply::reply(Reply::RPL_TOPIC, names[i], ch.getTopic()));
            else
                client.sendReply(serverName,
                                 Reply::reply(Reply::RPL_NOTOPIC, names[i]));
            //send names
            client.sendReply(serverName,
                             Reply::reply(Reply::RPL_NAMREPLY, "= " + names[i],
                                          ch.namesList()));
            client.sendReply(serverName,
                             Reply::reply(Reply::RPL_ENDOFNAMES, names[i]));
        } catch (const Error &e) {
            if (e.hasReply())
                client.sendReply(serverName, e.reply());
        }
    }
    return false;
}

bool Handlers::kick(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::kill(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::mode(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::names(Server &server, Client &client,
                     std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::nick(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::message(Server &server, Client &client,
                       std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::oper(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::part(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::pass(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::ping(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::pong(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::quit(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::rehash(Server &server, Client &client,
                      std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::rename(Server &server, Client &client,
                      std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

/*
TOPIC <channel> [ <topic> ]

The TOPIC command is used to change or view the topic of a channel.

The topic for channel <channel> is returned if there is no <topic> given.
If the <topic> parameter is present, the topic for that channel will be changed,
if this action is allowed for the user requesting it. If the <topic> parameter
is an empty string, the topic for that channel will be removed.
*/
bool topic(Server &server, Client &client, std::vector<std::string> &params) {
    try {
        Channel &ch = server.getChannelManager().at(params[0]);
        if (params.size() > 1)
            ch.setTopic(client, params[1]);
        else
            ch.sendTopic(server.getServerName(), client);
    } catch (const Error &e) {
        if (e.hasReply())
            client.sendReply(server.getServerName(), e.reply());
    }
    return false;
}

bool Handlers::user(Server &server, Client &client,
                    std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::version(Server &server, Client &client,
                       std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::who(Server &server, Client &client,
                   std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::whois(Server &server, Client &client,
                     std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
	return false;
}
