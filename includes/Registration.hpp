#pragma once

class Client;
class Server;

// Connection registration (RFC 2812 section 3.1). PASS / NICK / USER / CAP
// are ordinary handlers; tryRegister is not a command. CommandProcessor::run
// calls it after every command of an unregistered client. Once NICK and
// USER are both known it either completes the registration (001-005 + 422)
// or, if PASS did not match, sends 464 and asks for the connection to be
// closed (returns true).
namespace Registration {
bool tryRegister(Server &server, Client &client);
} // namespace Registration
