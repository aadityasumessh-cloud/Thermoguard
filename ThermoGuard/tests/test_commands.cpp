#include <gtest/gtest.h>

#include <string>

#include "plant/command_server.hpp"
#include "plant/commands.hpp"
#include "plant/control_system.hpp"
#include "plant/net.hpp"

using plant::ControlConfig;
using plant::ControlSystem;
using plant::handle_command;

TEST(CommandsTest, StatusReportsAllFields) {
    ControlSystem system{ControlConfig{}};
    const std::string reply = handle_command(system, "STATUS");
    EXPECT_EQ(reply.rfind("OK temp=25.00 setpoint=50.00 heater=0.0 alarm=NORMAL", 0), 0u);
    EXPECT_NE(reply.find("samples=0"), std::string::npos);
    EXPECT_NE(reply.find("dropped=0"), std::string::npos);
}

TEST(CommandsTest, CommandsAreCaseInsensitive) {
    ControlSystem system{ControlConfig{}};
    EXPECT_EQ(handle_command(system, "status").rfind("OK", 0), 0u);
    EXPECT_EQ(handle_command(system, "SetPoint 55").rfind("OK setpoint=55.00", 0), 0u);
}

TEST(CommandsTest, SetpointUpdatesTheSystem) {
    ControlSystem system{ControlConfig{}};
    EXPECT_EQ(handle_command(system, "SETPOINT 62.5"), "OK setpoint=62.50");
    EXPECT_DOUBLE_EQ(system.status().setpoint_c, 62.5);
}

TEST(CommandsTest, SetpointRejectsOutOfRange) {
    ControlSystem system{ControlConfig{}};
    EXPECT_EQ(handle_command(system, "SETPOINT 5"), "ERR setpoint must be between 10 and 90");
    EXPECT_EQ(handle_command(system, "SETPOINT 500"), "ERR setpoint must be between 10 and 90");
    EXPECT_DOUBLE_EQ(system.status().setpoint_c, 50.0);
}

TEST(CommandsTest, SetpointRejectsMalformedInput) {
    ControlSystem system{ControlConfig{}};
    EXPECT_EQ(handle_command(system, "SETPOINT"), "ERR usage: SETPOINT <celsius>");
    EXPECT_EQ(handle_command(system, "SETPOINT 1 2"), "ERR usage: SETPOINT <celsius>");
    EXPECT_EQ(handle_command(system, "SETPOINT abc"), "ERR setpoint must be a number");
    EXPECT_EQ(handle_command(system, "SETPOINT 50abc"), "ERR setpoint must be a number");
    EXPECT_EQ(handle_command(system, "SETPOINT nan"), "ERR setpoint must be a number");
}

TEST(CommandsTest, HelpQuitAndUnknown) {
    ControlSystem system{ControlConfig{}};
    EXPECT_EQ(handle_command(system, "HELP").rfind("OK commands:", 0), 0u);
    EXPECT_EQ(handle_command(system, "QUIT"), "BYE");
    EXPECT_EQ(handle_command(system, "FLY"), "ERR unknown command");
    EXPECT_EQ(handle_command(system, "   "), "ERR empty command");
    EXPECT_EQ(handle_command(system, ""), "ERR empty command");
}

TEST(CommandsTest, JsonTelemetryHasExpectedShape) {
    plant::SystemStatus status;
    status.temperature_c = 48.1234;
    status.setpoint_c = 50.0;
    status.heater_percent = 41.7;
    status.alarm = plant::AlarmState::Normal;
    status.samples = 12;
    EXPECT_EQ(plant::to_json(status),
              "{\"temp\":48.123,\"setpoint\":50.00,\"heater\":41.7,\"alarm\":\"NORMAL\",\"samples\":12}");
}

// End to end: real system + real TCP server + real UDP telemetry on loopback.
TEST(EndToEndTest, TcpCommandsControlTheSystemAndUdpStreamsTelemetry) {
    ControlConfig config;
    config.sample_period = std::chrono::milliseconds(20);
    config.plant_period = std::chrono::milliseconds(10);
    config.time_scale = 50.0;
    ControlSystem system(config);

    plant::net::UdpReceiver receiver("127.0.0.1", 0);
    plant::net::UdpPublisher publisher("127.0.0.1", receiver.port());
    system.set_telemetry_callback(
        [&publisher](const plant::SystemStatus& s) { publisher.send(plant::to_json(s)); });

    plant::CommandServer server("127.0.0.1", 0, [&system](const std::string& line) {
        return handle_command(system, line);
    });
    system.start();
    server.start();

    plant::net::Socket client = plant::net::tcp_connect("127.0.0.1", server.port());
    ASSERT_TRUE(plant::net::send_all(client.fd(), "SETPOINT 70\n"));
    EXPECT_EQ(plant::net::recv_line(client.fd(), 2000).value(), "OK setpoint=70.00");

    ASSERT_TRUE(plant::net::send_all(client.fd(), "STATUS\n"));
    const auto status_reply = plant::net::recv_line(client.fd(), 2000);
    ASSERT_TRUE(status_reply.has_value());
    EXPECT_NE(status_reply->find("setpoint=70.00"), std::string::npos);

    // Packets sent before the SETPOINT command may still be queued, so read until
    // one reflects the new setpoint (or give up after a deadline).
    bool saw_new_setpoint = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!saw_new_setpoint && std::chrono::steady_clock::now() < deadline) {
        const auto packet = receiver.receive(500);
        if (!packet) continue;
        ASSERT_EQ(packet->front(), '{');
        saw_new_setpoint = packet->find("\"setpoint\":70.00") != std::string::npos;
    }
    EXPECT_TRUE(saw_new_setpoint);

    server.stop();
    system.stop();
}
