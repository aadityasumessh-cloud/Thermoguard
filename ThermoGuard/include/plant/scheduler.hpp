#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace plant {

// Runs a function at a fixed period on its own thread, like a periodic task in
// a real-time system.
//
//   * Deadlines are absolute (next += period), so timing errors do not
//     accumulate the way they would with sleep_for(period).
//   * The worst lateness seen is recorded (max_jitter) so timing can be checked.
//   * On Linux, a SCHED_FIFO priority can be requested. It needs privileges
//     (root or CAP_SYS_NICE); if it is refused the task keeps running at normal
//     priority and realtime_applied() reports false.
//
// This is POSIX soft-real-time scheduling on a general-purpose OS. It is not a
// hard real-time RTOS.
class PeriodicTask {
public:
    PeriodicTask(std::string name, std::chrono::milliseconds period, std::function<void()> work,
                 int realtime_priority = 0);
    ~PeriodicTask();

    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;

    void start();
    void stop();

    std::uint64_t iterations() const noexcept { return iterations_.load(); }
    std::uint64_t errors() const noexcept { return errors_.load(); }
    std::chrono::microseconds max_jitter() const noexcept {
        return std::chrono::microseconds(max_jitter_us_.load());
    }
    bool realtime_applied() const noexcept { return realtime_applied_.load(); }
    const std::string& name() const noexcept { return name_; }

private:
    void run();
    void apply_realtime_priority();

    std::string name_;
    std::chrono::milliseconds period_;
    std::function<void()> work_;
    int realtime_priority_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_ = false;

    std::atomic<std::uint64_t> iterations_{0};
    std::atomic<std::uint64_t> errors_{0};
    std::atomic<std::int64_t> max_jitter_us_{0};
    std::atomic<bool> realtime_applied_{false};
};

}  // namespace plant
