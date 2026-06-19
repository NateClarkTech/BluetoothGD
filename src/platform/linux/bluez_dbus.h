#pragma once

#include <dbus/dbus.h>

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>

#include <functional>
#include <vector>

namespace bluetooth {

struct BluezDeviceProperties {
	godot::String object_path;
	godot::String address;
	godot::String name;
	godot::String alias;
	bool paired = false;
	bool connected = false;
	bool trusted = false;
	uint32_t device_class = 0;
	int16_t rssi = 0;
	bool has_rssi = false;
	bool valid = false;
};

struct BluezAdapterInfo {
	godot::String object_path;
	bool powered = false;
	bool discovering = false;
	bool valid = false;
};

class BluezDBus {
public:
	using SignalHandler = std::function<void(DBusMessage *p_message)>;

	~BluezDBus();

	bool connect();
	void disconnect();
	bool is_connected() const { return connection != nullptr; }

	void poll(int p_timeout_ms = 0);

	bool add_match(const godot::String &p_rule, const godot::String &p_tag);
	void remove_match(const godot::String &p_tag);
	void clear_matches();

	void set_signal_handler(SignalHandler p_handler);

	bool get_managed_objects(std::vector<BluezDeviceProperties> &p_devices, std::vector<BluezAdapterInfo> &p_adapters);
	bool find_default_adapter(godot::String &p_out_path);

	bool adapter_start_discovery(const godot::String &p_adapter_path);
	bool adapter_stop_discovery(const godot::String &p_adapter_path);
	bool adapter_remove_device(const godot::String &p_adapter_path, const godot::String &p_device_path);

	bool device_pair(const godot::String &p_device_path, int p_timeout_ms = 60000);
	bool device_connect(const godot::String &p_device_path, int p_timeout_ms = 30000);
	bool device_disconnect(const godot::String &p_device_path, int p_timeout_ms = 30000);
	bool device_set_trusted(const godot::String &p_device_path, bool p_trusted = true);

	bool adapter_set_powered(const godot::String &p_adapter_path, bool p_powered = true);
	bool adapter_set_discovery_filter(const godot::String &p_adapter_path, int p_min_rssi = -127,
			const godot::String &p_transport = "auto");
	bool adapter_clear_discovery_filter(const godot::String &p_adapter_path);

	bool get_device_properties(const godot::String &p_device_path, BluezDeviceProperties &p_out_props);
	bool get_all_device_properties(const godot::String &p_device_path, BluezDeviceProperties &p_out_props);

	static bool parse_interface_properties(const godot::String &p_path,
			const godot::String &p_interface,
			DBusMessageIter *p_props_iter,
			BluezDeviceProperties &p_device,
			BluezAdapterInfo &p_adapter);

	static bool read_variant_string(DBusMessageIter *p_iter, godot::String &p_out);
	static bool read_variant_bool(DBusMessageIter *p_iter, bool &p_out);
	static bool read_variant_uint32(DBusMessageIter *p_iter, uint32_t &p_out);
	static bool read_variant_int16(DBusMessageIter *p_iter, int16_t &p_out);

	godot::String get_last_error() const { return last_error; }
	void set_last_error(const godot::String &p_error) { last_error = p_error; }
	godot::String format_dbus_error(const DBusError &p_error) const;

	DBusConnection *get_connection() { return connection; }

private:
	DBusConnection *connection = nullptr;
	godot::String last_error;
	SignalHandler signal_handler;
	godot::HashMap<godot::String, godot::String> active_matches;
	int blocking_depth = 0;

	static DBusHandlerResult static_message_filter(DBusConnection *p_connection, DBusMessage *p_message, void *p_user_data);
	DBusHandlerResult handle_message(DBusMessage *p_message);

	bool send_method_call(const godot::String &p_destination,
			const godot::String &p_path,
			const godot::String &p_interface,
			const godot::String &p_method,
			DBusMessage **p_out_reply,
			int p_timeout_ms,
			const std::function<void(DBusMessage *)> &p_append_args = nullptr);

	bool get_property(const godot::String &p_path,
			const godot::String &p_interface,
			const godot::String &p_property,
			const std::function<bool(DBusMessageIter *)> &p_reader);

	bool set_property_bool(const godot::String &p_path,
			const godot::String &p_interface,
			const godot::String &p_property,
			bool p_value);
};

} // namespace bluetooth