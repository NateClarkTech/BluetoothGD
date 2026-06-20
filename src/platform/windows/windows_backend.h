#pragma once

#include "../../backend/bluetooth_backend.h"
#include "../../backend/device_cache.h"
#include "../../backend/pairing_pending.h"
#include "../../backend/scan_options.h"

#include <godot_cpp/variant/dictionary.hpp>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/base.h>

#include <chrono>
#include <mutex>
#include <vector>

namespace bluetooth {

struct ActiveDeviceWatcher {
	winrt::Windows::Devices::Enumeration::DeviceWatcher watcher{ nullptr };
	winrt::event_token added_token{};
	winrt::event_token updated_token{};
	winrt::event_token removed_token{};
};

struct HidGamepadCache {
	mutable std::mutex mutex;
	std::chrono::steady_clock::time_point cached_at{};
	godot::HashMap<godot::String, bool> results;
	static constexpr int TTL_MS = 2000;

	bool is_connected(const godot::String &p_normalized);
};

class WindowsBackend : public BluetoothBackend {
public:
	~WindowsBackend() override;

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
	bool is_radio_on() const override;
	godot::Dictionary get_capabilities() const override;

private:
	void emit(const BluetoothEvent &p_event);
	void emit_error(const godot::String &p_operation, const godot::String &p_message);
	void emit_device_removed(const godot::String &p_address);
	void emit_paired_devices_updated();
	bool ensure_winrt_ready();
	bool passes_scan_filters(const DeviceInfo &p_info) const;

	void handle_device_added(const winrt::Windows::Devices::Enumeration::DeviceInformation &p_info);
	void handle_device_updated(const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate &p_update);
	void handle_device_removed(const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate &p_update);
	void handle_pairing_requested(const winrt::Windows::Devices::Enumeration::DevicePairingRequestedEventArgs &p_args,
			const godot::String &p_address);

	void enumerate_snapshot(const winrt::hstring &p_selector, bool p_emit_events = true, bool p_force_emit = true);
	void enumerate_hid_gamepads(bool p_emit_events = true, bool p_force_emit = true);
	void start_device_watcher(const winrt::hstring &p_selector,
			winrt::Windows::Devices::Enumeration::DeviceInformationKind p_kind =
					winrt::Windows::Devices::Enumeration::DeviceInformationKind::Unknown);
	bool try_start_device_watcher(const winrt::hstring &p_selector,
			winrt::Windows::Devices::Enumeration::DeviceInformationKind p_kind =
					winrt::Windows::Devices::Enumeration::DeviceInformationKind::Unknown);
	void stop_all_watchers();
	void upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit = false);

	winrt::Windows::Devices::Enumeration::DeviceInformation find_device_information(const godot::String &p_address);
	winrt::Windows::Devices::Enumeration::DeviceInformation find_device_information_by_id(const godot::String &p_device_id);
	void perform_pairing(const winrt::Windows::Devices::Enumeration::DeviceInformation &p_device_info,
			const godot::String &p_address);
	void finalize_pairing_success(const godot::String &p_device_id, const godot::String &p_fallback_address);
	void disconnect_before_unpair(const godot::String &p_normalized);
	void update_connection_state(const godot::String &p_address, bool p_report_errors = false, bool p_emit_event = true);

	bool initialized = false;
	bool scanning = false;
	ScanOptions scan_options;
	DeviceCache cache;
	PairingPendingState pairing_pending;
	HidGamepadCache hid_gamepad_cache;

	std::vector<ActiveDeviceWatcher> active_watchers;
};

} // namespace bluetooth