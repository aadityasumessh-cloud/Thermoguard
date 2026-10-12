#include "plant/control_system.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace plant {

namespace {
constexpr std::size_t kQueueCapacity = 64;
}

ControlSystem::ControlSystem(ControlConfig config)
    : config_(config),
      plant_(config.ambient_c, config.plant_gain_c_per_percent, config.plant_tau_s,
             config.ambient_c),
      sensor_(make_sensor("temperature", plant_, config.noise_stddev_c, config.seed)),
      queue_(kQueueCapacity),
      pid_(config.pid),
      alarms_(config.alarm_low_c, config.alarm_high_c, config.alarm_hysteresis_c),
      setpoint_(config.initial_setpoint_c) {
    status_.temperature_c = config.ambient_c;
    status_.setpoint_c = config.initial_setpoint_c;
}

ControlSystem::~ControlSystem() { stop(); }

void ControlSystem::set_telemetry_callback(std::function<void(const SystemStatus&)> callback) {
    telemetry_callback_ = std::move(callback);
}

void ControlSystem::start() {
    if (running_) {
        return;
    }
    running_ = true;
    last_plant_step_ = Clock::now();

    control_thread_ = std::thread(&ControlSystem::control_loop, this);
    plant_task_ = std::make_unique<PeriodicTask>("plant", config_.plant_period,
                                                 [this] { step_plant(); }, config_.realtime_priority);
    sensor_task_ = std::make_unique<PeriodicTask>("sensor", config_.sample_period,
                                                  [this] { sample_once(); }, config_.realtime_priority);
    plant_task_->start();
    sensor_task_->start();
}

void ControlSystem::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    sensor_task_->stop();
    plant_task_->stop();
    queue_.close();  // wakes the control thread, which drains and exits
    if (control_thread_.joinable()) {
        control_thread_.join();
    }
}

bool ControlSystem::set_setpoint(double celsius) {
    if (!std::isfinite(celsius) || celsius < kMinSetpointC || celsius > kMaxSetpointC) {
        return false;
    }
    setpoint_.store(celsius);
    return true;
}

SystemStatus ControlSystem::status() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    SystemStatus copy = status_;
    copy.setpoint_c = setpoint_.load();
    copy.dropped_samples = dropped_.load();
    return copy;
}

void ControlSystem::sample_once() {
    SensorReading reading{sensor_->id(), sensor_->read(), Clock::now()};
    if (!queue_.try_push(std::move(reading))) {
        ++dropped_;  // queue full: drop the newest sample rather than block the sensor task
    }
}

void ControlSystem::step_plant() {
    const auto now = Clock::now();
    const double wall_dt = std::chrono::duration<double>(now - last_plant_step_).count();
    last_plant_step_ = now;
    plant_.step(wall_dt * config_.time_scale);
}

void ControlSystem::control_loop() {
    std::optional<Clock::time_point> previous_timestamp;
    std::uint64_t samples = 0;

    while (auto reading = queue_.pop()) {
        double dt = std::chrono::duration<double>(config_.sample_period).count() *
                    config_.time_scale;
        if (previous_timestamp) {
            dt = std::chrono::duration<double>(reading->timestamp - *previous_timestamp).count() *
                 config_.time_scale;
        }
        previous_timestamp = reading->timestamp;
        dt = std::max(dt, 1e-3);

        alarms_.evaluate(reading->sensor_id, reading->value);
        const AlarmState alarm = alarms_.state();

        double heater = 0.0;
        if (alarm == AlarmState::High) {
            pid_.reset();  // safety interlock: heater off, and forget the integral
        } else {
            heater = pid_.update(setpoint_.load(), reading->value, dt);
        }
        plant_.set_heater_percent(heater);
        ++samples;

        SystemStatus snapshot;
        snapshot.temperature_c = reading->value;
        snapshot.setpoint_c = setpoint_.load();
        snapshot.heater_percent = heater;
        snapshot.alarm = alarm;
        snapshot.samples = samples;
        snapshot.dropped_samples = dropped_.load();
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            status_ = snapshot;
        }
        if (telemetry_callback_) {
            telemetry_callback_(snapshot);
        }
    }
}

}  // namespace plant
