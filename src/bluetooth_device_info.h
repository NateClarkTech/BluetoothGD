#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace bluetooth {

struct DeviceInfo {
	godot::String address;
	godot::String name;
	bool paired = false;
	bool connected = false;
	bool trusted = false;
	godot::String device_class;
	godot::String device_id;

	godot::Dictionary to_dictionary() const {
		godot::Dictionary dict;
		dict[godot::String("address")] = address;
		dict[godot::String("name")] = name;
		dict[godot::String("paired")] = paired;
		dict[godot::String("connected")] = connected;
		dict[godot::String("trusted")] = trusted;
		dict[godot::String("device_class")] = device_class;
		dict[godot::String("device_id")] = device_id;
		return dict;
	}
};

inline godot::String normalize_address(const godot::String &p_address) {
	godot::String normalized = p_address.to_upper();
	normalized = normalized.replace("-", ":");
	normalized = normalized.replace(" ", "");
	return normalized;
}

inline bool is_valid_bluetooth_address(const godot::String &p_address) {
	godot::PackedStringArray parts = normalize_address(p_address).split(":", false);
	if (parts.size() != 6) {
		return false;
	}
	for (int i = 0; i < 6; i++) {
		if (parts[i].length() != 2) {
			return false;
		}
	}
	return true;
}

inline bool addresses_match(const godot::String &p_a, const godot::String &p_b) {
	const godot::String a = normalize_address(p_a);
	const godot::String b = normalize_address(p_b);
	if (a == b) {
		return true;
	}
	if (is_valid_bluetooth_address(a) && (p_b.contains(a.replace(":", "")) || p_b.contains(a))) {
		return true;
	}
	if (is_valid_bluetooth_address(b) && (p_a.contains(b.replace(":", "")) || p_a.contains(b))) {
		return true;
	}
	return false;
}

inline godot::String infer_device_class(const godot::String &p_name) {
	godot::String lower = p_name.to_lower();
	if (lower.contains("xbox") || lower.contains("controller") || lower.contains("gamepad") ||
			lower.contains("dualsense") || lower.contains("dualshock") || lower.contains("joy-con") ||
			lower.contains("pro controller") || lower.contains("wireless controller")) {
		return "gamepad";
	}
	return "unknown";
}

} // namespace bluetooth