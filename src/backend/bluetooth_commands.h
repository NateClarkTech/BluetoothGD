#pragma once

#include "scan_options.h"

#include <godot_cpp/variant/string.hpp>

namespace bluetooth {

enum class CommandType {
	INITIALIZE,
	SHUTDOWN,
	START_SCAN,
	STOP_SCAN,
	PAIR_DEVICE,
	PAIR_DEVICE_BY_ID,
	UNPAIR_DEVICE,
	CONNECT_DEVICE,
	DISCONNECT_DEVICE,
	REFRESH_PAIRED_DEVICES,
	CONFIRM_PAIRING,
	REJECT_PAIRING,
	CANCEL_PAIRING,
};

struct BluetoothCommand {
	CommandType type = CommandType::INITIALIZE;
	godot::String address;
	godot::String pin;
	bool accepted = false;
	ScanOptions scan_options;
};

} // namespace bluetooth