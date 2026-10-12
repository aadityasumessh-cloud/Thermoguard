#pragma once

#include <memory>
#include <random>
#include <stdexcept>
#include <string>

#include "plant/thermal_plant.hpp"

namespace plant {

// Abstract sensor. Real hardware drivers would implement the same interface.
class ISensor {
public:
    virtual ~ISensor() = default;
    virtual std::string id() const = 0;
    virtual double read() = 0;
};

// Reads the simulated plant temperature and adds Gaussian measurement noise.
// read() is meant to be called from a single sensor thread.
class SimulatedTemperatureSensor : public ISensor {
public:
    SimulatedTemperatureSensor(const ThermalPlant& plant, double noise_stddev, unsigned seed)
        : plant_(plant), noise_(0.0, noise_stddev > 0.0 ? noise_stddev : 1e-12), rng_(seed),
          noisy_(noise_stddev > 0.0) {}

    std::string id() const override { return "temp0"; }

    double read() override {
        const double truth = plant_.temperature_c();
        return noisy_ ? truth + noise_(rng_) : truth;
    }

private:
    const ThermalPlant& plant_;
    std::normal_distribution<double> noise_;
    std::mt19937 rng_;
    bool noisy_;
};

// Factory: creates a sensor by kind. New sensor types plug in here.
inline std::unique_ptr<ISensor> make_sensor(const std::string& kind, const ThermalPlant& plant,
                                            double noise_stddev, unsigned seed) {
    if (kind == "temperature") {
        return std::make_unique<SimulatedTemperatureSensor>(plant, noise_stddev, seed);
    }
    throw std::invalid_argument("unknown sensor kind: " + kind);
}

}  // namespace plant
