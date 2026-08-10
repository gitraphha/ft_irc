#pragma once

/*
 * A command handler throws an Error to abort processing, the main event
 * loop catches it once and either sends the attached numeric reply to the
 * offending client (hasReply() == true) or just logs it (internal error,
 * nothing sent).
 *
 *   // main loop:
 *   try { ... } catch (const Error &e) {
 *       if (e.hasReply())
 *           send(c.fd(), Reply::build(serverName, c.nick(), e.reply()));
 *       else
 *           log(e.what());
 *   }
 */

#include "Reply.hpp"

#include <exception>
#include <string>

class Error : public std::exception {
    bool           _hasReply;
    Reply::Message _reply;
    std::string    _what;

  public:
    // A protocol error: carry a numeric reply to send back to the client.
    explicit Error(const Reply::Message &reply)
        : _hasReply(true), _reply(reply), _what(reply.body) {}

    // An internal error: no numeric reply is sent, what() is used for logging.
    explicit Error(const std::string &message)
        : _hasReply(false), _reply(), _what(message) {}

    virtual ~Error() throw() {}

    virtual const char *what() const throw() {
        return _what.c_str();
    }

    bool hasReply() const {
        return _hasReply;
    }

    // Only valid when hasReply() is true.
    const Reply::Message &reply() const {
        return _reply;
    }
};
