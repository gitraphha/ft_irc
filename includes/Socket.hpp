#pragma once

#include "Logger.hpp"

#include <map>
#include <string>
#include <sys/socket.h>

#define RECVBUFFLEN 1024
#define MAXMSGLEN 512
#define SOCKETERRORLIMIT 8

class Socket {
  public:
    struct SocketIOData {
        std::string readBuffer;
        std::string writeBuffer;
        ssize_t     bytesRead;
        size_t      recvErrorCount;
        size_t      sendErrorCount;
        bool        discardMode;
        bool        isSrvSocket;

        SocketIOData()
            : bytesRead(0), recvErrorCount(0), sendErrorCount(0),
              discardMode(false) {}
    };

  private:
    std::map<int, SocketIOData> sockIODataStore_;
    Logger const               &logger_;

  public:
    Socket(Logger const &logger) : logger_(logger) {}

    SocketIOData &getSocketIOData_(int const fd);
    size_t        clientDataSanityCheck_(Socket::SocketIOData &sIOD, int fd,
                                         std::string const &serverName,
                                         std::string const &nickName,
                                         bool const         isRegistered);
    void          addSocket(int const fd);
    bool          checkSocket_(struct pollfd &pfd);
    bool          sendMsg(int const fd, std::string const &msg);
    void          closeSocket_(int const fd);
};
