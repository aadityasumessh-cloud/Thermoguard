#include "plant/net.hpp"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
// Map POSIX-style names to Winsock equivalents
#  define poll         WSAPoll
#  define POLLHUP      0        // Winsock WSAPoll doesn't set POLLHUP
   using ssize_t = int;
   using nfds_t  = ULONG;
   struct pollfd_t { SOCKET fd; SHORT events; SHORT revents; };
#  undef  pollfd
#  define pollfd pollfd_t
   // Winsock send() doesn't have MSG_NOSIGNAL
#  ifndef MSG_NOSIGNAL
#    define MSG_NOSIGNAL 0
#  endif
// Winsock error helpers
   static std::string wsa_strerror(int code) {
       char buf[256] = {};
       FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      nullptr, static_cast<DWORD>(code), 0, buf, sizeof(buf), nullptr);
       // trim trailing newline
       auto len = strlen(buf);
       while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = 0;
       return buf;
   }
#else
#  include <arpa/inet.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace plant::net {

namespace {

// ---------------------------------------------------------------------------
// Platform-neutral error helpers
// ---------------------------------------------------------------------------
#ifdef _WIN32
[[noreturn]] void throw_net_error(const std::string& what) {
    throw std::runtime_error(what + ": " + wsa_strerror(WSAGetLastError()));
}
#else
[[noreturn]] void throw_net_error(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}
#endif

// ---------------------------------------------------------------------------
// Winsock initialisation (one-time, idempotent)
// ---------------------------------------------------------------------------
#ifdef _WIN32
struct WsaInit {
    WsaInit() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WsaInit() { WSACleanup(); }
};
static const WsaInit wsa_guard;  // NOLINT(cert-err58-cpp)
#endif

// ---------------------------------------------------------------------------
// Address helpers
// ---------------------------------------------------------------------------
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

std::uint16_t bound_port(socket_t fd) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        throw_net_error("getsockname");
    }
    return ntohs(address.sin_port);
}

Socket make_socket(int type, const char* what) {
#ifdef _WIN32
    const SOCKET fd = ::socket(AF_INET, type, 0);
    if (fd == INVALID_SOCKET) {
        throw_net_error(what);
    }
#else
    const int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) {
        throw_net_error(what);
    }
#endif
    return Socket(fd);
}

// Platform-neutral recv wrapper (returns -1 on error / 0 on close)
ssize_t net_recv(socket_t fd, void* buf, int len) {
#ifdef _WIN32
    return ::recv(fd, static_cast<char*>(buf), len, 0);
#else
    return ::recv(fd, buf, static_cast<std::size_t>(len), 0);
#endif
}

// Platform-neutral close
void close_socket(socket_t fd) noexcept {
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

// poll() wrapper that handles EINTR
int net_poll(pollfd* fds, int n, int timeout_ms) {
#ifdef _WIN32
    return WSAPoll(fds, static_cast<ULONG>(n), timeout_ms);
#else
    int r = 0;
    do { r = ::poll(fds, static_cast<nfds_t>(n), timeout_ms); }
    while (r < 0 && errno == EINTR);
    return r;
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Socket RAII
// ---------------------------------------------------------------------------
Socket::~Socket() { reset(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = kInvalidSocket; }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = other.fd_;
        other.fd_ = kInvalidSocket;
    }
    return *this;
}

void Socket::reset() noexcept {
    if (fd_ != kInvalidSocket) {
        close_socket(fd_);
        fd_ = kInvalidSocket;
    }
}

// ---------------------------------------------------------------------------
// TCP helpers
// ---------------------------------------------------------------------------
Listener tcp_listen(const std::string& host, std::uint16_t port, int backlog) {
    Socket socket = make_socket(SOCK_STREAM, "socket");
    const int yes = 1;
    if (setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&yes), sizeof(yes)) != 0) {
        throw_net_error("setsockopt(SO_REUSEADDR)");
    }
    const sockaddr_in address = make_address(host, port);
    if (::bind(socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw_net_error("bind");
    }
    if (::listen(socket.fd(), backlog) != 0) {
        throw_net_error("listen");
    }
    const std::uint16_t actual_port = bound_port(socket.fd());
    return Listener{std::move(socket), actual_port};
}

Socket tcp_connect(const std::string& host, std::uint16_t port) {
    Socket socket = make_socket(SOCK_STREAM, "socket");
    const sockaddr_in address = make_address(host, port);
    if (::connect(socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw_net_error("connect");
    }
    return socket;
}

Socket tcp_accept(socket_t listen_fd) {
#ifdef _WIN32
    const SOCKET fd = ::accept(listen_fd, nullptr, nullptr);
    return fd != INVALID_SOCKET ? Socket(fd) : Socket();
#else
    const int fd = ::accept(listen_fd, nullptr, nullptr);
    return fd >= 0 ? Socket(fd) : Socket();
#endif
}

bool send_all(socket_t fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd,
                                  data.data() + sent,
#ifdef _WIN32
                                  static_cast<int>(data.size() - sent),
#else
                                  data.size() - sent,
#endif
                                  MSG_NOSIGNAL);
        if (n < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool wait_readable(socket_t fd, int timeout_ms) {
    pollfd descriptor{fd, POLLIN, 0};
    const int result = net_poll(&descriptor, 1, timeout_ms);
    return result > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

std::optional<std::string> recv_line(socket_t fd, int timeout_ms) {
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
        const ssize_t n = net_recv(fd, &c, 1);
        if (n <= 0) {
            if (line.empty()) return std::nullopt;
            return line;
        }
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        line.push_back(c);
    }
}

// ---------------------------------------------------------------------------
// UDP helpers
// ---------------------------------------------------------------------------
UdpPublisher::UdpPublisher(const std::string& host, std::uint16_t port)
    : socket_(make_socket(SOCK_DGRAM, "socket")), destination_(make_address(host, port)) {}

bool UdpPublisher::send(const std::string& message) {
    const ssize_t n =
        ::sendto(socket_.fd(), message.data(),
#ifdef _WIN32
                 static_cast<int>(message.size()),
#else
                 message.size(),
#endif
                 MSG_NOSIGNAL,
                 reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
    return n == static_cast<ssize_t>(message.size());
}

UdpReceiver::UdpReceiver(const std::string& host, std::uint16_t port)
    : socket_(make_socket(SOCK_DGRAM, "socket")) {
    const sockaddr_in address = make_address(host, port);
    if (::bind(socket_.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw_net_error("bind");
    }
    port_ = bound_port(socket_.fd());
}

std::optional<std::string> UdpReceiver::receive(int timeout_ms) {
    if (!wait_readable(socket_.fd(), timeout_ms)) return std::nullopt;
    std::vector<char> buffer(2048);
    const ssize_t n = net_recv(socket_.fd(), buffer.data(), static_cast<int>(buffer.size()));
    if (n < 0) return std::nullopt;
    return std::string(buffer.data(), static_cast<std::size_t>(n));
}

}  // namespace plant::net
