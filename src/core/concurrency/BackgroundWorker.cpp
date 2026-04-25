#include "core/concurrency/BackgroundWorker.h"

#include <utility>

namespace rhythmreplugged
{
	BackgroundWorker::~BackgroundWorker()
	{
		stop();
	}

	void BackgroundWorker::start(size_t thread_count)
	{
		if (thread_count == 0)
			thread_count = 1;

		std::scoped_lock lock(mutex_);
		if (!threads_.empty())
			return;

		stop_requested_ = false;
		threads_.reserve(thread_count);
		for (size_t index = 0; index < thread_count; ++index)
			threads_.emplace_back([this]() { worker_main(); });
	}

	void BackgroundWorker::stop()
	{
		std::vector<std::thread> threads;
		{
			std::scoped_lock lock(mutex_);
			if (threads_.empty())
				return;

			stop_requested_ = true;
			std::queue<std::function<void()>> empty_jobs;
			jobs_.swap(empty_jobs);
			threads.swap(threads_);
		}

		condition_.notify_all();
		for (std::thread &thread : threads)
		{
			if (thread.joinable())
				thread.join();
		}

		std::scoped_lock lock(mutex_);
		stop_requested_ = false;
	}

	void BackgroundWorker::clear_pending()
	{
		std::scoped_lock lock(mutex_);
		std::queue<std::function<void()>> empty_jobs;
		jobs_.swap(empty_jobs);
	}

	bool BackgroundWorker::enqueue(std::function<void()> job)
	{
		if (!job)
			return false;

		{
			std::scoped_lock lock(mutex_);
			if (threads_.empty() || stop_requested_)
				return false;

			jobs_.push(std::move(job));
		}

		condition_.notify_one();
		return true;
	}

	bool BackgroundWorker::running() const
	{
		std::scoped_lock lock(mutex_);
		return !threads_.empty();
	}

	void BackgroundWorker::worker_main()
	{
		for (;;)
		{
			std::function<void()> job;
			{
				std::unique_lock lock(mutex_);
				condition_.wait(lock, [this]()
				{
					return stop_requested_ || !jobs_.empty();
				});

				if (stop_requested_ && jobs_.empty())
					return;

				job = std::move(jobs_.front());
				jobs_.pop();
			}

			job();
		}
	}
}
