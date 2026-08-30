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
bool Handlers::join(Server &server, Client &client,
                    std::vector<std::string> &params) {
    ChannelManager &cm = server.getChannelManager();
    if (params.size() == 1 && params.at(0) == "0") {
        cm.partAll(client, client.getNick());
        return false;
    }
    std::vector<std::string> channels = Utils::split(params[0], ',');
    std::vector<std::string> keys = params.size() > 1
                                        ? Utils::split(params[1], ',')
                                        : std::vector<std::string>();
    const std::string       &serverName = server.getServerName();
    for (size_t i = 0; i < channels.size(); i++) {
        if (channels[i].empty())
            continue;
        try {
            const std::string &key = i < keys.size() ? keys[i] : "";
            cm.join(client, channels[i], key);
            Channel &ch = cm.at(channels[i]);
            ch.broadcast(
                Reply::fromClient(client.getPrefix(), "JOIN", channels[i]));
            ch.sendTopic(serverName, client);
            ch.sendNames(serverName, client);
        } catch (const Error &e) {
            if (e.hasReply())
                client.sendReply(serverName, e.reply());
        }
    }
    return false;
}

/*
INVITE <nickname> <channel>
*/
bool Handlers::invite(Server &server, Client &client,
                      std::vector<std::string> &params) {}

/*
KICK <channel> *( "," <channel> ) <user> *( "," <user> ) [<comment>]

The KICK command can be used to request the forced removal of a user from a
channel.  It causes the <user> to PART from the <channel> by force. For the
message to be syntactically correct, there MUST be either one channel parameter
and multiple user parameter, or as many channel parameters as there are user
parameters.  If a "comment" is given, this will be sent instead of the default
message, the nickname of the user issuing the KICK.
*/
bool Handlers::kick(Server &server, Client &client,
                    std::vector<std::string> &params) {
    std::vector<std::string> channels = Utils::split(params[0], ',');
    std::vector<std::string> users = Utils::split(params[1], ',');
    if (channels.size() != users.size() && channels.size() != 1) {
        client.sendReply(server.getServerName(),
                         Reply::reply(Reply::ERR_NEEDMOREPARAMS, "KICK"));
        return false;
    }
    ChannelManager    &cm = server.getChannelManager();
    ClientManager     &clm = server.getClientManager();
    const std::string &comment =
        params.size() > 2 ? params[2] : client.getNick();
    for (size_t i = 0; i < users.size(); i++) {
        const std::string &chan =
            channels.size() == 1 ? channels[0] : channels[i];
        if (chan.empty()) {
            if (channels.size() == 1)
                break;
            continue;
        }
        try {
            Channel &ch = cm.at(chan);
            Client  &user = clm.at(users[i]);
            ch.kick(client, user, comment);
        } catch (const Error &e) {
            if (e.hasReply())
                client.sendReply(server.getServerName(), e.reply());
        }
    }
    return false;
}

/*
PART <channel> *( "," <channel> ) [ <Part Message> ]

The PART command causes the user sending the message to be removed from the list
of active members for all given channels listed in the parameter string. If a
"Part Message" is given, this will be sent instead of the default message, the
nickname.
*/
bool Handlers::part(Server &server, Client &client,
                    std::vector<std::string> &params) {
    std::vector<std::string> channels = Utils::split(params[0], ',');
    std::string     reason = (params.size() > 1) ? params[1] : client.getNick();
    ChannelManager &cm = server.getChannelManager();
    const std::string &serverName = server.getServerName();
    for (size_t i = 0; i < channels.size(); i++) {
        if (channels[i].empty())
            continue;
        try {
            cm.part(client, channels[i], reason);
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
bool Handlers::topic(Server &server, Client &client,
                     std::vector<std::string> &params) {
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
