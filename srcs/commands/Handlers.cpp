#include "Handlers.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Server.hpp"
#include "Utils.hpp"

// PASS, NICK, USER, CAP and tryRegister live in Registration.cpp.

/*
INVITE <nickname> <channel>

The INVITE command is used to invite a user to a channel. The parameter
<nickname> is the nickname of the person to be invited to the target channel
<channel>. There is no requirement that the channel the target user is being
invited to must exist or be a valid channel. However, if the channel exists,
only members of the channel are allowed to invite other users. When the channel
has invite-only flag set, only channel operators may issue INVITE command.

Only the user inviting and the user being invited will receive notification of
the invitation. Other channel members are not notified. (This is unlike the MODE
changes, and is occasionally the source of trouble for users.)
*/
bool Handlers::invite(Server &server, Client &client,
                      const std::vector<std::string> &params) {
    ChannelManager &cm = server.getChannelManager();
    ClientManager  &clm = server.getClientManager();
    try {
        Client &target = clm.at(params[0]);
        if (cm.hasChannel(params[1])) {
            Channel &chan = cm.at(params[1]);
            chan.checkInvite(client, target);
        }
        target.addInvite(params[1]);
        client.sendReply(server.getServerName(),
                         Reply::reply(Reply::RPL_INVITING,
                                      params[1] + " " + target.getNick()));
        target.send(Reply::fromClient(client.getPrefix(), "INVITE",
                                      target.getNick() + " " + params[1]));
    } catch (const Error &e) {
        if (e.hasReply())
            client.sendReply(server.getServerName(), e.reply());
    }
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
                    const std::vector<std::string> &params) {
    ChannelManager &cm = server.getChannelManager();
    if (params.size() == 1 && params.at(0) == "0") {
        cm.leaveAll(client, client.getNick(), true);
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
KICK <channel> *( "," <channel> ) <user> *( "," <user> ) [<comment>]

The KICK command can be used to request the forced removal of a user from a
channel.  It causes the <user> to PART from the <channel> by force. For the
message to be syntactically correct, there MUST be either one channel parameter
and multiple user parameter, or as many channel parameters as there are user
parameters.  If a "comment" is given, this will be sent instead of the default
message, the nickname of the user issuing the KICK.
*/
bool Handlers::kick(Server &server, Client &client,
                    const std::vector<std::string> &params) {
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
            Client &user = clm.at(users[i]);
            cm.kick(client, chan, user, comment);
        } catch (const Error &e) {
            if (e.hasReply())
                client.sendReply(server.getServerName(), e.reply());
        }
    }
    return false;
}

bool Handlers::kill(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::mode(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

/*
NAMES [ <channel> *( "," <channel> ) [ <target> ] ]

By using the NAMES command, a user can list all nicknames that are visible to
him. The <channel> parameter specifies which channel(s) to return information
about. There is no error reply for bad channel names.

NOTE:
- optional [ <target> ] parameter is ignored as we do not support multiple
server

- Sends only RPL_ENDOFNAMES when no params is given although RFC 2812 says,
"
If no <channel> parameter is given, a list of all channels and their
occupants is returned. At the end of this list, a list of users who
are visible but either not on any channel or not on a visible channel
are listed as being on `channel' "*".
"
*/
bool Handlers::names(Server &server, Client &client,
                     const std::vector<std::string> &params) {
    const std::string &serverName = server.getServerName();
    if (params.empty()) {
        client.sendReply(serverName, Reply::reply(Reply::RPL_ENDOFNAMES, "*"));
        return false;
    }
    std::vector<std::string> chans = Utils::split(params[0], ',');
    for (size_t i = 0; i < chans.size(); i++) {
        try {
            Channel &ch = server.getChannelManager().at(chans[i]);
            ch.sendNames(serverName, client);
        } catch (const Error &) {
            client.sendReply(serverName,
                             Reply::reply(Reply::RPL_ENDOFNAMES, chans[i]));
        }
    }
    return false;
}

bool Handlers::message(Server &server, Client &client,
                       const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::oper(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
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
                    const std::vector<std::string> &params) {
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

bool Handlers::ping(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::pong(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::quit(Server &server, Client &client,
                    const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::rehash(Server &server, Client &client,
                      const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::rename(Server &server, Client &client,
                      const std::vector<std::string> &params) {
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
                     const std::vector<std::string> &params) {
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

/*
VERSION [ <target> ]

The VERSION command is used to query the version of the server program.

NOTE: optional [ <target> ] parameter is ignored as we do not support multiple
server
*/
bool Handlers::version(Server &server, Client &client,
                       const std::vector<std::string> &params) {
    (void)params;
    const std::string &serverName = server.getServerName();
    // <version>.<debuglevel> <server> :<comments>
    client.sendReply(serverName,
                     Reply::reply(Reply::RPL_VERSION,
                                  server.getServerVersion() + " " + serverName,
                                  "ft_irc"));
    return false;
}

bool Handlers::who(Server &server, Client &client,
                   const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}

bool Handlers::whois(Server &server, Client &client,
                     const std::vector<std::string> &params) {
    (void)server;
    (void)client;
    (void)params;
    return false;
}
