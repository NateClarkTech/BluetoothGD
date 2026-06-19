#pragma once

#include "../../bluetooth_device_info.h"

#include <godot_cpp/variant/string.hpp>
#include <cstdint>
#include <optional>

namespace bluetooth {

std::optional<uint64_t> parse_bluetooth_address(const godot::String &p_address);
godot::String format_bluetooth_address(uint64_t p_address);
godot::String extract_address_from_device_id(const godot::String &p_device_id);
DeviceInfo device_info_from_address(const godot::String &p_address, const godot::String &p_name,
		bool p_paired, bool p_connected, const godot::String &p_device_id = "");

} // namespace bluetooth