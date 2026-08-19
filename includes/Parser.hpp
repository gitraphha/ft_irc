#pragma once

/*
 * Parser turns one raw IRC line into a command name and a list of parameters,
 * following the message grammar of RFC 2812 section 2.3.1.
 *
 * It is a namespace of free functions rather than a class because parsing
 * keeps no state between lines: the same input always produces the same
 * output. This matches Reply, Handlers and Utils, which are namespaces for
 * the same reason.
 * 
 * Parse splits line into its command name and parameters [ ":" prefix SPACE ]
 * command [ params ] [ SPACE ":" trailing ]. On success 'cmd' holds the command
 * name folded to upper case, and 'params' holds every parameter in order, and
 * parse returns true.
 * 
 * It returns false when the line carries no command at all: empty, only
 * spaces, or a prefix with nothing after it. RFC 2812 section 2.3 says an
 * empty message is silently ignored, so the caller drops those without
 * replying.
 *
 * CommandProcessor::parse has the same signature and is expected to forward
 * to Parser::parse in one line.
 */

#include <string>
#include <vector>

namespace Parser
{
	bool parse(const std::string &line, std::string &cmd,
           std::vector<std::string> &params);
}// namespace Parser
