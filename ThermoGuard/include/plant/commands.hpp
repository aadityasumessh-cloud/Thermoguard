#pragma once

#include <string>

#include "plant/control_system.hpp"

namespace plant {

// Text protocol spoken over TCP (one command per line):
//   STATUS            -> OK temp=.. setpoint=.. heater=.. alarm=.. samples=.. dropped=..
//   SETPOINT <value>  -> OK setpoint=..   (or ERR ... if out of range / malformed)
//   HELP              -> OK commands: ...
//   QUIT              -> BYE
// Commands are case-insensitive. Thread-safe because ControlSystem is.
std::string handle_command(ControlSystem& system, const std::string& line);

// One-line JSON used for UDP telemetry.
std::string to_json(const SystemStatus& status);

}  // namespace plant
