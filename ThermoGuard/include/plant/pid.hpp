#pragma once

#include <algorithm>
#include <stdexcept>

namespace plant {

struct PidConfig {
    double kp = 1.0;
    double ki = 0.0;
    double kd = 0.0;
    double out_min = 0.0;
    double out_max = 100.0;
};

// Discrete PID controller.
//   * Derivative acts on the measurement, not the error, so a setpoint change
//     does not cause a derivative "kick".
//   * Anti-windup: the integral term is clamped to the output range, so it
//     cannot grow without bound while the actuator is saturated.
// Not thread-safe: use it from a single control thread.
class PidController {
public:
    explicit PidController(PidConfig config) : config_(config) {
        if (config_.out_min >= config_.out_max) {
            throw std::invalid_argument("PID out_min must be less than out_max");
        }
    }

    double update(double setpoint, double measurement, double dt_s) {
        if (dt_s <= 0.0) {
            return last_output_;
        }
        const double error = setpoint - measurement;

        integral_ = std::clamp(integral_ + config_.ki * error * dt_s, config_.out_min,
                               config_.out_max);

        double derivative = 0.0;
        if (has_previous_) {
            derivative = -(measurement - previous_measurement_) / dt_s;
        }
        previous_measurement_ = measurement;
        has_previous_ = true;

        last_output_ = std::clamp(config_.kp * error + integral_ + config_.kd * derivative,
                                  config_.out_min, config_.out_max);
        return last_output_;
    }

    void reset() {
        integral_ = 0.0;
        previous_measurement_ = 0.0;
        has_previous_ = false;
        last_output_ = 0.0;
    }

    double last_output() const noexcept { return last_output_; }

private:
    PidConfig config_;
    double integral_ = 0.0;
    double previous_measurement_ = 0.0;
    bool has_previous_ = false;
    double last_output_ = 0.0;
};

}  // namespace plant
