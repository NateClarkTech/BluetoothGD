#pragma once

#include <godot_cpp/variant/string.hpp>

namespace bluetooth {

enum class CommandType {
	INITIALIZE,
	SHUTDOWN,
	START_SCAN,
	STOP_SCAN,
	PAIR_DEVICE,
	UNPAIR_DEVICE,
	CONNECT_DEVICE,
	DISCONNECT_DEVICE,
	REFRESH_PAIRED_DEVICES,
};

struct BluetoothCommand {
	CommandType type = CommandType::INITIALIZE;
	godot::String address;
};

} // namespace bluetooth