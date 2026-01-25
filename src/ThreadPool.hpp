#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace agni
{

using Task = std::function<void()>;

class ThreadPool
{
public:
	// Initialize thread pool with worker threads
	static void Initialize();

	// Shutdown thread pool and wait for all tasks to complete
	static void Shutdown();

	// Add a task to the queue, returns future for completion tracking
	static std::future<void> AddTask(Task&& task);

	// Parallel loop - distributes work across threads with range [start, end)
	// The function receives (startIndex, endIndex) and should process that range
	static void ParallelLoop(std::function<void(uint32_t start, uint32_t end)>&& function,
	                         uint32_t                                            workTotal);

	// Wait for all queued tasks to complete
	// If removeQueued is true, clears pending tasks before waiting
	static void Flush(bool removeQueued = false);

	// Statistics
	static uint32_t GetThreadCount();
	static uint32_t GetWorkingThreadCount();
	static uint32_t GetIdleThreadCount();
	static bool     AreTasksRunning();

private:
	// Worker thread function
	static void ThreadLoop();
};

} // namespace agni
