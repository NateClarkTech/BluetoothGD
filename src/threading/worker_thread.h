#pragma once

#include "../backend/bluetooth_backend.h"
#include "../backend/bluetooth_commands.h"
#include "../backend/bluetooth_events.h"
#include "thread_safe_queue.h"

#include <godot_cpp/variant/dictionary.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace bluetooth {

class WorkerThread {
public:
	WorkerThread();
	~WorkerThread();

	bool start();
	void stop();

	void enqueue_command(const BluetoothCommand &p_command);
	void submit_pairing_response(const PairingUserResponse &p_response);
	godot::Dictionary get_capabilities() const;
	bool is_radio_on() const;
	ThreadSafeQueue<BluetoothEvent> &get_event_queue();

private:
	void thread_main();
	void process_command(const BluetoothCommand &p_command);
	void emit_event(const BluetoothEvent &p_event);

	std::unique_ptr<BluetoothBackend> backend;
	ThreadSafeQueue<BluetoothCommand> command_queue;
	ThreadSafeQueue<BluetoothEvent> event_queue;
	std::thread thread;
	std::atomic<bool> running{ false };
};

} // namespace bluetooth