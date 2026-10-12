#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "plant/types.hpp"

namespace plant {

struct AlarmEvent {
    std::string sensor_id;
    AlarmState state = AlarmState::Normal;
    double value = 0.0;
};

// Observer interface: anything that wants to react to alarm transitions.
class IAlarmListener {
public:
    virtual ~IAlarmListener() = default;
    virtual void on_alarm(const AlarmEvent& event) = 0;
};

// Adapter that turns a lambda into a listener.
class CallbackAlarmListener : public IAlarmListener {
public:
    explicit CallbackAlarmListener(std::function<void(const AlarmEvent&)> callback)
        : callback_(std::move(callback)) {}
    void on_alarm(const AlarmEvent& event) override { callback_(event); }

private:
    std::function<void(const AlarmEvent&)> callback_;
};

// Threshold alarm with hysteresis, so a value hovering around a limit does not
// make the alarm chatter. Listeners are notified only on state transitions,
// and are called outside the internal lock (so they may call back in safely).
class AlarmManager {
public:
    AlarmManager(double low_limit, double high_limit, double hysteresis)
        : low_(low_limit), high_(high_limit), hysteresis_(hysteresis) {
        if (low_ >= high_) {
            throw std::invalid_argument("alarm low limit must be below high limit");
        }
        if (hysteresis_ < 0.0) {
            throw std::invalid_argument("alarm hysteresis must not be negative");
        }
    }

    void add_listener(std::shared_ptr<IAlarmListener> listener) {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.push_back(std::move(listener));
    }

    void evaluate(const std::string& sensor_id, double value) {
        std::vector<std::shared_ptr<IAlarmListener>> to_notify;
        AlarmEvent event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const AlarmState next = next_state_locked(value);
            if (next == state_) {
                return;
            }
            state_ = next;
            event = AlarmEvent{sensor_id, next, value};
            to_notify = listeners_;
        }
        for (const auto& listener : to_notify) {
            listener->on_alarm(event);
        }
    }

    AlarmState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

private:
    AlarmState next_state_locked(double value) const {
        switch (state_) {
            case AlarmState::Normal:
                if (value > high_) return AlarmState::High;
                if (value < low_) return AlarmState::Low;
                return AlarmState::Normal;
            case AlarmState::High:
                if (value < high_ - hysteresis_) {
                    return value < low_ ? AlarmState::Low : AlarmState::Normal;
                }
                return AlarmState::High;
            case AlarmState::Low:
                if (value > low_ + hysteresis_) {
                    return value > high_ ? AlarmState::High : AlarmState::Normal;
                }
                return AlarmState::Low;
        }
        return state_;
    }

    const double low_;
    const double high_;
    const double hysteresis_;
    AlarmState state_ = AlarmState::Normal;
    std::vector<std::shared_ptr<IAlarmListener>> listeners_;
    mutable std::mutex mutex_;
};

}  // namespace plant
