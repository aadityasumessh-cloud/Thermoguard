#include "plant/net.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace plant::net {

namespace {

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}

sockaddr_in make_address(const std::string& host, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const std::string ip = (host == "localhost") ? "127.0.0.1" : host;
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 address: " + host);
    }
    return address;
}

std::uint16_t bound_port(int fd) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        throw_errno("getsockname");
    }
    return ntohs(address.sin_port);
}

Socket make_socket(int type, const char* what) {
    const int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) {
        throw_errno(what);
    }
    return Socket(fd);
}

}  // namespace

Socket::~Socket() { reset(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::reset() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Listener tcp_listen(const std::string& host, std::uint16_t port, int backlog) {
    Socket socket = make_socket(SOCK_STREAM, "socket");
    const int yes = 1;
    if (setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        throw_errno("setsockopt(SO_REUSEADDR)");
    }
    const sockaddr_in address = make_address(host, port);
    if (::bind(socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw_errno("bind");
    }
    if (::listen(socket.fd(), backlog) != 0) {
        throw_errno("listen");
    }
    const std::uint16_t actual_port = bound_port(socket.fd());
    return Listener{std::move(socket), actual_port};
}

Socket tcp_connect(const std::string& host, std::uint16_t port) {
    Socket socket = make_socket(SOCK_STREAM, "socket");
    const sockaddr_in address = make_address(host, port);
    if (::connect(socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw_errno("connect");
    }
    return socket;
}

Socket tcp_accept(int listen_fd) {
    const int fd = ::accept(listen_fd, nullptr, nullptr);
    return fd >= 0 ? Socket(fd) : Socket();
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool wait_readable(int fd, int timeout_ms) {
    pollfd descriptor{fd, POLLIN, 0};
    int result = 0;
    do {
        result = ::poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

std::optional<std::string> recv_line(int fd, int timeout_ms) {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;

    const auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    std::string line;
    for (;;) {
        const auto remaining =
            duration_cast<milliseconds>(deadline - steady_clock::now()).count();
        if (remaining <= 0 || !wait_readable(fd, static_cast<int>(remaining))) {
            return std::nullopt;
        }
        char c = 0;
        const ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) {
            if (line.empty()) {
                return std::nullopt;
            }
            return line;
        }
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        line.push_back(c);
    }
}

UdpPublisher::UdpPublisher(const std::string& host, std::uint16_t port)
    : socket_(make_socket(SOCK_DGRAM, "socket")), destination_(make_address(host, port)) {}

bool UdpPublisher::send(const std::string& message) {
    const ssize_t n =
        ::sendto(socket_.fd(), message.data(), message.size(), MSG_NOSIGNAL,
                 reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
    return n == static_cast<ssize_t>(message.size());
}

UdpReceiver::UdpReceiver(const std::string& host, std::uint16_t port)
    : socket_(make_socket(SOCK_DGRAM, "socket")) {
    const sockaddr_in address = make_address(host, port);
    if (::bind(socket_.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw_errno("bind");
    }
    port_ = bound_port(socket_.fd());
}

std::optional<std::string> UdpReceiver::receive(int timeout_ms) {
    if (!wait_readable(socket_.fd(), timeout_ms)) {
        return std::nullopt;
    }
    std::vector<char> buffer(2048);
    const ssize_t n = ::recv(socket_.fd(), buffer.data(), buffer.size(), 0);
    if (n < 0) {
        return std::nullopt;
    }
    return std::string(buffer.data(), static_cast<std::size_t>(n));
}

}  // namespace plant::net
