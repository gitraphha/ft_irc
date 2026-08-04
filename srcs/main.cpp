#include "IRC.hpp"

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
    (void)argv;
    if (argc != 3)
        return (1);

    try {
        IRCServer srv;
        srv.run();
    } catch (std::exception const &e) {
    }
}
