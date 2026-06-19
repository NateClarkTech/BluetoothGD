#pragma once

#include "../../backend/bluetooth_backend.h"

namespace bluetooth {

class MacOSBackend : public BluetoothBackend {
public:
	bool initialize() override;
	void shutdown() override;

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
	godot::Dictionary get_capabilities() const override;

private:
	void emit_not_implemented(const godot::String &p_operation);
};

} // namespace bluetooth