#include "plant/command_server.hpp"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
   using ssize_t = int;
#else
#  include <sys/socket.h>
#endif

#include <utility>

namespace plant {

CommandServer::CommandServer(const std::string& host, std::uint16_t port, Handler handler)
    : handler_(std::move(handler)) {
    net::Listener listener = net::tcp_listen(host, port);
    listen_socket_ = std::move(listener.socket);
    port_ = listener.port;
}

CommandServer::~CommandServer() { stop(); }

void CommandServer::start() {
    if (running_.exchange(true)) {
        return;
    }
    accept_thread_ = std::thread(&CommandServer::accept_loop, this);
}

void CommandServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        threads.swap(client_threads_);
    }
    for (auto& thread : threads) {
        thread.join();
    }
}

void CommandServer::accept_loop() {
    while (running_) {
        if (!net::wait_readable(listen_socket_.fd(), 100)) {
            continue;
        }
        net::Socket client = net::tcp_accept(listen_socket_.fd());
        if (!client.valid()) {
            continue;
        }
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_threads_.emplace_back(&CommandServer::serve_client, this, std::move(client));
    }
}

void CommandServer::serve_client(net::Socket client) {
    std::string buffer;
    char chunk[256];

    while (running_) {
        if (!net::wait_readable(client.fd(), 100)) {
            continue;
        }
        const ssize_t n = ::recv(client.fd(), chunk,
#ifdef _WIN32
                                 static_cast<int>(sizeof(chunk)),
#else
                                 sizeof(chunk),
#endif
                                 0);
        if (n <= 0) {
            return;  // peer closed or error
        }
        buffer.append(chunk, static_cast<std::size_t>(n));

        std::size_t newline = 0;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const std::string reply = handler_(line);
            if (!net::send_all(client.fd(), reply + "\n")) {
                return;
            }
            if (reply == "BYE") {
                return;
            }
        }
        if (buffer.size() > kMaxLineBytes) {
            net::send_all(client.fd(), "ERR line too long\n");
            return;
        }
    }
}

}  // namespace plant
