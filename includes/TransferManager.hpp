#ifdef BONUS

#pragma once

#include <ctime>
#include <string>
#include <sys/types.h>

class Client;

enum Status { Pending, Accepted, Rejected };

struct Transfer {
    Client     *client;
    Client     *target;
    std::string filename;
    std::string filepath; // "files/" + filename
    off_t       filesize;
    int         listenerFd;
    int         dataFd;
    int         port;
    off_t       offset;
    Status      status;
    time_t      createdAt;

    Transfer()
        : client(0), target(0), filesize(0), listenerFd(-1), dataFd(-1),
          port(0), offset(0), status(Pending), createdAt(std::time(0)) {};

    Transfer(Client *req, Client *targ, std::string name, std::string path,
             off_t size)
        : client(req), target(targ), filename(name), filepath(path),
          filesize(size), listenerFd(-1), dataFd(-1), port(0), offset(0),
          status(Pending), createdAt(std::time(0)) {}
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

    bool openOffer(Client *requester, Client *target,
                   const std::string &filename);
    bool setOfferStatus(Client *target, bool accept);
    void closeOffer();
    bool expired() const;

    int getDataFd();

    ssize_t sendChunk();

    int       getListenerFd() const;
    bool      isActive() const;
    Transfer *current();
    bool      ownsFd(int fd) const;
};

#endif
