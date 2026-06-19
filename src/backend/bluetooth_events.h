#pragma once

#include "../bluetooth_device_info.h"

#include <godot_cpp/variant/string.hpp>

namespace bluetooth {

enum class EventType {
	SCAN_STARTED,
	SCAN_STOPPED,
	DEVICE_FOUND,
	PAIRING_STARTED,
	PAIRING_SUCCEEDED,
	PAIRING_FAILED,
	CONNECTION_CHANGED,
	ERROR_OCCURRED,
	PAIRED_DEVICES_UPDATED,
};

struct BluetoothEvent {
	EventType type = EventType::ERROR_OCCURRED;
	DeviceInfo device;
	godot::String address;
	godot::String operation;
	godot::String message;
	bool connected = false;
};

} // namespace bluetooth