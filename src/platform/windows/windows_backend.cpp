#include "windows_backend.h"
#include "winrt_bluetooth.h"

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.HumanInterfaceDevice.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <cstdio>
#include <string_view>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::HumanInterfaceDevice;

namespace bluetooth {

namespace {

godot::String hstring_to_godot(const hstring &p_value) {
	return godot::String(winrt::to_string(p_value).c_str());
}

godot::String format_hresult_hex(winrt::hresult p_code) {
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<uint32_t>(p_code));
	return godot::String(buffer);
}

godot::String format_winrt_error(const godot::String &p_operation, const winrt::hresult_error &p_error,
		const godot::String &p_details = "") {
	godot::String message = p_operation + godot::String(" failed: HRESULT ") + format_hresult_hex(p_error.code()) +
			godot::String(" — ") + hstring_to_godot(p_error.message());
	if (!p_details.is_empty()) {
		message += godot::String(" | ") + p_details;
	}
	return message;
}

godot::String pairing_result_to_string(DevicePairingResultStatus p_status) {
	switch (p_status) {
		case DevicePairingResultStatus::Paired:
			return "Paired";
		case DevicePairingResultStatus::AlreadyPaired:
			return "Already paired";
		case DevicePairingResultStatus::ConnectionRejected:
			return "Connection rejected";
		case DevicePairingResultStatus::TooManyConnections:
			return "Too many connections";
		case DevicePairingResultStatus::HardwareFailure:
			return "Hardware failure";
		case DevicePairingResultStatus::AuthenticationTimeout:
			return "Authentication timeout";
		case DevicePairingResultStatus::AuthenticationNotAllowed:
			return "Authentication not allowed";
		case DevicePairingResultStatus::AuthenticationFailure:
			return "Authentication failure";
		case DevicePairingResultStatus::NoSupportedProfiles:
			return "No supported profiles";
		case DevicePairingResultStatus::ProtectionLevelCouldNotBeMet:
			return "Protection level could not be met";
		case DevicePairingResultStatus::AccessDenied:
			return "Access denied";
		case DevicePairingResultStatus::InvalidCeremonyData:
			return "Invalid ceremony data";
		case DevicePairingResultStatus::PairingCanceled:
			return "Pairing canceled";
		case DevicePairingResultStatus::OperationAlreadyInProgress:
			return "Operation already in progress";
		case DevicePairingResultStatus::RequiredHandlerNotRegistered:
			return "Required handler not registered";
		case DevicePairingResultStatus::RejectedByHandler:
			return "Rejected by handler";
		case DevicePairingResultStatus::RemoteDeviceHasAssociation:
			return "Remote device already has an association";
		case DevicePairingResultStatus::Failed:
		default:
			return "Pairing failed";
	}
}

constexpr wchar_t AEP_BLUETOOTH_PROTOCOL_SELECTOR[] =
		L"System.Devices.Aep.ProtocolId:=\"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}\"";
constexpr wchar_t AEP_BLUETOOTH_LE_PROTOCOL_SELECTOR[] =
		L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";

void apply_display_name_fallback(DeviceInfo &p_info) {
	if (!p_info.name.is_empty()) {
		return;
	}

	if (parse_bluetooth_address(p_info.address).has_value()) {
		p_info.name = godot::String("Unknown Device (") + p_info.address + ")";
		return;
	}

	godot::String short_id = p_info.device_id;
	if (short_id.length() > 48) {
		short_id = short_id.substr(0, 48) + "...";
	}
	p_info.name = godot::String("Unknown Device (") + short_id + ")";
}

godot::String unpair_result_to_string(DeviceUnpairingResultStatus p_status) {
	switch (p_status) {
		case DeviceUnpairingResultStatus::Unpaired:
			return "Unpaired";
		case DeviceUnpairingResultStatus::AlreadyUnpaired:
			return "Already unpaired";
		case DeviceUnpairingResultStatus::OperationAlreadyInProgress:
			return "Operation already in progress";
		case DeviceUnpairingResultStatus::AccessDenied:
			return "Access denied";
		case DeviceUnpairingResultStatus::Failed:
		default:
			return "Unpair failed";
	}
}

godot::String pairing_status_detail(DevicePairingResultStatus p_status) {
	return pairing_result_to_string(p_status) + " (DevicePairingResultStatus=" +
			godot::String::num_int64(static_cast<int64_t>(p_status)) + ")";
}

godot::String unpair_status_detail(DeviceUnpairingResultStatus p_status) {
	return unpair_result_to_string(p_status) + " (DeviceUnpairingResultStatus=" +
			godot::String::num_int64(static_cast<int64_t>(p_status)) + ")";
}

DeviceInfo make_device_info(const DeviceInformation &p_info) {
	DeviceInfo info;
	info.device_id = hstring_to_godot(p_info.Id());
	info.name = hstring_to_godot(p_info.Name());
	info.paired = p_info.Pairing().IsPaired();
	info.connected = false;

	if (auto properties = p_info.Properties()) {
		if (properties.HasKey(L"System.Devices.Aep.DeviceAddress")) {
			auto value = properties.Lookup(L"System.Devices.Aep.DeviceAddress");
			if (value) {
				info.address = normalize_address(hstring_to_godot(unbox_value<hstring>(value)));
			}
		}
		if (properties.HasKey(L"System.ItemNameDisplay")) {
			auto value = properties.Lookup(L"System.ItemNameDisplay");
			if (value) {
				const godot::String display_name = hstring_to_godot(unbox_value<hstring>(value));
				if (!display_name.is_empty()) {
					info.name = display_name;
				}
			}
		}
	}

	try {
		auto device = BluetoothDevice::FromIdAsync(p_info.Id()).get();
		if (device) {
			info.address = format_bluetooth_address(device.BluetoothAddress());
			info.connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
			if (info.name.is_empty()) {
				info.name = hstring_to_godot(device.Name());
			}
		}
	} catch (...) {
	}

	if (info.address.is_empty() || info.address == info.device_id) {
		try {
			auto le_device = BluetoothLEDevice::FromIdAsync(p_info.Id()).get();
			if (le_device) {
				info.address = format_bluetooth_address(le_device.BluetoothAddress());
				if (info.name.is_empty()) {
					info.name = hstring_to_godot(le_device.Name());
				}
			}
		} catch (...) {
		}
	}

	if (info.address.is_empty()) {
		info.address = extract_address_from_device_id(info.device_id);
	}
	if (info.address.is_empty()) {
		info.address = info.device_id;
	}

	apply_display_name_fallback(info);
	info.device_class = infer_device_class(info.name);
	return info;
}

DeviceInfo make_hid_gamepad_info(const DeviceInformation &p_info) {
	DeviceInfo info;
	info.device_id = hstring_to_godot(p_info.Id());
	info.name = hstring_to_godot(p_info.Name());
	info.paired = p_info.Pairing().IsPaired();
	info.connected = true;
	info.device_class = "gamepad";
	info.address = extract_address_from_device_id(info.device_id);
	if (info.address.is_empty()) {
		info.address = info.device_id;
	}
	if (info.name.is_empty()) {
		info.name = "HID Gamepad";
	}
	return info;
}

bool is_hid_gamepad_connected(const godot::String &p_normalized) {
	try {
		const auto selector = HidDevice::GetDeviceSelector(0x0001, 0x0005);
		const auto devices = DeviceInformation::FindAllAsync(selector).get();
		for (const auto &device_info : devices) {
			const godot::String device_id = hstring_to_godot(device_info.Id());
			const bool is_bluetooth_hid = device_id.contains("BTHENUM") || device_id.contains("BTHLEDEVICE") ||
					device_id.contains("Bluetooth");
			if (!is_bluetooth_hid) {
				continue;
			}
			const godot::String address = normalize_address(extract_address_from_device_id(device_id));
			if (addresses_match(address, p_normalized)) {
				return true;
			}
		}
	} catch (...) {
	}
	return false;
}

bool query_device_connected(uint64_t p_address, const godot::String &p_normalized) {
	bool connected = false;
	try {
		auto device = BluetoothDevice::FromBluetoothAddressAsync(p_address).get();
		if (device) {
			connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
		}
	} catch (...) {
	}
	if (!connected) {
		connected = is_hid_gamepad_connected(p_normalized);
	}
	return connected;
}

DeviceInformationCollection find_devices_with_properties(const hstring &p_selector) {
	const auto additional_properties = single_threaded_vector<hstring>({
			L"System.Devices.Aep.DeviceAddress",
			L"System.Devices.Aep.IsConnected",
			L"System.ItemNameDisplay",
			L"System.Devices.Aep.Bluetooth.Le.IsConnectable",
	});
	return DeviceInformation::FindAllAsync(p_selector, additional_properties).get();
}

} // namespace

WindowsBackend::~WindowsBackend() {
	shutdown();
}

bool WindowsBackend::ensure_winrt_ready() {
	try {
		static bool apartment_initialized = false;
		if (!apartment_initialized) {
			init_apartment(apartment_type::multi_threaded);
			apartment_initialized = true;
		}
		return true;
	} catch (const winrt::hresult_error &error) {
		emit_error("initialize",
				format_winrt_error("initialize",
						error,
						"WinRT init_apartment(multi_threaded) threw during first backend use"));
		return false;
	}
}

bool WindowsBackend::initialize() {
	if (initialized) {
		return true;
	}
	if (!ensure_winrt_ready()) {
		return false;
	}

	initialized = true;
	refresh_paired_devices();
	return true;
}

void WindowsBackend::shutdown() {
	stop_scan();
	initialized = false;
}

void WindowsBackend::emit(const BluetoothEvent &p_event) {
	if (on_event) {
		on_event(p_event);
	}
}

void WindowsBackend::emit_error(const godot::String &p_operation, const godot::String &p_message) {
	BluetoothEvent event;
	event.type = EventType::ERROR_OCCURRED;
	event.operation = p_operation;
	event.message = p_message;
	emit(event);
}

void WindowsBackend::emit_device_removed(const godot::String &p_address) {
	BluetoothEvent event;
	event.type = EventType::DEVICE_REMOVED;
	event.address = is_valid_bluetooth_address(p_address) ? normalize_address(p_address) : p_address;
	emit(event);
}

void WindowsBackend::emit_paired_devices_updated() {
	BluetoothEvent event;
	event.type = EventType::PAIRED_DEVICES_UPDATED;
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			event.devices.push_back(item.value.to_dictionary());
		}
	}
	emit(event);
}

void WindowsBackend::remove_device_from_cache(const godot::String &p_key, const godot::String &p_address) {
	std::lock_guard<std::mutex> lock(state_mutex);
	discovered_devices.erase(p_key);
	paired_devices.erase(p_key);
	if (is_valid_bluetooth_address(p_address)) {
		address_to_device_id.erase(normalize_address(p_address));
	}
}

godot::String WindowsBackend::device_cache_key(const DeviceInfo &p_info) const {
	if (is_valid_bluetooth_address(p_info.address)) {
		return normalize_address(p_info.address);
	}
	if (!p_info.device_id.is_empty()) {
		return p_info.device_id;
	}
	return p_info.address;
}

void WindowsBackend::upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit) {
	DeviceInfo info = p_info;
	if (is_valid_bluetooth_address(info.address)) {
		info.address = normalize_address(info.address);
	}
	const godot::String key = device_cache_key(info);
	bool is_new = false;

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		is_new = !discovered_devices.has(key);

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
			discovered_devices.erase(stale_keys[i]);
			paired_devices.erase(stale_keys[i]);
		}

		if (discovered_devices.has(key)) {
			const DeviceInfo existing = discovered_devices[key];
			if (info.name.is_empty()) {
				info.name = existing.name;
			}
		}

		discovered_devices[key] = info;
		if (is_valid_bluetooth_address(info.address)) {
			address_to_device_id[info.address] = info.device_id;
		}
		if (info.paired) {
			paired_devices[key] = info;
		} else {
			paired_devices.erase(key);
		}
	}

	if (p_emit_event && (is_new || p_force_emit)) {
		BluetoothEvent event;
		event.type = EventType::DEVICE_FOUND;
		event.device = info;
		emit(event);
	}
}

void WindowsBackend::handle_device_added(const DeviceInformation &p_info) {
	DeviceInfo info = make_device_info(p_info);
	upsert_device(info, true);
}

void WindowsBackend::enumerate_snapshot(const hstring &p_selector, bool p_emit_events, bool p_force_emit) {
	try {
		const auto devices = find_devices_with_properties(p_selector);
		for (const auto &device_info : devices) {
			DeviceInfo info = make_device_info(device_info);
			upsert_device(info, p_emit_events, p_force_emit);
		}
	} catch (const winrt::hresult_error &error) {
		emit_error("enumerate_snapshot",
				format_winrt_error("enumerate_snapshot",
						error,
						"selector=\"" + hstring_to_godot(p_selector) +
								"\"; DeviceInformation::FindAllAsync with AEP properties failed"));
	}
}

void WindowsBackend::enumerate_hid_gamepads(bool p_emit_events, bool p_force_emit) {
	try {
		// Generic Desktop / Game Pad — catches Xbox, DualSense, etc. after Windows pairing.
		const auto selector = HidDevice::GetDeviceSelector(0x0001, 0x0005);
		const auto devices = DeviceInformation::FindAllAsync(selector).get();
		for (const auto &device_info : devices) {
			const godot::String device_id = hstring_to_godot(device_info.Id());
			const bool is_bluetooth_hid = device_id.contains("BTHENUM") || device_id.contains("BTHLEDEVICE") ||
					device_id.contains("Bluetooth");
			// Skip USB-only HID gamepads; keep all Bluetooth HID paths.
			if (!is_bluetooth_hid) {
				continue;
			}
			DeviceInfo info = make_hid_gamepad_info(device_info);
			info.paired = true;
			upsert_device(info, p_emit_events, p_force_emit);
		}
	} catch (const winrt::hresult_error &error) {
		emit_error("enumerate_hid_gamepads",
				format_winrt_error("enumerate_hid_gamepads",
						error,
						"selector=HidDevice::GetDeviceSelector(0x0001, 0x0005); "
						"DeviceInformation::FindAllAsync failed while listing Bluetooth HID gamepads"));
	}
}

void WindowsBackend::handle_device_updated(const DeviceInformationUpdate &p_update) {
	DeviceInfo partial;
	partial.device_id = hstring_to_godot(p_update.Id());

	if (auto properties = p_update.Properties()) {
		if (properties.HasKey(L"System.ItemNameDisplay")) {
			auto value = properties.Lookup(L"System.ItemNameDisplay");
			if (value) {
				partial.name = hstring_to_godot(unbox_value<hstring>(value));
			}
		}
		if (properties.HasKey(L"System.Devices.Aep.DeviceAddress")) {
			auto value = properties.Lookup(L"System.Devices.Aep.DeviceAddress");
			if (value) {
				partial.address = normalize_address(hstring_to_godot(unbox_value<hstring>(value)));
			}
		}
	}

	godot::String cache_key;
	DeviceInfo existing;
	bool found = false;

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.value.device_id == partial.device_id) {
				cache_key = item.key;
				existing = item.value;
				found = true;
				break;
			}
		}
	}

	if (!found) {
		DeviceInfo info = partial;
		if (info.address.is_empty()) {
			info.address = extract_address_from_device_id(info.device_id);
		}
		if (info.address.is_empty()) {
			info.address = info.device_id;
		}
		apply_display_name_fallback(info);
		info.device_class = infer_device_class(info.name);
		upsert_device(info, true);
		return;
	}

	if (!partial.name.is_empty()) {
		existing.name = partial.name;
	}
	if (!partial.address.is_empty()) {
		existing.address = partial.address;
	}
	apply_display_name_fallback(existing);

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		discovered_devices[cache_key] = existing;
		if (!existing.address.is_empty() && !existing.address.contains("#") && !existing.address.contains("\\")) {
			address_to_device_id[existing.address] = existing.device_id;
		}
	}

	if (parse_bluetooth_address(existing.address).has_value()) {
		update_connection_state(existing.address);
	}
}

void WindowsBackend::handle_device_removed(const DeviceInformationUpdate &p_update) {
	const godot::String device_id = hstring_to_godot(p_update.Id());
	godot::String removal_key;
	godot::String removal_address;
	{
		std::lock_guard<std::mutex> lock(state_mutex);
		for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.value.device_id == device_id) {
				removal_key = item.key;
				removal_address = item.value.address;
				break;
			}
		}
	}
	if (!removal_key.is_empty()) {
		remove_device_from_cache(removal_key, removal_address);
		emit_device_removed(removal_address);
	}
}

void WindowsBackend::start_device_watcher(const hstring &p_selector, DeviceInformationKind p_kind) {
	ActiveDeviceWatcher active;
	const auto additional_properties = single_threaded_vector<hstring>({
			L"System.Devices.Aep.DeviceAddress",
			L"System.Devices.Aep.IsConnected",
			L"System.Devices.Aep.Bluetooth.Le.IsConnectable",
			L"System.ItemNameDisplay",
	});

	if (p_kind == DeviceInformationKind::Unknown) {
		active.watcher = DeviceInformation::CreateWatcher(p_selector, additional_properties);
	} else {
		active.watcher = DeviceInformation::CreateWatcher(p_selector, additional_properties, p_kind);
	}

	active.added_token = active.watcher.Added([this](const DeviceWatcher &, const DeviceInformation &info) {
		handle_device_added(info);
	});
	active.updated_token = active.watcher.Updated([this](const DeviceWatcher &, const DeviceInformationUpdate &update) {
		handle_device_updated(update);
	});
	active.removed_token = active.watcher.Removed([this](const DeviceWatcher &, const DeviceInformationUpdate &update) {
		handle_device_removed(update);
	});
	active.watcher.Start();
	active_watchers.push_back(std::move(active));
}

void WindowsBackend::stop_all_watchers() {
	for (ActiveDeviceWatcher &active : active_watchers) {
		if (!active.watcher) {
			continue;
		}
		active.watcher.Added(active.added_token);
		active.watcher.Updated(active.updated_token);
		active.watcher.Removed(active.removed_token);
		active.watcher.Stop();
	}
	active_watchers.clear();
}

void WindowsBackend::start_scan() {
	if (!initialized && !initialize()) {
		return;
	}
	if (scanning) {
		return;
	}

	try {
		BluetoothEvent started;
		started.type = EventType::SCAN_STARTED;
		emit(started);

		// Silent snapshot of known devices — one batch sync instead of per-device DEVICE_FOUND.
		enumerate_snapshot(BluetoothDevice::GetDeviceSelectorFromPairingState(true), false, false);
		enumerate_snapshot(BluetoothLEDevice::GetDeviceSelectorFromPairingState(true), false, false);
		enumerate_snapshot(BluetoothDevice::GetDeviceSelector(), false, false);
		enumerate_snapshot(BluetoothLEDevice::GetDeviceSelector(), false, false);
		enumerate_hid_gamepads(false, false);
		emit_paired_devices_updated();

		// Active RF discovery — multiple watchers cover Classic, BLE, and AEP protocols.
		start_device_watcher(BluetoothDevice::GetDeviceSelectorFromPairingState(false));
		start_device_watcher(BluetoothLEDevice::GetDeviceSelectorFromPairingState(false),
				DeviceInformationKind::AssociationEndpoint);
		start_device_watcher(hstring(AEP_BLUETOOTH_PROTOCOL_SELECTOR), DeviceInformationKind::AssociationEndpoint);
		start_device_watcher(hstring(AEP_BLUETOOTH_LE_PROTOCOL_SELECTOR), DeviceInformationKind::AssociationEndpoint);

		scanning = true;
	} catch (const winrt::hresult_error &error) {
		stop_all_watchers();
		emit_error("start_scan",
				format_winrt_error("start_scan",
						error,
						"Failed while creating or starting one or more DeviceWatcher instances "
						"(Classic unpaired, BLE unpaired/AEP, Bluetooth AEP, Bluetooth LE AEP)"));
	}
}

void WindowsBackend::stop_scan() {
	if (!scanning) {
		return;
	}

	try {
		stop_all_watchers();
		scanning = false;

		BluetoothEvent event;
		event.type = EventType::SCAN_STOPPED;
		emit(event);
	} catch (const winrt::hresult_error &error) {
		emit_error("stop_scan",
				format_winrt_error("stop_scan", error, "Failed while stopping active DeviceWatcher instances"));
	}
}

DeviceInformation WindowsBackend::find_device_information(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	{
		std::lock_guard<std::mutex> lock(state_mutex);
		if (address_to_device_id.has(normalized)) {
			return find_device_information_by_id(address_to_device_id[normalized]);
		}
		for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
			if (item.key == normalized || item.value.address == normalized || item.value.device_id == normalized) {
				return find_device_information_by_id(item.value.device_id);
			}
		}
	}

	try {
		const auto parsed = parse_bluetooth_address(normalized);
		if (!parsed.has_value()) {
			return nullptr;
		}

		const auto selector = BluetoothDevice::GetDeviceSelectorFromBluetoothAddress(parsed.value());
		const auto devices = DeviceInformation::FindAllAsync(selector).get();
		if (devices.Size() > 0) {
			return devices.GetAt(0);
		}
	} catch (...) {
	}

	return nullptr;
}

DeviceInformation WindowsBackend::find_device_information_by_id(const godot::String &p_device_id) {
	try {
		const godot::CharString utf8 = p_device_id.utf8();
		return DeviceInformation::CreateFromIdAsync(winrt::to_hstring(std::string_view(utf8.get_data(), utf8.length()))).get();
	} catch (...) {
		return nullptr;
	}
}

void WindowsBackend::pair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	BluetoothEvent started;
	started.type = EventType::PAIRING_STARTED;
	started.address = normalized;
	emit(started);

	try {
		auto device_info = find_device_information(normalized);
		if (!device_info) {
			BluetoothEvent failed;
			failed.type = EventType::PAIRING_FAILED;
			failed.address = normalized;
			failed.message =
					"pair_device failed for address \"" + normalized +
					"\": DeviceInformation lookup returned null. Checked discovered_devices cache, "
					"address_to_device_id map, and BluetoothDevice::GetDeviceSelectorFromBluetoothAddress "
					"via FindAllAsync. Device may be out of range, not advertising, or scan may not be active.";
			emit(failed);
			return;
		}

		const godot::String resolved_device_id = hstring_to_godot(device_info.Id());
		auto pairing = device_info.Pairing();
		DevicePairingResult result{ nullptr };

		if (pairing.CanPair()) {
			if (pairing.IsPaired()) {
				BluetoothEvent succeeded;
				succeeded.type = EventType::PAIRING_SUCCEEDED;
				succeeded.address = normalized;
				emit(succeeded);
				refresh_paired_devices();
				return;
			}

			auto custom = pairing.Custom();
			if (custom) {
				custom.PairingRequested([](const DeviceInformationCustomPairing &,
							const DevicePairingRequestedEventArgs &args) {
					switch (args.PairingKind()) {
						case DevicePairingKinds::ConfirmOnly:
						case DevicePairingKinds::ConfirmPinMatch:
							args.Accept();
							break;
						case DevicePairingKinds::ProvidePin:
							args.Accept(L"0000");
							break;
						default:
							break;
					}
				});

				result = custom.PairAsync(
						DevicePairingKinds::ConfirmOnly | DevicePairingKinds::ProvidePin,
						DevicePairingProtectionLevel::EncryptionAndAuthentication)
								 .get();
			} else {
				result = pairing.PairAsync(DevicePairingProtectionLevel::EncryptionAndAuthentication).get();
			}
		} else if (pairing.IsPaired()) {
			BluetoothEvent succeeded;
			succeeded.type = EventType::PAIRING_SUCCEEDED;
			succeeded.address = normalized;
			emit(succeeded);
			refresh_paired_devices();
			return;
		} else {
			BluetoothEvent failed;
			failed.type = EventType::PAIRING_FAILED;
			failed.address = normalized;
			failed.message = "pair_device failed for address \"" + normalized + "\" (device_id=\"" +
					resolved_device_id +
					"\"): DeviceInformationPairing.CanPair() returned false and IsPaired() returned false. "
					"Windows reports this endpoint cannot enter a pairing ceremony.";
			emit(failed);
			return;
		}

		if (result && (result.Status() == DevicePairingResultStatus::Paired ||
							  result.Status() == DevicePairingResultStatus::AlreadyPaired)) {
			BluetoothEvent succeeded;
			succeeded.type = EventType::PAIRING_SUCCEEDED;
			succeeded.address = normalized;
			emit(succeeded);
			refresh_paired_devices();
		} else {
			BluetoothEvent failed;
			failed.type = EventType::PAIRING_FAILED;
			failed.address = normalized;
			if (result) {
				failed.message = "pair_device failed for address \"" + normalized + "\" (device_id=\"" +
						resolved_device_id + "\"): " + pairing_status_detail(result.Status()) +
						" after PairAsync(ConfirmOnly|ProvidePin, EncryptionAndAuthentication).";
			} else {
				failed.message = "pair_device failed for address \"" + normalized + "\" (device_id=\"" +
						resolved_device_id +
						"\"): PairAsync returned a null DevicePairingResult (no status available).";
			}
			emit(failed);
		}
	} catch (const winrt::hresult_error &error) {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = normalized;
		failed.message = format_winrt_error("pair_device", error, "address=\"" + normalized + "\"");
		emit(failed);
	}
}

void WindowsBackend::unpair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	try {
		auto device_info = find_device_information(normalized);
		if (!device_info) {
			emit_error("unpair_device",
					"unpair_device failed for address \"" + normalized +
							"\": DeviceInformation lookup returned null. Checked discovered_devices cache, "
							"address_to_device_id map, and BluetoothDevice::GetDeviceSelectorFromBluetoothAddress.");
			return;
		}

		const godot::String resolved_device_id = hstring_to_godot(device_info.Id());
		const auto result = device_info.Pairing().UnpairAsync().get();
		if (result.Status() == DeviceUnpairingResultStatus::Unpaired ||
				result.Status() == DeviceUnpairingResultStatus::AlreadyUnpaired) {
			godot::String removal_key = normalized;
			godot::String removal_address = normalized;
			{
				std::lock_guard<std::mutex> lock(state_mutex);
				for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
					if (item.value.device_id == resolved_device_id || addresses_match(item.value.address, normalized)) {
						removal_key = item.key;
						removal_address = item.value.address;
						break;
					}
				}
			}
			remove_device_from_cache(removal_key, removal_address);
			emit_device_removed(removal_address);
		} else {
			emit_error("unpair_device",
					"unpair_device failed for address \"" + normalized + "\" (device_id=\"" + resolved_device_id +
							"\"): " + unpair_status_detail(result.Status()) + " after UnpairAsync.");
		}
	} catch (const winrt::hresult_error &error) {
		emit_error("unpair_device", format_winrt_error("unpair_device", error, "address=\"" + normalized + "\""));
	}
}

void WindowsBackend::connect_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	try {
		const auto parsed = parse_bluetooth_address(normalized);
		if (!parsed.has_value()) {
			emit_error("connect_device",
					"connect_device failed for input \"" + normalized +
							"\": not a valid 6-byte Bluetooth MAC address (expected format AA:BB:CC:DD:EE:FF).");
			return;
		}

		auto device = BluetoothDevice::FromBluetoothAddressAsync(parsed.value()).get();
		if (!device) {
			emit_error("connect_device",
					"connect_device failed for address \"" + normalized +
							"\": BluetoothDevice::FromBluetoothAddressAsync returned null. Device may be unpaired, "
							"powered off, out of range, or only visible as a BLE/AEP endpoint.");
			return;
		}

		// HID gamepads typically auto-connect once paired and powered on.
		// Requesting RFCOMM services can encourage the stack to establish a connection.
		device.GetRfcommServicesAsync(BluetoothCacheMode::Uncached).get();
		update_connection_state(normalized, true);
	} catch (const winrt::hresult_error &error) {
		emit_error("connect_device", format_winrt_error("connect_device", error, "address=\"" + normalized + "\""));
	}
}

void WindowsBackend::disconnect_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	godot::String disconnect_note =
			"disconnect_device for address \"" + normalized +
			"\": Windows has no public API to force-disconnect Bluetooth HID gamepads (Xbox, DualSense, etc.). ";

	try {
		const auto parsed = parse_bluetooth_address(normalized);
		if (parsed.has_value()) {
			try {
				auto le_device = BluetoothLEDevice::FromBluetoothAddressAsync(parsed.value()).get();
				if (le_device) {
					le_device.Close();
					disconnect_note +=
							"BluetoothLEDevice::Close() was called for any LE handles on this address. ";
				}
			} catch (...) {
			}
		}
	} catch (...) {
	}

	disconnect_note +=
			"To fully disconnect HID controllers, power them off or remove pairing in Windows Settings. "
			"Refreshing connection state from the stack.";

	update_connection_state(normalized, false, false);

	BluetoothEvent event;
	event.type = EventType::CONNECTION_CHANGED;
	event.address = normalized;
	event.connected = is_connected(normalized);
	event.message = disconnect_note;
	emit(event);
}

void WindowsBackend::update_connection_state(const godot::String &p_address, bool p_report_errors, bool p_emit_event) {
	const godot::String normalized = normalize_address(p_address);

	try {
		const auto parsed = parse_bluetooth_address(normalized);
		if (!parsed.has_value()) {
			if (p_report_errors) {
				emit_error("update_connection_state",
						"update_connection_state failed for input \"" + normalized +
								"\": not a valid 6-byte Bluetooth MAC address (expected format AA:BB:CC:DD:EE:FF).");
			}
			return;
		}

		const bool connected = query_device_connected(parsed.value(), normalized);
		godot::PackedStringArray updated_keys;

		{
			std::lock_guard<std::mutex> lock(state_mutex);
			for (godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
				if (!addresses_match(item.value.address, normalized) && !addresses_match(item.key, normalized)) {
					continue;
				}
				item.value.connected = connected;
				if (is_valid_bluetooth_address(normalized)) {
					item.value.address = normalized;
				}
				discovered_devices[item.key] = item.value;
				if (item.value.paired) {
					paired_devices[item.key] = item.value;
				}
				updated_keys.append(item.key);
			}
		}

		for (int i = 0; i < updated_keys.size(); i++) {
			DeviceInfo info;
			{
				std::lock_guard<std::mutex> lock(state_mutex);
				if (discovered_devices.has(updated_keys[i])) {
					info = discovered_devices[updated_keys[i]];
				}
			}
			if (!info.address.is_empty()) {
				BluetoothEvent found;
				found.type = EventType::DEVICE_FOUND;
				found.device = info;
				emit(found);
			}
		}

		if (p_emit_event) {
			BluetoothEvent event;
			event.type = EventType::CONNECTION_CHANGED;
			event.address = normalized;
			event.connected = connected;
			if (p_report_errors) {
				const bool hid_active = is_hid_gamepad_connected(normalized);
				event.message = "Connection state for \"" + normalized + "\": classic=" +
						godot::String(connected && !hid_active ? "Connected" : (hid_active ? "HID active" : "Disconnected")) +
						", hid_gamepad=" + (hid_active ? "present" : "absent") + ".";
			}
			emit(event);
		}
	} catch (const winrt::hresult_error &error) {
		if (p_report_errors) {
			emit_error("update_connection_state",
					format_winrt_error("update_connection_state", error, "address=\"" + normalized + "\""));
		}
	} catch (...) {
		if (p_report_errors) {
			emit_error("update_connection_state",
					"update_connection_state failed for address \"" + normalized +
							"\": unknown non-WinRT exception while querying BluetoothDevice::ConnectionStatus.");
		}
	}
}

void WindowsBackend::refresh_paired_devices() {
	enumerate_snapshot(BluetoothDevice::GetDeviceSelectorFromPairingState(true), false, false);
	enumerate_hid_gamepads(false, false);
	emit_paired_devices_updated();
}

bool WindowsBackend::is_connected(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	std::lock_guard<std::mutex> lock(state_mutex);
	for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
		if (addresses_match(item.key, normalized) || addresses_match(item.value.address, normalized)) {
			return item.value.connected;
		}
	}
	for (const godot::KeyValue<godot::String, DeviceInfo> &item : paired_devices) {
		if (addresses_match(item.key, normalized) || addresses_match(item.value.address, normalized)) {
			return item.value.connected;
		}
	}
	return false;
}

bool WindowsBackend::is_paired(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	std::lock_guard<std::mutex> lock(state_mutex);
	for (const godot::KeyValue<godot::String, DeviceInfo> &item : paired_devices) {
		if (addresses_match(item.key, normalized) || addresses_match(item.value.address, normalized)) {
			return true;
		}
	}
	for (const godot::KeyValue<godot::String, DeviceInfo> &item : discovered_devices) {
		if (addresses_match(item.key, normalized) || addresses_match(item.value.address, normalized)) {
			return item.value.paired;
		}
	}
	return false;
}

} // namespace bluetooth