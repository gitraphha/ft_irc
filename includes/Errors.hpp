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
    bool           hasReply_;
    Reply::Message reply_;
    std::string    what_;

  public:
    // A protocol error: carry a numeric reply to send back to the client.
    explicit Error(const Reply::Message &reply)
        : hasReply_(true), reply_(reply), what_(reply.params) {}

    // An internal error: no numeric reply is sent, what() is used for logging.
    explicit Error(const std::string &message)
        : hasReply_(false), what_(message) {}

    virtual ~Error() throw() {}

    virtual const char *what() const throw() {
        return what_.c_str();
    }

    bool hasReply() const {
        return hasReply_;
    }

    // Only valid when hasReply() is true.
    const Reply::Message &reply() const {
        return reply_;
    }

    static Error reply(Reply::Code code, const std::string &params) {
        return Error(Reply::reply(code, params));
    }

    static Error reply(Reply::Code code, const std::string &params,
                       const std::string &trailing) {
        return Error(Reply::reply(code, params, trailing));
    }
};
