#include "linux_backend.h"

namespace bluetooth {

bool LinuxBackend::initialize() {
	emit_not_implemented("initialize");
	return false;
}

void LinuxBackend::shutdown() {
}

void LinuxBackend::start_scan() {
	emit_not_implemented("start_scan");
}

void LinuxBackend::stop_scan() {
}

void LinuxBackend::pair_device(const godot::String &p_address) {
	emit_not_implemented("pair_device");
	BluetoothEvent failed;
	failed.type = EventType::PAIRING_FAILED;
	failed.address = normalize_address(p_address);
	failed.message = "pair_device failed: Linux BlueZ backend is not implemented (Milestone 2). No D-Bus/BlueZ integration is available on this platform build.";
	if (on_event) {
		on_event(failed);
	}
}

void LinuxBackend::unpair_device(const godot::String &p_address) {
	emit_not_implemented("unpair_device");
}

void LinuxBackend::connect_device(const godot::String &p_address) {
	emit_not_implemented("connect_device");
}

void LinuxBackend::disconnect_device(const godot::String &p_address) {
	emit_not_implemented("disconnect_device");
}

void LinuxBackend::refresh_paired_devices() {
	emit_not_implemented("refresh_paired_devices");
}

bool LinuxBackend::is_connected(const godot::String &p_address) {
	return false;
}

bool LinuxBackend::is_paired(const godot::String &p_address) {
	return false;
}

void LinuxBackend::emit_not_implemented(const godot::String &p_operation) {
	if (!on_event) {
		return;
	}
	BluetoothEvent event;
	event.type = EventType::ERROR_OCCURRED;
	event.operation = p_operation;
	event.message = godot::String(p_operation) + " failed: Linux BlueZ backend is not implemented (Milestone 2). No D-Bus/BlueZ integration is available on this platform build.";
	on_event(event);
}

} // namespace bluetooth