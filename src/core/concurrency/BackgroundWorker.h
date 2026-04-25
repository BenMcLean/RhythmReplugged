#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace rhythmreplugged
{
	class BackgroundWorker
	{
	public:
		BackgroundWorker() = default;
		~BackgroundWorker();

		BackgroundWorker(const BackgroundWorker &) = delete;
		BackgroundWorker &operator=(const BackgroundWorker &) = delete;

		void start(size_t thread_count = 1);
		void stop();
		void clear_pending();
		bool enqueue(std::function<void()> job);
		bool running() const;

	private:
		void worker_main();

		mutable std::mutex mutex_;
		std::condition_variable condition_;
		std::queue<std::function<void()>> jobs_;
		std::vector<std::thread> threads_;
		bool stop_requested_ = false;
	};
}
