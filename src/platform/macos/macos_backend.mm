#include "macos_backend.h"

namespace bluetooth {

bool MacOSBackend::initialize() {
	emit_not_implemented("initialize");
	return false;
}

void MacOSBackend::shutdown() {
}

void MacOSBackend::start_scan() {
	emit_not_implemented("start_scan");
}

void MacOSBackend::stop_scan() {
}

void MacOSBackend::pair_device(const godot::String &p_address) {
	emit_not_implemented("pair_device");
	BluetoothEvent failed;
	failed.type = EventType::PAIRING_FAILED;
	failed.address = normalize_address(p_address);
	failed.message = "pair_device failed: macOS IOBluetooth backend is not implemented (Milestone 3). No IOBluetooth/CoreBluetooth integration is available on this platform build.";
	if (on_event) {
		on_event(failed);
	}
}

void MacOSBackend::unpair_device(const godot::String &p_address) {
	emit_not_implemented("unpair_device");
}

void MacOSBackend::connect_device(const godot::String &p_address) {
	emit_not_implemented("connect_device");
}

void MacOSBackend::disconnect_device(const godot::String &p_address) {
	emit_not_implemented("disconnect_device");
}

void MacOSBackend::refresh_paired_devices() {
	emit_not_implemented("refresh_paired_devices");
}

bool MacOSBackend::is_connected(const godot::String &p_address) {
	return false;
}

bool MacOSBackend::is_paired(const godot::String &p_address) {
	return false;
}

void MacOSBackend::emit_not_implemented(const godot::String &p_operation) {
	if (!on_event) {
		return;
	}
	BluetoothEvent event;
	event.type = EventType::ERROR_OCCURRED;
	event.operation = p_operation;
	event.message = godot::String(p_operation) + " failed: macOS IOBluetooth backend is not implemented (Milestone 3). No IOBluetooth/CoreBluetooth integration is available on this platform build.";
	on_event(event);
}

} // namespace bluetooth