#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>
#include <thread>
#include <vector>

#include "plant/command_server.hpp"
#include "plant/net.hpp"

using plant::CommandServer;
using namespace plant::net;

namespace {
std::string upper_handler(const std::string& line) {
    if (line == "QUIT") return "BYE";
    std::string out = line;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
}  // namespace

TEST(NetworkTest, TcpListenOnPortZeroReportsActualPort) {
    Listener listener = tcp_listen("127.0.0.1", 0);
    EXPECT_GT(listener.port, 0);
    EXPECT_TRUE(listener.socket.valid());
}

TEST(NetworkTest, InvalidAddressThrows) {
    EXPECT_THROW(tcp_listen("not-an-ip", 0), std::runtime_error);
}

TEST(NetworkTest, ConnectToClosedPortThrows) {
    std::uint16_t port = 0;
    {
        Listener listener = tcp_listen("127.0.0.1", 0);
        port = listener.port;
    }  // listener closed here
    EXPECT_THROW(tcp_connect("127.0.0.1", port), std::runtime_error);
}

TEST(NetworkTest, SocketIsMoveOnlyAndClosesOnce) {
    // Create a real socket (cross-platform: avoids POSIX dup(0))
    Listener tmp = tcp_listen("127.0.0.1", 0);
    Socket a = std::move(tmp.socket);
    ASSERT_TRUE(a.valid());
    Socket b = std::move(a);
    EXPECT_FALSE(a.valid());  // NOLINT: moved-from state is specified
    EXPECT_TRUE(b.valid());
}

TEST(CommandServerTest, RepliesToACommand) {
    CommandServer server("127.0.0.1", 0, upper_handler);
    server.start();
    Socket client = tcp_connect("127.0.0.1", server.port());
    ASSERT_TRUE(send_all(client.fd(), "hello\n"));
    auto reply = recv_line(client.fd(), 2000);
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(*reply, "HELLO");
}

TEST(CommandServerTest, HandlesSeveralCommandsInOneSend) {
    CommandServer server("127.0.0.1", 0, upper_handler);
    server.start();
    Socket client = tcp_connect("127.0.0.1", server.port());
    ASSERT_TRUE(send_all(client.fd(), "a\nb\r\nc\n"));
    EXPECT_EQ(recv_line(client.fd(), 2000).value(), "A");
    EXPECT_EQ(recv_line(client.fd(), 2000).value(), "B");
    EXPECT_EQ(recv_line(client.fd(), 2000).value(), "C");
}

TEST(CommandServerTest, ReassemblesCommandSplitAcrossPackets) {
    CommandServer server("127.0.0.1", 0, upper_handler);
    server.start();
    Socket client = tcp_connect("127.0.0.1", server.port());
    ASSERT_TRUE(send_all(client.fd(), "par"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(send_all(client.fd(), "tial\n"));
    EXPECT_EQ(recv_line(client.fd(), 2000).value(), "PARTIAL");
}

TEST(CommandServerTest, QuitClosesTheConnection) {
    CommandServer server("127.0.0.1", 0, upper_handler);
    server.start();
    Socket client = tcp_connect("127.0.0.1", server.port());
    ASSERT_TRUE(send_all(client.fd(), "QUIT\n"));
    EXPECT_EQ(recv_line(client.fd(), 2000).value(), "BYE");
    EXPECT_FALSE(recv_line(client.fd(), 500).has_value());  // server closed its end
}

TEST(CommandServerTest, ServesManyClientsConcurrently) {
    CommandServer server("127.0.0.1", 0, upper_handler);
    server.start();
    const std::uint16_t port = server.port();

    constexpr int kClients = 8;
    constexpr int kRequests = 50;
    std::atomic<int> failures{0};
    std::vector<std::thread> clients;
    for (int c = 0; c < kClients; ++c) {
        clients.emplace_back([&, c] {
            Socket socket = tcp_connect("127.0.0.1", port);
            for (int i = 0; i < kRequests; ++i) {
                const std::string message = "client" + std::to_string(c) + "-" + std::to_string(i);
                if (!send_all(socket.fd(), message + "\n")) {
                    ++failures;
                    return;
                }
                auto reply = recv_line(socket.fd(), 2000);
                std::string expected = message;
                for (char& ch : expected) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (!reply || *reply != expected) ++failures;
            }
        });
    }
    for (auto& t : clients) t.join();
    EXPECT_EQ(failures.load(), 0);
}

TEST(CommandServerTest, RejectsOverlongLinesWithoutNewline) {
    CommandServer server("127.0.0.1", 0, upper_handler);
    server.start();
    Socket client = tcp_connect("127.0.0.1", server.port());
    ASSERT_TRUE(send_all(client.fd(), std::string(2000, 'x')));
    auto reply = recv_line(client.fd(), 2000);
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(*reply, "ERR line too long");
}

TEST(CommandServerTest, StopJoinsEverythingEvenWithClientsConnected) {
    auto server = std::make_unique<CommandServer>("127.0.0.1", 0, upper_handler);
    server->start();
    Socket client = tcp_connect("127.0.0.1", server->port());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto start = std::chrono::steady_clock::now();
    server->stop();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
}

TEST(UdpTest, PublisherDeliversToReceiver) {
    UdpReceiver receiver("127.0.0.1", 0);
    UdpPublisher publisher("127.0.0.1", receiver.port());
    ASSERT_TRUE(publisher.send("telemetry-1"));
    auto message = receiver.receive(2000);
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(*message, "telemetry-1");
}

TEST(UdpTest, ReceiveTimesOutWhenNothingIsSent) {
    UdpReceiver receiver("127.0.0.1", 0);
    EXPECT_FALSE(receiver.receive(50).has_value());
}
