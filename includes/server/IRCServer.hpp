#include <poll.h>
#include <string>
#include <vector>

#define BUFFLEN 516
#define MAXMSGLEN 512

class IRCServer {
  private:
    std::vector<struct pollfd> _pollfds;
    std::vector<std::string>   _userInputBuffer;

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
