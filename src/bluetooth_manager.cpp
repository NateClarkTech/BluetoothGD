#include "bluetooth_manager.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void BluetoothManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start_scan"), &BluetoothManager::start_scan);
	ClassDB::bind_method(D_METHOD("stop_scan"), &BluetoothManager::stop_scan);
	ClassDB::bind_method(D_METHOD("get_discovered_devices"), &BluetoothManager::get_discovered_devices);

	ClassDB::bind_method(D_METHOD("pair_device", "address"), &BluetoothManager::pair_device);
	ClassDB::bind_method(D_METHOD("unpair_device", "address"), &BluetoothManager::unpair_device);
	ClassDB::bind_method(D_METHOD("refresh_paired_devices"), &BluetoothManager::refresh_paired_devices);
	ClassDB::bind_method(D_METHOD("get_paired_devices"), &BluetoothManager::get_paired_devices);
	ClassDB::bind_method(D_METHOD("is_paired", "address"), &BluetoothManager::is_paired);

	ClassDB::bind_method(D_METHOD("connect_device", "address"), &BluetoothManager::connect_device);
	ClassDB::bind_method(D_METHOD("disconnect_device", "address"), &BluetoothManager::disconnect_device);
	ClassDB::bind_method(D_METHOD("is_connected", "address"), &BluetoothManager::is_connected);

	ClassDB::bind_method(D_METHOD("normalize_address", "address"), &BluetoothManager::normalize_address);
	ClassDB::bind_method(D_METHOD("is_valid_bluetooth_address", "address"), &BluetoothManager::is_valid_bluetooth_address);
	ClassDB::bind_method(D_METHOD("can_unpair_while_connected"), &BluetoothManager::can_unpair_while_connected);
	ClassDB::bind_method(D_METHOD("is_bluetooth_available"), &BluetoothManager::is_bluetooth_available);
	ClassDB::bind_method(D_METHOD("get_platform_name"), &BluetoothManager::get_platform_name);

	ADD_SIGNAL(MethodInfo("device_found", PropertyInfo(Variant::DICTIONARY, "device_info")));
	ADD_SIGNAL(MethodInfo("device_removed", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("devices_refreshed"));
	ADD_SIGNAL(MethodInfo("bluetooth_ready"));
	ADD_SIGNAL(MethodInfo("scan_started"));
	ADD_SIGNAL(MethodInfo("scan_stopped"));
	ADD_SIGNAL(MethodInfo("pairing_started", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("pairing_succeeded", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("pairing_failed", PropertyInfo(Variant::STRING, "address"), PropertyInfo(Variant::STRING, "error")));
	ADD_SIGNAL(MethodInfo("connection_changed", PropertyInfo(Variant::STRING, "address"), PropertyInfo(Variant::BOOL, "connected"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("error_occurred", PropertyInfo(Variant::STRING, "operation"), PropertyInfo(Variant::STRING, "message")));
}

void BluetoothManager::_ready() {
	backend_available = worker.start();
	enqueue_command(bluetooth::CommandType::INITIALIZE);
	enqueue_command(bluetooth::CommandType::REFRESH_PAIRED_DEVICES);
}

void BluetoothManager::_exit_tree() {
	worker.stop();
}

void BluetoothManager::_process(double p_delta) {
	(void)p_delta;
	auto &event_queue = worker.get_event_queue();
	while (true) {
		auto event = event_queue.try_pop();
		if (!event.has_value()) {
			break;
		}
		handle_event(*event);
	}
}

void BluetoothManager::enqueue_command(bluetooth::CommandType p_type, const String &p_address) {
	bluetooth::BluetoothCommand command;
	command.type = p_type;
	command.address = p_address;
	worker.enqueue_command(command);
}

String BluetoothManager::resolve_command_address(const String &p_address) const {
	if (bluetooth::is_valid_bluetooth_address(p_address)) {
		return bluetooth::normalize_address(p_address);
	}
	return p_address;
}

void BluetoothManager::maybe_emit_bluetooth_ready() {
	if (bluetooth_ready_emitted || !backend_available) {
		return;
	}
	bluetooth_ready_emitted = true;
	emit_signal("bluetooth_ready");
}

void BluetoothManager::handle_event(const bluetooth::BluetoothEvent &p_event) {
	switch (p_event.type) {
		case bluetooth::EventType::SCAN_STARTED:
			emit_signal("scan_started");
			break;
		case bluetooth::EventType::SCAN_STOPPED:
			emit_signal("scan_stopped");
			break;
		case bluetooth::EventType::DEVICE_FOUND:
			upsert_device(p_event.device);
			emit_signal("device_found", p_event.device.to_dictionary());
			break;
		case bluetooth::EventType::DEVICE_REMOVED: {
			const String address = p_event.address.is_empty() ? p_event.device.address : p_event.address;
			remove_device_for_address(address);
			break;
		}
		case bluetooth::EventType::PAIRING_STARTED:
			emit_signal("pairing_started", p_event.address);
			break;
		case bluetooth::EventType::PAIRING_SUCCEEDED:
			update_devices_for_address(p_event.address, [](bluetooth::DeviceInfo &p_info) {
				p_info.paired = true;
			});
			emit_device_updates_for_address(p_event.address);
			emit_signal("pairing_succeeded", p_event.address);
			break;
		case bluetooth::EventType::PAIRING_FAILED:
			emit_signal("pairing_failed", p_event.address, p_event.message);
			break;
		case bluetooth::EventType::CONNECTION_CHANGED:
			update_devices_for_address(p_event.address, [&p_event](bluetooth::DeviceInfo &p_info) {
				p_info.connected = p_event.connected;
			});
			emit_device_updates_for_address(p_event.address);
			emit_signal("connection_changed", p_event.address, p_event.connected, p_event.message);
			break;
		case bluetooth::EventType::PAIRED_DEVICES_UPDATED:
			sync_devices_from_snapshot(p_event.devices);
			emit_signal("devices_refreshed");
			maybe_emit_bluetooth_ready();
			break;
		case bluetooth::EventType::ERROR_OCCURRED:
			if (p_event.operation == "initialize") {
				backend_available = false;
			}
			emit_signal("error_occurred", p_event.operation, p_event.message);
			break;
	}
}

String BluetoothManager::canonical_device_key(const bluetooth::DeviceInfo &p_device) const {
	if (bluetooth::is_valid_bluetooth_address(p_device.address)) {
		return bluetooth::normalize_address(p_device.address);
	}
	if (!p_device.device_id.is_empty()) {
		return p_device.device_id;
	}
	return p_device.address;
}

void BluetoothManager::remove_device_for_address(const String &p_address) {
	PackedStringArray keys_to_remove;
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (bluetooth::addresses_match(item.key, p_address) || bluetooth::addresses_match(item.value.address, p_address)) {
			keys_to_remove.append(item.key);
		}
	}
	if (keys_to_remove.is_empty()) {
		return;
	}

	String signal_address = p_address;
	for (int i = 0; i < keys_to_remove.size(); i++) {
		if (discovered_devices.has(keys_to_remove[i])) {
			const bluetooth::DeviceInfo &info = discovered_devices[keys_to_remove[i]];
			if (bluetooth::is_valid_bluetooth_address(info.address)) {
				signal_address = bluetooth::normalize_address(info.address);
			} else if (!info.address.is_empty()) {
				signal_address = info.address;
			}
		}
		discovered_devices.erase(keys_to_remove[i]);
		paired_devices.erase(keys_to_remove[i]);
	}

	emit_signal("device_removed", signal_address);
}

void BluetoothManager::sync_devices_from_snapshot(const Array &p_devices) {
	discovered_devices.clear();
	paired_devices.clear();
	for (int i = 0; i < p_devices.size(); i++) {
		const Variant entry = p_devices[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			continue;
		}
		upsert_device(bluetooth::DeviceInfo::from_dictionary(entry));
	}
}

void BluetoothManager::update_devices_for_address(const String &p_address,
		const std::function<void(bluetooth::DeviceInfo &)> &p_mutator) {
	PackedStringArray keys_to_remove;
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (!bluetooth::addresses_match(item.value.address, p_address) &&
				!bluetooth::addresses_match(item.key, p_address)) {
			continue;
		}
		bluetooth::DeviceInfo info = item.value;
		p_mutator(info);
		if (bluetooth::is_valid_bluetooth_address(info.address)) {
			info.address = bluetooth::normalize_address(info.address);
		}
		const String key = canonical_device_key(info);
		if (key != item.key) {
			keys_to_remove.append(item.key);
		}
		discovered_devices[key] = info;
		if (info.paired) {
			paired_devices[key] = info;
		} else {
			paired_devices.erase(key);
		}
	}
	for (int i = 0; i < keys_to_remove.size(); i++) {
		discovered_devices.erase(keys_to_remove[i]);
		paired_devices.erase(keys_to_remove[i]);
	}
}

void BluetoothManager::emit_device_updates_for_address(const String &p_address) {
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (bluetooth::addresses_match(item.value.address, p_address) ||
				bluetooth::addresses_match(item.key, p_address)) {
			emit_signal("device_found", item.value.to_dictionary());
		}
	}
}

void BluetoothManager::upsert_device(const bluetooth::DeviceInfo &p_device) {
	bluetooth::DeviceInfo info = p_device;
	if (bluetooth::is_valid_bluetooth_address(info.address)) {
		info.address = bluetooth::normalize_address(info.address);
	}
	const String key = canonical_device_key(info);

	PackedStringArray stale_keys;
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (item.key == key) {
			continue;
		}
		if (item.value.device_id == info.device_id && !info.device_id.is_empty()) {
			stale_keys.append(item.key);
			continue;
		}
		if (bluetooth::is_valid_bluetooth_address(info.address) &&
				bluetooth::addresses_match(item.value.address, info.address)) {
			stale_keys.append(item.key);
		}
	}
	for (int i = 0; i < stale_keys.size(); i++) {
		discovered_devices.erase(stale_keys[i]);
		paired_devices.erase(stale_keys[i]);
	}

	discovered_devices[key] = info;
	if (info.paired) {
		paired_devices[key] = info;
	} else {
		paired_devices.erase(key);
	}
}

Array BluetoothManager::devices_to_array(const HashMap<String, bluetooth::DeviceInfo> &p_devices) const {
	Array result;
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : p_devices) {
		result.push_back(item.value.to_dictionary());
	}
	return result;
}

void BluetoothManager::start_scan() {
	enqueue_command(bluetooth::CommandType::START_SCAN);
}

void BluetoothManager::stop_scan() {
	enqueue_command(bluetooth::CommandType::STOP_SCAN);
}

Array BluetoothManager::get_discovered_devices() const {
	return devices_to_array(discovered_devices);
}

void BluetoothManager::pair_device(const String &p_address) {
	enqueue_command(bluetooth::CommandType::PAIR_DEVICE, resolve_command_address(p_address));
}

void BluetoothManager::unpair_device(const String &p_address) {
	enqueue_command(bluetooth::CommandType::UNPAIR_DEVICE, resolve_command_address(p_address));
}

void BluetoothManager::refresh_paired_devices() {
	enqueue_command(bluetooth::CommandType::REFRESH_PAIRED_DEVICES);
}

Array BluetoothManager::get_paired_devices() const {
	return devices_to_array(paired_devices);
}

bool BluetoothManager::is_paired(const String &p_address) const {
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : paired_devices) {
		if (bluetooth::addresses_match(item.key, p_address) || bluetooth::addresses_match(item.value.address, p_address)) {
			return true;
		}
	}
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (bluetooth::addresses_match(item.key, p_address) || bluetooth::addresses_match(item.value.address, p_address)) {
			return item.value.paired;
		}
	}
	return false;
}

void BluetoothManager::connect_device(const String &p_address) {
	enqueue_command(bluetooth::CommandType::CONNECT_DEVICE, resolve_command_address(p_address));
}

void BluetoothManager::disconnect_device(const String &p_address) {
	enqueue_command(bluetooth::CommandType::DISCONNECT_DEVICE, resolve_command_address(p_address));
}

bool BluetoothManager::is_connected(const String &p_address) const {
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (bluetooth::addresses_match(item.key, p_address) || bluetooth::addresses_match(item.value.address, p_address)) {
			return item.value.connected;
		}
	}
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : paired_devices) {
		if (bluetooth::addresses_match(item.key, p_address) || bluetooth::addresses_match(item.value.address, p_address)) {
			return item.value.connected;
		}
	}
	return false;
}

String BluetoothManager::normalize_address(const String &p_address) const {
	return bluetooth::normalize_address(p_address);
}

bool BluetoothManager::is_valid_bluetooth_address(const String &p_address) const {
	return bluetooth::is_valid_bluetooth_address(p_address);
}

bool BluetoothManager::can_unpair_while_connected() const {
#if defined(_WIN32)
	return false;
#elif defined(__linux__)
	return true;
#else
	return false;
#endif
}

bool BluetoothManager::is_bluetooth_available() const {
	return backend_available;
}

String BluetoothManager::get_platform_name() const {
#if defined(_WIN32)
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#elif defined(__linux__)
	return "linux";
#else
	return "unknown";
#endif
}