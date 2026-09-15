#ifdef BONUS

#include "Client.hpp"
#include "Errors.hpp"
#include "Handlers.hpp"
#include "Server.hpp"
#include "Utils.hpp"

/*
DCC

IRC-related sub-protocol enabling peers to interconnect using an IRC server for
handshaking in order to exchange files or perform non-relayed chats. Once
established, a typical DCC session runs independently from the IRC server. 

DCC Query Syntax
  DCC <type> <argument> <host> <port>
    <type> contains the type of DCC being initiated, such as CHAT or SEND.
    <argument> refers to a type-specific kind of argument, such as a filename.
    <host> represents to the IP address which should be connected to.
    <port> refers to a valid port on which the connection should be established.

DCC CHAT
  DCC CHAT <unused> <host> <port>

DCC SEND
  DCC SEND <filename> <host> <port>

  After receiving the query, the receiver will have the option of accepting or
  rejecting the file send request. End users MUST be given the option to either
  accept or ignore this request, as opening it will expose their public IP
  address, and automatically receiving files on certain systems may be used to
  exploit vulnerabilities.

  Clients SHOULD NOT allow saving files into system directories, directories
  that could affect the operation of the IRC client or the system as a whole.
  Clients SHOULD instead restrict saved files to a single directory chosen by
  the user or purposefully chosen to be the destination of received DCC files.
  End users MUST also be given the option to rename the file and save it under a
  different filename.

  To accept a given chat request, open a TCP connection to the given port. To
  reject a given request, simply ignore the query and do not respond to it.

Trigger:
sender -> server: PRIVMSG target :\x01DCC SEND <filename> <host> <port> <size>\x01
target -> server: PRIVMSG sender :\x01DCC ACCEPT ...\x01
target -> server: PRIVMSG sender :\x01DCC REJECT <filename>\x01 -> non standard mIRC

server -> target: :sender!user@host PRIVMSG target :\x01DCC SEND <filename> <host> <port>\x01
*/

/*
ergo

func serviceCTCPHandler(service *ircService, client *Client, message string) {
	ctcp := strings.TrimSuffix(message[1:], "\x01")

	ctcpSplit := utils.FieldsN(ctcp, 2)
	ctcpCmd := strings.ToUpper(ctcpSplit[0])
	ctcpOut := ""

	switch ctcpCmd {
	case "VERSION":
		ctcpOut = fmt.Sprintf("%s (%s)", service.Name, Ver)
	case "PING":
		if len(ctcpSplit) > 1 {
			ctcpOut = ctcpSplit[1]
		}
	case "TIME":
		ctcpOut = time.Now().UTC().Format(time.RFC1123)
	}

	if ctcpOut != "" {
		client.Send(nil, service.prefix, "NOTICE", client.Nick(), fmt.Sprintf("\x01%s %s\x01", ctcpCmd, ctcpOut))
	}
}
*/

#endif
