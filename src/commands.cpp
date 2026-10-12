#include "plant/commands.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace plant {

namespace {

std::string format(const char* pattern, double a, double b, double c, const char* d,
                   unsigned long long e, unsigned long long f) {
    char buffer[200];
    std::snprintf(buffer, sizeof(buffer), pattern, a, b, c, d, e, f);
    return buffer;
}

}  // namespace

std::string handle_command(ControlSystem& system, const std::string& line) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    for (char& c : command) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (command.empty()) {
        return "ERR empty command";
    }

    if (command == "STATUS") {
        const SystemStatus s = system.status();
        return format("OK temp=%.2f setpoint=%.2f heater=%.1f alarm=%s samples=%llu dropped=%llu",
                      s.temperature_c, s.setpoint_c, s.heater_percent, to_string(s.alarm),
                      static_cast<unsigned long long>(s.samples),
                      static_cast<unsigned long long>(s.dropped_samples));
    }

    if (command == "SETPOINT") {
        std::string argument;
        std::string extra;
        if (!(input >> argument) || (input >> extra)) {
            return "ERR usage: SETPOINT <celsius>";
        }
        char* end = nullptr;
        const double value = std::strtod(argument.c_str(), &end);
        if (end == argument.c_str() || *end != '\0' || !std::isfinite(value)) {
            return "ERR setpoint must be a number";
        }
        if (!system.set_setpoint(value)) {
            return "ERR setpoint must be between 10 and 90";
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "OK setpoint=%.2f", value);
        return buffer;
    }

    if (command == "HELP") {
        return "OK commands: STATUS | SETPOINT <celsius> | HELP | QUIT";
    }

    if (command == "QUIT") {
        return "BYE";
    }

    return "ERR unknown command";
}

std::string to_json(const SystemStatus& s) {
    char buffer[220];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"temp\":%.3f,\"setpoint\":%.2f,\"heater\":%.1f,\"alarm\":\"%s\","
                  "\"samples\":%llu}",
                  s.temperature_c, s.setpoint_c, s.heater_percent, to_string(s.alarm),
                  static_cast<unsigned long long>(s.samples));
    return buffer;
}

}  // namespace plant
