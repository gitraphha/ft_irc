#include "Parser.hpp"

#include <cctype>

namespace Parser
{
// upperCase returns 's' folded to upper case. Command names are compared
// case-insensitively (RFC 2812 section 2.3)
// toUpperChar wraps std::toupper rather than calling it directly because
// std::toupper is undefined for negative values of char, and char is signed on
// some platforms. The cast to unsigned char is safe because std::toupper is
// defined for all values of unsigned char and EOF.
	static char toUpperChar(char c)
	{
	    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	static std::string upperCase(const std::string &s)
	{
	    std::string folded(s);
	    for (std::string::size_type i = 0; i < folded.size(); i++)
	        folded[i] = toUpperChar(folded[i]);
	    return folded;
	}
	bool parse(const std::string &line, std::string &cmd,
	           std::vector<std::string> &params)
	{
	    std::string::size_type pos = 0;
	    // A prefix is only a prefix at position 0. Clients are not supposed to
	    // send one (RFC 2812 section 2.3), and a client may not claim to be
	    // someone else, so we skip it.
	    if (pos < line.size() && line[pos] == ':')
	    {
	        std::string::size_type space = line.find(' ', pos);
	        if (space == std::string::npos)
	            return (false); // a prefix with no command: nothing to run
	        pos = space + 1;
	    }
	    pos = line.find_first_not_of(' ', pos);
	    if (pos == std::string::npos)
	        return (false); // blank line
	    std::string::size_type end = line.find(' ', pos);
	    if (end == std::string::npos)
	        end = line.size();
	    cmd = upperCase(line.substr(pos, end - pos));
	    pos = end;
	    while (pos < line.size()) {
	        pos = line.find_first_not_of(' ', pos);
	        if (pos == std::string::npos)
	            break;
	        // " :" opens the trailing parameter: it runs to the end of the line
	        // and may contain spaces and further colons. It is also the only
	        // parameter allowed to be empty
	        if (line[pos] == ':')
	        {
	            params.push_back(line.substr(pos + 1));
	            break;
	        }
	        end = line.find(' ', pos);
	        if (end == std::string::npos)
	            end = line.size();
	        params.push_back(line.substr(pos, end - pos));
	        pos = end;
	    }
	    return (!cmd.empty());
}
}// namespace Parser
