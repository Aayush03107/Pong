#include "Network.hpp"
#include <optional>
#include <stdexcept>
#include <iostream>


Network::Network(sf::IpAddress serverIp, unsigned short serverPort)
    : serverIp(serverIp), serverPort(serverPort) {
    socket.setBlocking(false);
    if (socket.bind(sf::Socket::AnyPort) != sf::Socket::Status::Done) {
        std::cout << "Failed to bind\n";
        throw std::runtime_error("Failed to bind");
    }
}

void Network::sendMessage(const std::string& message) {
    if (socket.send(message.data(), message.size(),serverIp, serverPort)
        != sf::Socket::Status::Done) {
        std::cout << "Failed to send\n";
    }
}

std::optional<std::string> Network::receiveMessage() {
    std::array<char, 2048> buffer{};
    std::size_t received = 0;
    std::optional<sf::IpAddress> senderIp;
    unsigned short senderPort = 0;
    auto status = socket.receive(buffer.data(), buffer.size(), received,
                       senderIp, senderPort);
    if (status == sf::Socket::Status::NotReady) {
        return std::nullopt;
    }
    if(status == sf::Socket::Status::Done) {
        return std::string(buffer.data(), received);
    }
    throw std::runtime_error("UDP receive failed");
}
