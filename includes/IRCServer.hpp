#include <csignal>
#include <exception>
#include <iostream>

class IRCServer {
  private:
    volatile static std::sig_atomic_t _stopServerLoop;
    static void                       _handle_sigint(int signal);

  public:
    IRCServer() {
        _stopServerLoop = 0;
        if (std::signal(SIGINT, _handle_sigint) == SIG_ERR) {
            throw std::runtime_error("std::signal failed");
        }
    }

    ~IRCServer() {
        if (std::signal(SIGINT, SIG_DFL) == SIG_ERR) {
            // throw std::runtime_error("std::signal failed");
        }
    }

    void run();
};
