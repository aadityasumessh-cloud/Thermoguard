#include "plant/scheduler.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

#include "plant/types.hpp"

// Real-time scheduling: Linux uses SCHED_FIFO via pthread.
// Windows uses SetThreadPriority (no true FIFO, but we raise to TIME_CRITICAL).
#ifdef __linux__
#  include <pthread.h>
#  include <sched.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace plant {

PeriodicTask::PeriodicTask(std::string name, std::chrono::milliseconds period,
                           std::function<void()> work, int realtime_priority)
    : name_(std::move(name)),
      period_(period),
      work_(std::move(work)),
      realtime_priority_(realtime_priority) {
    if (period_.count() <= 0) {
        throw std::invalid_argument("PeriodicTask period must be positive");
    }
    if (!work_) {
        throw std::invalid_argument("PeriodicTask needs a function to run");
    }
}

PeriodicTask::~PeriodicTask() { stop(); }

void PeriodicTask::start() {
    if (thread_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = false;
    }
    thread_ = std::thread(&PeriodicTask::run, this);
}

void PeriodicTask::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PeriodicTask::apply_realtime_priority() {
#ifdef __linux__
    if (realtime_priority_ > 0) {
        sched_param param{};
        param.sched_priority = realtime_priority_;
        realtime_applied_ = (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0);
    }
#elif defined(_WIN32)
    if (realtime_priority_ > 0) {
        // Map to Windows thread priority: use TIME_CRITICAL for highest fidelity
        realtime_applied_ =
            (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0);
    }
#endif
}

void PeriodicTask::run() {
    apply_realtime_priority();

    auto next = Clock::now();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (cv_.wait_until(lock, next, [this] { return stop_requested_; })) {
                break;
            }
        }

        const auto lateness =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - next).count();
        std::int64_t seen = max_jitter_us_.load();
        while (lateness > seen && !max_jitter_us_.compare_exchange_weak(seen, lateness)) {
        }

        try {
            work_();
        } catch (const std::exception&) {
            ++errors_;
        } catch (...) {
            ++errors_;
        }
        ++iterations_;

        next += period_;
        const auto now = Clock::now();
        if (next < now) {
            next = now;  // overran the period: skip missed ticks instead of bursting
        }
    }
}

}  // namespace plant
