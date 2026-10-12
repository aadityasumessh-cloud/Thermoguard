// plant_controller: runs the simulated temperature control system and exposes it
// over TCP (commands) and UDP (telemetry stream).
//
//   ./plant_controller [--host IP] [--tcp-port N] [--udp-host IP] [--udp-port N]
//                      [--speed X] [--setpoint C] [--duration SECONDS] [--realtime PRIORITY]

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "plant/command_server.hpp"
#include "plant/commands.hpp"
#include "plant/control_system.hpp"
#include "plant/net.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

void usage() {
    std::cout << "Usage: plant_controller [--host IP] [--tcp-port N] [--udp-host IP] [--udp-port N]\n"
                 "                        [--speed X] [--setpoint C] [--duration SECONDS] [--realtime PRIO]\n"
                 "  --host       address the command server binds to (default 127.0.0.1)\n"
                 "  --tcp-port   command server port (default 5000, 0 = any free port)\n"
                 "  --udp-host   telemetry destination address (default 127.0.0.1)\n"
                 "  --udp-port   telemetry destination port (default 5001)\n"
                 "  --speed      simulated seconds per real second (default 10)\n"
                 "  --setpoint   initial setpoint in C (default 50)\n"
                 "  --duration   stop after this many seconds (default: run until Ctrl+C)\n"
                 "  --realtime   request SCHED_FIFO priority (Linux, needs privileges)\n";
}

}  // namespace

int main(int argc, char** argv) {
    plant::ControlConfig config;
    config.time_scale = 10.0;
    std::string host = "127.0.0.1";
    std::string udp_host = "127.0.0.1";
    unsigned tcp_port = 5000;
    unsigned udp_port = 5001;
    double duration_s = 0.0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        const char* value = next();
        if (value == nullptr) {
            std::cerr << "missing value for " << arg << "\n";
            usage();
            return 2;
        }
        if (arg == "--host") host = value;
        else if (arg == "--udp-host") udp_host = value;
        else if (arg == "--tcp-port") tcp_port = static_cast<unsigned>(std::atoi(value));
        else if (arg == "--udp-port") udp_port = static_cast<unsigned>(std::atoi(value));
        else if (arg == "--speed") config.time_scale = std::atof(value);
        else if (arg == "--setpoint") config.initial_setpoint_c = std::atof(value);
        else if (arg == "--duration") duration_s = std::atof(value);
        else if (arg == "--realtime") config.realtime_priority = std::atoi(value);
        else {
            std::cerr << "unknown option " << arg << "\n";
            usage();
            return 2;
        }
    }

    if (config.time_scale <= 0.0) {
        std::cerr << "--speed must be positive\n";
        return 2;
    }

    struct sigaction action {};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    try {
        plant::ControlSystem system(config);

        plant::net::UdpPublisher telemetry(udp_host, static_cast<std::uint16_t>(udp_port));
        system.set_telemetry_callback(
            [&telemetry](const plant::SystemStatus& status) { telemetry.send(plant::to_json(status)); });

        system.alarms().add_listener(std::make_shared<plant::CallbackAlarmListener>(
            [](const plant::AlarmEvent& event) {
                std::cout << "[ALARM] " << event.sensor_id << " -> " << plant::to_string(event.state)
                          << " (value " << event.value << ")\n";
            }));

        plant::CommandServer server(host, static_cast<std::uint16_t>(tcp_port),
                                    [&system](const std::string& line) {
                                        return plant::handle_command(system, line);
                                    });

        system.start();
        server.start();

        std::cout << "plant_controller running\n"
                  << "  TCP commands on " << host << ":" << server.port() << "  (try: STATUS, SETPOINT 60)\n"
                  << "  UDP telemetry to " << udp_host << ":" << udp_port << "\n"
                  << "  simulation speed: x" << config.time_scale << "\n"
                  << "  Ctrl+C to stop\n";

        const auto started = std::chrono::steady_clock::now();
        auto last_print = started;
        while (!g_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto now = std::chrono::steady_clock::now();
            if (now - last_print >= std::chrono::seconds(1)) {
                last_print = now;
                std::cout << plant::handle_command(system, "STATUS") << std::endl;
            }
            if (duration_s > 0.0 &&
                std::chrono::duration<double>(now - started).count() >= duration_s) {
                break;
            }
        }

        std::cout << "shutting down\n";
        server.stop();
        system.stop();
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
