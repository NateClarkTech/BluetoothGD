#pragma once

#include "../bluetooth_device_info.h"

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/array.hpp>

#include <functional>
#include <mutex>

namespace bluetooth {

class DeviceCache {
public:
	godot::String device_cache_key(const DeviceInfo &p_info) const {
		if (is_valid_bluetooth_address(p_info.address)) {
			return normalize_address(p_info.address);
		}
		if (!p_info.device_id.is_empty()) {
			return p_info.device_id;
		}
		return p_info.address;
	}

	bool upsert(const DeviceInfo &p_info, bool &p_is_new) {
		DeviceInfo info = p_info;
		if (is_valid_bluetooth_address(info.address)) {
			info.address = normalize_address(info.address);
		}
		const godot::String key = device_cache_key(info);
		p_is_new = false;

		std::lock_guard<std::mutex> lock(state_mutex);
		p_is_new = !discovered_devices.has(key);

		godot::PackedStringArray stale_keys;
		for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.key == key) {
				continue;
			}
			if (!info.device_id.is_empty() && item.value.device_id == info.device_id) {
				stale_keys.append(item.key);
				continue;
			}
			if (is_valid_bluetooth_address(info.address) && addresses_match(item.value.address, info.address)) {
				stale_keys.append(item.key);
			}
		}
		for (int i = 0; i < stale_keys.size(); i++) {
			remove_locked(stale_keys[i]);
		}

		if (discovered_devices.has(key)) {
			const DeviceInfo existing = discovered_devices[key];
			if (info.name.is_empty()) {
				info.name = existing.name;
			}
		}

		discovered_devices[key] = info;
		if (!info.device_id.is_empty()) {
			device_id_to_key[info.device_id] = key;
		}
		if (is_valid_bluetooth_address(info.address)) {
			address_to_device_id[info.address] = info.device_id;
		}
		if (info.paired) {
			paired_devices[key] = info;
		} else {
			paired_devices.erase(key);
		}
		return true;
	}

	void remove(const godot::String &p_key, const godot::String &p_address) {
		std::lock_guard<std::mutex> lock(state_mutex);
		remove_locked(p_key, p_address);
	}

	bool find_by_device_id(const godot::String &p_device_id, godot::String &p_out_key, DeviceInfo &p_out_info) const {
		std::lock_guard<std::mutex> lock(state_mutex);
		if (device_id_to_key.has(p_device_id)) {
			const godot::String key = device_id_to_key[p_device_id];
			if (discovered_devices.has(key)) {
				p_out_key = key;
				p_out_info = discovered_devices[key];
				return true;
			}
		}
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.value.device_id == p_device_id) {
				p_out_key = item.key;
				p_out_info = item.value;
				return true;
			}
		}
		return false;
	}

	bool lookup_cached_state(const godot::String &p_address, bool &p_out_value,
			const std::function<bool(const DeviceInfo &)> &p_selector) const {
		const godot::String normalized = normalize_address(p_address);
		std::lock_guard<std::mutex> lock(state_mutex);
		if (discovered_devices.has(normalized)) {
			p_out_value = p_selector(discovered_devices[normalized]);
			return true;
		}
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (addresses_match(item.value.address, normalized)) {
				p_out_value = p_selector(item.value);
				return true;
			}
		}
		return false;
	}

	godot::String resolve_device_id(const godot::String &p_address) const {
		const godot::String normalized = normalize_address(p_address);
		std::lock_guard<std::mutex> lock(state_mutex);
		if (address_to_device_id.has(normalized)) {
			return address_to_device_id[normalized];
		}
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.key == normalized || addresses_match(item.value.address, normalized)) {
				return item.value.device_id;
			}
		}
		return "";
	}

	godot::Array all_discovered_array() const {
		std::lock_guard<std::mutex> lock(state_mutex);
		godot::Array devices;
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			devices.push_back(item.value.to_dictionary());
		}
		return devices;
	}

	godot::Array paired_array() const {
		std::lock_guard<std::mutex> lock(state_mutex);
		godot::Array devices;
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : paired_devices) {
			devices.push_back(item.value.to_dictionary());
		}
		return devices;
	}

	void for_each_discovered(const std::function<void(const godot::String &, DeviceInfo &)> &p_fn) {
		std::lock_guard<std::mutex> lock(state_mutex);
		for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			p_fn(item.key, item.value);
		}
	}

	void update_connection_for_address(const godot::String &p_address, bool p_connected,
			godot::PackedStringArray &p_updated_keys) {
		const godot::String normalized = normalize_address(p_address);
		std::lock_guard<std::mutex> lock(state_mutex);
		for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (!addresses_match(item.value.address, normalized) && !addresses_match(item.key, normalized)) {
				continue;
			}
			item.value.connected = p_connected;
			if (is_valid_bluetooth_address(normalized)) {
				item.value.address = normalized;
			}
			discovered_devices[item.key] = item.value;
			if (item.value.paired) {
				paired_devices[item.key] = item.value;
			}
			p_updated_keys.append(item.key);
		}
	}

	bool get_device(const godot::String &p_key, DeviceInfo &p_out) const {
		std::lock_guard<std::mutex> lock(state_mutex);
		if (!discovered_devices.has(p_key)) {
			return false;
		}
		p_out = discovered_devices[p_key];
		return true;
	}

private:
	void remove_locked(const godot::String &p_key, const godot::String &p_address = "") {
		if (discovered_devices.has(p_key)) {
			const godot::String device_id = discovered_devices[p_key].device_id;
			if (!device_id.is_empty()) {
				device_id_to_key.erase(device_id);
			}
		}
		discovered_devices.erase(p_key);
		paired_devices.erase(p_key);
		if (is_valid_bluetooth_address(p_address)) {
			address_to_device_id.erase(normalize_address(p_address));
		}
	}

	mutable std::mutex state_mutex;
	godot::HashMap<godot::String, DeviceInfo> discovered_devices;
	godot::HashMap<godot::String, DeviceInfo> paired_devices;
	godot::HashMap<godot::String, godot::String> address_to_device_id;
	godot::HashMap<godot::String, godot::String> device_id_to_key;
};

} // namespace bluetooth