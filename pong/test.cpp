
#include <SFML/Network.hpp>
#include <iostream>
#include <string>
#include <array>
#include "Network.hpp"

int main() {
    auto serverIp = sf::IpAddress::resolve("127.0.0.1");
    
    if (!serverIp) {
        std::cerr << "Could not resolve server IP\n";
        return 1;
    }
    
    Network network(*serverIp, 5050);
    std::vector<std::string> messages = {
        R"({"Typeo":"INPUT","Action":"MOVE_UP"})",
        R"({"Typeo":"INPUT","Action":"MOVE_DOWN"})",
        R"({"Typeo":"INPUT","Action":"MOVE_UP"})"
    };
    for (auto message : messages) {
        network.sendMessage(message);
        while(true) {
            std::optional<std::string> reply = network.receiveMessage();
            if(reply) {
                std::cout << *reply << "\n";
                break;
            }
        }
    };
}
