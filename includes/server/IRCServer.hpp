#include <csignal>
#include <vector>
#include <poll.h>

class IRCServer {
  private:
    std::vector<struct pollfd>        _pollfds;

    IRCServer(IRCServer const &other);
    IRCServer &operator=(IRCServer const &other);

    int        _createServerSocket();
    void       _acceptClient();
    void       _disconnectClient(size_t const idx);

  public:
    IRCServer();
    // ~IRCServer();

    void run();
};
