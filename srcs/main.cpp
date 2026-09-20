#include "Server.hpp"        // for Server
#include "SignalHandler.hpp" // for SignalHandler
#include "colours.hpp"       // for CRed, CReset, CCyan, CGreen, CYellow

#include <exception> // for exception
#include <iostream>  // for basic_ostream, operator<<, basic_istream
#include <sstream>   // for basic_istringstream
#include <string>    // for char_traits, allocator, basic_string

static bool checkExtractCmdLnArgs(int const argc, char *const argv[], int &port,
                                  std::string &password);

/*
	NAME:
		ircserv
	
	ARGUMENTS:
		port: The listening port
		password: The connection password

	DESCRIPTION:
		ircserv is an IRC(Internet Relay Chat) server in C++ 98

		FEATURES:
		• Simultaneous multi-client handling over TCP/IP
		• Non-blocking I/O with a single poll() multiplexing all sockets
		• Authenticate with server password, set a nickname, a username, join a
			channel, send and receive private messages
		• Channel operators and regular users
		• Operator commands: KICK, INVITE, TOPIC, MODE
		• Channel modes: i, t, k, o, l
		• Compatible with a chosen reference IRC client
		
		Bonus features / TODO:
		• Handle file transfer
		• A bot

	EXTERNAL FUNC(S):
		• Everything in C++ 98.
		• socket, close, setsockopt, getsockname,
		getprotobyname, gethostbyname, getaddrinfo,
		freeaddrinfo, bind, connect, listen, accept,
		htons, htonl, ntohs, ntohl, inet_addr, inet_ntoa,
		inet_ntop, send, recv, signal, sigaction,
		sigemptyset, sigfillset, sigaddset, sigdelset,
		sigismember, lseek, fstat, fcntl, poll (or
		equivalent)
*/

int main(int argc, char **argv) {
    int         port = 0;
    std::string password;

    try {
        if (!checkExtractCmdLnArgs(argc, argv, port, password))
            return 1;

        SignalHandler sigHandler;
        Server        srv(port, password);
        srv.run();

    } catch (std::exception const &e) {
        std::cerr << CCyan << " !!! Exception: " << e.what() << CReset
                  << std::endl;
        return 1;
    }
    std::cout << CGreen << "Clean exit." << CReset << std::endl;
    return 0;
}

static void toCerr(std::string const &msg, std::string const &colour) {
    std::cerr << colour << msg << CReset << std::endl;
}

static bool checkExtractCmdLnArgs(int const argc, char *const argv[], int &port,
                                  std::string &password) {
    if (argc != 3) {
        toCerr("Usage: ./ircserv <int:port> <string:password>", CYellow);
        return false;
    }

    std::istringstream issPort(argv[1]); // NOLINT
    std::string        remainder;
    if (!((issPort >> port) && (issPort >> remainder).eof())) {
        toCerr("Error while extracting port number from commandline parameter.",
               CRed);
        return false;
    }
    if (port < 1 || port > 65535) {
        toCerr("Error: port must be in the range 1-65535.", CRed);
        return false;
    }

    std::istringstream issPass(argv[2]); // NOLINT
    if (!(issPass >> password).eof()) {
        toCerr("Error while extracting password from commandline parameter '" +
                   issPass.str() +
                   "'\n      Make sure it consist of alphanumeric characters "
                   "without spaces",
               CRed);
        return false;
    }
    if (password.empty()) {
        toCerr("Error: password cannot be empty.", CRed);
        return false;
    }

    return true;
}
