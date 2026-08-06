#include <csignal>
#include <vector>

class IRCServer {
  private:
    volatile static std::sig_atomic_t g_stopServerLoop;
    static void                       g_handleStopSignals(int signal);
    std::vector<int>                  _stopSignals;
    void                              _initSignalHandling();
    void                              _resetSignalHandling();
    bool                              _setStopSignals(sighandler_t handler);

    std::vector<struct pollfd>        _pollfds;

    IRCServer(IRCServer const &other);
    IRCServer &operator=(IRCServer const &other);

    int        _createDummyServerSocket();
    void       _acceptClient();
    void       _disconnectClient(int const idx);

  public:
    IRCServer();

    ~IRCServer();

    void run();
};
