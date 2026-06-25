#include "bluetooth_manager.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void BluetoothManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start_scan", "options"), &BluetoothManager::start_scan, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("stop_scan"), &BluetoothManager::stop_scan);
	ClassDB::bind_method(D_METHOD("get_discovered_devices"), &BluetoothManager::get_discovered_devices);

	ClassDB::bind_method(D_METHOD("pair_device", "address"), &BluetoothManager::pair_device);
	ClassDB::bind_method(D_METHOD("pair_device_by_id", "device_id"), &BluetoothManager::pair_device_by_id);
	ClassDB::bind_method(D_METHOD("unpair_device", "address"), &BluetoothManager::unpair_device);
	ClassDB::bind_method(D_METHOD("refresh_paired_devices"), &BluetoothManager::refresh_paired_devices);
	ClassDB::bind_method(D_METHOD("get_paired_devices"), &BluetoothManager::get_paired_devices);
	ClassDB::bind_method(D_METHOD("is_paired", "address"), &BluetoothManager::is_paired);

	ClassDB::bind_method(D_METHOD("connect_device", "address"), &BluetoothManager::connect_device);
	ClassDB::bind_method(D_METHOD("disconnect_device", "address"), &BluetoothManager::disconnect_device);
	ClassDB::bind_method(D_METHOD("is_connected", "address"), &BluetoothManager::is_connected);

	ClassDB::bind_method(D_METHOD("confirm_pairing", "pin"), &BluetoothManager::confirm_pairing, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("reject_pairing"), &BluetoothManager::reject_pairing);
	ClassDB::bind_method(D_METHOD("cancel_pairing"), &BluetoothManager::cancel_pairing);

	ClassDB::bind_method(D_METHOD("normalize_address", "address"), &BluetoothManager::normalize_address);
	ClassDB::bind_method(D_METHOD("is_valid_bluetooth_address", "address"), &BluetoothManager::is_valid_bluetooth_address);
	ClassDB::bind_method(D_METHOD("can_unpair_while_connected"), &BluetoothManager::can_unpair_while_connected);
	ClassDB::bind_method(D_METHOD("is_bluetooth_available"), &BluetoothManager::is_bluetooth_available);
	ClassDB::bind_method(D_METHOD("is_radio_on"), &BluetoothManager::is_radio_on);
	ClassDB::bind_method(D_METHOD("get_platform_name"), &BluetoothManager::get_platform_name);
	ClassDB::bind_method(D_METHOD("get_capabilities"), &BluetoothManager::get_capabilities);
	ClassDB::bind_method(D_METHOD("get_error_code_name", "error_code"), &BluetoothManager::get_error_code_name);

	ADD_SIGNAL(MethodInfo("device_found", PropertyInfo(Variant::DICTIONARY, "device_info")));
	ADD_SIGNAL(MethodInfo("device_removed", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("devices_refreshed"));
	ADD_SIGNAL(MethodInfo("bluetooth_ready"));
	ADD_SIGNAL(MethodInfo("scan_started"));
	ADD_SIGNAL(MethodInfo("scan_stopped"));
	ADD_SIGNAL(MethodInfo("pairing_started", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("pairing_succeeded", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("pairing_failed", PropertyInfo(Variant::STRING, "address"), PropertyInfo(Variant::STRING, "error"), PropertyInfo(Variant::INT, "error_code")));
	ADD_SIGNAL(MethodInfo("pairing_confirmation_requested", PropertyInfo(Variant::STRING, "address"), PropertyInfo(Variant::STRING, "kind")));
	ADD_SIGNAL(MethodInfo("pairing_pin_requested", PropertyInfo(Variant::STRING, "address")));
	ADD_SIGNAL(MethodInfo("pairing_display_pin", PropertyInfo(Variant::STRING, "address"), PropertyInfo(Variant::STRING, "pin")));
	ADD_SIGNAL(MethodInfo("connection_changed", PropertyInfo(Variant::STRING, "address"), PropertyInfo(Variant::BOOL, "connected"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("error_occurred", PropertyInfo(Variant::STRING, "operation"), PropertyInfo(Variant::STRING, "message"), PropertyInfo(Variant::INT, "error_code")));
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

void BluetoothManager::enqueue_command(bluetooth::CommandType p_type, const String &p_address,
		const String &p_pin, bool p_accepted, const bluetooth::ScanOptions &p_scan_options) {
	bluetooth::BluetoothCommand command;
	command.type = p_type;
	command.address = p_address;
	command.pin = p_pin;
	command.accepted = p_accepted;
	command.scan_options = p_scan_options;
	worker.enqueue_command(command);
}

bluetooth::ScanOptions BluetoothManager::scan_options_from_dictionary(const Dictionary &p_options) const {
	bluetooth::ScanOptions options;
	if (p_options.has("named_only")) {
		options.named_only = p_options["named_only"];
	}
	if (p_options.has("gamepads_only")) {
		options.gamepads_only = p_options["gamepads_only"];
	}
	if (p_options.has("min_rssi")) {
		options.min_rssi = p_options["min_rssi"];
	}
	if (p_options.has("timeout_seconds")) {
		options.timeout_seconds = p_options["timeout_seconds"];
	}
	return options;
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
			emit_signal("pairing_failed", p_event.address, p_event.message, static_cast<int>(p_event.error_code));
			break;
		case bluetooth::EventType::PAIRING_CONFIRMATION_REQUESTED:
			emit_signal("pairing_confirmation_requested", p_event.address, p_event.pairing_kind);
			break;
		case bluetooth::EventType::PAIRING_PIN_REQUESTED:
			emit_signal("pairing_pin_requested", p_event.address);
			break;
		case bluetooth::EventType::PAIRING_DISPLAY_PIN:
			emit_signal("pairing_display_pin", p_event.address, p_event.display_pin);
			break;
		case bluetooth::EventType::CONNECTION_CHANGED:
			update_devices_for_address(p_event.address, [&p_event](bluetooth::DeviceInfo &p_info) {
				p_info.connected = p_event.connected;
			});
			emit_device_updates_for_address(p_event.address);
			emit_signal("connection_changed", p_event.address, p_event.connected, p_event.message);
			break;
		case bluetooth::EventType::BACKEND_INITIALIZED:
			cached_capabilities = worker.get_capabilities();
			radio_on = worker.is_radio_on();
			maybe_emit_bluetooth_ready();
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
			if (p_event.error_code == bluetooth::BluetoothErrorCode::RADIO_OFF) {
				radio_on = false;
			}
			emit_signal("error_occurred", p_event.operation, p_event.message, static_cast<int>(p_event.error_code));
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
	for (int i = 0; i < p_devices.size(); i++) {
		const Variant entry = p_devices[i];
		if (entry.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const bluetooth::DeviceInfo info = bluetooth::DeviceInfo::from_dictionary(entry);
		upsert_device(info);
		emit_signal("device_found", info.to_dictionary());
	}
}

void BluetoothManager::update_devices_for_address(const String &p_address,
		const std::function<void(bluetooth::DeviceInfo &)> &p_mutator) {
	PackedStringArray keys_to_remove;
	for (const KeyValue<String, bluetooth::DeviceInfo> &item : discovered_devices) {
		if (!bluetooth::addresses_match(item.value.address, p_address) &&
				!bluetooth::addresses_match(item.key, p_address) &&
				item.value.device_id != p_address) {
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
				bluetooth::addresses_match(item.key, p_address) ||
				item.value.device_id == p_address) {
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
		if (discovered_devices.has(stale_keys[i])) {
			bluetooth::merge_device_endpoints(info, discovered_devices[stale_keys[i]]);
		}
		discovered_devices.erase(stale_keys[i]);
		paired_devices.erase(stale_keys[i]);
	}

	if (discovered_devices.has(key)) {
		bluetooth::merge_device_identity(info, discovered_devices[key]);
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

void BluetoothManager::start_scan(const Dictionary &p_options) {
	enqueue_command(bluetooth::CommandType::START_SCAN, "", "", false, scan_options_from_dictionary(p_options));
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

void BluetoothManager::pair_device_by_id(const String &p_device_id) {
	enqueue_command(bluetooth::CommandType::PAIR_DEVICE_BY_ID, p_device_id);
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
	if (!cached_capabilities.is_empty() && cached_capabilities.has("can_unpair_while_connected")) {
		return cached_capabilities["can_unpair_while_connected"];
	}
#if defined(_WIN32) || defined(__linux__)
	return true;
#else
	return false;
#endif
}

void BluetoothManager::confirm_pairing(const String &p_pin) {
	bluetooth::PairingUserResponse response;
	response.accepted = true;
	response.pin = p_pin;
	worker.submit_pairing_response(response);
}

void BluetoothManager::reject_pairing() {
	bluetooth::PairingUserResponse response;
	response.accepted = false;
	worker.submit_pairing_response(response);
}

void BluetoothManager::cancel_pairing() {
	bluetooth::PairingUserResponse response;
	response.accepted = false;
	worker.submit_pairing_response(response);
	enqueue_command(bluetooth::CommandType::CANCEL_PAIRING);
}

bool BluetoothManager::is_bluetooth_available() const {
	return backend_available;
}

bool BluetoothManager::is_radio_on() const {
	return radio_on;
}

Dictionary BluetoothManager::get_capabilities() const {
	if (!cached_capabilities.is_empty()) {
		return cached_capabilities;
	}

	Dictionary caps = worker.get_capabilities();
	if (caps.is_empty()) {
		caps["platform"] = get_platform_name();
		caps["implemented"] = backend_available;
#if defined(_WIN32)
		caps["implemented"] = true;
		caps["can_disconnect_hid"] = false;
		caps["can_unpair_while_connected"] = true;
		caps["needs_pin_ui"] = true;
		caps["supports_ble"] = true;
		caps["supports_device_id"] = true;
#elif defined(__linux__)
		caps["requires_libdbus"] = true;
		caps["can_disconnect_hid"] = false;
		caps["can_unpair_while_connected"] = true;
		caps["needs_pin_ui"] = true;
		caps["supports_ble"] = true;
		caps["supports_rssi"] = true;
		caps["supports_device_id"] = true;
#else
		caps["implemented"] = false;
		caps["can_disconnect_hid"] = false;
		caps["can_unpair_while_connected"] = false;
		caps["needs_pin_ui"] = false;
		caps["supports_ble"] = false;
#endif
	}
	return caps;
}

String BluetoothManager::get_error_code_name(int p_error_code) const {
	return bluetooth::error_code_to_string(static_cast<bluetooth::BluetoothErrorCode>(p_error_code));
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