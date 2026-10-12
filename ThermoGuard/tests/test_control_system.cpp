#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "plant/control_system.hpp"

using namespace std::chrono_literals;
using plant::AlarmState;
using plant::ControlConfig;
using plant::ControlSystem;

namespace {

ControlConfig fast_config() {
    ControlConfig config;
    config.sample_period = 20ms;
    config.plant_period = 10ms;
    config.time_scale = 50.0;  // 50 simulated seconds per real second
    config.noise_stddev_c = 0.0;
    return config;
}

bool wait_until(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return condition();
}

}  // namespace

TEST(ControlSystemTest, InitialStatusBeforeStart) {
    ControlSystem system(fast_config());
    const auto status = system.status();
    EXPECT_DOUBLE_EQ(status.temperature_c, 25.0);
    EXPECT_DOUBLE_EQ(status.setpoint_c, 50.0);
    EXPECT_EQ(status.samples, 0u);
    EXPECT_EQ(status.alarm, AlarmState::Normal);
}

TEST(ControlSystemTest, SetpointIsValidated) {
    ControlSystem system(fast_config());
    EXPECT_TRUE(system.set_setpoint(60.0));
    EXPECT_DOUBLE_EQ(system.status().setpoint_c, 60.0);
    EXPECT_FALSE(system.set_setpoint(5.0));
    EXPECT_FALSE(system.set_setpoint(95.0));
    EXPECT_FALSE(system.set_setpoint(std::nan("")));
    EXPECT_DOUBLE_EQ(system.status().setpoint_c, 60.0);  // unchanged by the rejected calls
}

TEST(ControlSystemTest, CollectsSamplesAndDrivesHeater) {
    ControlSystem system(fast_config());
    system.start();
    ASSERT_TRUE(wait_until([&] { return system.status().samples >= 5; }, 5s));
    EXPECT_GT(system.status().heater_percent, 0.0);
    system.stop();
}

TEST(ControlSystemTest, TemperatureRisesTowardSetpoint) {
    ControlSystem system(fast_config());
    system.start();
    ASSERT_TRUE(wait_until([&] { return system.status().temperature_c > 40.0; }, 10s));
    system.stop();
}

TEST(ControlSystemTest, TelemetryCallbackReceivesEverySample) {
    ControlSystem system(fast_config());
    std::atomic<std::uint64_t> last_seen{0};
    system.set_telemetry_callback(
        [&](const plant::SystemStatus& status) { last_seen = status.samples; });
    system.start();
    ASSERT_TRUE(wait_until([&] { return last_seen.load() >= 5; }, 5s));
    system.stop();
    EXPECT_EQ(last_seen.load(), system.status().samples);
}

TEST(ControlSystemTest, OverTemperatureInterlockForcesHeaterOffAndRaisesAlarm) {
    ControlConfig config = fast_config();
    config.alarm_high_c = 35.0;   // low limit so the alarm trips quickly
    config.initial_setpoint_c = 80.0;  // controller wants far more heat than is allowed
    ControlSystem system(config);

    std::atomic<int> high_events{0};
    system.alarms().add_listener(std::make_shared<plant::CallbackAlarmListener>(
        [&](const plant::AlarmEvent& event) {
            if (event.state == AlarmState::High) ++high_events;
        }));

    system.start();
    ASSERT_TRUE(wait_until([&] { return system.status().alarm == AlarmState::High; }, 10s));
    ASSERT_TRUE(wait_until([&] { return system.status().heater_percent == 0.0; }, 5s));
    EXPECT_GE(high_events.load(), 1);
    system.stop();
}

TEST(ControlSystemTest, StopIsIdempotentAndDestructorStopsRunningSystem) {
    {
        ControlSystem system(fast_config());
        system.start();
        std::this_thread::sleep_for(50ms);
        system.stop();
        system.stop();
    }
    {
        ControlSystem system(fast_config());
        system.start();
        std::this_thread::sleep_for(50ms);
    }  // destructor must stop and join all threads
    SUCCEED();
}

TEST(ControlSystemTest, StatusCanBeReadWhileRunningFromManyThreads) {
    ControlSystem system(fast_config());
    system.start();
    std::vector<std::thread> readers;
    std::atomic<int> bad{0};
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            for (int j = 0; j < 500; ++j) {
                const auto status = system.status();
                if (status.heater_percent < 0.0 || status.heater_percent > 100.0) ++bad;
                system.set_setpoint(40.0 + (j % 20));
            }
        });
    }
    for (auto& t : readers) t.join();
    system.stop();
    EXPECT_EQ(bad.load(), 0);
}
