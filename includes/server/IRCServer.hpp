#include <netinet/in.h>
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

    size_t                     numServerSockets_;
    std::vector<struct pollfd> pollFds_;
    std::vector<std::string>   clientInputBuffers_;

    // IRCServer(IRCServer const &other);
    // IRCServer &operator=(IRCServer const &other);

    int                        createServerSocket_(int const domain);
    void                       acceptClient_(int const serverFd);
    void                       disconnectClient_(size_t const idx);

  public:
    IRCServer(int const port, std::string const &password);
    // ~IRCServer();

    void run();
};

typedef union uSocketAddrX {
    struct sockaddr_in  sai4;
    struct sockaddr_in6 sai6;
} socketAddrX;
