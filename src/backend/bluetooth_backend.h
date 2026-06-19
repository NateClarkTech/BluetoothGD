#pragma once

#include "bluetooth_commands.h"
#include "bluetooth_events.h"
#include "pairing_pending.h"

#include <functional>

namespace bluetooth {

class BluetoothBackend {
public:
	virtual ~BluetoothBackend() = default;

	virtual bool initialize() = 0;
	virtual void shutdown() = 0;

	virtual void start_scan(const ScanOptions &p_options = {}) = 0;
	virtual void stop_scan() = 0;

	virtual void pair_device(const godot::String &p_address) = 0;
	virtual void pair_device_by_id(const godot::String &p_device_id) = 0;
	virtual void unpair_device(const godot::String &p_address) = 0;
	virtual void connect_device(const godot::String &p_address) = 0;
	virtual void disconnect_device(const godot::String &p_address) = 0;

	virtual void refresh_paired_devices() = 0;
	virtual void confirm_pairing(const godot::String &p_pin = "") = 0;
	virtual void reject_pairing() = 0;
	virtual void cancel_pairing() = 0;
	virtual void submit_pairing_response(const PairingUserResponse &p_response) = 0;

	virtual bool is_connected(const godot::String &p_address) = 0;
	virtual bool is_paired(const godot::String &p_address) = 0;
	virtual bool is_radio_on() const { return true; }
	virtual godot::Dictionary get_capabilities() const = 0;

	virtual void poll() {}

	std::function<void(const BluetoothEvent &)> on_event;
};

BluetoothBackend *create_platform_backend();

} // namespace bluetooth