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

godot::String read_device_path(DBusMessage *p_message) {
	DBusMessageIter args_iter;
	if (!dbus_message_iter_init(p_message, &args_iter)) {
		return "";
	}
	if (dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_OBJECT_PATH) {
		return "";
	}
	const char *object_path = nullptr;
	dbus_message_iter_get_basic(&args_iter, &object_path);
	return object_path ? godot::String(object_path) : godot::String();
}

godot::String read_pin(DBusMessage *p_message) {
	DBusMessageIter args_iter;
	if (!dbus_message_iter_init(p_message, &args_iter)) {
		return "";
	}
	if (dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_OBJECT_PATH) {
		return "";
	}
	if (!dbus_message_iter_next(&args_iter) || dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_UINT32) {
		return "";
	}
	if (!dbus_message_iter_next(&args_iter) || dbus_message_iter_get_arg_type(&args_iter) != DBUS_TYPE_STRING) {
		return "";
	}
	const char *pin = nullptr;
	dbus_message_iter_get_basic(&args_iter, &pin);
	return pin ? godot::String(pin) : godot::String();
}

} // namespace

BluezAgent *BluezAgent::active_instance = nullptr;

DBusMessage *BluezAgent::create_empty_reply(DBusMessage *p_message) {
	return dbus_message_new_method_return(p_message);
}

DBusMessage *BluezAgent::create_rejected_reply(DBusMessage *p_message, const char *p_message_text) {
	return dbus_message_new_error(p_message, "org.bluez.Error.Rejected", p_message_text);
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
	if (!member || !active_instance) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	BluezAgent *self = active_instance;
	PairingPendingState *pairing = self->pairing_state;
	DBusMessage *reply = nullptr;

	if (strcmp(member, "RequestPinCode") == 0 || strcmp(member, "RequestPasskey") == 0) {
		const godot::String device_path = read_device_path(p_message);
		if (pairing) {
			pairing->begin(device_path, "provide_pin");
			if (self->pairing_request_handler) {
				self->pairing_request_handler(device_path, "provide_pin", "");
			}
			PairingUserResponse response;
			if (pairing->wait_for_response(60000, response) && response.accepted && !response.pin.is_empty()) {
				const godot::CharString pin_utf8 = response.pin.utf8();
				const char *pin_ptr = pin_utf8.get_data();
				reply = create_empty_reply(p_message);
				DBusMessageIter iter;
				dbus_message_iter_init_append(reply, &iter);
				dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &pin_ptr);
			} else {
				reply = create_rejected_reply(p_message, "PIN entry rejected or timed out.");
			}
			pairing->reset();
		} else {
			reply = create_rejected_reply(p_message, "PIN/passkey entry not supported.");
		}
	} else if (strcmp(member, "RequestConfirmation") == 0 || strcmp(member, "RequestAuthorization") == 0) {
		const godot::String device_path = read_device_path(p_message);
		if (pairing) {
			pairing->begin(device_path, "confirm");
			if (self->pairing_request_handler) {
				self->pairing_request_handler(device_path, "confirm", "");
			}
			PairingUserResponse response;
			if (pairing->wait_for_response(60000, response) && response.accepted) {
				reply = create_empty_reply(p_message);
			} else {
				reply = create_rejected_reply(p_message, "Pairing confirmation rejected or timed out.");
			}
			pairing->reset();
		} else {
			reply = create_empty_reply(p_message);
		}
	} else if (strcmp(member, "DisplayPinCode") == 0 || strcmp(member, "DisplayPasskey") == 0) {
		const godot::String pin = read_pin(p_message);
		if (pairing) {
			const godot::String device_path = read_device_path(p_message);
			pairing->begin(device_path, "display_pin", pin);
			if (self->pairing_request_handler) {
				self->pairing_request_handler(device_path, "display_pin", pin);
			}
			PairingUserResponse response;
			pairing->wait_for_response(60000, response);
			pairing->reset();
		}
		reply = create_empty_reply(p_message);
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

	active_instance = this;

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
		active_instance = nullptr;
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
	active_instance = nullptr;
}

} // namespace bluetooth