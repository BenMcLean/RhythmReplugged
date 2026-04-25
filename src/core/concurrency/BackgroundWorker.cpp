#include "core/concurrency/BackgroundWorker.h"

#include <utility>

namespace rhythmreplugged::core
{
	size_t BackgroundWorker::automatic_thread_count(size_t job_count_hint)
	{
		size_t thread_count = std::thread::hardware_concurrency();
		if (thread_count == 0)
			thread_count = 1;

		if (thread_count > 2)
			--thread_count;

		thread_count = std::min(thread_count, static_cast<size_t>(4));
		if (job_count_hint > 0)
			thread_count = std::min(thread_count, job_count_hint);

		return std::max(thread_count, static_cast<size_t>(1));
	}

	BackgroundWorker::~BackgroundWorker()
	{
		stop();
	}

	void BackgroundWorker::start(size_t thread_count)
	{
		if (thread_count == 0)
			thread_count = automatic_thread_count();

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
			std::queue<std::function<void()>> empty_high_priority_jobs;
			high_priority_jobs_.swap(empty_high_priority_jobs);
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
		std::queue<std::function<void()>> empty_high_priority_jobs;
		high_priority_jobs_.swap(empty_high_priority_jobs);
		std::queue<std::function<void()>> empty_jobs;
		jobs_.swap(empty_jobs);
	}

	bool BackgroundWorker::enqueue(std::function<void()> job, JobPriority priority)
	{
		if (!job)
			return false;

		{
			std::scoped_lock lock(mutex_);
			if (threads_.empty() || stop_requested_)
				return false;

			if (priority == JobPriority::High)
				high_priority_jobs_.push(std::move(job));
			else
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
					return stop_requested_ || !high_priority_jobs_.empty() || !jobs_.empty();
				});

				if (stop_requested_ && high_priority_jobs_.empty() && jobs_.empty())
					return;

				if (!high_priority_jobs_.empty())
				{
					job = std::move(high_priority_jobs_.front());
					high_priority_jobs_.pop();
				}
				else
				{
					job = std::move(jobs_.front());
					jobs_.pop();
				}
			}

			job();
		}
	}
}
