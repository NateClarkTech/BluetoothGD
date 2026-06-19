#pragma once

#include "../../backend/bluetooth_backend.h"
#include "../../backend/device_cache.h"
#include "../../backend/pairing_pending.h"
#include "../../backend/scan_options.h"
#include "bluez_agent.h"
#include "bluez_dbus.h"

#include <chrono>
#include <optional>

namespace bluetooth {

class LinuxBackend : public BluetoothBackend {
public:
	~LinuxBackend() override;

	bool initialize() override;
	void shutdown() override;
	void poll() override;

	void start_scan(const ScanOptions &p_options = {}) override;
	void stop_scan() override;

	void pair_device(const godot::String &p_address) override;
	void pair_device_by_id(const godot::String &p_device_id) override;
	void unpair_device(const godot::String &p_address) override;
	void connect_device(const godot::String &p_address) override;
	void disconnect_device(const godot::String &p_address) override;

	void refresh_paired_devices() override;
	void confirm_pairing(const godot::String &p_pin = "") override;
	void reject_pairing() override;
	void cancel_pairing() override;
	void submit_pairing_response(const PairingUserResponse &p_response) override;

	bool is_connected(const godot::String &p_address) override;
	bool is_paired(const godot::String &p_address) override;
	bool is_radio_on() const override;
	godot::Dictionary get_capabilities() const override;

private:
	void emit(const BluetoothEvent &p_event);
	void emit_error(const godot::String &p_operation, const godot::String &p_message,
			BluetoothErrorCode p_error_code = BluetoothErrorCode::UNKNOWN);

	void register_signal_matches();
	void handle_dbus_signal(DBusMessage *p_message);
	void handle_interfaces_added(DBusMessage *p_message);
	void handle_interfaces_removed(DBusMessage *p_message);
	void handle_properties_changed(DBusMessage *p_message);
	void emit_device_removed(const godot::String &p_address);
	void emit_paired_devices_updated();

	void enumerate_devices(bool p_emit_events, bool p_force_emit = false);
	DeviceInfo device_info_from_bluez(const BluezDeviceProperties &p_props) const;
	void upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit = false);
	bool device_passes_scan_filter(const DeviceInfo &p_info) const;
	godot::String resolve_device_path(const godot::String &p_address);
	void pair_device_at_path(const godot::String &p_device_path, const godot::String &p_event_address);
	bool ensure_adapter_powered();

	BluezDBus dbus;
	BluezAgent agent;
	DeviceCache cache;
	PairingPendingState pairing_pending;
	godot::String adapter_path;
	ScanOptions scan_options;
	std::optional<std::chrono::steady_clock::time_point> scan_deadline;
	bool initialized = false;
	bool scanning = false;
	bool adapter_powered = false;
	bool signal_matches_registered = false;
};

} // namespace bluetooth