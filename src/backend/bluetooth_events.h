#pragma once

#include "../bluetooth_device_info.h"
#include "bluetooth_error.h"

#include <godot_cpp/variant/string.hpp>

namespace bluetooth {

enum class EventType {
	SCAN_STARTED,
	SCAN_STOPPED,
	DEVICE_FOUND,
	DEVICE_REMOVED,
	PAIRING_STARTED,
	PAIRING_SUCCEEDED,
	PAIRING_FAILED,
	PAIRING_CONFIRMATION_REQUESTED,
	PAIRING_PIN_REQUESTED,
	PAIRING_DISPLAY_PIN,
	CONNECTION_CHANGED,
	ERROR_OCCURRED,
	PAIRED_DEVICES_UPDATED,
};

struct BluetoothEvent {
	EventType type = EventType::ERROR_OCCURRED;
	DeviceInfo device;
	godot::Array devices;
	godot::String address;
	godot::String operation;
	godot::String message;
	godot::String pairing_kind;
	godot::String display_pin;
	BluetoothErrorCode error_code = BluetoothErrorCode::UNKNOWN;
	bool connected = false;
};

} // namespace bluetooth