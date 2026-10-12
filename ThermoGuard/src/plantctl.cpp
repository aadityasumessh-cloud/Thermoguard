// plantctl: tiny client for plant_controller.
//
//   plantctl send <host> <port> <command...>   send one command over TCP, print the reply
//   plantctl listen <udp-port> [count]         print UDP telemetry packets

#include <cstdlib>
#include <iostream>
#include <string>

#include "plant/net.hpp"

int main(int argc, char** argv) {
    try {
        if (argc >= 5 && std::string(argv[1]) == "send") {
            std::string command;
            for (int i = 4; i < argc; ++i) {
                command += (i > 4 ? " " : "") + std::string(argv[i]);
            }
            plant::net::Socket socket =
                plant::net::tcp_connect(argv[2], static_cast<std::uint16_t>(std::atoi(argv[3])));
            plant::net::send_all(socket.fd(), command + "\n");
            const auto reply = plant::net::recv_line(socket.fd(), 2000);
            if (!reply) {
                std::cerr << "no reply\n";
                return 1;
            }
            std::cout << *reply << "\n";
            return 0;
        }

        if (argc >= 3 && std::string(argv[1]) == "listen") {
            const int count = argc >= 4 ? std::atoi(argv[3]) : 10;
            plant::net::UdpReceiver receiver("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[2])));
            for (int received = 0; received < count;) {
                if (auto message = receiver.receive(3000)) {
                    std::cout << *message << "\n";
                    ++received;
                } else {
                    std::cerr << "timeout waiting for telemetry\n";
                    return 1;
                }
            }
            return 0;
        }

        std::cerr << "usage:\n  plantctl send <host> <port> <command...>\n"
                     "  plantctl listen <udp-port> [count]\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
