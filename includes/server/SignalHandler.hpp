#include <csignal>
#include <vector>

class SignalHandler {
  private:
    volatile static std::sig_atomic_t g_stopServerLoop;
    static void                       g_handleStopSignals(int signal);
    std::vector<int>                  _stopSignals;
    bool                              _setStopSignals(sighandler_t handler);

    std::vector<struct pollfd>        _pollfds;

    SignalHandler(SignalHandler const &other);
    SignalHandler &operator=(SignalHandler const &other);

  public:
    SignalHandler();
    ~SignalHandler();
  static bool stopLoopSignalReceived();	
};
