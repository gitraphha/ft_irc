#pragma once

#include <string>

// draft to be completed by blunez-

class Client {
    std::string nick;
    std::string nickCaseFolded;

  public:
    std::string getNick() const {
        return nick;
    }

    std::string getNickCaseFolded() const {
        return nickCaseFolded;
    }

    void send(const std::string &msg);
};
