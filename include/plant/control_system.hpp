#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "plant/alarm.hpp"
#include "plant/bounded_queue.hpp"
#include "plant/pid.hpp"
#include "plant/scheduler.hpp"
#include "plant/sensor.hpp"
#include "plant/thermal_plant.hpp"
#include "plant/types.hpp"

namespace plant {

struct ControlConfig {
    // Simulated process
    double ambient_c = 25.0;
    double plant_gain_c_per_percent = 0.6;  // 100% heater -> ambient + 60 C
    double plant_tau_s = 30.0;
    double noise_stddev_c = 0.05;
    unsigned seed = 42;

    // Control
    double initial_setpoint_c = 50.0;
    PidConfig pid{4.0, 0.4, 0.0, 0.0, 100.0};

    // Safety: above alarm_high_c the heater is forced off (interlock)
    double alarm_low_c = 15.0;
    double alarm_high_c = 80.0;
    double alarm_hysteresis_c = 2.0;

    // Timing
    std::chrono::milliseconds sample_period{100};
    std::chrono::milliseconds plant_period{50};
    double time_scale = 1.0;  // simulated seconds per real second (speeds up demos/tests)
    int realtime_priority = 0;  // >0 requests SCHED_FIFO on Linux (needs privileges)
};

struct SystemStatus {
    double temperature_c = 0.0;
    double setpoint_c = 0.0;
    double heater_percent = 0.0;
    AlarmState alarm = AlarmState::Normal;
    std::uint64_t samples = 0;
    std::uint64_t dropped_samples = 0;
};

// Wires everything together:
//
//   sensor task --(BoundedQueue)--> control thread --> heater --> plant
//                                       |                           ^
//                                  AlarmManager             plant task steps it
//
// Threads: sensor task, plant task, control thread (+ whatever calls status()).
// Call start() once, then stop(). Setpoint and status are safe to use from any thread.
class ControlSystem {
public:
    static constexpr double kMinSetpointC = 10.0;
    static constexpr double kMaxSetpointC = 90.0;

    explicit ControlSystem(ControlConfig config);
    ~ControlSystem();

    ControlSystem(const ControlSystem&) = delete;
    ControlSystem& operator=(const ControlSystem&) = delete;

    // Must be called before start(). Invoked on the control thread after every sample.
    void set_telemetry_callback(std::function<void(const SystemStatus&)> callback);

    void start();
    void stop();

    // Returns false (and changes nothing) if the value is outside the allowed range.
    bool set_setpoint(double celsius);
    SystemStatus status() const;
    AlarmManager& alarms() noexcept { return alarms_; }
    const ControlConfig& config() const noexcept { return config_; }

private:
    void sample_once();
    void step_plant();
    void control_loop();

    ControlConfig config_;
    ThermalPlant plant_;
    std::unique_ptr<ISensor> sensor_;
    BoundedQueue<SensorReading> queue_;
    PidController pid_;
    AlarmManager alarms_;
    std::atomic<double> setpoint_;
    std::atomic<std::uint64_t> dropped_{0};

    mutable std::mutex status_mutex_;
    SystemStatus status_;
    std::function<void(const SystemStatus&)> telemetry_callback_;

    std::unique_ptr<PeriodicTask> sensor_task_;
    std::unique_ptr<PeriodicTask> plant_task_;
    std::thread control_thread_;
    Clock::time_point last_plant_step_{};
    bool running_ = false;
};

}  // namespace plant
