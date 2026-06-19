extends Control

enum DeviceAction { NONE, PAIR, CONNECT, DISCONNECT }

@onready var device_list: ItemList = %DeviceList
@onready var status_label: Label = %StatusLabel
@onready var log_output: RichTextLabel = %LogOutput
@onready var advanced_log: RichTextLabel = %AdvancedLog
@onready var advanced_scroll: ScrollContainer = %AdvancedScroll
@onready var scan_activity_label: Label = %ScanActivityLabel
@onready var scan_progress_bar: ProgressBar = %ScanProgressBar
@onready var start_scan_button: Button = %StartScanButton
@onready var stop_scan_button: Button = %StopScanButton
@onready var toggle_advanced_button: Button = %ToggleAdvancedButton
@onready var advanced_panel: PanelContainer = %AdvancedPanel
@onready var devices_header: Label = %DevicesHeader
@onready var named_only_checkbox: CheckButton = %NamedOnlyCheckButton
@onready var device_action_button: Button = %DeviceActionButton
@onready var unpair_button: Button = %UnpairButton

var _is_scanning: bool = false
var _advanced_visible: bool = false
var _named_only_filter: bool = true
var _scan_elapsed_sec: float = 0.0
var _devices_found_this_scan: int = 0
var _scan_tween: Tween = null
var _dot_timer: float = 0.0
var _dot_count: int = 0
var _discovery_sequence: int = 0
var _device_cache: Dictionary = {}
var _current_device_action: DeviceAction = DeviceAction.NONE


func _ready() -> void:
	_connect_signals()
	device_list.item_selected.connect(_on_device_selected)
	_log("Bluetooth platform: %s" % Bluetooth.get_platform_name())
	_log("Bluetooth available: %s" % str(Bluetooth.is_bluetooth_available()))
	_log_advanced("INFO", "Advanced log started — tracks connect/disconnect attempts and errors.")
	_sync_cache_from_bluetooth()
	_refresh_device_list()
	_set_scan_ui_active(false)
	_set_advanced_visible(false)
	_update_device_action_button()


func _process(delta: float) -> void:
	if not _is_scanning:
		return

	_scan_elapsed_sec += delta
	_dot_timer += delta
	if _dot_timer >= 0.4:
		_dot_timer = 0.0
		_dot_count = (_dot_count + 1) % 4
		var dots := ".".repeat(_dot_count)
		scan_activity_label.text = "Scanning for nearby controllers%s  (%.0fs elapsed)" % [dots, _scan_elapsed_sec]


func _connect_signals() -> void:
	Bluetooth.device_found.connect(_on_device_found)
	Bluetooth.scan_started.connect(_on_scan_started)
	Bluetooth.scan_stopped.connect(_on_scan_stopped)
	Bluetooth.pairing_started.connect(_on_pairing_started)
	Bluetooth.pairing_succeeded.connect(_on_pairing_succeeded)
	Bluetooth.pairing_failed.connect(_on_pairing_failed)
	Bluetooth.connection_changed.connect(_on_connection_changed)
	Bluetooth.error_occurred.connect(_on_error_occurred)


func _log(message: String) -> void:
	log_output.append_text(message + "\n")


func _log_advanced(category: String, message: String) -> void:
	var stamp := Time.get_time_string_from_system()
	advanced_log.append_text("[%s] [%s] %s\n" % [stamp, category, message])
	call_deferred("_scroll_advanced_log_to_end")


func _scroll_advanced_log_to_end() -> void:
	var bar := advanced_scroll.get_v_scroll_bar()
	if bar:
		bar.value = bar.max_value


func _set_status(message: String) -> void:
	status_label.text = message


func _set_advanced_visible(visible: bool) -> void:
	_advanced_visible = visible
	advanced_panel.visible = visible
	toggle_advanced_button.text = "Hide Advanced Log" if visible else "Show Advanced Log"


func _on_toggle_advanced_pressed() -> void:
	_set_advanced_visible(not _advanced_visible)


func _on_device_selected(_index: int, _at_position: Vector2 = Vector2.ZERO) -> void:
	_update_device_action_button()


func _on_named_only_toggled(enabled: bool) -> void:
	_named_only_filter = enabled
	_refresh_device_list()
	var hidden_count := _device_cache.size() - _count_visible_devices()
	if enabled and hidden_count > 0:
		_log("Named-only filter enabled — %d unnamed device(s) hidden but still cached." % hidden_count)
	elif not enabled and hidden_count > 0:
		_log("Named-only filter disabled — showing all %d cached device(s)." % _device_cache.size())


func _device_address(device_info: Dictionary) -> String:
	return device_info.get("address", "")


func _normalized_address(address: String) -> String:
	return Bluetooth.normalize_address(address)


func _is_mac_address(address: String) -> bool:
	var parts := _normalized_address(address).split(":", false)
	return parts.size() == 6 and parts[0].length() == 2


func _addresses_match(a: String, b: String) -> bool:
	var na := _normalized_address(a)
	var nb := _normalized_address(b)
	if na == nb:
		return true
	if _is_mac_address(na) and (b.contains(na.replace(":", "")) or b.contains(na)):
		return true
	if _is_mac_address(nb) and (a.contains(nb.replace(":", "")) or a.contains(nb)):
		return true
	return false


func _cache_key_for(device_info: Dictionary) -> String:
	var address: String = device_info.get("address", "")
	if _is_mac_address(address):
		return _normalized_address(address)
	var device_id: String = device_info.get("device_id", "")
	if not device_id.is_empty():
		return device_id
	return address


func _has_friendly_name(device_info: Dictionary) -> bool:
	var name: String = device_info.get("name", "").strip_edges()
	if name.is_empty():
		return false
	if name.begins_with("Unknown Device"):
		return false
	if name == "HID Gamepad":
		return false
	return true


func _get_selected_address() -> String:
	var selected := device_list.get_selected_items()
	if selected.is_empty():
		return ""
	return device_list.get_item_metadata(selected[0])


func _get_device_state(address: String) -> Dictionary:
	if address.is_empty():
		return {"paired": false, "connected": false, "address": "", "name": ""}

	var info: Dictionary = {}
	for entry in _device_cache.values():
		var cached: Dictionary = entry.get("info", {})
		if _addresses_match(cached.get("address", ""), address) or _addresses_match(entry.get("info", {}).get("device_id", ""), address):
			info = cached
			break

	var resolved_address: String = info.get("address", address)
	var paired: bool = info.get("paired", false)
	var connected: bool = info.get("connected", false)

	if Bluetooth.call("is_paired", resolved_address):
		paired = true
	if Bluetooth.call("is_connected", resolved_address):
		connected = true

	return {
		"address": resolved_address,
		"name": _display_name_for(info) if not info.is_empty() else resolved_address,
		"paired": paired,
		"connected": connected,
	}


func _resolve_device_action(state: Dictionary) -> DeviceAction:
	if state.get("address", "").is_empty():
		return DeviceAction.NONE
	if state.get("connected", false):
		return DeviceAction.DISCONNECT
	if state.get("paired", false):
		return DeviceAction.CONNECT
	return DeviceAction.PAIR


func _action_label(action: DeviceAction) -> String:
	match action:
		DeviceAction.PAIR:
			return "Pair Device"
		DeviceAction.CONNECT:
			return "Connect"
		DeviceAction.DISCONNECT:
			return "Disconnect"
		_:
			return "Select a device"


func _update_device_action_button() -> void:
	var address := _get_selected_address()
	var state := _get_device_state(address)
	_current_device_action = _resolve_device_action(state)

	if _current_device_action == DeviceAction.NONE:
		device_action_button.text = "Select a device"
		device_action_button.disabled = true
		unpair_button.disabled = true
		return

	device_action_button.text = _action_label(_current_device_action)
	device_action_button.disabled = false
	unpair_button.disabled = not state.get("paired", false)


func _upsert_device_cache(device_info: Dictionary) -> void:
	var key := _cache_key_for(device_info)
	if key.is_empty():
		return

	var stale_keys: PackedStringArray = []
	for cache_key in _device_cache:
		if cache_key == key:
			continue
		var existing: Dictionary = _device_cache[cache_key].get("info", {})
		if not device_info.get("device_id", "").is_empty() and existing.get("device_id", "") == device_info.get("device_id", ""):
			stale_keys.append(cache_key)
		elif _addresses_match(existing.get("address", ""), device_info.get("address", "")):
			stale_keys.append(cache_key)
	for stale_key in stale_keys:
		_device_cache.erase(stale_key)

	if _device_cache.has(key):
		var entry: Dictionary = _device_cache[key]
		entry["info"] = device_info.duplicate(true)
		_device_cache[key] = entry
	else:
		_discovery_sequence += 1
		_device_cache[key] = {
			"info": device_info.duplicate(true),
			"discovered_order": _discovery_sequence,
		}


func _update_cached_state(address: String, state: Dictionary) -> void:
	for cache_key in _device_cache:
		var info: Dictionary = _device_cache[cache_key].get("info", {})
		if _addresses_match(info.get("address", ""), address) or _addresses_match(cache_key, address):
			for field in state:
				info[field] = state[field]
			_device_cache[cache_key]["info"] = info


func _sync_cache_from_bluetooth() -> void:
	for device_info in Bluetooth.get_discovered_devices():
		_upsert_device_cache(device_info)


func _get_sorted_cache_entries() -> Array:
	var entries: Array = []
	for cache_key in _device_cache:
		entries.append(_device_cache[cache_key])

	entries.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var a_info: Dictionary = a.get("info", {})
		var b_info: Dictionary = b.get("info", {})
		var a_named: bool = _has_friendly_name(a_info)
		var b_named: bool = _has_friendly_name(b_info)
		if a_named != b_named:
			return a_named
		return a.get("discovered_order", 0) < b.get("discovered_order", 0)
	)
	return entries


func _count_visible_devices() -> int:
	var count := 0
	for entry in _get_sorted_cache_entries():
		var device_info: Dictionary = entry.get("info", {})
		if _named_only_filter and not _has_friendly_name(device_info):
			continue
		count += 1
	return count


func _count_named_devices() -> int:
	var count := 0
	for entry in _device_cache.values():
		if _has_friendly_name(entry.get("info", {})):
			count += 1
	return count


func _update_devices_header(visible_count: int) -> void:
	var cached_count := _device_cache.size()
	var named_count := _count_named_devices()
	if _named_only_filter:
		devices_header.text = "Devices (%d shown, %d cached, %d named)" % [visible_count, cached_count, named_count]
	else:
		devices_header.text = "Devices (%d total, %d named)" % [cached_count, named_count]


func _display_name_for(device_info: Dictionary) -> String:
	var name: String = device_info.get("name", "").strip_edges()
	var address: String = _device_address(device_info)
	if name.is_empty():
		if address.contains(":"):
			return "Unknown Device (%s)" % address
		return "Unknown Device"
	return name


func _set_scan_ui_active(active: bool) -> void:
	_is_scanning = active
	scan_activity_label.visible = active
	scan_progress_bar.visible = active
	start_scan_button.disabled = active
	stop_scan_button.disabled = not active

	if active:
		_start_scan_animation()
	else:
		_stop_scan_animation()


func _start_scan_animation() -> void:
	_stop_scan_animation()
	scan_progress_bar.value = 0.0
	_scan_tween = create_tween().set_loops()
	_scan_tween.tween_property(scan_progress_bar, "value", 100.0, 0.9).set_ease(Tween.EASE_IN_OUT).set_trans(Tween.TRANS_SINE)
	_scan_tween.tween_property(scan_progress_bar, "value", 0.0, 0.9).set_ease(Tween.EASE_IN_OUT).set_trans(Tween.TRANS_SINE)


func _stop_scan_animation() -> void:
	if _scan_tween:
		_scan_tween.kill()
		_scan_tween = null
	scan_progress_bar.value = 0.0


func _refresh_device_list() -> void:
	var selected_address := _get_selected_address()
	device_list.clear()

	_sync_cache_from_bluetooth()

	var visible_count := 0
	for entry in _get_sorted_cache_entries():
		var device_info: Dictionary = entry.get("info", {})
		if _named_only_filter and not _has_friendly_name(device_info):
			continue

		var address: String = _device_address(device_info)
		var name: String = _display_name_for(device_info)
		var paired: bool = device_info.get("paired", false)
		var connected: bool = device_info.get("connected", false)
		var device_class: String = device_info.get("device_class", "unknown")

		var suffix := ""
		if not paired:
			suffix += " [Unpaired]"
		if paired:
			suffix += " [Paired]"
		if connected:
			suffix += " [Connected]"

		device_list.add_item("%s (%s) [%s]%s" % [name, address, device_class, suffix])
		var item_index := device_list.item_count - 1
		device_list.set_item_metadata(item_index, address)
		if address == selected_address:
			device_list.select(item_index)
		visible_count += 1

	_update_devices_header(visible_count)
	_update_device_action_button()


func _on_start_scan_pressed() -> void:
	_devices_found_this_scan = 0
	_scan_elapsed_sec = 0.0
	_dot_count = 0
	_dot_timer = 0.0
	_sync_cache_from_bluetooth()
	_refresh_device_list()
	_set_scan_ui_active(true)
	_set_status("Starting Bluetooth scan...")
	_log("Scan requested — known devices stay listed; new ones appear as they are found.")
	Bluetooth.start_scan()


func _on_stop_scan_pressed() -> void:
	_set_status("Stopping scan...")
	_log("Stop scan requested.")
	Bluetooth.stop_scan()


func _on_device_action_pressed() -> void:
	var address := _get_selected_address()
	if address.is_empty():
		_set_status("Select a device first.")
		_log_advanced("ERROR", "Device action blocked — no device selected.")
		return

	var state := _get_device_state(address)
	var action := _resolve_device_action(state)
	var label: String = state.get("name", address)

	match action:
		DeviceAction.PAIR:
			if state.get("paired", false):
				_log_advanced("ERROR", "Pair blocked — %s is already paired. Use Connect." % label)
				_set_status("%s is already paired." % label)
				_update_device_action_button()
				return
			_log_advanced("PAIR", "Attempting pair with %s (%s)" % [label, state.get("address", address)])
			_log("Pairing requested: %s" % label)
			Bluetooth.pair_device(state.get("address", address))

		DeviceAction.CONNECT:
			if not state.get("paired", false):
				_log_advanced("ERROR", "Connect blocked — %s is not paired. Pair first." % label)
				_set_status("Pair %s before connecting." % label)
				_update_device_action_button()
				return
			if state.get("connected", false):
				_log_advanced("ERROR", "Connect blocked — %s is already connected." % label)
				_set_status("%s is already connected." % label)
				_update_device_action_button()
				return
			_log_advanced("CONNECT", "Attempting connect to %s (%s)" % [label, state.get("address", address)])
			_log("Connect requested: %s" % label)
			Bluetooth.connect_device(state.get("address", address))

		DeviceAction.DISCONNECT:
			if not state.get("connected", false):
				_log_advanced("ERROR", "Disconnect blocked — %s is not connected." % label)
				_set_status("%s is not connected." % label)
				_update_device_action_button()
				return
			_log_advanced("DISCONNECT", "Attempting disconnect from %s (%s)" % [label, state.get("address", address)])
			_log("Disconnect requested: %s" % label)
			Bluetooth.disconnect_device(state.get("address", address))

		_:
			_log_advanced("ERROR", "Device action blocked — no valid action for selection.")
			_set_status("Select a device first.")


func _on_unpair_pressed() -> void:
	var address := _get_selected_address()
	if address.is_empty():
		_set_status("Select a device to unpair.")
		_log_advanced("ERROR", "Unpair blocked — no device selected.")
		return

	var state := _get_device_state(address)
	var label: String = state.get("name", address)

	if not state.get("paired", false):
		_log_advanced("ERROR", "Unpair blocked — %s is not paired." % label)
		_set_status("%s is not paired." % label)
		return

	if state.get("connected", false):
		_log_advanced("ERROR", "Unpair blocked — disconnect %s first." % label)
		_set_status("Disconnect %s before unpairing." % label)
		return

	_log_advanced("UNPAIR", "Attempting unpair of %s (%s)" % [label, state.get("address", address)])
	_log("Unpair requested: %s" % label)
	Bluetooth.unpair_device(state.get("address", address))


func _on_refresh_paired_pressed() -> void:
	for device_info in Bluetooth.get_paired_devices():
		_upsert_device_cache(device_info)
	_refresh_device_list()
	_set_status("Paired devices refreshed.")
	_log("Paired devices refreshed.")


func _on_device_found(device_info: Dictionary) -> void:
	var key := _cache_key_for(device_info)
	var is_new := not _device_cache.has(key)
	_upsert_device_cache(device_info)

	var address: String = _device_address(device_info)
	var name: String = _display_name_for(device_info)
	var device_class: String = device_info.get("device_class", "unknown")
	if is_new:
		_devices_found_this_scan += 1
	_log("Found: %s (%s) [%s]" % [name, address, device_class])
	_refresh_device_list()


func _on_scan_started() -> void:
	_set_scan_ui_active(true)
	_refresh_device_list()
	_set_status("Scanning — discovery active (%d device(s) listed)" % device_list.item_count)
	_log("Scan started. DeviceWatcher is running on the native backend.")


func _on_scan_stopped() -> void:
	_set_scan_ui_active(false)
	_refresh_device_list()
	_set_status("Scan stopped. Found %d device(s) this session." % _devices_found_this_scan)
	_log("Scan stopped. Total discovered this scan: %d" % _devices_found_this_scan)


func _on_pairing_started(address: String) -> void:
	_set_status("Pairing %s..." % address)
	_log("Pairing started: %s" % address)


func _on_pairing_succeeded(address: String) -> void:
	_set_status("Paired %s" % address)
	_log("Pairing succeeded: %s" % address)
	_update_cached_state(address, {"paired": true})
	_sync_cache_from_bluetooth()
	_refresh_device_list()


func _on_pairing_failed(address: String, error: String) -> void:
	_set_status("Pairing failed.")
	_log("Pairing failed for %s: %s" % [address, error])
	_log_advanced("ERROR", "Pairing failed for %s: %s" % [address, error])


func _on_connection_changed(address: String, connected: bool, message: String = "") -> void:
	var state := "connected" if connected else "disconnected"
	_set_status("%s is %s" % [address, state])
	_log("Connection changed: %s -> %s" % [address, state])
	if connected:
		_log_advanced("CONNECT", "Connected to %s" % address)
	else:
		_log_advanced("DISCONNECT", "Disconnected from %s" % address)
	if not message.is_empty():
		_log("  %s" % message)
		if message.to_lower().contains("error") or message.to_lower().contains("fail") or message.to_lower().contains("unable"):
			_log_advanced("ERROR", "Connection note for %s: %s" % [address, message])
	_update_cached_state(address, {"connected": connected})
	_sync_cache_from_bluetooth()
	_refresh_device_list()


func _on_error_occurred(operation: String, message: String) -> void:
	_set_status("Error during %s" % operation)
	_log("Error [%s]: %s" % [operation, message])
	_log_advanced("ERROR", "[%s] %s" % [operation, message])
	_update_device_action_button()
