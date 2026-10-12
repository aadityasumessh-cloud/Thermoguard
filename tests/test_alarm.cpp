#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "plant/alarm.hpp"

using plant::AlarmEvent;
using plant::AlarmManager;
using plant::AlarmState;
using plant::CallbackAlarmListener;

namespace {
struct Recorder {
    std::vector<AlarmEvent> events;
    std::shared_ptr<CallbackAlarmListener> listener() {
        return std::make_shared<CallbackAlarmListener>(
            [this](const AlarmEvent& e) { events.push_back(e); });
    }
};
}  // namespace

TEST(AlarmTest, RejectsInvalidLimits) {
    EXPECT_THROW(AlarmManager(50.0, 50.0, 1.0), std::invalid_argument);
    EXPECT_THROW(AlarmManager(10.0, 50.0, -1.0), std::invalid_argument);
}

TEST(AlarmTest, StartsNormalAndStaysNormalInsideLimits) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    alarm.evaluate("s", 30.0);
    EXPECT_EQ(alarm.state(), AlarmState::Normal);
}

TEST(AlarmTest, RaisesHighAboveLimit) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    alarm.evaluate("s", 51.0);
    EXPECT_EQ(alarm.state(), AlarmState::High);
}

TEST(AlarmTest, RaisesLowBelowLimit) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    alarm.evaluate("s", 9.0);
    EXPECT_EQ(alarm.state(), AlarmState::Low);
}

TEST(AlarmTest, HysteresisPreventsChattering) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    alarm.evaluate("s", 51.0);
    ASSERT_EQ(alarm.state(), AlarmState::High);
    alarm.evaluate("s", 49.5);  // below the limit, but inside the hysteresis band
    EXPECT_EQ(alarm.state(), AlarmState::High);
    alarm.evaluate("s", 47.9);  // below limit - hysteresis: clears
    EXPECT_EQ(alarm.state(), AlarmState::Normal);
}

TEST(AlarmTest, LowAlarmAlsoUsesHysteresis) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    alarm.evaluate("s", 9.0);
    ASSERT_EQ(alarm.state(), AlarmState::Low);
    alarm.evaluate("s", 11.0);
    EXPECT_EQ(alarm.state(), AlarmState::Low);
    alarm.evaluate("s", 12.5);
    EXPECT_EQ(alarm.state(), AlarmState::Normal);
}

TEST(AlarmTest, ListenersAreNotifiedOnlyOnTransitions) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    Recorder recorder;
    alarm.add_listener(recorder.listener());

    alarm.evaluate("s", 30.0);  // no change
    alarm.evaluate("s", 60.0);  // -> High
    alarm.evaluate("s", 70.0);  // still High, no new event
    alarm.evaluate("s", 30.0);  // -> Normal

    ASSERT_EQ(recorder.events.size(), 2u);
    EXPECT_EQ(recorder.events[0].state, AlarmState::High);
    EXPECT_DOUBLE_EQ(recorder.events[0].value, 60.0);
    EXPECT_EQ(recorder.events[1].state, AlarmState::Normal);
}

TEST(AlarmTest, AllListenersAreNotified) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    Recorder first;
    Recorder second;
    alarm.add_listener(first.listener());
    alarm.add_listener(second.listener());
    alarm.evaluate("s", 99.0);
    EXPECT_EQ(first.events.size(), 1u);
    EXPECT_EQ(second.events.size(), 1u);
}

TEST(AlarmTest, ListenerMayCallBackIntoManager) {
    AlarmManager alarm(10.0, 50.0, 2.0);
    AlarmState seen = AlarmState::Normal;
    alarm.add_listener(std::make_shared<CallbackAlarmListener>(
        [&](const AlarmEvent&) { seen = alarm.state(); }));  // would deadlock if called under lock
    alarm.evaluate("s", 99.0);
    EXPECT_EQ(seen, AlarmState::High);
}
