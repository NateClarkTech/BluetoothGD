#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace bluetooth {

template <typename T>
class ThreadSafeQueue {
public:
	void push(T p_item) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			queue.push(std::move(p_item));
		}
		condition.notify_one();
	}

	std::optional<T> try_pop() {
		std::lock_guard<std::mutex> lock(mutex);
		if (queue.empty()) {
			return std::nullopt;
		}
		T item = std::move(queue.front());
		queue.pop();
		return item;
	}

	std::optional<T> wait_pop(int p_timeout_ms = -1) {
		std::unique_lock<std::mutex> lock(mutex);
		if (p_timeout_ms < 0) {
			condition.wait(lock, [this] { return !queue.empty() || stopped; });
		} else {
			condition.wait_for(lock, std::chrono::milliseconds(p_timeout_ms), [this] {
				return !queue.empty() || stopped;
			});
		}
		if (queue.empty()) {
			return std::nullopt;
		}
		T item = std::move(queue.front());
		queue.pop();
		return item;
	}

	void stop() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopped = true;
		}
		condition.notify_all();
	}

	void reset() {
		std::lock_guard<std::mutex> lock(mutex);
		stopped = false;
		while (!queue.empty()) {
			queue.pop();
		}
	}

private:
	std::queue<T> queue;
	std::mutex mutex;
	std::condition_variable condition;
	bool stopped = false;
};

} // namespace bluetooth