#include "bluez_dbus.h"

namespace bluetooth {

namespace {

constexpr const char *BLUEZ_SERVICE = "org.bluez";
constexpr const char *BLUEZ_ROOT_PATH = "/";
constexpr const char *OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager";
constexpr const char *PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";
constexpr const char *ADAPTER_INTERFACE = "org.bluez.Adapter1";
constexpr const char *DEVICE_INTERFACE = "org.bluez.Device1";

godot::String dbus_string_to_godot(const char *p_value) {
	return p_value ? godot::String(p_value) : godot::String();
}

bool iter_next(DBusMessageIter *p_iter, DBusMessageIter *p_sub) {
	if (dbus_message_iter_get_arg_type(p_iter) == DBUS_TYPE_INVALID) {
		return false;
	}
	dbus_message_iter_recurse(p_iter, p_sub);
	return true;
}

} // namespace

BluezDBus::~BluezDBus() {
	disconnect();
}

bool BluezDBus::connect() {
	if (connection != nullptr) {
		return true;
	}

	static bool threads_initialized = false;
	if (!threads_initialized) {
		dbus_threads_init_default();
		threads_initialized = true;
	}

	DBusError error;
	dbus_error_init(&error);
	connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (dbus_error_is_set(&error)) {
		last_error = format_dbus_error(error);
		dbus_error_free(&error);
		connection = nullptr;
		return false;
	}
	if (!connection) {
		last_error = "dbus_bus_get(DBUS_BUS_SYSTEM) returned null.";
		return false;
	}

	dbus_connection_add_filter(connection, static_message_filter, this, nullptr);

	dbus_error_init(&error);
	dbus_bus_request_name(connection, "org.bluetoothgd", DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &error);
	if (dbus_error_is_set(&error)) {
		dbus_error_free(&error);
	}

	return true;
}

void BluezDBus::disconnect() {
	clear_matches();
	if (connection) {
		dbus_connection_remove_filter(connection, static_message_filter, this);
		dbus_connection_unref(connection);
		connection = nullptr;
	}
}

void BluezDBus::poll(int p_timeout_ms) {
	if (!connection) {
		return;
	}
	dbus_connection_read_write(connection, p_timeout_ms);
	while (dbus_connection_get_dispatch_status(connection) == DBUS_DISPATCH_DATA_REMAINS) {
		dbus_connection_dispatch(connection);
	}
}

godot::String BluezDBus::format_dbus_error(const DBusError &p_error) const {
	if (!p_error.name && !p_error.message) {
		return "Unknown D-Bus error.";
	}
	godot::String message;
	if (p_error.name) {
		message += godot::String(p_error.name);
	}
	if (p_error.message) {
		if (!message.is_empty()) {
			message += ": ";
		}
		message += godot::String(p_error.message);
	}
	return message;
}

bool BluezDBus::add_match(const godot::String &p_rule, const godot::String &p_tag) {
	if (!connection) {
		last_error = "D-Bus connection is not open.";
		return false;
	}
	if (active_matches.has(p_tag)) {
		return true;
	}

	DBusError error;
	dbus_error_init(&error);
	const godot::CharString rule_utf8 = p_rule.utf8();
	dbus_bus_add_match(connection, rule_utf8.get_data(), &error);
	if (dbus_error_is_set(&error)) {
		last_error = format_dbus_error(error);
		dbus_error_free(&error);
		return false;
	}

	active_matches[p_tag] = p_rule;
	return true;
}

void BluezDBus::remove_match(const godot::String &p_tag) {
	if (!connection) {
		return;
	}
	if (!active_matches.has(p_tag)) {
		return;
	}

	DBusError error;
	dbus_error_init(&error);
	const godot::CharString rule_utf8 = active_matches[p_tag].utf8();
	dbus_bus_remove_match(connection, rule_utf8.get_data(), &error);
	dbus_error_free(&error);
	active_matches.erase(p_tag);
}

void BluezDBus::clear_matches() {
	if (!connection) {
		active_matches.clear();
		return;
	}
	for (const godot::KeyValue<godot::String, godot::String> &entry : active_matches) {
		DBusError error;
		dbus_error_init(&error);
		const godot::CharString rule_utf8 = entry.value.utf8();
		dbus_bus_remove_match(connection, rule_utf8.get_data(), &error);
		dbus_error_free(&error);
	}
	active_matches.clear();
}

void BluezDBus::set_signal_handler(SignalHandler p_handler) {
	signal_handler = std::move(p_handler);
}

DBusHandlerResult BluezDBus::static_message_filter(DBusConnection *p_connection, DBusMessage *p_message, void *p_user_data) {
	(void)p_connection;
	BluezDBus *self = static_cast<BluezDBus *>(p_user_data);
	return self ? self->handle_message(p_message) : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusHandlerResult BluezDBus::handle_message(DBusMessage *p_message) {
	if (!p_message || dbus_message_get_type(p_message) != DBUS_MESSAGE_TYPE_SIGNAL) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (blocking_depth > 0 || !signal_handler) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	signal_handler(p_message);
	return DBUS_HANDLER_RESULT_HANDLED;
}

bool BluezDBus::read_variant_string(DBusMessageIter *p_iter, godot::String &p_out) {
	if (dbus_message_iter_get_arg_type(p_iter) != DBUS_TYPE_VARIANT) {
		return false;
	}
	DBusMessageIter variant_iter;
	dbus_message_iter_recurse(p_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING) {
		return false;
	}
	const char *value = nullptr;
	dbus_message_iter_get_basic(&variant_iter, &value);
	p_out = dbus_string_to_godot(value);
	return true;
}

bool BluezDBus::read_variant_bool(DBusMessageIter *p_iter, bool &p_out) {
	if (dbus_message_iter_get_arg_type(p_iter) != DBUS_TYPE_VARIANT) {
		return false;
	}
	DBusMessageIter variant_iter;
	dbus_message_iter_recurse(p_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_BOOLEAN) {
		return false;
	}
	dbus_bool_t value = FALSE;
	dbus_message_iter_get_basic(&variant_iter, &value);
	p_out = value == TRUE;
	return true;
}

bool BluezDBus::read_variant_uint32(DBusMessageIter *p_iter, uint32_t &p_out) {
	if (dbus_message_iter_get_arg_type(p_iter) != DBUS_TYPE_VARIANT) {
		return false;
	}
	DBusMessageIter variant_iter;
	dbus_message_iter_recurse(p_iter, &variant_iter);
	const int variant_type = dbus_message_iter_get_arg_type(&variant_iter);
	if (variant_type == DBUS_TYPE_UINT32) {
		dbus_uint32_t value = 0;
		dbus_message_iter_get_basic(&variant_iter, &value);
		p_out = static_cast<uint32_t>(value);
		return true;
	}
	if (variant_type == DBUS_TYPE_UINT16) {
		dbus_uint16_t value = 0;
		dbus_message_iter_get_basic(&variant_iter, &value);
		p_out = static_cast<uint32_t>(value);
		return true;
	}
	return false;
}

bool BluezDBus::parse_interface_properties(const godot::String &p_path,
		const godot::String &p_interface,
		DBusMessageIter *p_props_iter,
		BluezDeviceProperties &p_device,
		BluezAdapterInfo &p_adapter) {
	if (dbus_message_iter_get_arg_type(p_props_iter) != DBUS_TYPE_ARRAY) {
		return false;
	}

	DBusMessageIter prop_iter;
	dbus_message_iter_recurse(p_props_iter, &prop_iter);

	while (dbus_message_iter_get_arg_type(&prop_iter) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry_iter;
		dbus_message_iter_recurse(&prop_iter, &entry_iter);
		if (dbus_message_iter_get_arg_type(&entry_iter) != DBUS_TYPE_STRING) {
			dbus_message_iter_next(&prop_iter);
			continue;
		}

		const char *key = nullptr;
		dbus_message_iter_get_basic(&entry_iter, &key);
		const godot::String property_name = dbus_string_to_godot(key);

		if (!dbus_message_iter_next(&entry_iter) ||
				dbus_message_iter_get_arg_type(&entry_iter) != DBUS_TYPE_VARIANT) {
			dbus_message_iter_next(&prop_iter);
			continue;
		}

		DBusMessageIter value_iter;
		dbus_message_iter_recurse(&entry_iter, &value_iter);

		if (p_interface == DEVICE_INTERFACE) {
			p_device.object_path = p_path;
			p_device.valid = true;
			if (property_name == "Address" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_STRING) {
				const char *value = nullptr;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_device.address = dbus_string_to_godot(value);
			} else if (property_name == "Name" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_STRING) {
				const char *value = nullptr;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_device.name = dbus_string_to_godot(value);
			} else if (property_name == "Alias" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_STRING) {
				const char *value = nullptr;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_device.alias = dbus_string_to_godot(value);
			} else if (property_name == "Paired" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t value = FALSE;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_device.paired = value == TRUE;
			} else if (property_name == "Connected" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t value = FALSE;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_device.connected = value == TRUE;
			} else if (property_name == "Trusted" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t value = FALSE;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_device.trusted = value == TRUE;
			} else if (property_name == "Class") {
				uint32_t class_value = 0;
				if (read_variant_uint32(&entry_iter, class_value)) {
					p_device.device_class = class_value;
				}
			}
		} else if (p_interface == ADAPTER_INTERFACE) {
			p_adapter.object_path = p_path;
			p_adapter.valid = true;
			if (property_name == "Powered" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t value = FALSE;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_adapter.powered = value == TRUE;
			} else if (property_name == "Discovering" && dbus_message_iter_get_arg_type(&value_iter) == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t value = FALSE;
				dbus_message_iter_get_basic(&value_iter, &value);
				p_adapter.discovering = value == TRUE;
			}
		}

		dbus_message_iter_next(&prop_iter);
	}

	return true;
}

bool BluezDBus::send_method_call(const godot::String &p_destination,
		const godot::String &p_path,
		const godot::String &p_interface,
		const godot::String &p_method,
		DBusMessage **p_out_reply,
		int p_timeout_ms,
		const std::function<void(DBusMessage *)> &p_append_args) {
	if (!connection) {
		last_error = "D-Bus connection is not open.";
		return false;
	}

	const godot::CharString dest_utf8 = p_destination.utf8();
	const godot::CharString path_utf8 = p_path.utf8();
	const godot::CharString iface_utf8 = p_interface.utf8();
	const godot::CharString method_utf8 = p_method.utf8();

	DBusMessage *message = dbus_message_new_method_call(dest_utf8.get_data(),
			path_utf8.get_data(),
			iface_utf8.get_data(),
			method_utf8.get_data());
	if (!message) {
		last_error = "dbus_message_new_method_call failed.";
		return false;
	}

	if (p_append_args) {
		p_append_args(message);
	}

	DBusError error;
	dbus_error_init(&error);
	blocking_depth++;
	*p_out_reply = dbus_connection_send_with_reply_and_block(connection, message, p_timeout_ms, &error);
	blocking_depth--;
	dbus_message_unref(message);

	if (dbus_error_is_set(&error)) {
		last_error = format_dbus_error(error);
		dbus_error_free(&error);
		return false;
	}
	if (!*p_out_reply) {
		last_error = "D-Bus method call returned null reply.";
		return false;
	}
	if (dbus_message_get_type(*p_out_reply) == DBUS_MESSAGE_TYPE_ERROR) {
		const char *error_name = dbus_message_get_error_name(*p_out_reply);
		last_error = dbus_string_to_godot(error_name);
		dbus_message_unref(*p_out_reply);
		*p_out_reply = nullptr;
		return false;
	}
	return true;
}

bool BluezDBus::get_managed_objects(std::vector<BluezDeviceProperties> &p_devices, std::vector<BluezAdapterInfo> &p_adapters) {
	p_devices.clear();
	p_adapters.clear();

	DBusMessage *reply = nullptr;
	if (!send_method_call(BLUEZ_SERVICE, BLUEZ_ROOT_PATH, OBJECT_MANAGER_INTERFACE, "GetManagedObjects", &reply, 10000)) {
		return false;
	}

	DBusMessageIter root_iter;
	if (!dbus_message_iter_init(reply, &root_iter) || dbus_message_iter_get_arg_type(&root_iter) != DBUS_TYPE_ARRAY) {
		last_error = "GetManagedObjects reply has unexpected format.";
		dbus_message_unref(reply);
		return false;
	}

	DBusMessageIter objects_iter;
	dbus_message_iter_recurse(&root_iter, &objects_iter);
	while (dbus_message_iter_get_arg_type(&objects_iter) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter dict_entry;
		dbus_message_iter_recurse(&objects_iter, &dict_entry);
		if (dbus_message_iter_get_arg_type(&dict_entry) != DBUS_TYPE_OBJECT_PATH) {
			dbus_message_iter_next(&objects_iter);
			continue;
		}

		const char *object_path = nullptr;
		dbus_message_iter_get_basic(&dict_entry, &object_path);
		const godot::String path = dbus_string_to_godot(object_path);

		if (!dbus_message_iter_next(&dict_entry) ||
				dbus_message_iter_get_arg_type(&dict_entry) != DBUS_TYPE_ARRAY) {
			dbus_message_iter_next(&objects_iter);
			continue;
		}

		DBusMessageIter interfaces_iter;
		dbus_message_iter_recurse(&dict_entry, &interfaces_iter);

		BluezDeviceProperties device;
		BluezAdapterInfo adapter;
		while (dbus_message_iter_get_arg_type(&interfaces_iter) == DBUS_TYPE_DICT_ENTRY) {
			DBusMessageIter iface_entry;
			dbus_message_iter_recurse(&interfaces_iter, &iface_entry);
			if (dbus_message_iter_get_arg_type(&iface_entry) != DBUS_TYPE_STRING) {
				dbus_message_iter_next(&interfaces_iter);
				continue;
			}

			const char *iface_name = nullptr;
			dbus_message_iter_get_basic(&iface_entry, &iface_name);
			const godot::String interface = dbus_string_to_godot(iface_name);

			if (!dbus_message_iter_next(&iface_entry) ||
					dbus_message_iter_get_arg_type(&iface_entry) != DBUS_TYPE_ARRAY) {
				dbus_message_iter_next(&interfaces_iter);
				continue;
			}

			parse_interface_properties(path, interface, &iface_entry, device, adapter);
			dbus_message_iter_next(&interfaces_iter);
		}

		if (device.valid) {
			p_devices.push_back(device);
		}
		if (adapter.valid) {
			p_adapters.push_back(adapter);
		}

		dbus_message_iter_next(&objects_iter);
	}

	dbus_message_unref(reply);
	return true;
}

bool BluezDBus::find_default_adapter(godot::String &p_out_path) {
	std::vector<BluezDeviceProperties> devices;
	std::vector<BluezAdapterInfo> adapters;
	if (!get_managed_objects(devices, adapters)) {
		return false;
	}
	for (const BluezAdapterInfo &adapter : adapters) {
		if (adapter.powered) {
			p_out_path = adapter.object_path;
			return true;
		}
	}
	for (const BluezAdapterInfo &adapter : adapters) {
		if (!adapter.object_path.is_empty()) {
			p_out_path = adapter.object_path;
			return true;
		}
	}
	last_error = "No Bluetooth adapter found via org.bluez ObjectManager.";
	return false;
}

bool BluezDBus::adapter_start_discovery(const godot::String &p_adapter_path) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE, p_adapter_path, ADAPTER_INTERFACE, "StartDiscovery", &reply, 10000);
	if (reply) {
		dbus_message_unref(reply);
	}
	return ok;
}

bool BluezDBus::adapter_stop_discovery(const godot::String &p_adapter_path) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE, p_adapter_path, ADAPTER_INTERFACE, "StopDiscovery", &reply, 10000);
	if (reply) {
		dbus_message_unref(reply);
	}
	return ok;
}

bool BluezDBus::adapter_remove_device(const godot::String &p_adapter_path, const godot::String &p_device_path) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE,
			p_adapter_path,
			ADAPTER_INTERFACE,
			"RemoveDevice",
			&reply,
			10000,
			[&p_device_path](DBusMessage *p_message) {
				const godot::CharString path_utf8 = p_device_path.utf8();
				const char *path_ptr = path_utf8.get_data();
				dbus_message_append_args(p_message, DBUS_TYPE_OBJECT_PATH, &path_ptr, DBUS_TYPE_INVALID);
			});
	if (reply) {
		dbus_message_unref(reply);
	}
	return ok;
}

bool BluezDBus::device_pair(const godot::String &p_device_path, int p_timeout_ms) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE, p_device_path, DEVICE_INTERFACE, "Pair", &reply, p_timeout_ms);
	if (reply) {
		dbus_message_unref(reply);
	}
	return ok;
}

bool BluezDBus::device_connect(const godot::String &p_device_path, int p_timeout_ms) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE, p_device_path, DEVICE_INTERFACE, "Connect", &reply, p_timeout_ms);
	if (reply) {
		dbus_message_unref(reply);
	}
	return ok;
}

bool BluezDBus::device_disconnect(const godot::String &p_device_path, int p_timeout_ms) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE, p_device_path, DEVICE_INTERFACE, "Disconnect", &reply, p_timeout_ms);
	if (reply) {
		dbus_message_unref(reply);
	}
	return ok;
}

bool BluezDBus::get_property(const godot::String &p_path,
		const godot::String &p_interface,
		const godot::String &p_property,
		const std::function<bool(DBusMessageIter *)> &p_reader) {
	DBusMessage *reply = nullptr;
	const bool ok = send_method_call(BLUEZ_SERVICE,
			p_path,
			PROPERTIES_INTERFACE,
			"Get",
			&reply,
			5000,
			[&p_interface, &p_property](DBusMessage *p_message) {
				const godot::CharString iface_utf8 = p_interface.utf8();
				const godot::CharString prop_utf8 = p_property.utf8();
				const char *iface_ptr = iface_utf8.get_data();
				const char *prop_ptr = prop_utf8.get_data();
				dbus_message_append_args(p_message,
						DBUS_TYPE_STRING,
						&iface_ptr,
						DBUS_TYPE_STRING,
						&prop_ptr,
						DBUS_TYPE_INVALID);
			});
	if (!ok || !reply) {
		return false;
	}

	DBusMessageIter root_iter;
	if (!dbus_message_iter_init(reply, &root_iter) || dbus_message_iter_get_arg_type(&root_iter) != DBUS_TYPE_VARIANT) {
		last_error = "Properties.Get reply has unexpected format.";
		dbus_message_unref(reply);
		return false;
	}

	DBusMessageIter value_iter;
	dbus_message_iter_recurse(&root_iter, &value_iter);
	const bool read_ok = p_reader(&value_iter);
	dbus_message_unref(reply);
	return read_ok;
}

bool BluezDBus::get_device_properties(const godot::String &p_device_path, BluezDeviceProperties &p_out_props) {
	p_out_props = BluezDeviceProperties();
	p_out_props.object_path = p_device_path;

	auto read_string_value = [](DBusMessageIter *p_iter, godot::String &p_out) {
		if (dbus_message_iter_get_arg_type(p_iter) != DBUS_TYPE_STRING) {
			return false;
		}
		const char *value = nullptr;
		dbus_message_iter_get_basic(p_iter, &value);
		p_out = dbus_string_to_godot(value);
		return true;
	};
	auto read_bool_value = [](DBusMessageIter *p_iter, bool &p_out) {
		if (dbus_message_iter_get_arg_type(p_iter) != DBUS_TYPE_BOOLEAN) {
			return false;
		}
		dbus_bool_t value = FALSE;
		dbus_message_iter_get_basic(p_iter, &value);
		p_out = value == TRUE;
		return true;
	};
	auto read_uint32_value = [](DBusMessageIter *p_iter, uint32_t &p_out) {
		const int value_type = dbus_message_iter_get_arg_type(p_iter);
		if (value_type == DBUS_TYPE_UINT32) {
			dbus_uint32_t value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			p_out = static_cast<uint32_t>(value);
			return true;
		}
		if (value_type == DBUS_TYPE_UINT16) {
			dbus_uint16_t value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			p_out = static_cast<uint32_t>(value);
			return true;
		}
		return false;
	};

	bool ok = false;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Address", [&](DBusMessageIter *p_iter) {
		return read_string_value(p_iter, p_out_props.address);
	}) || ok;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Name", [&](DBusMessageIter *p_iter) {
		return read_string_value(p_iter, p_out_props.name);
	}) || ok;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Alias", [&](DBusMessageIter *p_iter) {
		return read_string_value(p_iter, p_out_props.alias);
	}) || ok;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Paired", [&](DBusMessageIter *p_iter) {
		return read_bool_value(p_iter, p_out_props.paired);
	}) || ok;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Connected", [&](DBusMessageIter *p_iter) {
		return read_bool_value(p_iter, p_out_props.connected);
	}) || ok;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Trusted", [&](DBusMessageIter *p_iter) {
		return read_bool_value(p_iter, p_out_props.trusted);
	}) || ok;
	ok = get_property(p_device_path, DEVICE_INTERFACE, "Class", [&](DBusMessageIter *p_iter) {
		return read_uint32_value(p_iter, p_out_props.device_class);
	}) || ok;

	p_out_props.valid = ok;
	return ok;
}

} // namespace bluetooth