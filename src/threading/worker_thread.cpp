#include "worker_thread.h"

namespace bluetooth {

WorkerThread::WorkerThread() {
	backend.reset(create_platform_backend());
}

WorkerThread::~WorkerThread() {
	stop();
}

bool WorkerThread::start() {
	if (running.load()) {
		return true;
	}

	command_queue.reset();
	event_queue.reset();
	running.store(true);
	thread = std::thread(&WorkerThread::thread_main, this);
	return true;
}

void WorkerThread::stop() {
	if (!running.load()) {
		return;
	}

	command_queue.push({ CommandType::SHUTDOWN });
	running.store(false);
	command_queue.stop();

	if (thread.joinable()) {
		thread.join();
	}
}

void WorkerThread::enqueue_command(const BluetoothCommand &p_command) {
	command_queue.push(p_command);
}

ThreadSafeQueue<BluetoothEvent> &WorkerThread::get_event_queue() {
	return event_queue;
}

void WorkerThread::thread_main() {
	if (!backend) {
		BluetoothEvent event;
		event.type = EventType::ERROR_OCCURRED;
		event.operation = "initialize";
		event.message = "initialize failed: create_platform_backend() returned null (no platform backend compiled for this OS).";
		event_queue.push(event);
		return;
	}

	backend->on_event = [this](const BluetoothEvent &p_event) {
		event_queue.push(p_event);
	};

	if (!backend->initialize()) {
		BluetoothEvent event;
		event.type = EventType::ERROR_OCCURRED;
		event.operation = "initialize";
		event.message = "initialize failed: backend->initialize() returned false. Check earlier error_occurred events for the platform-specific cause.";
		event_queue.push(event);
		return;
	}

	while (running.load()) {
		auto command = command_queue.wait_pop(100);
		if (!command.has_value()) {
			continue;
		}
		process_command(*command);
	}

	backend->shutdown();
}

void WorkerThread::process_command(const BluetoothCommand &p_command) {
	if (!backend) {
		return;
	}

	switch (p_command.type) {
		case CommandType::INITIALIZE:
			break;
		case CommandType::SHUTDOWN:
			running.store(false);
			break;
		case CommandType::START_SCAN:
			backend->start_scan();
			break;
		case CommandType::STOP_SCAN:
			backend->stop_scan();
			break;
		case CommandType::PAIR_DEVICE:
			backend->pair_device(p_command.address);
			break;
		case CommandType::UNPAIR_DEVICE:
			backend->unpair_device(p_command.address);
			break;
		case CommandType::CONNECT_DEVICE:
			backend->connect_device(p_command.address);
			break;
		case CommandType::DISCONNECT_DEVICE:
			backend->disconnect_device(p_command.address);
			break;
		case CommandType::REFRESH_PAIRED_DEVICES:
			backend->refresh_paired_devices();
			break;
	}
}

} // namespace bluetooth