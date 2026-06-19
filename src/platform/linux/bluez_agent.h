#pragma once

#include "../../backend/pairing_pending.h"
#include "bluez_dbus.h"

#include <functional>

namespace bluetooth {

class BluezAgent {
public:
	static constexpr const char *AGENT_PATH = "/org/bluetoothgd/agent";
	static constexpr const char *AGENT_CAPABILITY = "DisplayYesNo";

	void set_pairing_state(PairingPendingState *p_state) { pairing_state = p_state; }
	void set_pairing_request_handler(std::function<void(const godot::String &, const godot::String &, const godot::String &)> p_handler) {
		pairing_request_handler = std::move(p_handler);
	}

	bool register_agent(BluezDBus &p_dbus, godot::String *p_error = nullptr);
	void unregister_agent(BluezDBus &p_dbus);

private:
	bool object_registered = false;
	PairingPendingState *pairing_state = nullptr;
	std::function<void(const godot::String &, const godot::String &, const godot::String &)> pairing_request_handler;

	static BluezAgent *active_instance;

	static DBusHandlerResult agent_message_handler(DBusConnection *p_connection,
			DBusMessage *p_message,
			void *p_user_data);

	static DBusMessage *create_empty_reply(DBusMessage *p_message);
	static DBusMessage *create_rejected_reply(DBusMessage *p_message, const char *p_message_text);
};

} // namespace bluetooth