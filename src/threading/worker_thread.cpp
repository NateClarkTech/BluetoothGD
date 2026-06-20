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

void WorkerThread::submit_pairing_response(const PairingUserResponse &p_response) {
	if (backend) {
		backend->submit_pairing_response(p_response);
	}
}

godot::Dictionary WorkerThread::get_capabilities() const {
	if (backend) {
		return backend->get_capabilities();
	}
	return godot::Dictionary();
}

bool WorkerThread::is_radio_on() const {
	if (backend) {
		return backend->is_radio_on();
	}
	return true;
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

	{
		BluetoothEvent initialized;
		initialized.type = EventType::BACKEND_INITIALIZED;
		event_queue.push(initialized);
	}

	while (running.load()) {
		auto command = command_queue.wait_pop(100);
		if (!command.has_value()) {
			backend->poll();
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
			backend->start_scan(p_command.scan_options);
			break;
		case CommandType::STOP_SCAN:
			backend->stop_scan();
			break;
		case CommandType::PAIR_DEVICE:
			backend->pair_device(p_command.address);
			break;
		case CommandType::PAIR_DEVICE_BY_ID:
			backend->pair_device_by_id(p_command.address);
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
		case CommandType::CONFIRM_PAIRING:
			backend->confirm_pairing(p_command.pin);
			break;
		case CommandType::REJECT_PAIRING:
			backend->reject_pairing();
			break;
		case CommandType::CANCEL_PAIRING:
			backend->cancel_pairing();
			break;
	}
}

} // namespace bluetooth