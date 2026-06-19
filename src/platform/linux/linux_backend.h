#pragma once

#include "../../backend/bluetooth_backend.h"
#include "bluez_agent.h"
#include "bluez_dbus.h"

#include <godot_cpp/templates/hash_map.hpp>

#include <mutex>

namespace bluetooth {

class LinuxBackend : public BluetoothBackend {
public:
	~LinuxBackend() override;

	bool initialize() override;
	void shutdown() override;
	void poll() override;

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
	void emit(const BluetoothEvent &p_event);
	void emit_error(const godot::String &p_operation, const godot::String &p_message);

	void handle_dbus_signal(DBusMessage *p_message);
	void handle_interfaces_added(DBusMessage *p_message);
	void handle_interfaces_removed(DBusMessage *p_message);
	void handle_properties_changed(DBusMessage *p_message);
	void emit_device_removed(const godot::String &p_address);
	void emit_paired_devices_updated();
	void remove_device_from_cache(const godot::String &p_key, const godot::String &p_address);

	void enumerate_devices(bool p_emit_events, bool p_force_emit = false);
	DeviceInfo device_info_from_bluez(const BluezDeviceProperties &p_props) const;
	void upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit = false);
	godot::String device_cache_key(const DeviceInfo &p_info) const;
	godot::String resolve_device_path(const godot::String &p_address);
	bool lookup_cached_state(const godot::String &p_address, bool &p_out_value,
			const std::function<bool(const DeviceInfo &)> &p_selector);

	BluezDBus dbus;
	BluezAgent agent;
	godot::String adapter_path;
	bool initialized = false;
	bool scanning = false;
	std::mutex state_mutex;
	godot::HashMap<godot::String, DeviceInfo> discovered_devices;
	godot::HashMap<godot::String, DeviceInfo> paired_devices;
	godot::HashMap<godot::String, godot::String> address_to_device_id;
};

} // namespace bluetooth