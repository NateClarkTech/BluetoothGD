extends Control

@onready var _bluetooth: BluetoothManager = $Bluetooth
@onready var _toggle_scan_button: Button = %ToggleScanButton
@onready var _device_list: ItemList = %DeviceList
@onready var _pair_button: Button = %PairButton
@onready var _status_label: Label = %StatusLabel
@onready var _log_output: TextEdit = %LogOutput

var _devices: Dictionary = {}
var _selected_device_id: String = ""
var _is_scanning: bool = false
var _bluetooth_ready: bool = false


func _ready() -> void:
	_connect_bluetooth_signals()

	_toggle_scan_button.pressed.connect(_on_toggle_scan_pressed)
	_pair_button.pressed.connect(_on_pair_pressed)
	_device_list.item_selected.connect(_on_device_selected)

	_toggle_scan_button.disabled = true
	_set_status("Initializing Bluetooth...")
	_log("Platform: %s" % _bluetooth.get_platform_name())
	_log("Waiting for Bluetooth backend...")
	_update_pair_button()


func _connect_bluetooth_signals() -> void:
	_bluetooth.bluetooth_ready.connect(_on_bluetooth_ready)
	_bluetooth.scan_started.connect(_on_scan_started)
	_bluetooth.scan_stopped.connect(_on_scan_stopped)
	_bluetooth.device_found.connect(_on_device_found)
	_bluetooth.device_removed.connect(_on_device_removed)
	_bluetooth.devices_refreshed.connect(_on_devices_refreshed)
	_bluetooth.pairing_started.connect(_on_pairing_started)
	_bluetooth.pairing_succeeded.connect(_on_pairing_succeeded)
	_bluetooth.pairing_failed.connect(_on_pairing_failed)
	_bluetooth.pairing_pin_requested.connect(_on_pairing_pin_requested)
	_bluetooth.pairing_confirmation_requested.connect(_on_pairing_confirmation_requested)
	_bluetooth.error_occurred.connect(_on_error_occurred)


func _on_bluetooth_ready() -> void:
	_bluetooth_ready = true
	_toggle_scan_button.disabled = false
	_set_status("Ready")
	_log("Bluetooth ready. Available: %s, Radio on: %s" % [
		_bluetooth.is_bluetooth_available(),
		_bluetooth.is_radio_on(),
	])
	for device_info: Dictionary in _bluetooth.get_paired_devices():
		_register_device(device_info)
	_refresh_device_list()


func _on_toggle_scan_pressed() -> void:
	if _is_scanning:
		_log("Stop scan requested.")
		_set_status("Stopping scan...")
		_bluetooth.stop_scan()
		return

	_log("Start scan requested.")
	_set_status("Starting scan...")
	_bluetooth.start_scan({"named_only": false, "gamepads_only": false})


func _on_scan_started() -> void:
	_is_scanning = true
	_toggle_scan_button.text = "Stop Scan"
	_set_status("Scanning for devices...")
	print("Bluetooth scan started")
	_log("Bluetooth scan started")


func _on_scan_stopped() -> void:
	_is_scanning = false
	_toggle_scan_button.text = "Start Scan"
	_set_status("Scan stopped. %d device(s) in list." % _devices.size())
	print("Bluetooth scan stopped")
	_log("Bluetooth scan stopped")


func _on_device_found(device_info: Dictionary) -> void:
	_register_device(device_info)
	_refresh_device_list()
	var name: String = device_info.get("name", "Unknown")
	if name.is_empty():
		name = "Unknown"
	_log("Found: %s (%s)" % [name, device_info.get("address", "no address")])


func _on_devices_refreshed() -> void:
	for device_info: Dictionary in _bluetooth.get_paired_devices():
		_register_device(device_info)
	_refresh_device_list()
	_update_pair_button()


func _on_device_removed(address: String) -> void:
	var keys_to_remove: Array[String] = []
	for key: String in _devices:
		var info: Dictionary = _devices[key]
		if key == address or info.get("address", "") == address or info.get("device_id", "") == address:
			keys_to_remove.append(key)

	for key: String in keys_to_remove:
		_devices.erase(key)
		if _selected_device_id == key:
			_selected_device_id = ""

	_refresh_device_list()
	_update_pair_button()
	_log("Device removed: %s" % address)


func _on_device_selected(index: int) -> void:
	_selected_device_id = _device_list.get_item_metadata(index)
	_update_pair_button()


func _on_pair_pressed() -> void:
	if _selected_device_id.is_empty() or not _devices.has(_selected_device_id):
		return

	var info: Dictionary = _devices[_selected_device_id]
	if info.get("paired", false):
		var address: String = info.get("address", _selected_device_id)
		_log("Attempting to unpair: %s" % address)
		_set_status("Unpairing %s..." % info.get("name", address))
		_bluetooth.unpair_device(address)
	else:
		var address: String = info.get("address", "")
		if not address.is_empty() and _bluetooth.is_valid_bluetooth_address(address):
			_log("Attempting to pair: %s" % address)
			_set_status("Pairing %s..." % info.get("name", address))
			_bluetooth.pair_device(address)
		else:
			var device_id: String = info.get("device_id", _selected_device_id)
			_log("Attempting to pair by id: %s" % device_id)
			_set_status("Pairing %s..." % info.get("name", device_id))
			_bluetooth.pair_device_by_id(device_id)


func _on_pairing_started(address: String) -> void:
	_log("Pairing started: %s" % address)


func _on_pairing_succeeded(address: String) -> void:
	_log("Pairing succeeded: %s" % address)
	_set_status("Paired %s" % address)
	_bluetooth.refresh_paired_devices()


func _on_pairing_failed(address: String, error: String, error_code: int) -> void:
	_log("Pairing failed for %s: %s (%s)" % [
		address, error, _bluetooth.get_error_code_name(error_code)
	])
	_set_status("Pairing failed")


func _on_pairing_pin_requested(address: String) -> void:
	_log("PIN requested for %s — confirming with 0000" % address)
	_bluetooth.confirm_pairing("0000")


func _on_pairing_confirmation_requested(address: String, kind: String) -> void:
	_log("Pairing confirmation requested for %s (%s) — accepting" % [address, kind])
	_bluetooth.confirm_pairing()


func _on_error_occurred(operation: String, message: String, error_code: int) -> void:
	_log("Error [%s]: %s (%s)" % [
		operation, message, _bluetooth.get_error_code_name(error_code)
	])
	_set_status("Error during %s" % operation)
	if operation == "start_scan":
		_is_scanning = false
		_toggle_scan_button.text = "Start Scan"


func _register_device(device_info: Dictionary) -> void:
	var key := _device_key(device_info)
	if key.is_empty():
		return
	_devices[key] = device_info


func _device_key(device_info: Dictionary) -> String:
	var address: String = device_info.get("address", "")
	if _bluetooth.is_valid_bluetooth_address(address):
		return _bluetooth.normalize_address(address)
	return device_info.get("device_id", address)


func _format_device_label(device_info: Dictionary) -> String:
	var name: String = device_info.get("name", "")
	if name.is_empty():
		name = "Unknown"

	var flags: PackedStringArray = []
	if device_info.get("paired", false):
		flags.append("paired")
	if device_info.get("connected", false):
		flags.append("connected")
	if device_info.get("device_class", "") == "gamepad":
		flags.append("gamepad")

	var flag_text := ""
	if not flags.is_empty():
		flag_text = " [%s]" % ", ".join(flags)

	var address: String = device_info.get("address", "")
	if not address.is_empty():
		return "%s (%s)%s" % [name, address, flag_text]
	return "%s%s" % [name, flag_text]


func _refresh_device_list() -> void:
	var selected_key := _selected_device_id
	_device_list.clear()

	var keys: Array = _devices.keys()
	keys.sort()
	for key: String in keys:
		var index := _device_list.add_item(_format_device_label(_devices[key]))
		_device_list.set_item_metadata(index, key)
		if key == selected_key:
			_device_list.select(index)


func _update_pair_button() -> void:
	if _selected_device_id.is_empty() or not _devices.has(_selected_device_id):
		_pair_button.disabled = true
		_pair_button.text = "Pair / Unpair Selected Device"
		return

	_pair_button.disabled = false
	var info: Dictionary = _devices[_selected_device_id]
	if info.get("paired", false):
		_pair_button.text = "Unpair Selected Device"
	else:
		_pair_button.text = "Pair Selected Device"


func _set_status(message: String) -> void:
	_status_label.text = message


func _log(message: String) -> void:
	print(message)
	_log_output.text += message + "\n"
	var line_count := _log_output.get_line_count()
	if line_count > 0:
		_log_output.set_caret_line(line_count - 1)