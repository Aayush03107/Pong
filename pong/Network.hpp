#pragma once

#include <SFML/Network.hpp>

class Network {
private:
    sf::UdpSocket socket;
    sf::IpAddress serverIp;
    unsigned short serverPort;

public:
    Network(sf::IpAddress serverIp, unsigned short serverPort);

    void sendMessage(const std::string& message);
    std::optional<std::string> receiveMessage();
};