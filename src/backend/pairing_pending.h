#pragma once

#include <godot_cpp/variant/string.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace bluetooth {

struct PairingUserResponse {
	bool accepted = false;
	godot::String pin;
};

class PairingPendingState {
public:
	void begin(const godot::String &p_address, const godot::String &p_kind, const godot::String &p_display_pin = "") {
		std::lock_guard<std::mutex> lock(mutex);
		address = p_address;
		kind = p_kind;
		display_pin = p_display_pin;
		response.reset();
	}

	bool wait_for_response(int p_timeout_ms, PairingUserResponse &p_out) {
		std::unique_lock<std::mutex> lock(mutex);
		const bool ready = cv.wait_for(lock, std::chrono::milliseconds(p_timeout_ms), [this] {
			return response.has_value();
		});
		if (!ready || !response.has_value()) {
			return false;
		}
		p_out = *response;
		return true;
	}

	void submit(const PairingUserResponse &p_response) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			response = p_response;
		}
		cv.notify_all();
	}

	void reset() {
		std::lock_guard<std::mutex> lock(mutex);
		address = "";
		kind = "";
		display_pin = "";
		response.reset();
	}

	godot::String get_address() const {
		std::lock_guard<std::mutex> lock(mutex);
		return address;
	}

	godot::String get_kind() const {
		std::lock_guard<std::mutex> lock(mutex);
		return kind;
	}

	godot::String get_display_pin() const {
		std::lock_guard<std::mutex> lock(mutex);
		return display_pin;
	}

private:
	mutable std::mutex mutex;
	std::condition_variable cv;
	std::optional<PairingUserResponse> response;
	godot::String address;
	godot::String kind;
	godot::String display_pin;
};

} // namespace bluetooth