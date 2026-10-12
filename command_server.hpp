#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "plant/net.hpp"

namespace plant {

// A small multi-client TCP server with a line-based text protocol.
// Each received line is passed to the handler; the returned string is sent back
// followed by '\n'. A reply of exactly "BYE" closes that connection.
//
// The handler is called from several threads (one per client) and must be
// thread-safe. The accept loop and every client loop poll with a short timeout,
// so stop() returns quickly and joins every thread (no detached threads).
class CommandServer {
public:
    using Handler = std::function<std::string(const std::string& line)>;

    // Binds immediately (port 0 = pick a free port; see port()).
    CommandServer(const std::string& host, std::uint16_t port, Handler handler);
    ~CommandServer();

    CommandServer(const CommandServer&) = delete;
    CommandServer& operator=(const CommandServer&) = delete;

    void start();
    void stop();
    std::uint16_t port() const noexcept { return port_; }

private:
    void accept_loop();
    void serve_client(net::Socket client);

    static constexpr std::size_t kMaxLineBytes = 1024;

    Handler handler_;
    net::Socket listen_socket_;
    std::uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<std::thread> client_threads_;
};

}  // namespace plant
