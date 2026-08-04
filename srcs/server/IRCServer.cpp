#include "IRCServer.hpp"

#include <csignal>
#include <exception>
#include <iostream>

volatile std::sig_atomic_t IRCServer::_stopServerLoop = 0;

void                       IRCServer::_handle_sigint(int signal) {
    _stopServerLoop = signal;
}

void IRCServer::run() {
    while (!_stopServerLoop) {
        std::cout << "running..." << std::endl;
        sleep(1);
    }
    std::cout << "caught signal: " << _stopServerLoop << "." << std::endl;
}
