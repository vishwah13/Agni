#include <gtest/gtest.h>
#include <ThreadPool.hpp>

#include <atomic>
#include <numeric>
#include <vector>

using namespace agni;

class ThreadPoolTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		ThreadPool::Initialize();
	}

	void TearDown() override
	{
		ThreadPool::Shutdown();
	}
};

// --- Initialization ---

TEST_F(ThreadPoolTest, InitializesWithThreads)
{
	EXPECT_GT(ThreadPool::GetThreadCount(), 0u);
}

// --- Task execution ---

TEST_F(ThreadPoolTest, SingleTaskExecutes)
{
	std::atomic<bool> executed {false};
	auto future = ThreadPool::AddTask([&]() { executed = true; });
	future.get();
	EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, MultipleTasksExecute)
{
	constexpr int NUM_TASKS = 100;
	std::atomic<int> counter {0};
	std::vector<std::future<void>> futures;

	for (int i = 0; i < NUM_TASKS; i++)
		futures.push_back(ThreadPool::AddTask([&]() { counter++; }));

	for (auto& f : futures)
		f.get();

	EXPECT_EQ(counter.load(), NUM_TASKS);
}

TEST_F(ThreadPoolTest, FutureReturnsAfterCompletion)
{
	std::atomic<int> value {0};
	auto future = ThreadPool::AddTask([&]() { value = 42; });
	future.get();
	EXPECT_EQ(value.load(), 42);
}

// --- Parallel loop ---

TEST_F(ThreadPoolTest, ParallelLoopProcessesAllItems)
{
	constexpr uint32_t SIZE = 1000;
	std::vector<std::atomic<int>> results(SIZE);
	for (auto& r : results)
		r = 0;

	ThreadPool::ParallelLoop(
	    [&](uint32_t start, uint32_t end)
	    {
		    for (uint32_t i = start; i < end; i++)
			    results[i] = static_cast<int>(i * 2);
	    },
	    SIZE);

	ThreadPool::Flush();

	for (uint32_t i = 0; i < SIZE; i++)
		EXPECT_EQ(results[i].load(), static_cast<int>(i * 2)) << "Mismatch at index " << i;
}

TEST_F(ThreadPoolTest, ParallelLoopZeroWorkIsNoOp)
{
	std::atomic<bool> called {false};
	ThreadPool::ParallelLoop(
	    [&]([[maybe_unused]] uint32_t start, [[maybe_unused]] uint32_t end)
	    { called = true; },
	    0);

	ThreadPool::Flush();
	EXPECT_FALSE(called);
}

// --- Flush ---

TEST_F(ThreadPoolTest, FlushWaitsForCompletion)
{
	std::atomic<int> counter {0};

	for (int i = 0; i < 50; i++)
		ThreadPool::AddTask([&]() { counter++; });

	ThreadPool::Flush();
	EXPECT_EQ(counter.load(), 50);
}

// --- Statistics ---

TEST_F(ThreadPoolTest, IdlePlusWorkingEqualsTotal)
{
	EXPECT_EQ(ThreadPool::GetIdleThreadCount() + ThreadPool::GetWorkingThreadCount(),
	          ThreadPool::GetThreadCount());
}
