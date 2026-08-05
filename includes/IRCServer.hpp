#include <csignal>
#include <exception>
#include <iostream>
#include <vector>

class IRCServer {
  public:
    volatile static std::sig_atomic_t g_stopServerLoop;
    static std::vector<int>           g_signals;
    static void                       g_handleSignal(int signal);
    // static void                       _initSignalHandling();
    // static void                       _resetSignalHandling();
  private:
    void _initSignalHandling();
    void _resetSignalHandling();

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
