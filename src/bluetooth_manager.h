#pragma once

#include "backend/bluetooth_events.h"
#include "bluetooth_device_info.h"
#include "threading/worker_thread.h"

#include <functional>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_map.hpp>

namespace godot {

class BluetoothManager : public Node {
	GDCLASS(BluetoothManager, Node)

public:
	void start_scan(const Dictionary &p_options = Dictionary());
	void stop_scan();
	Array get_discovered_devices() const;

	void pair_device(const String &p_address);
	void pair_device_by_id(const String &p_device_id);
	void unpair_device(const String &p_address);
	void refresh_paired_devices();
	Array get_paired_devices() const;
	bool is_paired(const String &p_address) const;

	void connect_device(const String &p_address);
	void disconnect_device(const String &p_address);
	bool is_connected(const String &p_address) const;

	void confirm_pairing(const String &p_pin = "");
	void reject_pairing();
	void cancel_pairing();

	String normalize_address(const String &p_address) const;
	bool is_valid_bluetooth_address(const String &p_address) const;
	bool can_unpair_while_connected() const;
	bool is_bluetooth_available() const;
	bool is_radio_on() const;
	String get_platform_name() const;
	Dictionary get_capabilities() const;
	String get_error_code_name(int p_error_code) const;

	void _ready() override;
	void _process(double p_delta) override;
	void _exit_tree() override;

protected:
	static void _bind_methods();

private:
	void enqueue_command(bluetooth::CommandType p_type, const String &p_address = String(),
			const String &p_pin = String(), bool p_accepted = false,
			const bluetooth::ScanOptions &p_scan_options = {});
	bluetooth::ScanOptions scan_options_from_dictionary(const Dictionary &p_options) const;
	void handle_event(const bluetooth::BluetoothEvent &p_event);
	void upsert_device(const bluetooth::DeviceInfo &p_device);
	void remove_device_for_address(const String &p_address);
	void sync_devices_from_snapshot(const Array &p_devices);
	void maybe_emit_bluetooth_ready();
	String resolve_command_address(const String &p_address) const;
	String canonical_device_key(const bluetooth::DeviceInfo &p_device) const;
	void update_devices_for_address(const String &p_address, const std::function<void(bluetooth::DeviceInfo &)> &p_mutator);
	void emit_device_updates_for_address(const String &p_address);
	Array devices_to_array(const HashMap<String, bluetooth::DeviceInfo> &p_devices) const;

	bluetooth::WorkerThread worker;
	bool backend_available = false;
	bool radio_on = true;
	bool bluetooth_ready_emitted = false;
	Dictionary cached_capabilities;
	HashMap<String, bluetooth::DeviceInfo> discovered_devices;
	HashMap<String, bluetooth::DeviceInfo> paired_devices;
};

} // namespace godot