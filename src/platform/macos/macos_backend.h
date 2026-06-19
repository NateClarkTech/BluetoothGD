#pragma once

#include "../../backend/bluetooth_backend.h"

namespace bluetooth {

class MacOSBackend : public BluetoothBackend {
public:
	bool initialize() override;
	void shutdown() override;

	void start_scan() override;
	void stop_scan() override;

	void pair_device(const godot::String &p_address) override;
	void unpair_device(const godot::String &p_address) override;
	void connect_device(const godot::String &p_address) override;
	void disconnect_device(const godot::String &p_address) override;

	void refresh_paired_devices() override;

	bool is_connected(const godot::String &p_address) override;
	bool is_paired(const godot::String &p_address) override;

private:
	void emit_not_implemented(const godot::String &p_operation);
};

} // namespace bluetooth