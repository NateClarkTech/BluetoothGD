#include "windows_backend.h"
#include "winrt_bluetooth.h"

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.HumanInterfaceDevice.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <cstdio>
#include <string_view>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::HumanInterfaceDevice;
using namespace Windows::Devices::Radios;

namespace bluetooth {

namespace {

godot::String hstring_to_godot(const hstring &p_value) {
	return godot::String(winrt::to_string(p_value).c_str());
}

hstring godot_to_hstring(const godot::String &p_value) {
	const godot::CharString utf8 = p_value.utf8();
	return winrt::to_hstring(std::string_view(utf8.get_data(), utf8.length()));
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

template <typename PropertiesMap>
bool read_aep_connected_from_properties(const PropertiesMap &p_properties, bool &p_out_connected) {
	if (!p_properties || !p_properties.HasKey(L"System.Devices.Aep.IsConnected")) {
		return false;
	}
	const auto value = p_properties.Lookup(L"System.Devices.Aep.IsConnected");
	if (!value) {
		return false;
	}
	p_out_connected = unbox_value<bool>(value);
	return true;
}

template <typename PropertiesMap>
bool read_aep_address_from_properties(const PropertiesMap &p_properties, godot::String &p_out_address) {
	if (!p_properties || !p_properties.HasKey(L"System.Devices.Aep.DeviceAddress")) {
		return false;
	}
	const auto value = p_properties.Lookup(L"System.Devices.Aep.DeviceAddress");
	if (!value) {
		return false;
	}
	p_out_address = normalize_address(hstring_to_godot(unbox_value<hstring>(value)));
	return !p_out_address.is_empty();
}

DeviceInfo make_device_info(const DeviceInformation &p_info) {
	DeviceInfo info;
	info.device_id = hstring_to_godot(p_info.Id());
	info.name = hstring_to_godot(p_info.Name());
	info.paired = p_info.Pairing().IsPaired();
	info.connected = false;

	bool has_address_from_props = false;
	bool has_connected_from_props = false;

	if (auto properties = p_info.Properties()) {
		has_address_from_props = read_aep_address_from_properties(properties, info.address);
		has_connected_from_props = read_aep_connected_from_properties(properties, info.connected);

		if (properties.HasKey(L"System.ItemNameDisplay")) {
			const auto value = properties.Lookup(L"System.ItemNameDisplay");
			if (value) {
				const godot::String display_name = hstring_to_godot(unbox_value<hstring>(value));
				if (!display_name.is_empty()) {
					info.name = display_name;
				}
			}
		}
	}

	if (!has_address_from_props || !has_connected_from_props) {
		try {
			const auto device = BluetoothDevice::FromIdAsync(p_info.Id()).get();
			if (device) {
				if (!has_address_from_props) {
					info.address = format_bluetooth_address(device.BluetoothAddress());
				}
				if (!has_connected_from_props) {
					info.connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
				}
				if (info.name.is_empty()) {
					info.name = hstring_to_godot(device.Name());
				}
			}
		} catch (...) {
		}
	}

	if (info.address.is_empty() || info.address == info.device_id) {
		try {
			const auto le_device = BluetoothLEDevice::FromIdAsync(p_info.Id()).get();
			if (le_device) {
				if (!has_address_from_props) {
					info.address = format_bluetooth_address(le_device.BluetoothAddress());
				}
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

bool is_bluetooth_hid_device_id(const godot::String &p_device_id) {
	return p_device_id.contains("BTHENUM") || p_device_id.contains("BTHLEDEVICE") || p_device_id.contains("Bluetooth");
}

void refresh_hid_gamepad_cache_locked(HidGamepadCache &p_cache) {
	p_cache.results.clear();
	try {
		const auto selector = HidDevice::GetDeviceSelector(0x0001, 0x0005);
		const auto devices = DeviceInformation::FindAllAsync(selector).get();
		for (const auto &device_info : devices) {
			const godot::String device_id = hstring_to_godot(device_info.Id());
			if (!is_bluetooth_hid_device_id(device_id)) {
				continue;
			}
			const godot::String address = normalize_address(extract_address_from_device_id(device_id));
			if (!address.is_empty()) {
				p_cache.results[address] = true;
			}
		}
	} catch (...) {
	}
	p_cache.cached_at = std::chrono::steady_clock::now();
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

bool HidGamepadCache::is_connected(const godot::String &p_normalized) {
	const godot::String normalized = normalize_address(p_normalized);
	std::lock_guard<std::mutex> lock(mutex);
	const auto now = std::chrono::steady_clock::now();
	const bool stale = cached_at.time_since_epoch().count() == 0 ||
			std::chrono::duration_cast<std::chrono::milliseconds>(now - cached_at).count() > TTL_MS;
	if (stale) {
		refresh_hid_gamepad_cache_locked(*this);
	}
	if (results.has(normalized)) {
		return results[normalized];
	}
	return false;
}

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
	pairing_pending.reset();
	stop_scan();
	stop_all_watchers();
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
	event.devices = cache.paired_array();
	emit(event);
}

bool WindowsBackend::passes_scan_filters(const DeviceInfo &p_info) const {
	if (scan_options.named_only) {
		if (p_info.name.is_empty() || is_mac_address_name(p_info.name, p_info.address)) {
			return false;
		}
	}
	if (scan_options.gamepads_only && p_info.device_class != "gamepad") {
		return false;
	}
	return true;
}

void WindowsBackend::upsert_device(const DeviceInfo &p_info, bool p_emit_event, bool p_force_emit) {
	bool is_new = false;
	DeviceInfo info = p_info;
	cache.upsert(info, is_new);

	if (p_emit_event && (is_new || p_force_emit) && passes_scan_filters(info)) {
		BluetoothEvent event;
		event.type = EventType::DEVICE_FOUND;
		event.device = info;
		emit(event);
	}
}

void WindowsBackend::handle_device_added(const DeviceInformation &p_info) {
	const DeviceInfo info = make_device_info(p_info);
	upsert_device(info, true);
}

void WindowsBackend::enumerate_snapshot(const hstring &p_selector, bool p_emit_events, bool p_force_emit) {
	try {
		const auto devices = find_devices_with_properties(p_selector);
		for (const auto &device_info : devices) {
			const DeviceInfo info = make_device_info(device_info);
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
		const auto selector = HidDevice::GetDeviceSelector(0x0001, 0x0005);
		const auto devices = DeviceInformation::FindAllAsync(selector).get();
		for (const auto &device_info : devices) {
			const godot::String device_id = hstring_to_godot(device_info.Id());
			if (!is_bluetooth_hid_device_id(device_id)) {
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
	bool has_connected_update = false;

	if (auto properties = p_update.Properties()) {
		if (properties.HasKey(L"System.ItemNameDisplay")) {
			const auto value = properties.Lookup(L"System.ItemNameDisplay");
			if (value) {
				partial.name = hstring_to_godot(unbox_value<hstring>(value));
			}
		}
		read_aep_address_from_properties(properties, partial.address);
		if (read_aep_connected_from_properties(properties, partial.connected)) {
			has_connected_update = true;
		}
	}

	godot::String cache_key;
	DeviceInfo existing;
	if (!cache.find_by_device_id(partial.device_id, cache_key, existing)) {
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
	if (has_connected_update) {
		existing.connected = partial.connected;
	}
	apply_display_name_fallback(existing);
	upsert_device(existing, true);

	if (!has_connected_update && parse_bluetooth_address(existing.address).has_value()) {
		update_connection_state(existing.address);
	}
}

void WindowsBackend::handle_device_removed(const DeviceInformationUpdate &p_update) {
	const godot::String device_id = hstring_to_godot(p_update.Id());
	godot::String removal_key;
	DeviceInfo existing;
	if (cache.find_by_device_id(device_id, removal_key, existing)) {
		const godot::String removal_address = existing.address;
		cache.remove(removal_key, removal_address);
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
		active.added_token = {};
		active.updated_token = {};
		active.removed_token = {};
		active.watcher = nullptr;
	}
	active_watchers.clear();
}

void WindowsBackend::start_scan(const ScanOptions &p_options) {
	if (!initialized && !initialize()) {
		return;
	}
	if (scanning) {
		return;
	}

	scan_options = p_options;

	try {
		BluetoothEvent started;
		started.type = EventType::SCAN_STARTED;
		emit(started);

		enumerate_snapshot(BluetoothDevice::GetDeviceSelectorFromPairingState(true), false, false);
		enumerate_snapshot(BluetoothLEDevice::GetDeviceSelectorFromPairingState(true), false, false);
		enumerate_snapshot(BluetoothDevice::GetDeviceSelector(), false, false);
		enumerate_snapshot(BluetoothLEDevice::GetDeviceSelector(), false, false);
		if (!scan_options.gamepads_only) {
			enumerate_hid_gamepads(false, false);
		}
		emit_paired_devices_updated();

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

	const godot::String cached_device_id = cache.resolve_device_id(normalized);
	if (!cached_device_id.is_empty()) {
		return find_device_information_by_id(cached_device_id);
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
		return DeviceInformation::CreateFromIdAsync(godot_to_hstring(p_device_id)).get();
	} catch (...) {
		return nullptr;
	}
}

void WindowsBackend::handle_pairing_requested(const DevicePairingRequestedEventArgs &p_args,
		const godot::String &p_address) {
	const auto pairing_kind = p_args.PairingKind();
	const godot::String pin = hstring_to_godot(p_args.Pin());

	switch (pairing_kind) {
		case DevicePairingKinds::ConfirmOnly:
		case DevicePairingKinds::ConfirmPinMatch: {
			pairing_pending.begin(p_address, "confirm", pin);

			BluetoothEvent event;
			event.type = EventType::PAIRING_CONFIRMATION_REQUESTED;
			event.address = p_address;
			event.pairing_kind = "confirm";
			if (!pin.is_empty()) {
				event.display_pin = pin;
			}
			emit(event);

			PairingUserResponse response;
			if (pairing_pending.wait_for_response(120000, response) && response.accepted) {
				if (!pin.is_empty()) {
					p_args.Accept(godot_to_hstring(pin));
				} else {
					p_args.Accept();
				}
			}
			pairing_pending.reset();
			break;
		}
		case DevicePairingKinds::ProvidePin: {
			pairing_pending.begin(p_address, "provide_pin");

			BluetoothEvent event;
			event.type = EventType::PAIRING_PIN_REQUESTED;
			event.address = p_address;
			event.pairing_kind = "provide_pin";
			emit(event);

			PairingUserResponse response;
			if (pairing_pending.wait_for_response(120000, response) && response.accepted && !response.pin.is_empty()) {
				p_args.Accept(godot_to_hstring(response.pin));
			}
			pairing_pending.reset();
			break;
		}
		case DevicePairingKinds::DisplayPin: {
			pairing_pending.begin(p_address, "display_pin", pin);

			BluetoothEvent event;
			event.type = EventType::PAIRING_DISPLAY_PIN;
			event.address = p_address;
			event.pairing_kind = "display_pin";
			event.display_pin = pin;
			emit(event);

			PairingUserResponse response;
			pairing_pending.wait_for_response(3000, response);
			p_args.Accept();
			pairing_pending.reset();
			break;
		}
		default:
			break;
	}
}

void WindowsBackend::perform_pairing(const DeviceInformation &p_device_info, const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);
	const godot::String resolved_device_id = hstring_to_godot(p_device_info.Id());
	auto pairing = p_device_info.Pairing();
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

		const auto custom = pairing.Custom();
		if (custom) {
			custom.PairingRequested([this, normalized](const DeviceInformationCustomPairing &,
						const DevicePairingRequestedEventArgs &args) {
				handle_pairing_requested(args, normalized);
			});

			result = custom.PairAsync(
					DevicePairingKinds::ConfirmOnly | DevicePairingKinds::ConfirmPinMatch |
							DevicePairingKinds::ProvidePin | DevicePairingKinds::DisplayPin,
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
		failed.message = "pair_device failed for address \"" + normalized + "\" (device_id=\"" + resolved_device_id +
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
			failed.message = "pair_device failed for address \"" + normalized + "\" (device_id=\"" + resolved_device_id +
					"\"): " + pairing_status_detail(result.Status()) +
					" after PairAsync(ConfirmOnly|ConfirmPinMatch|ProvidePin|DisplayPin, EncryptionAndAuthentication).";
		} else {
			failed.message = "pair_device failed for address \"" + normalized + "\" (device_id=\"" + resolved_device_id +
					"\"): PairAsync returned a null DevicePairingResult (no status available).";
		}
		emit(failed);
	}
}

void WindowsBackend::pair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	BluetoothEvent started;
	started.type = EventType::PAIRING_STARTED;
	started.address = normalized;
	emit(started);

	try {
		const auto device_info = find_device_information(normalized);
		if (!device_info) {
			BluetoothEvent failed;
			failed.type = EventType::PAIRING_FAILED;
			failed.address = normalized;
			failed.message =
					"pair_device failed for address \"" + normalized +
					"\": DeviceInformation lookup returned null. Checked device cache, "
					"address_to_device_id map, and BluetoothDevice::GetDeviceSelectorFromBluetoothAddress "
					"via FindAllAsync. Device may be out of range, not advertising, or scan may not be active.";
			emit(failed);
			return;
		}

		perform_pairing(device_info, normalized);
	} catch (const winrt::hresult_error &error) {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = normalized;
		failed.message = format_winrt_error("pair_device", error, "address=\"" + normalized + "\"");
		emit(failed);
	}
}

void WindowsBackend::pair_device_by_id(const godot::String &p_device_id) {
	godot::String normalized;
	{
		godot::String cache_key;
		DeviceInfo cached;
		if (cache.find_by_device_id(p_device_id, cache_key, cached) && is_valid_bluetooth_address(cached.address)) {
			normalized = normalize_address(cached.address);
		}
	}

	BluetoothEvent started;
	started.type = EventType::PAIRING_STARTED;
	started.address = normalized.is_empty() ? p_device_id : normalized;
	emit(started);

	try {
		const auto device_info = find_device_information_by_id(p_device_id);
		if (!device_info) {
			BluetoothEvent failed;
			failed.type = EventType::PAIRING_FAILED;
			failed.address = normalized.is_empty() ? p_device_id : normalized;
			failed.message = "pair_device_by_id failed for device_id \"" + p_device_id +
					"\": DeviceInformation::CreateFromIdAsync returned null. "
					"The device may have been removed from the system or the ID is stale.";
			emit(failed);
			return;
		}

		if (normalized.is_empty()) {
			const DeviceInfo info = make_device_info(device_info);
			normalized = is_valid_bluetooth_address(info.address) ? normalize_address(info.address) : p_device_id;
		}

		perform_pairing(device_info, normalized);
	} catch (const winrt::hresult_error &error) {
		BluetoothEvent failed;
		failed.type = EventType::PAIRING_FAILED;
		failed.address = normalized.is_empty() ? p_device_id : normalized;
		failed.message = format_winrt_error("pair_device_by_id", error, "device_id=\"" + p_device_id + "\"");
		emit(failed);
	}
}

void WindowsBackend::confirm_pairing(const godot::String &p_pin) {
	PairingUserResponse response;
	response.accepted = true;
	response.pin = p_pin;
	pairing_pending.submit(response);
}

void WindowsBackend::reject_pairing() {
	PairingUserResponse response;
	response.accepted = false;
	pairing_pending.submit(response);
}

void WindowsBackend::cancel_pairing() {
	reject_pairing();
	pairing_pending.reset();
}

void WindowsBackend::submit_pairing_response(const PairingUserResponse &p_response) {
	pairing_pending.submit(p_response);
}

void WindowsBackend::unpair_device(const godot::String &p_address) {
	const godot::String normalized = normalize_address(p_address);

	try {
		const auto device_info = find_device_information(normalized);
		if (!device_info) {
			emit_error("unpair_device",
					"unpair_device failed for address \"" + normalized +
							"\": DeviceInformation lookup returned null. Checked device cache, "
							"address_to_device_id map, and BluetoothDevice::GetDeviceSelectorFromBluetoothAddress.");
			return;
		}

		const godot::String resolved_device_id = hstring_to_godot(device_info.Id());
		const auto result = device_info.Pairing().UnpairAsync().get();
		if (result.Status() == DeviceUnpairingResultStatus::Unpaired ||
				result.Status() == DeviceUnpairingResultStatus::AlreadyUnpaired) {
			godot::String removal_key = normalized;
			godot::String removal_address = normalized;
			godot::String cache_key;
			DeviceInfo cached;
			if (cache.find_by_device_id(resolved_device_id, cache_key, cached)) {
				removal_key = cache_key;
				removal_address = cached.address;
			}
			cache.remove(removal_key, removal_address);
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
		BluetoothDevice device{ nullptr };
		const auto parsed = parse_bluetooth_address(normalized);
		if (parsed.has_value()) {
			device = BluetoothDevice::FromBluetoothAddressAsync(parsed.value()).get();
		}

		if (!device) {
			const godot::String device_id = cache.resolve_device_id(normalized);
			if (!device_id.is_empty()) {
				try {
					device = BluetoothDevice::FromIdAsync(godot_to_hstring(device_id)).get();
				} catch (...) {
				}
			}
		}

		if (!device) {
			const auto device_info = find_device_information(normalized);
			if (device_info) {
				try {
					device = BluetoothDevice::FromIdAsync(device_info.Id()).get();
				} catch (...) {
				}
			}
		}

		if (!device) {
			emit_error("connect_device",
					"connect_device failed for address \"" + normalized +
							"\": BluetoothDevice lookup returned null after trying FromBluetoothAddressAsync, "
							"cached device_id, and DeviceInformation::CreateFromIdAsync. Device may be unpaired, "
							"powered off, out of range, or only visible as a BLE/AEP endpoint.");
			return;
		}

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
				const auto le_device = BluetoothLEDevice::FromBluetoothAddressAsync(parsed.value()).get();
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

		bool connected = false;
		try {
			const auto device = BluetoothDevice::FromBluetoothAddressAsync(parsed.value()).get();
			if (device) {
				connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
			}
		} catch (...) {
		}
		if (!connected) {
			connected = hid_gamepad_cache.is_connected(normalized);
		}

		godot::PackedStringArray updated_keys;
		cache.update_connection_for_address(normalized, connected, updated_keys);

		for (int i = 0; i < updated_keys.size(); i++) {
			DeviceInfo info;
			if (cache.get_device(updated_keys[i], info) && !info.address.is_empty()) {
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
				const bool hid_active = hid_gamepad_cache.is_connected(normalized);
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
	bool connected = false;
	if (cache.lookup_cached_state(p_address, connected, [](const DeviceInfo &p_info) {
				return p_info.connected;
			})) {
		return connected;
	}
	return false;
}

bool WindowsBackend::is_paired(const godot::String &p_address) {
	bool paired = false;
	if (cache.lookup_cached_state(p_address, paired, [](const DeviceInfo &p_info) {
				return p_info.paired;
			})) {
		return paired;
	}
	return false;
}

bool WindowsBackend::is_radio_on() const {
	try {
		const auto radios = Radio::GetRadiosAsync().get();
		for (const auto &radio : radios) {
			if (radio.Kind() == RadioKind::Bluetooth) {
				return radio.State() == RadioState::On;
			}
		}
	} catch (...) {
	}
	return false;
}

godot::Dictionary WindowsBackend::get_capabilities() const {
	godot::Dictionary caps;
	caps["platform"] = "windows";
	caps["classic_bluetooth"] = true;
	caps["ble_discovery"] = true;
	caps["hid_gamepad_detection"] = true;
	caps["interactive_pairing"] = true;
	caps["pair_by_device_id"] = true;
	caps["radio_state_query"] = true;
	caps["force_disconnect_hid"] = false;
	caps["unpair_while_connected"] = false;
	caps["scan_filter_named_only"] = true;
	caps["scan_filter_gamepads_only"] = true;
	return caps;
}

} // namespace bluetooth