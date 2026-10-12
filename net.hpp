#pragma once

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using socket_t = SOCKET;
   static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <netinet/in.h>
   using socket_t = int;
   static constexpr socket_t kInvalidSocket = -1;
#endif

#include <cstdint>
#include <optional>
#include <string>

// Small socket helpers (IPv4 only). Sockets are owned by an RAII wrapper so
// descriptors / handles are always closed, even when an exception is thrown.
// Cross-platform: POSIX (Linux/macOS) and Windows (Winsock2).
namespace plant::net {

class Socket {
public:
    Socket() = default;
    explicit Socket(socket_t fd) noexcept : fd_(fd) {}
    ~Socket();

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    socket_t fd() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ != kInvalidSocket; }
    void reset() noexcept;

private:
    socket_t fd_ = kInvalidSocket;
};

struct Listener {
    Socket socket;
    std::uint16_t port = 0;  // actual bound port (useful when 0 was requested)
};

// Listens on host:port. Pass port 0 to let the OS pick a free port.
Listener tcp_listen(const std::string& host, std::uint16_t port, int backlog = 16);
Socket tcp_connect(const std::string& host, std::uint16_t port);

// Accepts one connection; returns an invalid Socket on error.
Socket tcp_accept(socket_t listen_fd);

// Sends the whole buffer. Returns false if the peer is gone.
bool send_all(socket_t fd, const std::string& data);

// True if the descriptor becomes readable (or hung up) within the timeout.
bool wait_readable(socket_t fd, int timeout_ms);

// Reads one '\n'-terminated line (without the terminator). Returns std::nullopt
// on timeout or if the connection closes with nothing read.
std::optional<std::string> recv_line(socket_t fd, int timeout_ms);

// Fire-and-forget UDP sender.
class UdpPublisher {
public:
    UdpPublisher(const std::string& host, std::uint16_t port);
    bool send(const std::string& message);

private:
    Socket socket_;
    sockaddr_in destination_{};
};

// UDP receiver bound to host:port (port 0 = pick a free port).
class UdpReceiver {
public:
    explicit UdpReceiver(const std::string& host = "127.0.0.1", std::uint16_t port = 0);
    std::uint16_t port() const noexcept { return port_; }
    std::optional<std::string> receive(int timeout_ms);

private:
    Socket socket_;
    std::uint16_t port_ = 0;
};

}  // namespace plant::net
