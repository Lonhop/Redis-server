#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <future>
#include <functional>

#include "core/thread_pool.h"

namespace redis::core::test {

    class ThreadPoolTest : public ::testing::Test {
    protected:
        void SetUp() override {}
        void TearDown() override {}
    };
    TEST_F(ThreadPoolTest, CreateAndDestroy) {
        ThreadPool pool(2);
        EXPECT_TRUE(pool.isRunning());
        EXPECT_EQ(pool.threadCount(), 2);
    }
    TEST_F(ThreadPoolTest, EnqueueSimpleTask) {
        ThreadPool pool(2);
        std::atomic<int> counter{0};
        pool.enqueue([&counter]() { counter++; });
        pool.waitAll();
        EXPECT_EQ(counter.load(), 1);
    }
    TEST_F(ThreadPoolTest, EnqueueMultipleTasks) {
        ThreadPool pool(4);
        std::atomic<int> counter{0};
        const int numTasks = 100;
        for (int i = 0; i < numTasks; ++i) {
            pool.enqueue([&counter]() {
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            });
        }
        pool.waitAll();
        EXPECT_EQ(counter.load(), numTasks);
    }
    TEST_F(ThreadPoolTest, EnqueueWithFuture) {
        ThreadPool pool(2);
        auto future = pool.enqueue([](int a, int b) { return a + b; }, 5, 3);
        int result = future.get();
        EXPECT_EQ(result, 8);
    }
    TEST_F(ThreadPoolTest, EnqueueVoidFuture) {
        ThreadPool pool(2);
        std::atomic<bool> done{false};
        auto future = pool.enqueue([&done]() { done = true; });
        future.get();
        EXPECT_TRUE(done);
    }
    TEST_F(ThreadPoolTest, PriorityOrder) {
        ThreadPool pool(1);
        pool.pause();
        std::vector<int> order;
        pool.enqueue(std::function<void()>([&order]() { order.push_back(1); }), TaskPriority::LOW);
        pool.enqueue(std::function<void()>([&order]() { order.push_back(2); }), TaskPriority::NORMAL);
        pool.enqueue(std::function<void()>([&order]() { order.push_back(3); }), TaskPriority::HIGH);
        pool.enqueue(std::function<void()>([&order]() { order.push_back(4); }), TaskPriority::CRITICAL);
        pool.resume();
        pool.waitAll();
        auto it_critical = std::find(order.begin(), order.end(), 4);
        auto it_high = std::find(order.begin(), order.end(), 3);
        auto it_normal = std::find(order.begin(), order.end(), 2);
        auto it_low = std::find(order.begin(), order.end(), 1);
        EXPECT_LT(it_critical - order.begin(), it_low - order.begin());
        EXPECT_LT(it_high - order.begin(), it_normal - order.begin());
    }
    TEST_F(ThreadPoolTest, PauseResume) {
        ThreadPool pool(2);
        std::atomic<int> counter{0};
        pool.pause();
        pool.enqueue([&counter]() { counter++; });
        pool.enqueue([&counter]() { counter++; });
        pool.enqueue([&counter]() { counter++; });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_EQ(counter.load(), 0);
        pool.resume();
        pool.waitAll();
        EXPECT_EQ(counter.load(), 3);
    }
    TEST_F(ThreadPoolTest, ClearQueue) {
        ThreadPool pool(1);
        std::atomic<int> counter{0};
        std::atomic<bool> started{false};
        pool.enqueue([&]() {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter++;
        });
        while (!started) std::this_thread::yield();
        auto pending = pool.clear();
        pool.waitAll();
        EXPECT_EQ(counter.load(), 1);
        EXPECT_EQ(pending.size(), 0);
    }
    TEST_F(ThreadPoolTest, WaitAll) {
        ThreadPool pool(4);
        std::atomic<int> counter{0};
        const int numTasks = 20;
        for (int i = 0; i < numTasks; ++i) {
            pool.enqueue([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                counter++;
            });
        }
        pool.waitAll();
        EXPECT_EQ(counter.load(), numTasks);
    }
    TEST_F(ThreadPoolTest, WaitAllForTimeout) {
        ThreadPool pool(2);
        std::atomic<int> counter{0};
        pool.enqueue([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            counter++;
        });
        bool completed = pool.waitAllFor(std::chrono::milliseconds(50));
        EXPECT_FALSE(completed);
        EXPECT_EQ(counter.load(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed = pool.waitAllFor(std::chrono::milliseconds(10));
        EXPECT_TRUE(completed);
        EXPECT_EQ(counter.load(), 1);
    }
    TEST_F(ThreadPoolTest, Resize) {
        ThreadPool pool(2);
        EXPECT_EQ(pool.threadCount(), 2);
        EXPECT_TRUE(pool.resize(4));
        EXPECT_EQ(pool.threadCount(), 4);
        EXPECT_TRUE(pool.resize(2));
        EXPECT_EQ(pool.threadCount(), 2);
    }
    TEST_F(ThreadPoolTest, ResizeInvalid) {
        ThreadPool pool(2);
        EXPECT_FALSE(pool.resize(0));
        EXPECT_FALSE(pool.resize(100));
    }
    TEST_F(ThreadPoolTest, Stats) {
        ThreadPool pool(2);
        auto stats = pool.getStats();
        EXPECT_EQ(stats.active_threads, 0);
        EXPECT_EQ(stats.pending_tasks, 0);
        pool.enqueue([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
        pool.enqueue([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        stats = pool.getStats();
        EXPECT_GE(stats.active_threads, 1);
        EXPECT_LE(stats.active_threads, 2);
        pool.waitAll();
        stats = pool.getStats();
        EXPECT_EQ(stats.active_threads, 0);
        EXPECT_EQ(stats.pending_tasks, 0);
        EXPECT_EQ(stats.total_tasks_completed, 2);
    }
    TEST_F(ThreadPoolTest, ManyTasks) {
        ThreadPool pool(8);
        const int numTasks = 10000;
        std::atomic<int> counter{0};
        for (int i = 0; i < numTasks; ++i) {
            pool.enqueue([&counter]() { counter++; });
        }
        pool.waitAll();
        EXPECT_EQ(counter.load(), numTasks);
    }
    TEST_F(ThreadPoolTest, MoveSemantics) {
        ThreadPool pool1(2);
        std::atomic<int> counter{0};

        pool1.enqueue([&counter]() { counter++; });

        ThreadPool pool2 = std::move(pool1);
        EXPECT_EQ(pool1.threadCount(), 0);
        EXPECT_EQ(pool2.threadCount(), 2);

        pool2.enqueue([&counter]() { counter++; });
        pool2.waitAll();
        EXPECT_EQ(counter.load(), 2);
    }
    TEST_F(ThreadPoolTest, ConcurrentEnqueue) {
        ThreadPool pool(4);
        std::atomic<int> counter{0};
        const int numTasks = 1000;
        std::vector<std::thread> producers;
        for (int t = 0; t < 4; ++t) {
            producers.emplace_back([&]() {
                for (int i = 0; i < numTasks / 4; ++i) {
                    pool.enqueue([&counter]() { counter++; });
                }
            });
        }
        for (auto& th : producers) th.join();

        pool.waitAll();
        EXPECT_EQ(counter.load(), numTasks);
    }

}