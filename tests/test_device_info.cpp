// Standalone tests mirroring src/bluetooth_device_info.h helpers without Godot runtime.
// Keep algorithms in sync with production inline functions.

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace test {

std::string normalize_address(const std::string &p_address) {
	std::string normalized = p_address;
	for (char &ch : normalized) {
		if (ch >= 'a' && ch <= 'z') {
			ch = static_cast<char>(ch - 'a' + 'A');
		}
	}
	auto replace_all = [](std::string &s, const std::string &from, const std::string &to) {
		size_t pos = 0;
		while ((pos = s.find(from, pos)) != std::string::npos) {
			s.replace(pos, from.length(), to);
			pos += to.length();
		}
	};
	replace_all(normalized, "-", ":");
	replace_all(normalized, " ", "");
	return normalized;
}

bool is_valid_bluetooth_address(const std::string &p_address) {
	std::vector<std::string> parts;
	std::string current;
	const std::string normalized = normalize_address(p_address);
	for (char ch : normalized) {
		if (ch == ':') {
			parts.push_back(current);
			current.clear();
		} else {
			current.push_back(ch);
		}
	}
	parts.push_back(current);
	if (parts.size() != 6) {
		return false;
	}
	for (const std::string &part : parts) {
		if (part.length() != 2) {
			return false;
		}
	}
	return true;
}

bool addresses_match(const std::string &p_a, const std::string &p_b) {
	const std::string a = normalize_address(p_a);
	const std::string b = normalize_address(p_b);
	if (a == b) {
		return true;
	}
	if (is_valid_bluetooth_address(a) && (p_b.find(a) != std::string::npos || p_b.find(a) != std::string::npos)) {
		std::string compact = a;
		compact.erase(std::remove(compact.begin(), compact.end(), ':'), compact.end());
		if (p_b.find(compact) != std::string::npos) {
			return true;
		}
	}
	if (is_valid_bluetooth_address(b) && (p_a.find(b) != std::string::npos)) {
		std::string compact = b;
		compact.erase(std::remove(compact.begin(), compact.end(), ':'), compact.end());
		if (p_a.find(compact) != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool is_mac_address_name(const std::string &p_name, const std::string &p_address = "") {
	if (p_name.empty()) {
		return false;
	}
	if (is_valid_bluetooth_address(p_name)) {
		return true;
	}
	if (!p_address.empty() && addresses_match(p_name, p_address)) {
		return true;
	}
	return false;
}

void clear_unhelpful_device_name(std::string &p_name, const std::string &p_address) {
	if (is_mac_address_name(p_name, p_address)) {
		p_name.clear();
	}
}

std::string infer_device_class(const std::string &p_name) {
	std::string lower = p_name;
	for (char &ch : lower) {
		if (ch >= 'A' && ch <= 'Z') {
			ch = static_cast<char>(ch - 'A' + 'a');
		}
	}
	if (lower.find("xbox") != std::string::npos || lower.find("controller") != std::string::npos ||
			lower.find("gamepad") != std::string::npos || lower.find("dualsense") != std::string::npos ||
			lower.find("dualshock") != std::string::npos || lower.find("joy-con") != std::string::npos ||
			lower.find("pro controller") != std::string::npos || lower.find("wireless controller") != std::string::npos) {
		return "gamepad";
	}
	return "unknown";
}

} // namespace test

static void test_normalize_address() {
	assert(test::normalize_address("aa-bb-cc-dd-ee-ff") == "AA:BB:CC:DD:EE:FF");
	assert(test::normalize_address("AA:BB:CC:DD:EE:FF") == "AA:BB:CC:DD:EE:FF");
	assert(test::normalize_address("aa bb cc dd ee ff") == "AABBCCDDEEFF");
}

static void test_is_valid_bluetooth_address() {
	assert(test::is_valid_bluetooth_address("AA:BB:CC:DD:EE:FF"));
	assert(!test::is_valid_bluetooth_address("AA:BB:CC:DD:EE"));
	assert(!test::is_valid_bluetooth_address("not-a-mac"));
}

static void test_addresses_match() {
	assert(test::addresses_match("AA:BB:CC:DD:EE:FF", "aa-bb-cc-dd-ee-ff"));
	assert(!test::addresses_match("AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"));
}

static void test_clear_unhelpful_device_name() {
	std::string name = "AA:BB:CC:DD:EE:FF";
	test::clear_unhelpful_device_name(name, "AA:BB:CC:DD:EE:FF");
	assert(name.empty());

	name = "Xbox Controller";
	test::clear_unhelpful_device_name(name, "AA:BB:CC:DD:EE:FF");
	assert(name == "Xbox Controller");
}

static void test_infer_device_class() {
	assert(test::infer_device_class("Xbox Wireless Controller") == "gamepad");
	assert(test::infer_device_class("DualSense Wireless Controller") == "gamepad");
	assert(test::infer_device_class("Random Speaker") == "unknown");
}

int main() {
	test_normalize_address();
	test_is_valid_bluetooth_address();
	test_addresses_match();
	test_clear_unhelpful_device_name();
	test_infer_device_class();
	std::printf("All device_info tests passed.\n");
	return 0;
}