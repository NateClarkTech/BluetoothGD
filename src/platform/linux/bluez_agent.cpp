#include "bluez_agent.h"

#include <cstring>

namespace bluetooth {

namespace {

constexpr const char *AGENT_MANAGER_INTERFACE = "org.bluez.AgentManager1";
constexpr const char *AGENT_INTERFACE = "org.bluez.Agent1";
constexpr const char *BLUEZ_SERVICE = "org.bluez";
constexpr const char *BLUEZ_AGENT_MANAGER_PATH = "/org/bluez";

godot::String dbus_error_message(DBusError *p_error, DBusMessage *p_reply) {
	if (p_error && dbus_error_is_set(p_error)) {
		godot::String message;
		if (p_error->name) {
			message += godot::String(p_error->name);
		}
		if (p_error->message) {
			if (!message.is_empty()) {
				message += ": ";
			}
			message += godot::String(p_error->message);
		}
		return message;
	}
	if (p_reply && dbus_message_get_type(p_reply) == DBUS_MESSAGE_TYPE_ERROR) {
		const char *error_name = dbus_message_get_error_name(p_reply);
		return error_name ? godot::String(error_name) : "Unknown D-Bus error reply.";
	}
	return "Unknown D-Bus error.";
}

} // namespace

DBusMessage *BluezAgent::create_empty_reply(DBusMessage *p_message) {
	return dbus_message_new_method_return(p_message);
}

DBusHandlerResult BluezAgent::agent_message_handler(DBusConnection *p_connection,
		DBusMessage *p_message,
		void *p_user_data) {
	(void)p_user_data;
	if (!p_message || dbus_message_get_type(p_message) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	const char *interface = dbus_message_get_interface(p_message);
	if (!interface || strcmp(interface, AGENT_INTERFACE) != 0) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	const char *member = dbus_message_get_member(p_message);
	if (!member) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	DBusMessage *reply = nullptr;
	if (strcmp(member, "RequestPinCode") == 0 || strcmp(member, "RequestPasskey") == 0) {
		reply = dbus_message_new_error(p_message, "org.bluez.Error.Rejected", "PIN/passkey entry not supported.");
	} else {
		reply = create_empty_reply(p_message);
	}

	if (reply) {
		dbus_connection_send(p_connection, reply, nullptr);
		dbus_message_unref(reply);
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

bool BluezAgent::register_agent(BluezDBus &p_dbus, godot::String *p_error) {
	DBusConnection *connection = p_dbus.get_connection();
	if (!connection) {
		if (p_error) {
			*p_error = "D-Bus connection is not open.";
		}
		return false;
	}

	static DBusObjectPathVTable vtable = {};
	vtable.message_function = agent_message_handler;

	const godot::CharString path_utf8 = godot::String(AGENT_PATH).utf8();
	const char *path_ptr = path_utf8.get_data();

	{
		DBusMessage *cleanup_message = dbus_message_new_method_call(BLUEZ_SERVICE,
				BLUEZ_AGENT_MANAGER_PATH,
				AGENT_MANAGER_INTERFACE,
				"UnregisterAgent");
		if (cleanup_message) {
			dbus_message_append_args(cleanup_message, DBUS_TYPE_OBJECT_PATH, &path_ptr, DBUS_TYPE_INVALID);
			DBusError cleanup_error;
			dbus_error_init(&cleanup_error);
			DBusMessage *cleanup_reply = dbus_connection_send_with_reply_and_block(connection,
					cleanup_message,
					2000,
					&cleanup_error);
			if (cleanup_reply) {
				dbus_message_unref(cleanup_reply);
			}
			dbus_error_free(&cleanup_error);
			dbus_message_unref(cleanup_message);
		}
	}

	if (!object_registered) {
		if (!dbus_connection_register_object_path(connection, AGENT_PATH, &vtable, nullptr)) {
			if (p_error) {
				*p_error = "dbus_connection_register_object_path failed for " + godot::String(AGENT_PATH) + ".";
			}
			return false;
		}
		object_registered = true;
	}
	const godot::CharString capability_utf8 = godot::String(AGENT_CAPABILITY).utf8();
	const char *capability_ptr = capability_utf8.get_data();

	DBusMessage *register_message = dbus_message_new_method_call(BLUEZ_SERVICE,
			BLUEZ_AGENT_MANAGER_PATH,
			AGENT_MANAGER_INTERFACE,
			"RegisterAgent");
	if (!register_message) {
		if (p_error) {
			*p_error = "dbus_message_new_method_call(RegisterAgent) failed.";
		}
		return false;
	}

	dbus_message_append_args(register_message,
			DBUS_TYPE_OBJECT_PATH,
			&path_ptr,
			DBUS_TYPE_STRING,
			&capability_ptr,
			DBUS_TYPE_INVALID);

	DBusError register_error;
	dbus_error_init(&register_error);
	DBusMessage *register_reply = dbus_connection_send_with_reply_and_block(connection,
			register_message,
			5000,
			&register_error);
	dbus_message_unref(register_message);

	if (dbus_error_is_set(&register_error) || !register_reply ||
			dbus_message_get_type(register_reply) == DBUS_MESSAGE_TYPE_ERROR) {
		const godot::String message = "RegisterAgent failed on " + godot::String(BLUEZ_AGENT_MANAGER_PATH) + ": " +
				dbus_error_message(&register_error, register_reply);
		if (register_reply) {
			dbus_message_unref(register_reply);
		}
		dbus_error_free(&register_error);
		if (p_error) {
			*p_error = message;
		}
		return false;
	}
	dbus_message_unref(register_reply);
	dbus_error_free(&register_error);

	DBusMessage *default_message = dbus_message_new_method_call(BLUEZ_SERVICE,
			BLUEZ_AGENT_MANAGER_PATH,
			AGENT_MANAGER_INTERFACE,
			"RequestDefaultAgent");
	if (!default_message) {
		return true;
	}

	dbus_message_append_args(default_message, DBUS_TYPE_OBJECT_PATH, &path_ptr, DBUS_TYPE_INVALID);

	DBusError default_error;
	dbus_error_init(&default_error);
	DBusMessage *default_reply = dbus_connection_send_with_reply_and_block(connection,
			default_message,
			5000,
			&default_error);
	dbus_message_unref(default_message);

	if (dbus_error_is_set(&default_error) || !default_reply ||
			dbus_message_get_type(default_reply) == DBUS_MESSAGE_TYPE_ERROR) {
		const godot::String warning = "RequestDefaultAgent failed (pairing may still work): " +
				dbus_error_message(&default_error, default_reply);
		p_dbus.set_last_error(warning);
		if (p_error) {
			*p_error = warning;
		}
	}
	if (default_reply) {
		dbus_message_unref(default_reply);
	}
	dbus_error_free(&default_error);

	return true;
}

void BluezAgent::unregister_agent(BluezDBus &p_dbus) {
	DBusConnection *connection = p_dbus.get_connection();
	if (!connection || !object_registered) {
		return;
	}

	DBusMessage *message = dbus_message_new_method_call(BLUEZ_SERVICE,
			BLUEZ_AGENT_MANAGER_PATH,
			AGENT_MANAGER_INTERFACE,
			"UnregisterAgent");
	if (message) {
		const godot::CharString path_utf8 = godot::String(AGENT_PATH).utf8();
		const char *path_ptr = path_utf8.get_data();
		dbus_message_append_args(message, DBUS_TYPE_OBJECT_PATH, &path_ptr, DBUS_TYPE_INVALID);

		DBusError error;
		dbus_error_init(&error);
		DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, message, 5000, &error);
		if (reply) {
			dbus_message_unref(reply);
		}
		dbus_error_free(&error);
		dbus_message_unref(message);
	}

	dbus_connection_unregister_object_path(connection, AGENT_PATH);
	object_registered = false;
}

} // namespace bluetooth