#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "plant/scheduler.hpp"

using namespace std::chrono_literals;
using plant::PeriodicTask;

TEST(SchedulerTest, RejectsInvalidArguments) {
    EXPECT_THROW(PeriodicTask("t", 0ms, [] {}), std::invalid_argument);
    EXPECT_THROW(PeriodicTask("t", 10ms, nullptr), std::invalid_argument);
}

TEST(SchedulerTest, RunsRepeatedlyAtRoughlyTheRequestedPeriod) {
    std::atomic<int> count{0};
    PeriodicTask task("count", 10ms, [&] { ++count; });
    task.start();
    std::this_thread::sleep_for(300ms);
    task.stop();
    // ~30 expected; the bounds are wide so slow CI machines and sanitizers pass.
    EXPECT_GE(count.load(), 8);
    EXPECT_LE(count.load(), 40);
    EXPECT_EQ(task.iterations(), static_cast<std::uint64_t>(count.load()));
}

TEST(SchedulerTest, StopReturnsPromptlyEvenWithLongPeriod) {
    PeriodicTask task("slow", 10s, [] {});
    task.start();
    std::this_thread::sleep_for(50ms);
    const auto start = std::chrono::steady_clock::now();
    task.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST(SchedulerTest, StopWithoutStartIsSafe) {
    PeriodicTask task("idle", 10ms, [] {});
    task.stop();
    SUCCEED();
}

TEST(SchedulerTest, ExceptionInWorkIsCountedAndTaskKeepsRunning) {
    PeriodicTask task("throws", 5ms, [] { throw std::runtime_error("boom"); });
    task.start();
    std::this_thread::sleep_for(100ms);
    task.stop();
    EXPECT_GT(task.errors(), 1u);
    EXPECT_EQ(task.errors(), task.iterations());
}

TEST(SchedulerTest, RecordsJitterAndTolertesRealtimeRequestFailing) {
    PeriodicTask task("rt", 10ms, [] {}, /*realtime_priority=*/10);
    task.start();
    std::this_thread::sleep_for(100ms);
    task.stop();
    EXPECT_GE(task.max_jitter().count(), 0);
    // Without privileges SCHED_FIFO is refused; either outcome is acceptable,
    // the task must simply keep running.
    EXPECT_GT(task.iterations(), 0u);
}

TEST(SchedulerTest, CanBeRestartedAfterStop) {
    std::atomic<int> count{0};
    PeriodicTask task("restart", 5ms, [&] { ++count; });
    task.start();
    std::this_thread::sleep_for(50ms);
    task.stop();
    const int after_first = count.load();
    task.start();
    std::this_thread::sleep_for(50ms);
    task.stop();
    EXPECT_GT(count.load(), after_first);
}
