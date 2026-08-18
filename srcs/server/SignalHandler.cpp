#include "server/SignalHandler.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <exception>
#include <iostream>
#include <sys/poll.h>

/*
	Signal handling
	will be refactored in a separate class
*/
volatile std::sig_atomic_t SignalHandler::g_stopServerLoop = 0;

/*
	Signal callback function
	Will set a static class member to the triggering signal,
	i.e. != 0, so the state can be used as a bool value directly.
	Will be refactored in a separate class, and with a more explicit
	stop state request function
*/
void                       SignalHandler::g_handleStopSignals(int signal) {
	std::cout << "Caught stop signal: " << g_stopServerLoop << "." << std::endl;
    g_stopServerLoop = signal;
}

/*
	Helper function to actually set the signal behaviour
	on system level using sigaction, as this is the most
	portable POSIX compliant way to do it.
*/
bool SignalHandler::_setStopSignals(sighandler_t handler) {
    bool             success = true;

    struct sigaction sigact = {};
    sigact.sa_handler = handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    for (size_t i = 0; success && i < _stopSignals.size(); ++i)
        if (sigaction(_stopSignals.at(i), &sigact, NULL) != 0)
            success = false;

    return success;
}

/*
	SignalHandler Constructor
		* setup signal handling
*/
SignalHandler::SignalHandler() {
    g_stopServerLoop = 0;
    _stopSignals.push_back(SIGINT);
    _stopSignals.push_back(SIGQUIT);
    _stopSignals.push_back(SIGTERM);

    if (!_setStopSignals(g_handleStopSignals))
        throw std::runtime_error("std::signal failed");
}

/*
	SignalHandler Destructor
		* reset signal handling to default
*/
SignalHandler::~SignalHandler() {
    _setStopSignals(SIG_DFL);
}

bool SignalHandler::stopLoopSignalReceived() {
    return (g_stopServerLoop > 0);
}
