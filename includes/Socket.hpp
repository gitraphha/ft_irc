#pragma once

#include "Logger.hpp" // for Logger

#include <map>         // for map, operator!=, operator==
#include <stddef.h>    // for size_t
#include <string>      // for string, basic_string
#include <sys/types.h> // for ssize_t
#include <utility>     // for pair

#define RECVBUFFLEN 4196
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
        bool        closeFd;

        SocketIOData(bool srvSocket)
            : bytesRead(0), recvErrorCount(0), sendErrorCount(0),
              discardMode(false), isSrvSocket(srvSocket), closeFd(false) {}
    };

    std::map<int, SocketIOData> sockIODataStore_;
    Logger const               &logger_;

    size_t bufferLengthAndSanityCheck_(SocketIOData &sIOD, bool &msgTooLong);
    bool   checkSocket4Recv(struct pollfd &pfd, SocketIOData &sIOD); // NOLINT

  public:
    Socket(Logger const &logger) : logger_(logger) {}

    void        addSocket(int const fd, bool srvSocket);
    std::string getCRLFLineFromBuffer(int const fd, bool &buffHasMoreCmds,
                                      bool &msgTooLong);
    bool        sendMsg(int const fd, std::string const &msg);
    bool       &socketCloseMarker(int const fd);
    void        closeSocket(int const fd);

    bool          updateBuffer_(struct pollfd &pfd);
    SocketIOData *getSockIOData(int const fd);
    bool          isServerSocket(int const fd);
};
