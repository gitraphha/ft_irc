#pragma once

#include "Logger.hpp" // for Logger

#include <map>         // for map, operator!=, operator==
#include <stddef.h>    // for size_t
#include <string>      // for string, basic_string
#include <sys/types.h> // for ssize_t
#include <utility>     // for pair

#define RECVBUFFLEN 1024
#define MAXMSGLEN 512
#define SOCKETERRORLIMIT 3

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

    bool   updateBuffer_(int const fd, SocketIOData &sIOD);
    size_t bufferLengthAndSanityCheck_(int const fd, bool &msgTooLong);

  public:
    Socket(Logger const &logger) : logger_(logger) {}

    void        addSocket(int const fd, bool srvSocket);
    bool        checkSocket4Recv(struct pollfd &pfd); // NOLINT
    std::string getCRLFLineFromBuffer(int const fd, bool &buffHasMoreCmds,
                                      bool &msgTooLong);
    bool        sendMsg(int const fd, std::string const &msg);
    void        closeSocket(int const fd);

    SocketIOData *getSockIOData(int const fd) {
        std::map<int, SocketIOData>::iterator it = sockIODataStore_.find(fd);
        if (it == sockIODataStore_.end())
            return NULL;
        return &(it->second);
    }

    bool isServerSocket(int const fd) const {
        SocketIOData const *const sIODptr =
            const_cast<Socket *>(this)->getSockIOData(fd);
        if (sIODptr)
            return sIODptr->isSrvSocket;
        return false;
    };
};
