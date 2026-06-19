#pragma once

#include <godot_cpp/variant/string.hpp>

namespace bluetooth {

enum class BluetoothErrorCode {
	NONE = 0,
	DEVICE_NOT_FOUND,
	RADIO_OFF,
	NOT_SUPPORTED,
	PIN_REQUIRED,
	PAIRING_REJECTED,
	PERMISSION_DENIED,
	OPERATION_TIMEOUT,
	INVALID_ADDRESS,
	ALREADY_IN_PROGRESS,
	UNKNOWN,
};

inline godot::String error_code_to_string(BluetoothErrorCode p_code) {
	switch (p_code) {
		case BluetoothErrorCode::NONE:
			return "none";
		case BluetoothErrorCode::DEVICE_NOT_FOUND:
			return "device_not_found";
		case BluetoothErrorCode::RADIO_OFF:
			return "radio_off";
		case BluetoothErrorCode::NOT_SUPPORTED:
			return "not_supported";
		case BluetoothErrorCode::PIN_REQUIRED:
			return "pin_required";
		case BluetoothErrorCode::PAIRING_REJECTED:
			return "pairing_rejected";
		case BluetoothErrorCode::PERMISSION_DENIED:
			return "permission_denied";
		case BluetoothErrorCode::OPERATION_TIMEOUT:
			return "operation_timeout";
		case BluetoothErrorCode::INVALID_ADDRESS:
			return "invalid_address";
		case BluetoothErrorCode::ALREADY_IN_PROGRESS:
			return "already_in_progress";
		case BluetoothErrorCode::UNKNOWN:
		default:
			return "unknown";
	}
}

} // namespace bluetooth