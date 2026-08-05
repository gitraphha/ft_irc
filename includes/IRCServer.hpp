#include <csignal>
#include <exception>
#include <iostream>
#include <vector>

class IRCServer {
  private:
    volatile static std::sig_atomic_t g_stopServerLoop;
    static void                       g_handleStopSignals(int signal);
    std::vector<int>                  _stopSignals;
    void                              _initSignalHandling();
    void                              _resetSignalHandling();
    bool                              _setStopSignals(sighandler_t handler);

    IRCServer(IRCServer const &other);
    IRCServer &operator=(IRCServer const &other);

  public:
    IRCServer() {
        _initSignalHandling();
    }

    ~IRCServer() {
        _resetSignalHandling();
    }

    void run();
};
