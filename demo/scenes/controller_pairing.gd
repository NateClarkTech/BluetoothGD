extends Control

const DeviceListPresenterScript := preload("res://scripts/device_list_presenter.gd")

enum DeviceAction { NONE, PAIR, CONNECT, DISCONNECT, UNPAIR }

const MAX_LOG_LINES := 500

@onready var device_list: Tree = %DeviceList
@onready var status_label: Label = %StatusLabel
@onready var log_output: TextEdit = %LogOutput
@onready var advanced_log: TextEdit = %AdvancedLog
@onready var scan_activity_label: Label = %ScanActivityLabel
@onready var scan_progress_bar: ProgressBar = %ScanProgressBar
@onready var scan_button: Button = %ScanButton
@onready var toggle_advanced_button: Button = %ToggleAdvancedButton
@onready var advanced_panel: PanelContainer = %AdvancedPanel
@onready var devices_header: Label = %DevicesHeader
@onready var named_only_checkbox: CheckButton = %NamedOnlyCheckButton
@onready var device_action_button: Button = %DeviceActionButton
@onready var unpair_button: Button = %UnpairButton

var _is_scanning: bool = false
var _advanced_visible: bool = false
var _scan_elapsed_sec: float = 0.0
var _devices_found_this_scan: int = 0
var _scan_tween: Tween = null
var _dot_timer: float = 0.0
var _dot_count: int = 0
var _bluetooth_ready: bool = false
var _init_wait_sec: float = 0.0
var _scan_start_wait_sec: float = 0.0
var _scan_start_pending: bool = false
const INIT_READY_TIMEOUT_SEC := 8.0
const SCAN_START_TIMEOUT_SEC := 6.0
var _pending_operation: DeviceAction = DeviceAction.NONE
var _pending_address: String = ""
var _presenter: Node


func _ready() -> void:
	_setup_device_list()
	_setup_presenter()
	_connect_signals()
	_set_scan_ui_active(false)
	_set_advanced_visible(false)
	_set_status("Initializing Bluetooth...")
	_log("Bluetooth platform: %s" % Bluetooth.get_platform_name())
	_log("Waiting for Bluetooth backend to initialize...")
	_log_advanced_system_info()
	_update_device_action_button()
	call_deferred("_check_bluetooth_startup_state")


func _process(delta: float) -> void:
	if not _bluetooth_ready:
		_init_wait_sec += delta
		if _init_wait_sec >= INIT_READY_TIMEOUT_SEC:
			_report_bluetooth_init_stalled()

	if _scan_start_pending and not _is_scanning:
		_scan_start_wait_sec += delta
		if _scan_start_wait_sec >= SCAN_START_TIMEOUT_SEC:
			_report_scan_start_stalled()

	if not _is_scanning:
		return

	_scan_elapsed_sec += delta
	_dot_timer += delta
	if _dot_timer >= 0.4:
		_dot_timer = 0.0
		_dot_count = (_dot_count + 1) % 4
		var dots := ".".repeat(_dot_count)
		scan_activity_label.text = "Scanning for nearby controllers%s  (%.0fs elapsed)" % [dots, _scan_elapsed_sec]


func _setup_presenter() -> void:
	_presenter = DeviceListPresenterScript.new()
	_presenter.name = "DeviceListPresenter"
	add_child(_presenter)
	_presenter.setup(device_list)
	_presenter.named_only_filter = named_only_checkbox.button_pressed
	_presenter.selection_changed.connect(_on_presenter_selection_changed)
	_presenter.counts_changed.connect(_on_presenter_counts_changed)


func _connect_signals() -> void:
	Bluetooth.bluetooth_ready.connect(_on_bluetooth_ready)
	Bluetooth.devices_refreshed.connect(_on_devices_refreshed)
	Bluetooth.device_found.connect(_on_device_found)
	Bluetooth.device_removed.connect(_on_device_removed)
	Bluetooth.scan_started.connect(_on_scan_started)
	Bluetooth.scan_stopped.connect(_on_scan_stopped)
	Bluetooth.pairing_started.connect(_on_pairing_started)
	Bluetooth.pairing_succeeded.connect(_on_pairing_succeeded)
	Bluetooth.pairing_failed.connect(_on_pairing_failed)
	Bluetooth.connection_changed.connect(_on_connection_changed)
	Bluetooth.error_occurred.connect(_on_error_occurred)


func _on_bluetooth_ready() -> void:
	_bluetooth_ready = true
	_init_wait_sec = 0.0
	_presenter.rebuild_from_bluetooth()
	_set_scan_ui_active(_is_scanning)
	_set_status("Ready")
	_log("Bluetooth backend ready.")
	_log("Bluetooth available: %s" % str(Bluetooth.is_bluetooth_available()))
	_log_advanced_bluetooth_state()


func _check_bluetooth_startup_state() -> void:
	if _bluetooth_ready:
		return
	if not Bluetooth.is_bluetooth_available():
		_set_status("Bluetooth unavailable")
		_log("Scan disabled: Bluetooth backend failed to start. Check Advanced Log for WinRT or driver errors.")
		_log_advanced("ERROR", "Bluetooth.is_bluetooth_available() returned false during startup.")
		return
	_log("Bluetooth worker started; loading paired devices in the background...")


func _report_bluetooth_init_stalled() -> void:
	if _bluetooth_ready:
		return
	_init_wait_sec = 0.0
	_set_status("Bluetooth init stalled")
	var reason := "Scan disabled: Bluetooth backend did not become ready within %.0f seconds." % INIT_READY_TIMEOUT_SEC
	if not Bluetooth.is_bluetooth_available():
		reason += " Backend reported unavailable (initialize may have failed)."
	else:
		reason += " Paired-device refresh may still be running; try Refresh Paired or restart the demo."
	_log(reason)
	_log_advanced("ERROR", reason)


func _on_devices_refreshed() -> void:
	_presenter.rebuild_from_bluetooth()
	var paired_count: int = Bluetooth.get_paired_devices().size()
	var discovered_count: int = Bluetooth.get_discovered_devices().size()
	_log("Device list updated (%d paired, %d discovered)." % [paired_count, discovered_count])
	if _bluetooth_ready:
		_update_device_action_button()


func _on_presenter_selection_changed(_address: String) -> void:
	_update_device_action_button()


func _on_presenter_counts_changed(visible_count: int, total_count: int, named_count: int) -> void:
	if _presenter.named_only_filter:
		devices_header.text = "Devices (%d shown, %d cached, %d named)" % [visible_count, total_count, named_count]
	else:
		devices_header.text = "Devices (%d total, %d named)" % [total_count, named_count]


func _log(message: String) -> void:
	log_output.text += message + "\n"
	_trim_log(log_output)
	_scroll_text_edit_to_end(log_output)


func _log_advanced(category: String, message: String) -> void:
	var stamp := Time.get_time_string_from_system()
	advanced_log.text += "[%s] [%s] %s\n" % [stamp, category, message]
	_trim_log(advanced_log)
	call_deferred("_scroll_text_edit_to_end", advanced_log)


func _log_advanced_system_info() -> void:
	var version_info: Dictionary = Engine.get_version_info()
	var godot_version := "%s.%s.%s%s" % [
		version_info.get("major", 0),
		version_info.get("minor", 0),
		version_info.get("patch", 0),
		str(version_info.get("status", "")),
	]
	_log_advanced("INFO", "Advanced log — technical diagnostics for pairing and connection events.")
	_log_advanced("SYS", "OS: %s %s (%s)" % [OS.get_name(), OS.get_version(), OS.get_version_alias()])
	_log_advanced("SYS", "Godot: %s (%s)" % [godot_version, str(version_info.get("build", "unknown"))])
	_log_advanced("SYS", "Locale: %s | Display: %s" % [OS.get_locale(), DisplayServer.screen_get_size(DisplayServer.get_primary_screen())])
	_log_advanced("SYS", "Bluetooth platform: %s" % Bluetooth.get_platform_name())


func _log_advanced_bluetooth_state() -> void:
	var caps: Dictionary = Bluetooth.get_capabilities()
	_log_advanced("BT", "Backend available: %s" % str(Bluetooth.is_bluetooth_available()))
	_log_advanced("BT", "Radio on: %s" % str(Bluetooth.is_radio_on()))
	_log_advanced("BT", "Can unpair while connected: %s" % str(Bluetooth.can_unpair_while_connected()))
	for key in caps.keys():
		_log_advanced("BT", "Capability %s = %s" % [str(key), str(caps[key])])


func _addresses_match(a: String, b: String) -> bool:
	if a.is_empty() or b.is_empty():
		return false
	if a == b:
		return true
	if Bluetooth.is_valid_bluetooth_address(a) and Bluetooth.is_valid_bluetooth_address(b):
		return Bluetooth.normalize_address(a) == Bluetooth.normalize_address(b)
	return a in b or b in a


func _is_pending_for_address(address: String) -> bool:
	return _pending_operation != DeviceAction.NONE and _addresses_match(_pending_address, address)


func _pending_operation_name() -> String:
	match _pending_operation:
		DeviceAction.PAIR:
			return "pair_device"
		DeviceAction.CONNECT:
			return "connect_device"
		DeviceAction.DISCONNECT:
			return "disconnect_device"
		DeviceAction.UNPAIR:
			return "unpair_device"
		_:
			return ""


func _trim_log(text_edit: TextEdit) -> void:
	var line_count := text_edit.get_line_count()
	if line_count <= MAX_LOG_LINES:
		return
	var lines := text_edit.text.split("\n")
	if lines.size() <= MAX_LOG_LINES:
		return
	text_edit.text = "\n".join(lines.slice(lines.size() - MAX_LOG_LINES))


func _scroll_text_edit_to_end(text_edit: TextEdit) -> void:
	var line_count := text_edit.get_line_count()
	if line_count > 0:
		text_edit.set_caret_line(line_count - 1)
	var scroll_bar := text_edit.get_v_scroll_bar()
	if scroll_bar:
		scroll_bar.value = scroll_bar.max_value


func _set_status(message: String) -> void:
	status_label.text = message


func _set_advanced_visible(visible: bool) -> void:
	_advanced_visible = visible
	advanced_panel.visible = visible
	toggle_advanced_button.text = "Hide Advanced Log" if visible else "Show Advanced Log"


func _on_toggle_advanced_pressed() -> void:
	_set_advanced_visible(not _advanced_visible)


func _setup_device_list() -> void:
	device_list.columns = 5
	device_list.column_titles_visible = true
	device_list.hide_root = true
	device_list.set_column_title(0, "Name")
	device_list.set_column_title(1, "Address")
	device_list.set_column_title(2, "Type")
	device_list.set_column_title(3, "Paired")
	device_list.set_column_title(4, "Connected")
	device_list.set_column_expand(0, true)
	device_list.set_column_expand(1, false)
	device_list.set_column_expand(2, false)
	device_list.set_column_expand(3, false)
	device_list.set_column_expand(4, false)
	device_list.set_column_custom_minimum_width(1, 170)
	device_list.set_column_custom_minimum_width(2, 72)
	device_list.set_column_custom_minimum_width(3, 72)
	device_list.set_column_custom_minimum_width(4, 88)


func _on_named_only_toggled(enabled: bool) -> void:
	_presenter.set_named_only_filter(enabled)
	var hidden_count: int = _presenter.get_cached_count() - _presenter.get_visible_count()
	if enabled and hidden_count > 0:
		_log("Named-only filter enabled — %d unnamed device(s) hidden." % hidden_count)
	elif not enabled and hidden_count > 0:
		_log("Named-only filter disabled — showing all cached devices.")


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


func _set_pending_operation(action: DeviceAction, address: String) -> void:
	_pending_operation = action
	_pending_address = address
	_update_action_buttons_enabled()


func _clear_pending_operation() -> void:
	_pending_operation = DeviceAction.NONE
	_pending_address = ""
	_update_action_buttons_enabled()


func _update_action_buttons_enabled() -> void:
	var busy := _pending_operation != DeviceAction.NONE
	device_action_button.disabled = busy or _resolve_device_action(_presenter.get_device_state(_presenter.get_selected_address())) == DeviceAction.NONE
	unpair_button.disabled = busy or not _presenter.get_device_state(_presenter.get_selected_address()).get("paired", false)


func _update_device_action_button() -> void:
	var address: String = _presenter.get_selected_address()
	var state: Dictionary = _presenter.get_device_state(address)
	var action: DeviceAction = _resolve_device_action(state)

	if action == DeviceAction.NONE:
		device_action_button.text = "Select a device"
	else:
		device_action_button.text = _action_label(action)
	_update_action_buttons_enabled()


func _punch_ui(button: Button) -> void:
	var tw := create_tween()
	tw.tween_property(button, "scale", Vector2(0.92, 0.92), 0.05)
	tw.tween_property(button, "scale", Vector2.ONE, 0.08).set_ease(Tween.EASE_OUT).set_trans(Tween.TRANS_BACK)


func _set_scan_ui_active(active: bool) -> void:
	_is_scanning = active
	_presenter.set_scanning(active)
	scan_activity_label.visible = active
	scan_progress_bar.visible = active
	scan_button.text = "Stop Scan" if active else "Start Scan"
	scan_button.disabled = not _bluetooth_ready

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


func _on_scan_button_pressed() -> void:
	_punch_ui(scan_button)
	if _is_scanning:
		_set_status("Stopping scan...")
		_log("Stop scan requested.")
		Bluetooth.stop_scan()
		return

	_devices_found_this_scan = 0
	_scan_elapsed_sec = 0.0
	_dot_count = 0
	_dot_timer = 0.0
	_set_status("Starting Bluetooth scan...")
	_log("Scan requested — known devices stay listed; new ones appear as they are found.")
	if _presenter.named_only_filter:
		_log("Note: 'Named devices only' is ON — unnamed nearby devices are logged but hidden from the list.")
	_scan_start_pending = true
	_scan_start_wait_sec = 0.0
	Bluetooth.start_scan()


func _on_device_action_pressed() -> void:
	_punch_ui(device_action_button)
	var address: String = _presenter.get_selected_address()
	if address.is_empty():
		_set_status("Select a device first.")
		_log_advanced("ERROR", "Device action blocked — no device selected.")
		return

	var state: Dictionary = _presenter.get_device_state(address)
	var action: DeviceAction = _resolve_device_action(state)
	var label: String = state.get("name", address)

	match action:
		DeviceAction.PAIR:
			if state.get("paired", false):
				_log_advanced("ERROR", "Pair blocked — %s is already paired. Use Connect." % label)
				_set_status("%s is already paired." % label)
				_update_device_action_button()
				return
			_set_pending_operation(DeviceAction.PAIR, state.get("address", address))
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
			_set_pending_operation(DeviceAction.CONNECT, state.get("address", address))
			_log_advanced("CONNECT", "Attempting connect to %s (%s)" % [label, state.get("address", address)])
			_log("Connect requested: %s" % label)
			Bluetooth.connect_device(state.get("address", address))

		DeviceAction.DISCONNECT:
			if not state.get("connected", false):
				_log_advanced("ERROR", "Disconnect blocked — %s is not connected." % label)
				_set_status("%s is not connected." % label)
				_update_device_action_button()
				return
			_set_pending_operation(DeviceAction.DISCONNECT, state.get("address", address))
			_log_advanced("DISCONNECT", "Attempting disconnect from %s (%s)" % [label, state.get("address", address)])
			_log("Disconnect requested: %s" % label)
			Bluetooth.disconnect_device(state.get("address", address))

		_:
			_log_advanced("ERROR", "Device action blocked — no valid action for selection.")
			_set_status("Select a device first.")


func _on_unpair_pressed() -> void:
	_punch_ui(unpair_button)
	var address: String = _presenter.get_selected_address()
	if address.is_empty():
		_set_status("Select a device to unpair.")
		_log_advanced("ERROR", "Unpair blocked — no device selected.")
		return

	var state: Dictionary = _presenter.get_device_state(address)
	var label: String = state.get("name", address)

	if not state.get("paired", false):
		_log_advanced("ERROR", "Unpair blocked — %s is not paired." % label)
		_set_status("%s is not paired." % label)
		return

	if state.get("connected", false):
		_set_status("Disconnecting %s before unpair..." % label)
		_log_advanced("UNPAIR", "Device connected — disconnecting before unpair: %s" % label)
	else:
		_set_status("Unpairing %s..." % label)

	_set_pending_operation(DeviceAction.UNPAIR, state.get("address", address))
	_log_advanced("UNPAIR", "Attempting unpair of %s (%s)" % [label, state.get("address", address)])
	_log("Unpair requested: %s" % label)
	Bluetooth.unpair_device(state.get("address", address))


func _on_refresh_paired_pressed() -> void:
	_set_status("Refreshing paired devices...")
	_log("Paired devices refresh requested.")
	Bluetooth.refresh_paired_devices()


func _on_device_found(device_info: Dictionary) -> void:
	var before_count: int = _presenter.get_cached_count()
	var hidden_by_filter: bool = _presenter.handle_device_found(device_info)
	if _is_scanning and _presenter.get_cached_count() > before_count:
		_devices_found_this_scan += 1
	var address: String = device_info.get("address", "")
	var name: String = device_info.get("name", address)
	if name.is_empty():
		name = "Unnamed Device"
	var device_class: String = device_info.get("device_class", "unknown")
	var paired: bool = device_info.get("paired", false)
	var connected: bool = device_info.get("connected", false)
	_log("Found: %s (%s) [%s] paired=%s connected=%s" % [
		name, address, device_class, str(paired), str(connected)
	])
	if hidden_by_filter:
		_log("Hidden by filter: turn off 'Named devices only' to show this device in the list.")


func _on_device_removed(address: String) -> void:
	var label: String = _presenter.get_removal_log_label(address)
	var was_unpair_pending := _pending_operation == DeviceAction.UNPAIR and _addresses_match(_pending_address, address)
	_presenter.handle_device_removed(address)
	_log("Device removed: %s" % label)
	if was_unpair_pending:
		_log_advanced("UNPAIR", "Unpair succeeded: %s (%s)" % [label, address])
		_clear_pending_operation()
	_update_device_action_button()


func _on_scan_started() -> void:
	_scan_start_pending = false
	_scan_start_wait_sec = 0.0
	_set_scan_ui_active(true)
	_set_status("Scanning — discovery active")
	_log("Scan started. DeviceWatcher is running on the native backend.")


func _on_scan_stopped() -> void:
	_scan_start_pending = false
	_scan_start_wait_sec = 0.0
	_set_scan_ui_active(false)
	_set_status("Scan stopped. Found %d device(s) this session." % _devices_found_this_scan)
	_log("Scan stopped. Total discovered this scan: %d" % _devices_found_this_scan)


func _report_scan_start_stalled() -> void:
	if not _scan_start_pending or _is_scanning:
		return
	_scan_start_pending = false
	_scan_start_wait_sec = 0.0
	_set_status("Scan start delayed")
	_log("Scan did not start within %.0f seconds. The Bluetooth worker may still be refreshing paired devices — try again shortly." % SCAN_START_TIMEOUT_SEC)
	_log_advanced("ERROR", "scan_started signal was not received within %.0f seconds of start_scan()." % SCAN_START_TIMEOUT_SEC)


func _on_pairing_started(address: String) -> void:
	_set_status("Pairing %s..." % address)
	_log("Pairing started: %s" % address)


func _on_pairing_succeeded(address: String) -> void:
	_set_status("Paired %s" % address)
	_log("Pairing succeeded: %s" % address)
	if _pending_operation == DeviceAction.PAIR and _addresses_match(_pending_address, address):
		_log_advanced("PAIR", "Pairing succeeded: %s" % address)
	_presenter.handle_pairing_succeeded(address)
	_clear_pending_operation()
	_update_device_action_button()


func _on_pairing_failed(address: String, error: String) -> void:
	_set_status("Pairing failed.")
	_log("Pairing failed for %s: %s" % [address, error])
	if _pending_operation == DeviceAction.PAIR and _addresses_match(_pending_address, address):
		_log_advanced("ERROR", "Pairing failed for %s: %s" % [address, error])
	_clear_pending_operation()
	_update_device_action_button()


func _on_connection_changed(address: String, connected: bool, message: String = "") -> void:
	var state := "connected" if connected else "disconnected"
	_set_status("%s is %s" % [address, state])
	_log("Connection changed: %s -> %s" % [address, state])

	var device_state: Dictionary = _presenter.get_device_state(address)
	var is_paired: bool = device_state.get("paired", false)
	var is_pending: bool = _is_pending_for_address(address)
	var is_paired_or_pairing: bool = is_paired or (is_pending and _pending_operation == DeviceAction.PAIR)

	if connected:
		if is_pending and _pending_operation == DeviceAction.CONNECT:
			_log_advanced("CONNECT", "Connect succeeded: %s" % address)
		elif is_paired_or_pairing:
			_log_advanced("CONNECT", "Paired device connected: %s" % address)
	else:
		if is_pending and _pending_operation == DeviceAction.DISCONNECT:
			_log_advanced("DISCONNECT", "Disconnect succeeded: %s" % address)
		elif is_paired and _pending_operation != DeviceAction.UNPAIR:
			_log_advanced("DISCONNECT", "Paired device disconnected: %s" % address)

	if not message.is_empty():
		_log("  %s" % message)
		var message_lower := message.to_lower()
		var looks_like_error := message_lower.contains("error") or message_lower.contains("fail") or message_lower.contains("unable")
		if is_pending and looks_like_error:
			_log_advanced("ERROR", "Connection note for %s: %s" % [address, message])

	_presenter.handle_connection_changed(address, connected)
	if is_pending and _pending_operation in [DeviceAction.CONNECT, DeviceAction.DISCONNECT]:
		_clear_pending_operation()
	_update_device_action_button()


func _on_error_occurred(operation: String, message: String, _error_code: int = 0) -> void:
	_set_status("Error during %s" % operation)
	_log("Error [%s]: %s" % [operation, message])
	if operation == "initialize":
		_log("Scan disabled: Bluetooth initialize failed. See message above.")
		_set_scan_ui_active(false)
	if operation == "start_scan":
		_scan_start_pending = false
		_set_scan_ui_active(false)
		_log("Scan aborted due to backend error. See message above.")
	if _pending_operation != DeviceAction.NONE and operation == _pending_operation_name():
		_log_advanced("ERROR", "[%s] %s" % [operation, message])
	_clear_pending_operation()
	_update_device_action_button()