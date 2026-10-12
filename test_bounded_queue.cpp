#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "plant/bounded_queue.hpp"

using namespace std::chrono_literals;
using plant::BoundedQueue;

TEST(BoundedQueueTest, RejectsZeroCapacity) {
    EXPECT_THROW(BoundedQueue<int>(0), std::invalid_argument);
}

TEST(BoundedQueueTest, PreservesFifoOrder) {
    BoundedQueue<int> queue(4);
    queue.push(1);
    queue.push(2);
    queue.push(3);
    EXPECT_EQ(queue.pop().value(), 1);
    EXPECT_EQ(queue.pop().value(), 2);
    EXPECT_EQ(queue.pop().value(), 3);
}

TEST(BoundedQueueTest, TryPushFailsWhenFull) {
    BoundedQueue<int> queue(2);
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_FALSE(queue.try_push(3));
    EXPECT_EQ(queue.size(), 2u);
}

TEST(BoundedQueueTest, PopForTimesOutWhenEmpty) {
    BoundedQueue<int> queue(2);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(queue.pop_for(50ms).has_value());
    EXPECT_GE(std::chrono::steady_clock::now() - start, 40ms);
}

TEST(BoundedQueueTest, PopBlocksUntilItemArrives) {
    BoundedQueue<int> queue(2);
    std::thread producer([&] {
        std::this_thread::sleep_for(50ms);
        queue.push(42);
    });
    EXPECT_EQ(queue.pop().value(), 42);
    producer.join();
}

TEST(BoundedQueueTest, PushBlocksUntilSpaceIsFreed) {
    BoundedQueue<int> queue(1);
    queue.push(1);
    std::atomic<bool> second_pushed{false};
    std::thread producer([&] {
        queue.push(2);  // blocks: queue is full
        second_pushed = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(second_pushed.load());
    EXPECT_EQ(queue.pop().value(), 1);
    producer.join();
    EXPECT_TRUE(second_pushed.load());
    EXPECT_EQ(queue.pop().value(), 2);
}

TEST(BoundedQueueTest, CloseDrainsRemainingItemsThenReturnsNullopt) {
    BoundedQueue<int> queue(4);
    queue.push(1);
    queue.push(2);
    queue.close();
    EXPECT_FALSE(queue.push(3));  // closed: pushes are rejected
    EXPECT_EQ(queue.pop().value(), 1);
    EXPECT_EQ(queue.pop().value(), 2);
    EXPECT_FALSE(queue.pop().has_value());
}

TEST(BoundedQueueTest, CloseWakesBlockedConsumer) {
    BoundedQueue<int> queue(2);
    std::thread consumer([&] { EXPECT_FALSE(queue.pop().has_value()); });
    std::this_thread::sleep_for(50ms);
    queue.close();
    consumer.join();
}

TEST(BoundedQueueTest, WorksWithMoveOnlyTypes) {
    BoundedQueue<std::unique_ptr<int>> queue(2);
    queue.push(std::make_unique<int>(7));
    auto item = queue.pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(**item, 7);
}

TEST(BoundedQueueTest, MultiProducerMultiConsumerDeliversEverythingOnce) {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kItemsPerProducer = 5000;

    BoundedQueue<int> queue(16);
    std::atomic<long long> sum{0};
    std::atomic<int> count{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (auto item = queue.pop()) {
                sum += *item;
                ++count;
            }
        });
    }
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 1; i <= kItemsPerProducer; ++i) {
                queue.push(i);
            }
        });
    }
    for (auto& t : producers) t.join();
    queue.close();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(count.load(), kProducers * kItemsPerProducer);
    const long long expected_per_producer =
        static_cast<long long>(kItemsPerProducer) * (kItemsPerProducer + 1) / 2;
    EXPECT_EQ(sum.load(), expected_per_producer * kProducers);
}
