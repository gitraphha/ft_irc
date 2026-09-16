#ifdef BONUS

#pragma once

#include <ctime>
#include <string>
#include <sys/types.h>

class Client;

enum Status { Pending, Accepted, Rejected };

struct Transfer {
    int         clientFd;
    int         targetFd;
    std::string filename;
    std::string filepath; // "files/" + filename
    off_t       filesize;
    int         listenerFd;
    int         dataFd;
    int         port;
    off_t       offset;
    Status      status;
    time_t      lastActivity;

    Transfer()
        : clientFd(-1), targetFd(-1), filesize(0), listenerFd(-1), dataFd(-1),
          port(0), offset(0), status(Pending), lastActivity(std::time(0)) {};

    Transfer(int client, int target, std::string name, std::string path,
             off_t size)
        : clientFd(client), targetFd(target), filename(name), filepath(path),
          filesize(size), listenerFd(-1), dataFd(-1), port(0), offset(0),
          status(Pending), lastActivity(std::time(0)) {}
};

class TransferManager {
    bool     active_; // one transfer at a time. true = busy.
    Transfer current_;

    bool        openListener_();
    void        closeFds_();
    bool        fileSize_(const std::string &path, off_t &size) const;
    static void setNonBlocking_(int fd);

    TransferManager(const TransferManager &);
    TransferManager &operator=(const TransferManager &);

  public:
    TransferManager();
    ~TransferManager();

    bool openOffer(int clientFd, int targetFd, const std::string &filename);
    bool setOfferStatus(int targetFd, bool accept);
    void closeOffer();
    bool expired() const;

    int getDataFd();

    ssize_t sendChunk();

    int       getListenerFd() const;
    bool      isActive() const;
    Transfer *current();
    bool      ownsFd(int fd) const;
    bool      transferDone() const;
};

#endif
