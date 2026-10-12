#pragma once

#include <chrono>
#include <string>

namespace plant {

using Clock = std::chrono::steady_clock;

struct SensorReading {
    std::string sensor_id;
    double value = 0.0;
    Clock::time_point timestamp{};
};

enum class AlarmState { Normal, High, Low };

inline const char* to_string(AlarmState state) noexcept {
    switch (state) {
        case AlarmState::Normal: return "NORMAL";
        case AlarmState::High: return "HIGH";
        case AlarmState::Low: return "LOW";
    }
    return "UNKNOWN";
}

}  // namespace plant
