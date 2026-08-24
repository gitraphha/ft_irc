#include "IRC.hpp"

#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>

static void checkExtractCmdLnArgs(int const argc, char *const argv[], int &port,
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

int         main(int argc, char **argv) {
    int         port = 0;
    std::string password;

    try {
        checkExtractCmdLnArgs(argc, argv, port, password);

        SignalHandler sigHandler;
        Server        srv(port, password);
        srv.run();
    } catch (std::exception const &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    std::cout << "Clean exit." << std::endl;
    return 0;
}

static void checkExtractCmdLnArgs(int const argc, char *const argv[], int &port,
                                  std::string &password) {
    std::istringstream iss;
    std::string        remainder;
    if (argc != 3)
        throw std::out_of_range(
            "Usage: ./ircserv <int:port> <string:password>");

    iss.str(argv[1]); // NOLINT
    if (!((iss >> port) && (iss >> remainder).eof()))
        throw std::runtime_error(
            "Error while extracting port number from commandline parameter.");

    iss.str(argv[2]); // NOLINT
    if (!(iss >> password).eof())
        throw std::out_of_range(
            "Error while extracting password from commandline parameter.");
}
