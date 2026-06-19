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
	int rssi = 0;
	bool has_rssi = false;

	godot::Dictionary to_dictionary() const {
		godot::Dictionary dict;
		dict[godot::String("address")] = address;
		dict[godot::String("name")] = name;
		dict[godot::String("paired")] = paired;
		dict[godot::String("connected")] = connected;
		dict[godot::String("trusted")] = trusted;
		dict[godot::String("device_class")] = device_class;
		dict[godot::String("device_id")] = device_id;
		if (has_rssi) {
			dict[godot::String("rssi")] = rssi;
		}
		return dict;
	}

	static DeviceInfo from_dictionary(const godot::Dictionary &p_dict) {
		DeviceInfo info;
		if (p_dict.has("address")) {
			info.address = p_dict["address"];
		}
		if (p_dict.has("name")) {
			info.name = p_dict["name"];
		}
		if (p_dict.has("paired")) {
			info.paired = p_dict["paired"];
		}
		if (p_dict.has("connected")) {
			info.connected = p_dict["connected"];
		}
		if (p_dict.has("trusted")) {
			info.trusted = p_dict["trusted"];
		}
		if (p_dict.has("device_class")) {
			info.device_class = p_dict["device_class"];
		}
		if (p_dict.has("device_id")) {
			info.device_id = p_dict["device_id"];
		}
		if (p_dict.has("rssi")) {
			info.rssi = p_dict["rssi"];
			info.has_rssi = true;
		}
		return info;
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

inline bool is_mac_address_name(const godot::String &p_name, const godot::String &p_address = "") {
	const godot::String trimmed = p_name.strip_edges();
	if (trimmed.is_empty()) {
		return false;
	}
	if (is_valid_bluetooth_address(trimmed)) {
		return true;
	}
	if (!p_address.is_empty() && addresses_match(trimmed, p_address)) {
		return true;
	}
	return false;
}

inline void clear_unhelpful_device_name(DeviceInfo &p_info) {
	if (is_mac_address_name(p_info.name, p_info.address)) {
		p_info.name = "";
	}
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