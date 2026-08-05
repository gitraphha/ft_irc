#include "IRCServer.hpp"

#include <csignal>
#include <exception>
#include <iostream>

volatile std::sig_atomic_t IRCServer::g_stopServerLoop = 0;

void                       IRCServer::g_handleStopSignals(int signal) {
        g_stopServerLoop = signal;
}

void IRCServer::_initSignalHandling() {
    g_stopServerLoop = 0;
    _stopSignals.push_back(SIGINT);
	_stopSignals.push_back(SIGQUIT);
    _stopSignals.push_back(SIGTERM);

    if (!_setStopSignals(g_handleStopSignals))
        throw std::runtime_error("std::signal failed");
}

void IRCServer::_resetSignalHandling() {
    _setStopSignals(SIG_DFL);
}

bool IRCServer::_setStopSignals(sighandler_t handler) {
    bool             success = true;

    struct sigaction sigact = {};
    sigact.sa_handler = handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    for (size_t i = 0; i < _stopSignals.size(); ++i)
        if (sigaction(_stopSignals.at(i), &sigact, NULL) != 0)
            success = false;

    return success;
}

void IRCServer::run() {
    while (!g_stopServerLoop) {
        std::cout << "running..." << std::endl;
        sleep(1);
    }
    std::cout << "caught signal: " << g_stopServerLoop << "." << std::endl;
}
