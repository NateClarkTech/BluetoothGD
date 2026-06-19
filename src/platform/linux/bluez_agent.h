#pragma once

#include "bluez_dbus.h"

namespace bluetooth {

class BluezAgent {
public:
	static constexpr const char *AGENT_PATH = "/org/bluetoothgd/agent";
	static constexpr const char *AGENT_CAPABILITY = "DisplayYesNo";

	bool register_agent(BluezDBus &p_dbus, godot::String *p_error = nullptr);
	void unregister_agent(BluezDBus &p_dbus);

private:
	bool object_registered = false;

	static DBusHandlerResult agent_message_handler(DBusConnection *p_connection,
			DBusMessage *p_message,
			void *p_user_data);

	static DBusMessage *create_empty_reply(DBusMessage *p_message);
};

} // namespace bluetooth