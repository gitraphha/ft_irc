#pragma once

#include <string>
#include <vector>

class Client;
class Server;

/*
 * Every handler has the same shape:
 *   return true  -> the connection must be closed once pending output is
 *                   flushed (QUIT, password mismatch on registration, ...)
 *   return false -> keep the connection
 *   throw Error  -> abort the command; the event loop sends the numeric reply
 *                   carried by the Error. Commands that take a comma-separated
 *                   target list catch Error per target instead, so one bad
 *                   target does not abort the rest.
 *
 * PASS / NICK / USER / CAP and tryRegister live in Registration.cpp
 * (RFC 2812 section 3.1); the rest in Handlers.cpp.
 */

namespace Handlers {
bool cap(Server &server, Client &client,
         const std::vector<std::string> &params);
bool invite(Server &server, Client &client,
            const std::vector<std::string> &params);
bool join(Server &server, Client &client,
          const std::vector<std::string> &params);
bool kick(Server &server, Client &client,
          const std::vector<std::string> &params);
bool kill(Server &server, Client &client,
          const std::vector<std::string> &params);
bool mode(Server &server, Client &client,
          const std::vector<std::string> &params);
bool names(Server &server, Client &client,
           const std::vector<std::string> &params);
bool nick(Server &server, Client &client,
          const std::vector<std::string> &params);
bool notice(Server &server, Client &client,
            const std::vector<std::string> &params);
bool oper(Server &server, Client &client,
          const std::vector<std::string> &params);
bool part(Server &server, Client &client,
          const std::vector<std::string> &params);
bool pass(Server &server, Client &client,
          const std::vector<std::string> &params);
bool ping(Server &server, Client &client,
          const std::vector<std::string> &params);
bool pong(Server &server, Client &client,
          const std::vector<std::string> &params);
bool privmsg(Server &server, Client &client,
             const std::vector<std::string> &params);
bool quit(Server &server, Client &client,
          const std::vector<std::string> &params);
bool rehash(Server &server, Client &client,
            const std::vector<std::string> &params);
bool rename(Server &server, Client &client,
            const std::vector<std::string> &params);
bool topic(Server &server, Client &client,
           const std::vector<std::string> &params);
bool user(Server &server, Client &client,
          const std::vector<std::string> &params);
bool version(Server &server, Client &client,
             const std::vector<std::string> &params);
bool who(Server &server, Client &client,
         const std::vector<std::string> &params);
bool whois(Server &server, Client &client,
           const std::vector<std::string> &params);
} // namespace Handlers
