#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace plant {

// A first-order thermal process (think: a heated block, like a Peltier stage).
//
//   steady-state temperature = ambient + gain * heater_percent
//   the temperature approaches it exponentially with time constant tau
//
// step() uses the exact solution for a constant heater input, so the result
// does not depend on how large dt is (no numerical-integration error).
// All methods are thread-safe: a sensor thread reads while a plant thread steps.
class ThermalPlant {
public:
    ThermalPlant(double ambient_c, double gain_c_per_percent, double tau_s, double initial_c)
        : ambient_(ambient_c), gain_(gain_c_per_percent), tau_(tau_s), temperature_(initial_c) {
        if (tau_ <= 0.0) {
            throw std::invalid_argument("ThermalPlant tau must be positive");
        }
    }

    void set_heater_percent(double percent) {
        std::lock_guard<std::mutex> lock(mutex_);
        heater_ = std::clamp(percent, 0.0, 100.0);
    }

    double heater_percent() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return heater_;
    }

    double temperature_c() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return temperature_;
    }

    // Advances the simulation by dt seconds.
    void step(double dt_s) {
        if (dt_s <= 0.0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const double steady_state = ambient_ + gain_ * heater_;
        temperature_ = steady_state + (temperature_ - steady_state) * std::exp(-dt_s / tau_);
    }

private:
    const double ambient_;
    const double gain_;
    const double tau_;
    double temperature_;
    double heater_ = 0.0;
    mutable std::mutex mutex_;
};

}  // namespace plant
