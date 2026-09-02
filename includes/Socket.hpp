#pragma once

#include "Logger.hpp"

#include <map>
#include <string>
#include <sys/socket.h>

#define RECVBUFFLEN 1024
#define MAXMSGLEN 512
#define SOCKETERRORLIMIT 3

#define NETCATDBG 0 // For netcat debug

class Socket {
  private:
    struct SocketIOData {
        std::string readBuffer;
        std::string writeBuffer;
        ssize_t     bytesRead;
        size_t      recvErrorCount;
        size_t      sendErrorCount;
        bool        discardMode;
        bool        isSrvSocket;
        bool        fdClosed;

        SocketIOData(bool srvSocket)
            : bytesRead(0), recvErrorCount(0), sendErrorCount(0),
              discardMode(false), isSrvSocket(srvSocket), fdClosed(false) {}
    };

    std::map<int, SocketIOData> sockIODataStore_;
    Logger const               &logger_;

    bool updateBuffer_(int const fd);
    size_t
    bufferLengthAndSanityCheck_(int const fd /* , bool const isRegistered */);

  public:
    Socket(Logger const &logger) : logger_(logger) {}

    void        addSocket(int const fd, bool srvSocket);
    bool        checkSocket(struct pollfd &pfd); // NOLINT
    std::string getCmdLine(int const fd, bool &buffHasMoreCmds);
    bool        sendMsg(int const fd, std::string const &msg);
    void        closeSocket(int const fd);

    bool socketClosed(int const fd) const {
        return (sockIODataStore_.find(fd) == sockIODataStore_.end());
    }

    bool isServerSocket(int const fd) const {
        if (!socketClosed(fd))
            return sockIODataStore_.find(fd)->second.isSrvSocket;
        return false;
    };
};
