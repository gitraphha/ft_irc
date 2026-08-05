#include "IRCServer.hpp"

#include <csignal>
#include <exception>
#include <iostream>

volatile std::sig_atomic_t IRCServer::g_stopServerLoop = 0;
std::vector<int>           IRCServer::g_signals;

void                       IRCServer::g_handleSignal(int signal) {
    if (signal == SIGINT || signal == SIGTERM)
        g_stopServerLoop = signal;
}

void IRCServer::_initSignalHandling() {
    g_stopServerLoop = 0;
    g_signals.push_back(SIGINT);
    g_signals.push_back(SIGTERM);

    struct sigaction sigact = {};
    sigact.sa_handler = g_handleSignal;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    for (size_t i = 0; i < g_signals.size(); ++i)
        if (sigaction(g_signals.at(i), &sigact, NULL) != 0)
            throw std::runtime_error("std::signal failed");
}

void IRCServer::_resetSignalHandling() {

    struct sigaction sigact = {};
    sigact.sa_handler = SIG_DFL;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    for (size_t i = 0; i < g_signals.size(); ++i)
        sigaction(g_signals.at(i), &sigact, NULL);
}

void IRCServer::run() {
    while (!g_stopServerLoop) {
        std::cout << "running..." << std::endl;
        sleep(1);
    }
    std::cout << "caught signal: " << g_stopServerLoop << "." << std::endl;
}
