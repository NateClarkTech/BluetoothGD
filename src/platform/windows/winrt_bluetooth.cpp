#include "winrt_bluetooth.h"

#include <sstream>
#include <iomanip>

namespace bluetooth {

std::optional<uint64_t> parse_bluetooth_address(const godot::String &p_address) {
	godot::String normalized = normalize_address(p_address);
	godot::PackedStringArray parts = normalized.split(":", false);
	if (parts.size() != 6) {
		return std::nullopt;
	}

	uint64_t address = 0;
	for (int i = 0; i < 6; i++) {
		godot::CharString part = parts[i].ascii();
		const char *text = part.get_data();
		char *end = nullptr;
		unsigned long value = std::strtoul(text, &end, 16);
		if (end == text || value > 0xFF) {
			return std::nullopt;
		}
		address = (address << 8) | value;
	}
	return address;
}

godot::String extract_address_from_device_id(const godot::String &p_device_id) {
	godot::String id = p_device_id;
	const godot::String marker = "Bluetooth#Bluetooth";
	int start = id.find(marker);
	if (start >= 0) {
		godot::String suffix = id.substr(start + marker.length());
		godot::PackedStringArray parts = suffix.split("-", false);
		if (!parts.is_empty()) {
			return normalize_address(parts[0]);
		}
	}

	for (int i = 0; i < id.length() - 16; i++) {
		godot::String candidate = id.substr(i, 17);
		if (parse_bluetooth_address(candidate).has_value()) {
			return normalize_address(candidate);
		}
	}

	return "";
}

godot::String format_bluetooth_address(uint64_t p_address) {
	std::ostringstream stream;
	stream << std::uppercase << std::hex << std::setfill('0');
	for (int i = 5; i >= 0; --i) {
		if (i < 5) {
			stream << ":";
		}
		stream << std::setw(2) << ((p_address >> (i * 8)) & 0xFF);
	}
	return godot::String(stream.str().c_str());
}

DeviceInfo device_info_from_address(const godot::String &p_address, const godot::String &p_name,
		bool p_paired, bool p_connected, const godot::String &p_device_id) {
	DeviceInfo info;
	info.address = normalize_address(p_address);
	info.name = p_name;
	info.paired = p_paired;
	info.connected = p_connected;
	info.device_class = infer_device_class(p_name);
	info.device_id = p_device_id;
	return info;
}

} // namespace bluetooth