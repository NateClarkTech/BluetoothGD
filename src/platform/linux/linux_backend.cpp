#include "linux_backend.h"

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

} // namespace

LinuxBackend::~LinuxBackend() {
	shutdown();
}

void LinuxBackend::emit(const BluetoothEvent &p_event) {
	if (on_event) {
		on_event(p_event);
	}
}

void LinuxBackend::emit_error(const godot::String &p_operation, const godot::String &p_message) {
	BluetoothEvent event;
	event.type = EventType::ERROR_OCCURRED;
	event.operation = p_operation;
	event.message = p_message;
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
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			event.devices.push_back(item.value.to_dictionary());
		}
	}
	emit(event);
}

void LinuxBackend::remove_device_from_cache(const godot::String &p_key, const godot::String &p_address) {
	std::lock_guard<std::mutex> lock(state_mutex);
	discovered_devices.erase(p_key);
	paired_devices.erase(p_key);
	if (is_valid_bluetooth_address(p_address)) {
		address_to_device_id.erase(normalize_address(p_address));
	}
}

godot::String LinuxBackend::device_cache_key(const DeviceInfo &p_info) const {
	if (is_valid_bluetooth_address(p_info.address)) {
		return normalize_address(p_info.address);
	}
	if (!p_info.device_id.is_empty()) {
		return p_info.device_id;
	}
	return p_info.address;
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

	if (info.address.is_empty()) {
		info.address = info.device_id;
	}
	clear_unhelpful_device_name(info);
	return info;
}

void LinuxBackend::upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit) {
	DeviceInfo info = p_info;
	if (is_valid_bluetooth_address(info.address)) {
		info.address = normalize_address(info.address);
	}
	const godot::String key = device_cache_key(info);
	bool is_new = false;

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		is_new = !discovered_devices.has(key);

		godot::PackedStringArray stale_keys;
		for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.key == key) {
				continue;
			}
			if (!info.device_id.is_empty() && item.value.device_id == info.device_id) {
				stale_keys.append(item.key);
				continue;
			}
			if (is_valid_bluetooth_address(info.address) && addresses_match(item.value.address, info.address)) {
				stale_keys.append(item.key);
			}
		}
		for (int i = 0; i < stale_keys.size(); i++) {
			discovered_devices.erase(stale_keys[i]);
			paired_devices.erase(stale_keys[i]);
		}

		if (discovered_devices.has(key)) {
			const DeviceInfo existing = discovered_devices[key];
			if (info.name.is_empty()) {
				info.name = existing.name;
			}
		}

		discovered_devices[key] = info;
		if (is_valid_bluetooth_address(info.address)) {
			address_to_device_id[info.address] = info.device_id;
		}
		if (info.paired) {
			paired_devices[key] = info;
		} else {
			paired_devices.erase(key);
		}
	}

	if (p_emit_event && (is_new || p_force_emit)) {
		BluetoothEvent event;
		event.type = EventType::DEVICE_FOUND;
		event.device = info;
		emit(event);
	}
}

bool LinuxBackend::lookup_cached_state(const godot::String &p_address, bool &p_out_value,
		const std::function<bool(const DeviceInfo &)> &p_selector) {
	const godot::String normalized = normalize_address(p_address);
	std::lock_guard<std::mutex> lock(state_mutex);
	if (discovered_devices.has(normalized)) {
		p_out_value = p_selector(discovered_devices[normalized]);
		return true;
	}
	for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
		if (addresses_match(item.value.address, normalized)) {
			p_out_value = p_selector(item.value);
			return true;
		}
	}
	return false;
}

godot::String LinuxBackend::resolve_device_path(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		if (address_to_device_id.has(normalized)) {
			return address_to_device_id[normalized];
		}
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.key == normalized || addresses_match(item.value.address, normalized)) {
				return item.value.device_id;
			}
		}
	}

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
		}

		dbus_message_iter_next(&changed_iter);
	}

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.value.device_id == device_path) {
				if (props.address.is_empty()) {
					props.address = item.value.address;
				}
				if (props.name.is_empty()) {
					props.name = item.value.name;
				}
				if (!paired_changed) {
					props.paired = item.value.paired;
				}
				if (!connected_changed) {
					props.connected = item.value.connected;
				}
				props.trusted = item.value.trusted || props.trusted;
				break;
			}
		}
	}

	const DeviceInfo info = device_info_from_bluez(props);
	upsert_device(info, true, paired_changed || connected_changed);

	if (paired_changed && info.paired && is_valid_bluetooth_address(info.address)) {
		BluetoothEvent paired;
		paired.type = EventType::PAIRING_SUCCEEDED;
		paired.address = normalize_address(info.address);
		emit(paired);
	}
	// Paired=false updates via upsert_device above (force-emits DEVICE_FOUND).
	// Removal is handled by unpair_device, InterfacesRemoved, or explicit DEVICE_REMOVED.

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
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.value.device_id == path) {
				removal_key = item.key;
				removal_address = item.value.address;
				break;
			}
		}
	}

	if (!removal_key.is_empty()) {
		remove_device_from_cache(removal_key, removal_address);
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

	godot::String agent_error;
	if (!agent.register_agent(dbus, &agent_error)) {
		emit_error("initialize",
				"initialize failed: could not register BlueZ pairing agent at " + godot::String(BluezAgent::AGENT_PATH) +
						". " + agent_error +
						" Ensure bluetoothd is running and your user has BlueZ D-Bus permissions (bluetooth group).");
		dbus.disconnect();
		return false;
	}

	if (!dbus.find_default_adapter(adapter_path)) {
		emit_error("initialize", "initialize failed: " + dbus.get_last_error());
		agent.unregister_agent(dbus);
		dbus.disconnect();
		return false;
	}

	initialized = true;
	refresh_paired_devices();
	return true;
}

void LinuxBackend::shutdown() {
	stop_scan();
	if (initialized) {
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
}

void LinuxBackend::start_scan() {
	if (!initialized && !initialize()) {
		return;
	}
	if (scanning) {
		return;
	}

	BluetoothEvent started;
	started.type = EventType::SCAN_STARTED;
	emit(started);

	enumerate_devices(false, false);
	emit_paired_devices_updated();

	dbus.add_match("type='signal',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded',path='/'",
			"interfaces_added");
	dbus.add_match("type='signal',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesRemoved',path='/'",
			"interfaces_removed");
	dbus.add_match(
			"type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',arg0='org.bluez.Device1'",
			"device_properties_changed");

	if (!dbus.adapter_start_discovery(adapter_path)) {
		dbus.remove_match("interfaces_added");
		dbus.remove_match("interfaces_removed");
		dbus.remove_match("device_properties_changed");
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
	dbus.remove_match("interfaces_added");
	dbus.remove_match("interfaces_removed");
	dbus.remove_match("device_properties_changed");
	scanning = false;

	BluetoothEvent event;
	event.type = EventType::SCAN_STOPPED;
	emit(event);
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
		emit(failed);
		return;
	}

	bool already_paired = false;
	if (lookup_cached_state(normalized, already_paired, [](const DeviceInfo &p_info) { return p_info.paired; }) &&
			already_paired) {
		BluetoothEvent succeeded;
		succeeded.type = EventType::PAIRING_SUCCEEDED;
		succeeded.address = normalized;
		emit(succeeded);
		refresh_paired_devices();
		return;
	}

	if (dbus.device_pair(device_path)) {
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
		emit(failed);
	}
}

void LinuxBackend::unpair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	const godot::String device_path = resolve_device_path(normalized);
	if (device_path.is_empty()) {
		emit_error("unpair_device",
				"unpair_device failed for address \"" + normalized +
						"\": device not found in BlueZ cache or managed objects.");
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
			emit_error("unpair_device",
					"unpair_device failed to disconnect \"" + normalized + "\" (device_path=\"" + device_path +
							"\") before unpair: " + dbus.get_last_error());
			return;
		}
	}

	if (dbus.adapter_remove_device(adapter_path, device_path)) {
		godot::String removal_key = normalized;
		{
			std::lock_guard<std::mutex> lock(state_mutex);
			for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
				if (item.value.device_id == device_path || addresses_match(item.value.address, normalized)) {
					removal_key = item.key;
					break;
				}
			}
		}
		remove_device_from_cache(removal_key, normalized);
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
						"\": not a valid 6-byte Bluetooth MAC address (expected format AA:BB:CC:DD:EE:FF).");
		return;
	}

	const godot::String device_path = resolve_device_path(normalized);
	if (device_path.is_empty()) {
		emit_error("connect_device",
				"connect_device failed for address \"" + normalized +
						"\": device not found. Pair the device first or run a scan while it is powered on.");
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
				"disconnect_device failed for address \"" + normalized + "\": device not found in BlueZ cache.");
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

bool LinuxBackend::is_connected(const godot::String &p_address) {
	bool connected = false;
	if (lookup_cached_state(p_address, connected, [](const DeviceInfo &p_info) { return p_info.connected; })) {
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
	if (lookup_cached_state(p_address, paired, [](const DeviceInfo &p_info) { return p_info.paired; })) {
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

} // namespace bluetooth