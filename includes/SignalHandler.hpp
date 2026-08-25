#ifndef SIGNALHANDLER_HPP
#define SIGNALHANDLER_HPP

#include <csignal>
#include <vector>

typedef void (*sighandler_t) (int);

class SignalHandler {
  private:
    volatile static std::sig_atomic_t stopServerLoop_;
    static void                       handleStopSignals_(int signal);
    std::vector<int>                  stopSignals_;
    bool                              setStopSignals_(sighandler_t handler);

    SignalHandler(SignalHandler const &other);
    SignalHandler &operator=(SignalHandler const &other);

  public:
    SignalHandler();
    ~SignalHandler();
    static bool stopLoopSignalReceived();
};

#endif // SIGNALHANDLER_HPP