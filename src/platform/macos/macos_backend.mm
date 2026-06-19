#include "macos_backend.h"

namespace bluetooth {

bool MacOSBackend::initialize() {
	emit_not_implemented("initialize");
	return false;
}

void MacOSBackend::shutdown() {
}

void MacOSBackend::start_scan(const ScanOptions &p_options) {
	(void)p_options;
	emit_not_implemented("start_scan");
}

void MacOSBackend::stop_scan() {
}

void MacOSBackend::pair_device(const godot::String &p_address) {
	emit_not_implemented("pair_device");
	BluetoothEvent failed;
	failed.type = EventType::PAIRING_FAILED;
	failed.address = normalize_address(p_address);
	failed.message = "pair_device failed: macOS IOBluetooth backend is not implemented (Milestone 3).";
	failed.error_code = BluetoothErrorCode::NOT_SUPPORTED;
	if (on_event) {
		on_event(failed);
	}
}

void MacOSBackend::pair_device_by_id(const godot::String &p_device_id) {
	(void)p_device_id;
	emit_not_implemented("pair_device_by_id");
}

void MacOSBackend::unpair_device(const godot::String &p_address) {
	(void)p_address;
	emit_not_implemented("unpair_device");
}

void MacOSBackend::connect_device(const godot::String &p_address) {
	(void)p_address;
	emit_not_implemented("connect_device");
}

void MacOSBackend::disconnect_device(const godot::String &p_address) {
	(void)p_address;
	emit_not_implemented("disconnect_device");
}

void MacOSBackend::refresh_paired_devices() {
	emit_not_implemented("refresh_paired_devices");
}

void MacOSBackend::confirm_pairing(const godot::String &p_pin) {
	(void)p_pin;
}

void MacOSBackend::reject_pairing() {
}

void MacOSBackend::cancel_pairing() {
}

void MacOSBackend::submit_pairing_response(const PairingUserResponse &p_response) {
	(void)p_response;
}

bool MacOSBackend::is_connected(const godot::String &p_address) {
	(void)p_address;
	return false;
}

bool MacOSBackend::is_paired(const godot::String &p_address) {
	(void)p_address;
	return false;
}

godot::Dictionary MacOSBackend::get_capabilities() const {
	godot::Dictionary caps;
	caps["platform"] = "macos";
	caps["implemented"] = false;
	caps["can_disconnect_hid"] = false;
	caps["can_unpair_while_connected"] = false;
	caps["needs_pin_ui"] = false;
	caps["supports_ble"] = false;
	return caps;
}

void MacOSBackend::emit_not_implemented(const godot::String &p_operation) {
	if (!on_event) {
		return;
	}
	BluetoothEvent event;
	event.type = EventType::ERROR_OCCURRED;
	event.operation = p_operation;
	event.message = godot::String(p_operation) +
			" failed: macOS IOBluetooth backend is not implemented (Milestone 3).";
	event.error_code = BluetoothErrorCode::NOT_SUPPORTED;
	on_event(event);
}

} // namespace bluetooth