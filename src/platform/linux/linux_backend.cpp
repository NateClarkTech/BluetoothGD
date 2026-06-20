#include "linux_backend.h"

#include "../../backend/bluetooth_error.h"

#include <functional>

namespace bluetooth {

namespace {

constexpr const char *OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager";
constexpr const char *PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
constexpr const char *DEVICE_INTERFACE = "org.bluez.Device1";

godot::String dbus_string_to_godot(const char *p_value) {
	return p_value ? godot::String(p_value) : godot::String();
}

godot::String device_class_from_cod(uint32_t p_class) {
	const uint32_t major = (p_class >> 8) & 0x1f;
	if (major == 0x05) {
		return "gamepad";
	}
	return "unknown";
}

BluetoothErrorCode infer_error_code(const godot::String &p_message) {
	const godot::String lower = p_message.to_lower();
	if (lower.contains("not found") || lower.contains("device not found")) {
		return BluetoothErrorCode::DEVICE_NOT_FOUND;
	}
	if (lower.contains("not a valid") || lower.contains("invalid address")) {
		return BluetoothErrorCode::INVALID_ADDRESS;
	}
	if (lower.contains("access denied") || lower.contains("permission")) {
		return BluetoothErrorCode::PERMISSION_DENIED;
	}
	if (lower.contains("timeout") || lower.contains("timed out")) {
		return BluetoothErrorCode::OPERATION_TIMEOUT;
	}
	if (lower.contains("powered") || lower.contains("radio off")) {
		return BluetoothErrorCode::RADIO_OFF;
	}
	if (lower.contains("not supported")) {
		return BluetoothErrorCode::NOT_SUPPORTED;
	}
	if (lower.contains("rejected")) {
		return BluetoothErrorCode::PAIRING_REJECTED;
	}
	return BluetoothErrorCode::UNKNOWN;
}

} // namespace

LinuxBackend::~LinuxBackend() {
	shutdown();
}

void LinuxBackend::emit(const BluetoothEvent &p_event) {
	if (on_event) {
		on_event(p_event);
	}
}

void LinuxBackend::emit_error(const godot::String &p_operation, const godot::String &p_message,
		BluetoothErrorCode p_error_code) {
	BluetoothEvent event;
	event.type = EventType::ERROR_OCCURRED;
	event.operation = p_operation;
	event.message = p_message;
	event.error_code = p_error_code == BluetoothErrorCode::UNKNOWN ? infer_error_code(p_message) : p_error_code;
	emit(event);
}

void LinuxBackend::emit_device_removed(const godot::String &p_address) {
	BluetoothEvent event;
	event.type = EventType::DEVICE_REMOVED;
	event.address = is_valid_bluetooth_address(p_address) ? normalize_address(p_address) : p_address;
	emit(event);
}

void LinuxBackend::emit_paired_devices_updated() {
	BluetoothEvent event;
	event.type = EventType::PAIRED_DEVICES_UPDATED;
	event.devices = cache.paired_array();
	emit(event);
}

bool LinuxBackend::device_passes_scan_filter(const DeviceInfo &p_info) const {
	if (scan_options.named_only && p_info.name.is_empty()) {
		return false;
	}
	if (scan_options.gamepads_only && p_info.device_class != "gamepad") {
		return false;
	}
	if (scan_options.min_rssi > -127 && p_info.has_rssi && p_info.rssi < scan_options.min_rssi) {
		return false;
	}
	return true;
}

DeviceInfo LinuxBackend::device_info_from_bluez(const BluezDeviceProperties &p_props) const {
	DeviceInfo info;
	info.device_id = p_props.object_path;
	info.address = normalize_address(p_props.address);
	info.name = p_props.name;
	if (info.name.is_empty()) {
		info.name = p_props.alias;
	}
	info.paired = p_props.paired;
	info.connected = p_props.connected;
	info.trusted = p_props.trusted;

	const godot::String class_from_cod = device_class_from_cod(p_props.device_class);
	if (class_from_cod != "unknown") {
		info.device_class = class_from_cod;
	} else {
		info.device_class = infer_device_class(info.name);
	}

	if (p_props.has_rssi) {
		info.rssi = p_props.rssi;
		info.has_rssi = true;
	}

	if (info.address.is_empty()) {
		info.address = info.device_id;
	}
	clear_unhelpful_device_name(info);
	return info;
}

void LinuxBackend::upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit) {
	bool is_new = false;
	cache.upsert(p_info, is_new);

	if (p_emit_event && (is_new || p_force_emit)) {
		DeviceInfo emit_info = p_info;
		DeviceInfo cached;
		if (cache.get_device(cache.device_cache_key(p_info), cached)) {
			emit_info = cached;
		}
		if (!scanning || device_passes_scan_filter(emit_info)) {
			BluetoothEvent event;
			event.type = EventType::DEVICE_FOUND;
			event.device = emit_info;
			emit(event);
		}
	}
}

godot::String LinuxBackend::resolve_device_path(const godot::String &p_address) {
	const godot::String from_cache = cache.resolve_device_id(p_address);
	if (!from_cache.is_empty()) {
		return from_cache;
	}

	const godot::String normalized = normalize_address(p_address);
	std::vector<BluezDeviceProperties> devices;
	std::vector<BluezAdapterInfo> adapters;
	if (!dbus.get_managed_objects(devices, adapters)) {
		return "";
	}

	for (const BluezDeviceProperties &props : devices) {
		if (addresses_match(props.address, normalized)) {
			const DeviceInfo info = device_info_from_bluez(props);
			upsert_device(info, false);
			return props.object_path;
		}
	}

	return "";
}

void LinuxBackend::enumerate_devices(bool p_emit_events, bool p_force_emit) {
	std::vector<BluezDeviceProperties> devices;
	std::vector<BluezAdapterInfo> adapters;
	if (!dbus.get_managed_objects(devices, adapters)) {
		emit_error("enumerate_devices", "enumerate_devices failed: " + dbus.get_last_error());
		return;
	}

	for (const BluezDeviceProperties &props : devices) {
		const DeviceInfo info = device_info_from_bluez(props);
		upsert_device(info, p_emit_events, p_force_emit);
	}
}

void LinuxBackend::register_signal_matches() {
	if (signal_matches_registered) {
		return;
	}

	dbus.add_match("type='signal',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded',path='/'",
			"interfaces_added");
	dbus.add_match("type='signal',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesRemoved',path='/'",
			"interfaces_removed");
	dbus.add_match(
			"type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',arg0='org.bluez.Device1'",
			"device_properties_changed");
	signal_matches_registered = true;
}

bool LinuxBackend::ensure_adapter_powered() {
	std::vector<BluezDeviceProperties> devices;
	std::vector<BluezAdapterInfo> adapters;
	if (!dbus.get_managed_objects(devices, adapters)) {
		return false;
	}

	for (const BluezAdapterInfo &adapter : adapters) {
		if (adapter.object_path != adapter_path) {
			continue;
		}
		adapter_powered = adapter.powered;
		if (!adapter.powered) {
			if (dbus.adapter_set_powered(adapter_path, true)) {
				adapter_powered = true;
			} else {
				emit_error("initialize",
						"initialize: adapter is not powered and adapter_set_powered failed: " + dbus.get_last_error(),
						BluetoothErrorCode::RADIO_OFF);
			}
		}
		return true;
	}
	return false;
}

void LinuxBackend::handle_interfaces_added(DBusMessage *p_message) {
	DBusMessageIter args_iter;
	if (!dbus_message_iter_init(p_message, &args_iter)) {
		return;
	}

	const char *object_path = nullptr;
	if (dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_OBJECT_PATH) {
		return;
	}
	dbus_message_iter_get_basic(&args_iter, &object_path);
	const godot::String path = dbus_string_to_godot(object_path);

	if (!dbus_message_iter_next(&args_iter) || dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_ARRAY) {
		return;
	}

	DBusMessageIter interfaces_iter;
	dbus_message_iter_recurse(&args_iter, &interfaces_iter);
	while (dbus_message_iter_get_arg_type(&interfaces_iter) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter iface_entry;
		dbus_message_iter_recurse(&interfaces_iter, &iface_entry);
		if (dbus_message_iter_get_arg_type(&iface_entry) != DBUS_TYPE_STRING) {
			dbus_message_iter_next(&interfaces_iter);
			continue;
		}

		const char *iface_name = nullptr;
		dbus_message_iter_get_basic(&iface_entry, &iface_name);
		if (!iface_name || godot::String(iface_name) != DEVICE_INTERFACE) {
			dbus_message_iter_next(&interfaces_iter);
			continue;
		}

		if (!dbus_message_iter_next(&iface_entry) ||
				dbus_message_iter_get_arg_type(&iface_entry) != DBUS_TYPE_ARRAY) {
			dbus_message_iter_next(&interfaces_iter);
			continue;
		}

		BluezDeviceProperties props;
		BluezAdapterInfo adapter;
		BluezDBus::parse_interface_properties(path, DEVICE_INTERFACE, &iface_entry, props, adapter);
		if (props.valid) {
			const DeviceInfo info = device_info_from_bluez(props);
			upsert_device(info, true, true);
		}
		dbus_message_iter_next(&interfaces_iter);
	}
}

void LinuxBackend::handle_properties_changed(DBusMessage *p_message) {
	DBusMessageIter args_iter;
	if (!dbus_message_iter_init(p_message, &args_iter)) {
		return;
	}

	const char *interface_name = nullptr;
	if (dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_STRING) {
		return;
	}
	dbus_message_iter_get_basic(&args_iter, &interface_name);
	if (!interface_name || godot::String(interface_name) != DEVICE_INTERFACE) {
		return;
	}

	const godot::String device_path = dbus_string_to_godot(dbus_message_get_path(p_message));
	if (!dbus_message_iter_next(&args_iter) || dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_ARRAY) {
		return;
	}

	BluezDeviceProperties props;
	props.object_path = device_path;
	props.valid = true;
	bool connected_changed = false;
	bool paired_changed = false;
	bool rssi_changed = false;

	DBusMessageIter changed_iter;
	dbus_message_iter_recurse(&args_iter, &changed_iter);
	while (dbus_message_iter_get_arg_type(&changed_iter) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry_iter;
		dbus_message_iter_recurse(&changed_iter, &entry_iter);
		if (dbus_message_iter_get_arg_type(&entry_iter) != DBUS_TYPE_STRING) {
			dbus_message_iter_next(&changed_iter);
			continue;
		}

		const char *key = nullptr;
		dbus_message_iter_get_basic(&entry_iter, &key);
		const godot::String property_name = dbus_string_to_godot(key);

		if (!dbus_message_iter_next(&entry_iter) ||
				dbus_message_iter_get_arg_type(&entry_iter) != DBUS_TYPE_VARIANT) {
			dbus_message_iter_next(&changed_iter);
			continue;
		}

		if (property_name == "Address") {
			BluezDBus::read_variant_string(&entry_iter, props.address);
		} else if (property_name == "Name") {
			BluezDBus::read_variant_string(&entry_iter, props.name);
		} else if (property_name == "Alias") {
			BluezDBus::read_variant_string(&entry_iter, props.alias);
		} else if (property_name == "Paired") {
			paired_changed = BluezDBus::read_variant_bool(&entry_iter, props.paired);
		} else if (property_name == "Connected") {
			connected_changed = BluezDBus::read_variant_bool(&entry_iter, props.connected);
		} else if (property_name == "Trusted") {
			BluezDBus::read_variant_bool(&entry_iter, props.trusted);
		} else if (property_name == "Class") {
			BluezDBus::read_variant_uint32(&entry_iter, props.device_class);
		} else if (property_name == "RSSI") {
			int16_t rssi_value = 0;
			if (BluezDBus::read_variant_int16(&entry_iter, rssi_value)) {
				props.rssi = rssi_value;
				props.has_rssi = true;
				rssi_changed = true;
			}
		}

		dbus_message_iter_next(&changed_iter);
	}

	{
		godot::String existing_key;
		DeviceInfo existing_info;
		if (cache.find_by_device_id(device_path, existing_key, existing_info)) {
			if (props.address.is_empty()) {
				props.address = existing_info.address;
			}
			if (props.name.is_empty()) {
				props.name = existing_info.name;
			}
			if (!paired_changed) {
				props.paired = existing_info.paired;
			}
			if (!connected_changed) {
				props.connected = existing_info.connected;
			}
			if (!rssi_changed && existing_info.has_rssi) {
				props.rssi = existing_info.rssi;
				props.has_rssi = true;
			}
			props.trusted = existing_info.trusted || props.trusted;
		}
	}

	const DeviceInfo info = device_info_from_bluez(props);
	upsert_device(info, true, paired_changed || connected_changed || rssi_changed);

	if (connected_changed && is_valid_bluetooth_address(info.address)) {
		BluetoothEvent event;
		event.type = EventType::CONNECTION_CHANGED;
		event.address = normalize_address(info.address);
		event.connected = info.connected;
		event.message = info.connected ? "Device connected." : "Device disconnected.";
		emit(event);
	}
}

void LinuxBackend::handle_interfaces_removed(DBusMessage *p_message) {
	DBusMessageIter args_iter;
	if (!dbus_message_iter_init(p_message, &args_iter)) {
		return;
	}

	const char *object_path = nullptr;
	if (dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_OBJECT_PATH) {
		return;
	}
	dbus_message_iter_get_basic(&args_iter, &object_path);
	const godot::String path = dbus_string_to_godot(object_path);

	if (!dbus_message_iter_next(&args_iter) || dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_ARRAY) {
		return;
	}

	bool device_removed = false;
	DBusMessageIter interfaces_iter;
	dbus_message_iter_recurse(&args_iter, &interfaces_iter);
	while (dbus_message_iter_get_arg_type(&interfaces_iter) == DBUS_TYPE_STRING) {
		const char *iface_name = nullptr;
		dbus_message_iter_get_basic(&interfaces_iter, &iface_name);
		if (iface_name && godot::String(iface_name) == DEVICE_INTERFACE) {
			device_removed = true;
			break;
		}
		dbus_message_iter_next(&interfaces_iter);
	}

	if (!device_removed) {
		return;
	}

	godot::String removal_address;
	godot::String removal_key;
	DeviceInfo removal_info;
	if (cache.find_by_device_id(path, removal_key, removal_info)) {
		removal_address = removal_info.address;
		cache.remove(removal_key, removal_address);
		emit_device_removed(removal_address);
	}
}

void LinuxBackend::handle_dbus_signal(DBusMessage *p_message) {
	if (!p_message || dbus_message_get_type(p_message) != DBUS_MESSAGE_TYPE_SIGNAL) {
		return;
	}

	const char *interface = dbus_message_get_interface(p_message);
	const char *member = dbus_message_get_member(p_message);
	if (!interface || !member) {
		return;
	}

	const godot::String iface = interface;
	const godot::String signal_member = member;
	if (iface == OBJECT_MANAGER_INTERFACE && signal_member == "InterfacesAdded") {
		handle_interfaces_added(p_message);
	} else if (iface == OBJECT_MANAGER_INTERFACE && signal_member == "InterfacesRemoved") {
		handle_interfaces_removed(p_message);
	} else if (iface == PROPERTIES_INTERFACE && signal_member == "PropertiesChanged") {
		handle_properties_changed(p_message);
	}
}

bool LinuxBackend::initialize() {
	if (initialized) {
		return true;
	}

	if (!dbus.connect()) {
		emit_error("initialize", "initialize failed: " + dbus.get_last_error());
		return false;
	}

	dbus.set_signal_handler([this](DBusMessage *p_message) {
		handle_dbus_signal(p_message);
	});

	agent.set_pairing_state(&pairing_pending);
	agent.set_pairing_request_handler([this](const godot::String &p_address, const godot::String &p_kind,
												 const godot::String &p_pin) {
		BluetoothEvent event;
		event.address = p_address;
		event.pairing_kind = p_kind;
		event.display_pin = p_pin;
		if (p_kind == "confirm") {
			event.type = EventType::PAIRING_CONFIRMATION_REQUESTED;
		} else if (p_kind == "provide_pin") {
			event.type = EventType::PAIRING_PIN_REQUESTED;
		} else if (p_kind == "display_pin") {
			event.type = EventType::PAIRING_DISPLAY_PIN;
		} else {
			return;
		}
		emit(event);
	});

	godot::String agent_error;
	if (!agent.register_agent(dbus, &agent_error)) {
		emit_error("initialize",
				"initialize failed: could not register BlueZ pairing agent at " + godot::String(BluezAgent::AGENT_PATH) +
						". " + agent_error +
						" Ensure bluetoothd is running and your user has BlueZ D-Bus permissions (bluetooth group).",
				BluetoothErrorCode::PERMISSION_DENIED);
		dbus.disconnect();
		return false;
	}

	if (!dbus.find_default_adapter(adapter_path)) {
		emit_error("initialize", "initialize failed: " + dbus.get_last_error());
		agent.unregister_agent(dbus);
		dbus.disconnect();
		return false;
	}

	ensure_adapter_powered();
	register_signal_matches();

	initialized = true;
	return true;
}

void LinuxBackend::shutdown() {
	stop_scan();
	if (initialized) {
		if (signal_matches_registered) {
			dbus.clear_matches();
			signal_matches_registered = false;
		}
		agent.unregister_agent(dbus);
		dbus.disconnect();
		initialized = false;
	}
}

void LinuxBackend::poll() {
	if (!initialized) {
		return;
	}
	dbus.poll(0);

	if (scanning && scan_deadline.has_value()) {
		if (std::chrono::steady_clock::now() >= *scan_deadline) {
			stop_scan();
		}
	}
}

void LinuxBackend::start_scan(const ScanOptions &p_options) {
	if (!initialized && !initialize()) {
		return;
	}
	if (scanning) {
		return;
	}

	scan_options = p_options;
	if (p_options.timeout_seconds > 0) {
		scan_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(p_options.timeout_seconds);
	} else {
		scan_deadline.reset();
	}

	BluetoothEvent started;
	started.type = EventType::SCAN_STARTED;
	emit(started);

	enumerate_devices(false, false);
	emit_paired_devices_updated();

	if (!dbus.adapter_set_discovery_filter(adapter_path, p_options.min_rssi)) {
		emit_error("start_scan", "start_scan failed to set discovery filter: " + dbus.get_last_error());
		return;
	}

	if (!dbus.adapter_start_discovery(adapter_path)) {
		dbus.adapter_clear_discovery_filter(adapter_path);
		emit_error("start_scan", "start_scan failed: " + dbus.get_last_error());
		return;
	}

	scanning = true;
}

void LinuxBackend::stop_scan() {
	if (!scanning) {
		return;
	}

	dbus.adapter_stop_discovery(adapter_path);
	dbus.adapter_clear_discovery_filter(adapter_path);
	scanning = false;
	scan_deadline.reset();

	BluetoothEvent event;
	event.type = EventType::SCAN_STOPPED;
	emit(event);
}

void LinuxBackend::pair_device_at_path(const godot::String &p_device_path, const godot::String &p_event_address) {
	const godot::String event_address = is_valid_bluetooth_address(p_event_address)
			? normalize_address(p_event_address)
			: p_event_address;

	BluetoothEvent started;
	started.type = EventType::PAIRING_STARTED;
	started.address = event_address;
	emit(started);

	if (p_device_path.is_empty()) {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = event_address;
		failed.message = "pair_device failed: device not found in BlueZ cache. Start a scan and ensure the device is in pairing mode.";
		failed.error_code = BluetoothErrorCode::DEVICE_NOT_FOUND;
		emit(failed);
		return;
	}

	bool already_paired = false;
	if (!event_address.is_empty() &&
			cache.lookup_cached_state(event_address, already_paired, [](const DeviceInfo &p_info) { return p_info.paired; }) &&
			already_paired) {
		BluetoothEvent succeeded;
		succeeded.type = EventType::PAIRING_SUCCEEDED;
		succeeded.address = event_address;
		emit(succeeded);
		refresh_paired_devices();
		return;
	}

	if (dbus.device_pair(p_device_path)) {
		dbus.device_set_trusted(p_device_path, true);
		BluezDeviceProperties props;
		if (dbus.get_device_properties(p_device_path, props)) {
			const DeviceInfo info = device_info_from_bluez(props);
			upsert_device(info, true, true);
		}
		BluetoothEvent succeeded;
		succeeded.type = EventType::PAIRING_SUCCEEDED;
		succeeded.address = event_address;
		emit(succeeded);
		refresh_paired_devices();
	} else {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = event_address;
		failed.message = "pair_device failed for address \"" + event_address + "\" (device_path=\"" + p_device_path +
				"\"): " + dbus.get_last_error();
		failed.error_code = infer_error_code(failed.message);
		emit(failed);
	}
}

void LinuxBackend::pair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	BluetoothEvent started;
	started.type = EventType::PAIRING_STARTED;
	started.address = normalized;
	emit(started);

	if (!is_valid_bluetooth_address(normalized)) {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = normalized;
		failed.message = "pair_device failed for address \"" + normalized +
				"\": not a valid 6-byte Bluetooth MAC address (expected format AA:BB:CC:DD:EE:FF).";
		failed.error_code = BluetoothErrorCode::INVALID_ADDRESS;
		emit(failed);
		return;
	}

	const godot::String device_path = resolve_device_path(normalized);
	if (device_path.is_empty()) {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = normalized;
		failed.message = "pair_device failed for address \"" + normalized +
				"\": device not found in BlueZ cache. Start a scan and ensure the device is in pairing mode.";
		failed.error_code = BluetoothErrorCode::DEVICE_NOT_FOUND;
		emit(failed);
		return;
	}

	bool already_paired = false;
	if (cache.lookup_cached_state(normalized, already_paired, [](const DeviceInfo &p_info) { return p_info.paired; }) &&
			already_paired) {
		BluetoothEvent succeeded;
		succeeded.type = EventType::PAIRING_SUCCEEDED;
		succeeded.address = normalized;
		emit(succeeded);
		refresh_paired_devices();
		return;
	}

	if (dbus.device_pair(device_path)) {
		dbus.device_set_trusted(device_path, true);
		BluezDeviceProperties props;
		if (dbus.get_device_properties(device_path, props)) {
			const DeviceInfo info = device_info_from_bluez(props);
			upsert_device(info, true, true);
		}
		BluetoothEvent succeeded;
		succeeded.type = EventType::PAIRING_SUCCEEDED;
		succeeded.address = normalized;
		emit(succeeded);
		refresh_paired_devices();
	} else {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = normalized;
		failed.message = "pair_device failed for address \"" + normalized + "\" (device_path=\"" + device_path +
				"\"): " + dbus.get_last_error();
		failed.error_code = infer_error_code(failed.message);
		emit(failed);
	}
}

void LinuxBackend::pair_device_by_id(const godot::String &p_device_id) {
	godot::String event_address = p_device_id;
	godot::String key;
	DeviceInfo cached;
	if (cache.find_by_device_id(p_device_id, key, cached) && !cached.address.is_empty()) {
		event_address = cached.address;
	}

	pair_device_at_path(p_device_id, event_address);
}

void LinuxBackend::unpair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	const godot::String device_path = resolve_device_path(normalized);
	if (device_path.is_empty()) {
		emit_error("unpair_device",
				"unpair_device failed for address \"" + normalized +
						"\": device not found in BlueZ cache or managed objects.",
				BluetoothErrorCode::DEVICE_NOT_FOUND);
		return;
	}

	if (is_connected(normalized)) {
		if (dbus.device_disconnect(device_path)) {
			BluezDeviceProperties props;
			if (dbus.get_device_properties(device_path, props)) {
				const DeviceInfo info = device_info_from_bluez(props);
				upsert_device(info, true, true);
			}
			BluetoothEvent disconnected;
			disconnected.type = EventType::CONNECTION_CHANGED;
			disconnected.address = normalized;
			disconnected.connected = false;
			disconnected.message = "unpair_device disconnected \"" + normalized + "\" before removing pairing.";
			emit(disconnected);
		} else {
			BluetoothEvent disconnect_note;
			disconnect_note.type = EventType::CONNECTION_CHANGED;
			disconnect_note.address = normalized;
			disconnect_note.connected = true;
			disconnect_note.message = "unpair_device: disconnect before unpair failed for \"" + normalized +
					"\" (" + dbus.get_last_error() + "); proceeding with unpair.";
			emit(disconnect_note);
		}
	}

	if (dbus.adapter_remove_device(adapter_path, device_path)) {
		godot::String removal_key = normalized;
		godot::String removal_address = normalized;
		DeviceInfo removal_info;
		if (cache.find_by_device_id(device_path, removal_key, removal_info)) {
			removal_address = removal_info.address;
		}
		cache.remove(removal_key, removal_address);
		emit_device_removed(normalized);
	} else {
		emit_error("unpair_device",
				"unpair_device failed for address \"" + normalized + "\" (device_path=\"" + device_path +
						"\"): " + dbus.get_last_error());
	}
}

void LinuxBackend::connect_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	if (!is_valid_bluetooth_address(normalized)) {
		emit_error("connect_device",
				"connect_device failed for input \"" + normalized +
						"\": not a valid 6-byte Bluetooth MAC address (expected format AA:BB:CC:DD:EE:FF).",
				BluetoothErrorCode::INVALID_ADDRESS);
		return;
	}

	const godot::String device_path = resolve_device_path(normalized);
	if (device_path.is_empty()) {
		emit_error("connect_device",
				"connect_device failed for address \"" + normalized +
						"\": device not found. Pair the device first or run a scan while it is powered on.",
				BluetoothErrorCode::DEVICE_NOT_FOUND);
		return;
	}

	if (dbus.device_connect(device_path)) {
		BluezDeviceProperties props;
		if (dbus.get_device_properties(device_path, props)) {
			const DeviceInfo info = device_info_from_bluez(props);
			upsert_device(info, true, true);
		}
		BluetoothEvent event;
		event.type = EventType::CONNECTION_CHANGED;
		event.address = normalized;
		event.connected = true;
		event.message = "connect_device succeeded for address \"" + normalized + "\".";
		emit(event);
	} else {
		emit_error("connect_device",
				"connect_device failed for address \"" + normalized + "\" (device_path=\"" + device_path +
						"\"): " + dbus.get_last_error());
	}
}

void LinuxBackend::disconnect_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	const godot::String device_path = resolve_device_path(normalized);
	if (device_path.is_empty()) {
		emit_error("disconnect_device",
				"disconnect_device failed for address \"" + normalized + "\": device not found in BlueZ cache.",
				BluetoothErrorCode::DEVICE_NOT_FOUND);
		return;
	}

	if (dbus.device_disconnect(device_path)) {
		BluezDeviceProperties props;
		bool disconnected = false;
		if (dbus.get_device_properties(device_path, props)) {
			const DeviceInfo info = device_info_from_bluez(props);
			upsert_device(info, true, true);
			disconnected = !info.connected;
		} else {
			disconnected = true;
		}

		if (disconnected) {
			BluetoothEvent event;
			event.type = EventType::CONNECTION_CHANGED;
			event.address = normalized;
			event.connected = false;
			event.message = "disconnect_device succeeded for address \"" + normalized + "\".";
			emit(event);
		} else {
			emit_error("disconnect_device",
					"disconnect_device for address \"" + normalized +
							"\": Device1.Disconnect returned success but BlueZ still reports Connected=true.");
		}
	} else {
		emit_error("disconnect_device",
				"disconnect_device failed for address \"" + normalized + "\" (device_path=\"" + device_path +
						"\"): " + dbus.get_last_error() +
						". HID gamepads may reconnect automatically when powered on.");
	}
}

void LinuxBackend::refresh_paired_devices() {
	if (!initialized && !initialize()) {
		return;
	}
	enumerate_devices(false, false);
	emit_paired_devices_updated();
}

void LinuxBackend::confirm_pairing(const godot::String &p_pin) {
	PairingUserResponse response;
	response.accepted = true;
	response.pin = p_pin;
	pairing_pending.submit(response);
}

void LinuxBackend::reject_pairing() {
	PairingUserResponse response;
	response.accepted = false;
	pairing_pending.submit(response);
}

void LinuxBackend::cancel_pairing() {
	reject_pairing();
	pairing_pending.reset();
}

void LinuxBackend::submit_pairing_response(const PairingUserResponse &p_response) {
	pairing_pending.submit(p_response);
}

bool LinuxBackend::is_connected(const godot::String &p_address) {
	bool connected = false;
	if (cache.lookup_cached_state(p_address, connected, [](const DeviceInfo &p_info) { return p_info.connected; })) {
		return connected;
	}

	const godot::String device_path = resolve_device_path(p_address);
	if (device_path.is_empty()) {
		return false;
	}

	BluezDeviceProperties props;
	if (dbus.get_device_properties(device_path, props)) {
		const DeviceInfo info = device_info_from_bluez(props);
		upsert_device(info, false);
		return info.connected;
	}
	return false;
}

bool LinuxBackend::is_paired(const godot::String &p_address) {
	bool paired = false;
	if (cache.lookup_cached_state(p_address, paired, [](const DeviceInfo &p_info) { return p_info.paired; })) {
		return paired;
	}

	const godot::String device_path = resolve_device_path(p_address);
	if (device_path.is_empty()) {
		return false;
	}

	BluezDeviceProperties props;
	if (dbus.get_device_properties(device_path, props)) {
		const DeviceInfo info = device_info_from_bluez(props);
		upsert_device(info, false);
		return info.paired;
	}
	return false;
}

bool LinuxBackend::is_radio_on() const {
	return adapter_powered;
}

godot::Dictionary LinuxBackend::get_capabilities() const {
	godot::Dictionary caps;
	caps["platform"] = "linux";
	caps["implemented"] = true;
	caps["supports_ble"] = true;
	caps["supports_device_id"] = true;
	caps["supports_rssi"] = true;
	caps["can_disconnect_hid"] = false;
	caps["can_unpair_while_connected"] = true;
	caps["needs_pin_ui"] = true;
	caps["pair_by_address"] = true;
	caps["pair_by_device_id"] = true;
	caps["interactive_pairing"] = true;
	caps["scan_named_only_filter"] = true;
	caps["scan_gamepads_only_filter"] = true;
	caps["scan_min_rssi_filter"] = true;
	caps["scan_timeout"] = true;
	caps["radio_control"] = true;
	return caps;
}

} // namespace bluetooth