#pragma once

#include <string>
#include <vector>

class Client;
class Server;

namespace Handlers {
bool cap(Server &server, Client &client, std::vector<std::string> &params);
bool invite(Server &server, Client &client, std::vector<std::string> &params);
bool join(Server &server, Client &client, std::vector<std::string> &params);
bool kick(Server &server, Client &client, std::vector<std::string> &params);
bool kill(Server &server, Client &client, std::vector<std::string> &params);
bool mode(Server &server, Client &client, std::vector<std::string> &params);
bool names(Server &server, Client &client, std::vector<std::string> &params);
bool nick(Server &server, Client &client, std::vector<std::string> &params);
bool message(Server &server, Client &client, std::vector<std::string> &params);
bool oper(Server &server, Client &client, std::vector<std::string> &params);
bool part(Server &server, Client &client, std::vector<std::string> &params);
bool pass(Server &server, Client &client, std::vector<std::string> &params);
bool ping(Server &server, Client &client, std::vector<std::string> &params);
bool pong(Server &server, Client &client, std::vector<std::string> &params);
bool quit(Server &server, Client &client, std::vector<std::string> &params);
bool rehash(Server &server, Client &client, std::vector<std::string> &params);
bool rename(Server &server, Client &client, std::vector<std::string> &params);
bool topic(Server &server, Client &client, std::vector<std::string> &params);
bool user(Server &server, Client &client, std::vector<std::string> &params);
bool version(Server &server, Client &client, std::vector<std::string> &params);
bool who(Server &server, Client &client, std::vector<std::string> &params);
bool whois(Server &server, Client &client, std::vector<std::string> &params);
} // namespace Handlers
