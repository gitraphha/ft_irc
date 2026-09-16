#ifdef BONUS

#include "TransferManager.hpp"

#include "Client.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static const time_t STALE_TIMEOUT = 60;
static const int    LISTENER_BACKLOG = 8;
static const int    CHUNK_SIZE = 1024;
static const char  *FILES_DIR = "files/";

TransferManager::TransferManager() : active_(false) {}

TransferManager::~TransferManager() {
    closeFds_();
}

void TransferManager::setNonBlocking_(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool TransferManager::openListener_() {
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0); // let OS pick a port

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }
    if (::listen(fd, LISTENER_BACKLOG) < 0) {
        ::close(fd);
        return false;
    }
    setNonBlocking_(fd);

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        ::close(fd);
        return false;
    }

    current_.listenerFd = fd;
    current_.port = ntohs(addr.sin_port);
    return true;
}

void TransferManager::closeFds_() {
    if (current_.listenerFd != -1) {
        ::close(current_.listenerFd);
        current_.listenerFd = -1;
    }
    if (current_.dataFd != -1) {
        ::close(current_.dataFd);
        current_.dataFd = -1;
    }
}

bool TransferManager::fileSize_(const std::string &path, off_t &size) const {
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open())
        return false;
    f.seekg(0, std::ios::end);
    std::streamoff p = f.tellg();
    if (p < 0)
        return false;
    size = static_cast<off_t>(p);
    return true;
}

// true if offered, false if busy / file missing / socket failed
bool TransferManager::openOffer(Client *requester, Client *target,
                                const std::string &filename) {
    if (active_) {
        if (!stalePending_())
            return false;
        closeOffer();
    }
    std::string filepath = std::string(FILES_DIR) + filename;
    off_t       size = 0;
    if (!fileSize_(filepath, size))
        return false;
    current_ = Transfer(requester, target, filename, filepath, size);
    if (!openListener_()) {
        closeFds_();
        return false;
    }
    active_ = true;
    return true;
}

Transfer *TransferManager::current() {
    return active_ ? &current_ : 0;
}

bool TransferManager::setOfferStatus(Client *target, bool accept) {
    if (!active_ || current_.status != Pending || current_.target != target)
        return false;
    current_.status = accept ? Accepted : Rejected;
    if (!accept)
        closeOffer();
    return true;
}

void TransferManager::closeOffer() {
    closeFds_();
    active_ = false;
}

int TransferManager::getListenerFd() const {
    return active_ ? current_.listenerFd : -1;
}

// accept() receiver's connection
int TransferManager::getDataFd() {
    if (!active_ || current_.status != Accepted || current_.listenerFd == -1)
        return -1;
    if (current_.dataFd != -1)
        return current_.dataFd;

    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);
    int fd = ::accept(current_.listenerFd, (struct sockaddr *)&addr, &len);
    if (fd < 0)
        return -1;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    setNonBlocking_(fd);
    current_.dataFd = fd;
    return fd;
}

// send 1KB bytes
ssize_t TransferManager::sendChunk() {
    if (!active_ || current_.status != Accepted || current_.dataFd == -1)
        return -1;

    std::ifstream f(current_.filepath.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open())
        return -1;
    f.seekg(current_.offset);
    char buf[CHUNK_SIZE];
    f.read(buf, CHUNK_SIZE);
    std::streamsize n = f.gcount();
    if (n <= 0)
        return 0; // EOF
    ssize_t sent = ::send(current_.dataFd, buf, static_cast<size_t>(n), 0);
    if (sent > 0)
        current_.offset += sent;
    return sent;
}

bool TransferManager::isActive() const {
    return active_;
}

bool TransferManager::stalePending_() const {
    return active_ && std::time(0) - current_.createdAt > STALE_TIMEOUT;
}

bool TransferManager::ownsFd(int fd) const {
    return active_ && (fd == current_.listenerFd || fd == current_.dataFd);
}

#endif
