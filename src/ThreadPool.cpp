#include "ThreadPool.hpp"
#include <AgniLog.hpp>

#include <algorithm>
#include <iostream>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace agni
{

	// Static state
	static std::vector<std::thread> threads;
	static std::deque<Task>         tasks;
	static std::mutex               taskMutex;
	static std::condition_variable  taskCondition;
	static std::condition_variable  idleCondition;
	static std::atomic<uint32_t>    workingCount {0};
	static std::atomic<uint32_t>    pendingCount {0};
	static std::atomic<bool>        stopping {false};
	static uint32_t                 threadCount {0};
	static thread_local bool        isWorkerThread = false;

	void ThreadPool::Initialize()
	{
		// Calculate thread count: use half of hardware concurrency
		// This is good for mixed CPU/GPU workloads where we don't want to
		// starve the main thread
		uint32_t hwThreads = std::thread::hardware_concurrency();
		threadCount        = std::max(1u, hwThreads / 2);

		stopping = false;

		// Spawn worker threads
		threads.reserve(threadCount);
		for (uint32_t i = 0; i < threadCount; i++)
		{
			threads.emplace_back(ThreadLoop);
		}

		AGNI_PRINT("[ThreadPool] Initialized with {} worker threads\n",
		           threadCount);
	}

	void ThreadPool::Shutdown()
	{
		// Signal shutdown
		{
			std::lock_guard<std::mutex> lock(taskMutex);
			stopping = true;
		}
		taskCondition.notify_all();

		// Wait for all threads to finish
		for (auto& thread : threads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
		threads.clear();

		// Clear any remaining tasks
		{
			std::lock_guard<std::mutex> lock(taskMutex);
			tasks.clear();
			pendingCount = 0;
		}

		AGNI_PRINT("[ThreadPool] Shutdown complete\n");
	}

	std::future<void> ThreadPool::AddTask(Task&& task)
	{
		auto packagedTask =
		std::make_shared<std::packaged_task<void()>>(std::move(task));
		std::future<void> future = packagedTask->get_future();

		{
			std::lock_guard<std::mutex> lock(taskMutex);
			tasks.emplace_back([packagedTask]() { (*packagedTask)(); });
			pendingCount++;
		}

		taskCondition.notify_one();
		return future;
	}

	void ThreadPool::ParallelLoop(
	std::function<void(uint32_t start, uint32_t end)>&& function,
	uint32_t                                            workTotal)
	{
		if (workTotal == 0)
		{
			return;
		}

		// If we're already on a worker thread, just execute sequentially to
		// avoid deadlock
		if (isWorkerThread)
		{
			function(0, workTotal);
			return;
		}

		// If thread pool not initialized or only 1 item, run on this thread
		if (threadCount == 0 || workTotal == 1)
		{
			function(0, workTotal);
			return;
		}

		// Divide work among threads
		uint32_t numTasks    = std::min(threadCount, workTotal);
		uint32_t workPerTask = workTotal / numTasks;
		uint32_t remainder   = workTotal % numTasks;

		std::atomic<uint32_t>   completedTasks {0};
		std::mutex              completionMutex;
		std::condition_variable completionCondition;

		uint32_t currentStart = 0;
		for (uint32_t i = 0; i < numTasks; i++)
		{
			uint32_t taskWork = workPerTask + (i < remainder ? 1 : 0);
			uint32_t taskEnd  = currentStart + taskWork;

			// Capture by value for the lambda
			uint32_t start = currentStart;
			uint32_t end   = taskEnd;

			AddTask(
			[&function,
			 start,
			 end,
			 &completedTasks,
			 &completionMutex,
			 &completionCondition]()
			{
				function(start, end);

				// Signal completion
				completedTasks++;
				completionCondition.notify_one();
			});

			currentStart = taskEnd;
		}

		// Wait for all tasks to complete
		std::unique_lock<std::mutex> lock(completionMutex);
		completionCondition.wait(lock,
		                         [&]() { return completedTasks >= numTasks; });
	}

	void ThreadPool::Flush(bool removeQueued)
	{
		if (removeQueued)
		{
			std::lock_guard<std::mutex> lock(taskMutex);
			tasks.clear();
			pendingCount = 0;
		}

		// Wait until no tasks are pending and no workers are active
		std::unique_lock<std::mutex> lock(taskMutex);
		idleCondition.wait(
		lock, []() { return pendingCount == 0 && workingCount == 0; });
	}

	uint32_t ThreadPool::GetThreadCount()
	{
		return threadCount;
	}

	uint32_t ThreadPool::GetWorkingThreadCount()
	{
		return workingCount;
	}

	uint32_t ThreadPool::GetIdleThreadCount()
	{
		return threadCount - workingCount;
	}

	bool ThreadPool::AreTasksRunning()
	{
		return pendingCount > 0 || workingCount > 0;
	}

	void ThreadPool::ThreadLoop()
	{
		isWorkerThread = true;

#ifdef TRACY_ENABLE
		tracy::SetThreadName("ThreadPool Worker");
#endif

		while (true)
		{
			Task task;

			{
				std::unique_lock<std::mutex> lock(taskMutex);

				// Wait for a task or shutdown signal
				taskCondition.wait(lock,
				                   []() { return !tasks.empty() || stopping; });

				// Check for shutdown
				if (stopping && tasks.empty())
				{
					break;
				}

				// Get next task
				if (!tasks.empty())
				{
					task = std::move(tasks.front());
					tasks.pop_front();
					pendingCount--;
					workingCount++;
				}
			}

			// Execute task outside of lock
			if (task)
			{
#ifdef TRACY_ENABLE
				ZoneScopedN("ThreadPool Task");
#endif
				task();

				workingCount--;
				idleCondition.notify_all();
			}
		}
	}

} // namespace agni
