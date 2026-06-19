#pragma once

#include "../../backend/bluetooth_backend.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/base.h>

#include <mutex>
#include <vector>

namespace bluetooth {

struct ActiveDeviceWatcher {
	winrt::Windows::Devices::Enumeration::DeviceWatcher watcher{ nullptr };
	winrt::event_token added_token{};
	winrt::event_token updated_token{};
	winrt::event_token removed_token{};
};

class WindowsBackend : public BluetoothBackend {
public:
	~WindowsBackend() override;

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
	void emit(const BluetoothEvent &p_event);
	void emit_error(const godot::String &p_operation, const godot::String &p_message);
	void emit_device_removed(const godot::String &p_address);
	void emit_paired_devices_updated();
	void remove_device_from_cache(const godot::String &p_key, const godot::String &p_address);
	bool ensure_winrt_ready();

	void handle_device_added(const winrt::Windows::Devices::Enumeration::DeviceInformation &p_info);
	void handle_device_updated(const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate &p_update);
	void handle_device_removed(const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate &p_update);

	void enumerate_snapshot(const winrt::hstring &p_selector, bool p_emit_events = true, bool p_force_emit = true);
	void enumerate_hid_gamepads(bool p_emit_events = true, bool p_force_emit = true);
	void start_device_watcher(const winrt::hstring &p_selector,
			winrt::Windows::Devices::Enumeration::DeviceInformationKind p_kind =
					winrt::Windows::Devices::Enumeration::DeviceInformationKind::Unknown);
	void stop_all_watchers();
	void upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit = false);
	godot::String device_cache_key(const DeviceInfo &p_info) const;

	winrt::Windows::Devices::Enumeration::DeviceInformation find_device_information(const godot::String &p_address);
	winrt::Windows::Devices::Enumeration::DeviceInformation find_device_information_by_id(const godot::String &p_device_id);
	void update_connection_state(const godot::String &p_address, bool p_report_errors = false, bool p_emit_event = true);

	bool initialized = false;
	bool scanning = false;
	std::mutex state_mutex;
	godot::HashMap<godot::String, DeviceInfo> discovered_devices;
	godot::HashMap<godot::String, DeviceInfo> paired_devices;
	godot::HashMap<godot::String, godot::String> address_to_device_id;

	std::vector<ActiveDeviceWatcher> active_watchers;
};

} // namespace bluetooth