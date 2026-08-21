#include <poll.h>
#include <string>
#include <vector>

#define BUFFLEN 516
#define MAXMSGLEN 512
#define MAX_CONNECTIONS 512

class IRCServer {
  private:
    int                        port_;
    std::string                password_;
    std::vector<struct pollfd> pollFds_;
    std::vector<std::string>   userInputBuffer_;

    // IRCServer(IRCServer const &other);
    // IRCServer &operator=(IRCServer const &other);

    int                        createServerSocket_(int const domain);
    void                       acceptClient_();
    void                       disconnectClient_(size_t const idx);

  public:
    IRCServer(int const port, std::string const &password);
    // ~IRCServer();

    void run();
};
