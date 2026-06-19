class_name DeviceListPresenter
extends Node

signal selection_changed(address: String)
signal counts_changed(visible_count: int, total_count: int, named_count: int)

const RESORT_INTERVAL_SEC := 0.15
const FLASH_TEXT_COLOR := Color(0.4, 0.95, 1.0, 1.0)
const CONNECTED_TEXT_COLOR := Color(0.75, 0.88, 1.0, 1.0)

var named_only_filter: bool = true

var _device_list: Tree
var _tree_items: Dictionary = {}
var _item_states: Dictionary = {}
var _discovery_order: Dictionary = {}
var _discovery_sequence: int = 0
var _resort_timer: float = 0.0
var _resort_pending: bool = false
var _is_scanning: bool = false
var _selected_address: String = ""


func setup(device_list: Tree) -> void:
	_device_list = device_list
	_device_list.item_selected.connect(_on_item_selected)


func set_scanning(active: bool) -> void:
	_is_scanning = active
	if not active:
		_resort_pending = false
		_resort_timer = 0.0


func set_named_only_filter(enabled: bool) -> void:
	named_only_filter = enabled
	_rebuild_all()


func get_cached_count() -> int:
	return _tree_items.size()


func get_visible_count() -> int:
	return _count_visible_items()


func _process(delta: float) -> void:
	if not _resort_pending:
		return
	_resort_timer += delta
	if _resort_timer >= RESORT_INTERVAL_SEC:
		_resort_pending = false
		_resort_timer = 0.0
		_resort_items()


func rebuild_from_bluetooth() -> void:
	_rebuild_all()


func handle_device_found(device_info: Dictionary) -> void:
	var key := _cache_key_for(device_info)
	if key.is_empty():
		return

	if not _discovery_order.has(key):
		_discovery_sequence += 1
		_discovery_order[key] = _discovery_sequence

	_item_states[key] = {
		"paired": device_info.get("paired", false),
		"connected": device_info.get("connected", false),
	}

	var old_rank := -1
	if _tree_items.has(key):
		old_rank = _device_sort_rank(_item_states[key])

	_upsert_tree_item(key, device_info)

	var new_rank := _device_sort_rank(_item_states[key])
	if old_rank >= 0 and old_rank != new_rank:
		_schedule_resort()
	elif not _is_scanning:
		_resort_items()
	else:
		_schedule_resort()

	_update_counts()


func get_removal_log_label(address: String) -> String:
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var item_address := str(item.get_metadata(0))
		if not _addresses_match(item_address, address) and not _addresses_match(key, address):
			continue
		var name: String = item.get_text(0)
		if not name.is_empty() and not name.begins_with("Unnamed"):
			if not item_address.is_empty():
				return "%s (%s)" % [name, item_address]
			return name
		if not item_address.is_empty():
			return item_address
	return address


func handle_device_removed(address: String) -> void:
	var keys_to_remove: PackedStringArray = []
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var item_address := str(item.get_metadata(0))
		if _addresses_match(item_address, address) or _addresses_match(key, address):
			keys_to_remove.append(key)

	for key in keys_to_remove:
		var item: TreeItem = _tree_items[key]
		item.free()
		_tree_items.erase(key)
		_item_states.erase(key)
		_discovery_order.erase(key)

	if _selected_address.is_empty() or _addresses_match(_selected_address, address):
		_selected_address = _get_selected_address()

	_update_counts()
	selection_changed.emit(_selected_address)


func handle_connection_changed(address: String, connected: bool) -> void:
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var item_address := str(item.get_metadata(0))
		if not _addresses_match(item_address, address) and not _addresses_match(key, address):
			continue
		var state: Dictionary = _item_states.get(key, {})
		var old_rank := _device_sort_rank(state)
		state["connected"] = connected
		_item_states[key] = state
		item.set_text(4, _connected_status_text(connected))
		_flash_row(item, key)
		if old_rank != _device_sort_rank(state):
			_schedule_resort()
		break
	_update_counts()


func handle_pairing_succeeded(address: String) -> void:
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var item_address := str(item.get_metadata(0))
		if not _addresses_match(item_address, address) and not _addresses_match(key, address):
			continue
		var state: Dictionary = _item_states.get(key, {})
		var old_rank := _device_sort_rank(state)
		state["paired"] = true
		_item_states[key] = state
		item.set_text(3, _paired_status_text(true))
		if old_rank != _device_sort_rank(state):
			_schedule_resort()
		break
	_update_counts()


func get_selected_address() -> String:
	return _get_selected_address()


func get_device_state(address: String) -> Dictionary:
	if address.is_empty():
		return {"paired": false, "connected": false, "address": "", "name": ""}

	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var item_address := str(item.get_metadata(0))
		if _addresses_match(item_address, address) or _addresses_match(key, address):
			var state: Dictionary = _item_states.get(key, {})
			return {
				"address": item_address,
				"name": item.get_text(0),
				"paired": state.get("paired", false),
				"connected": state.get("connected", false),
			}

	return {"paired": false, "connected": false, "address": address, "name": address}


func _on_item_selected() -> void:
	_refresh_all_row_colors()
	_selected_address = _get_selected_address()
	selection_changed.emit(_selected_address)


func _refresh_all_row_colors() -> void:
	for key in _tree_items:
		var state: Dictionary = _item_states.get(key, {})
		_apply_row_text_color(_tree_items[key], state.get("connected", false))


func _get_selected_address() -> String:
	var selected: TreeItem = _device_list.get_selected()
	if selected == null:
		return ""
	var metadata: Variant = selected.get_metadata(0)
	if metadata != null and str(metadata) != "":
		return str(metadata)
	return selected.get_text(1)


func _rebuild_all() -> void:
	_selected_address = _get_selected_address()
	for key in _tree_items:
		_tree_items[key].free()
	_tree_items.clear()
	_item_states.clear()
	_device_list.clear()
	var root := _device_list.create_item()

	for device_info in _merged_devices():
		var key := _cache_key_for(device_info)
		if key.is_empty():
			continue
		if not _discovery_order.has(key):
			_discovery_sequence += 1
			_discovery_order[key] = _discovery_sequence
		_item_states[key] = {
			"paired": device_info.get("paired", false),
			"connected": device_info.get("connected", false),
		}
		if named_only_filter and not _has_friendly_name(device_info):
			continue
		var item := _create_tree_item(root, key, device_info)
		_tree_items[key] = item
		if _addresses_match(_device_address(device_info), _selected_address):
			item.select(0)

	_resort_items()
	_update_counts()


func _merged_devices() -> Array:
	var merged: Dictionary = {}
	for device_info in Bluetooth.get_discovered_devices():
		var key := _cache_key_for(device_info)
		if not key.is_empty():
			merged[key] = device_info
	for device_info in Bluetooth.get_paired_devices():
		var key := _cache_key_for(device_info)
		if not key.is_empty():
			merged[key] = device_info
	return merged.values()


func _upsert_tree_item(key: String, device_info: Dictionary) -> void:
	if named_only_filter and not _has_friendly_name(device_info):
		if _tree_items.has(key):
			_tree_items[key].free()
			_tree_items.erase(key)
		return

	var root := _device_list.get_root()
	if root == null:
		root = _device_list.create_item()

	if _tree_items.has(key):
		_update_tree_item(_tree_items[key], device_info)
	else:
		var item := _create_tree_item(root, key, device_info)
		_tree_items[key] = item
		if _addresses_match(_device_address(device_info), _selected_address):
			item.select(0)


func _create_tree_item(root: TreeItem, key: String, device_info: Dictionary) -> TreeItem:
	var item := _device_list.create_item(root)
	_update_tree_item(item, device_info)
	return item


func _update_tree_item(item: TreeItem, device_info: Dictionary) -> void:
	var address: String = _device_address(device_info)
	var paired: bool = device_info.get("paired", false)
	var connected: bool = device_info.get("connected", false)
	item.set_text(0, _display_name_for(device_info))
	item.set_text(1, address)
	item.set_text(2, device_info.get("device_class", "unknown"))
	item.set_text(3, _paired_status_text(paired))
	item.set_text(4, _connected_status_text(connected))
	item.set_metadata(0, address)
	_apply_row_text_color(item, connected)


func _device_sort_rank(state: Dictionary) -> int:
	if state.get("connected", false):
		return 0
	if state.get("paired", false):
		return 1
	return 2


func _schedule_resort() -> void:
	_resort_pending = true


func _resort_items() -> void:
	var root := _device_list.get_root()
	if root == null:
		return

	var entries: Array = []
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var state: Dictionary = _item_states.get(key, {})
		var check_info := {"name": item.get_text(0), "address": str(item.get_metadata(0))}
		if named_only_filter and not _has_friendly_name(check_info):
			continue
		entries.append({
			"item": item,
			"rank": _device_sort_rank(state),
			"order": _discovery_order.get(key, 0),
			"name": item.get_text(0),
		})

	entries.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if a.rank != b.rank:
			return a.rank < b.rank
		var a_named: bool = not str(a.name).begins_with("Unnamed")
		var b_named: bool = not str(b.name).begins_with("Unnamed")
		if a_named != b_named:
			return a_named
		return a.order < b.order
	)

	for i in range(1, entries.size()):
		entries[i].item.move_after(entries[i - 1].item)

	if not _selected_address.is_empty():
		for key in _tree_items:
			var item: TreeItem = _tree_items[key]
			if _addresses_match(str(item.get_metadata(0)), _selected_address):
				item.select(0)
				break
	_refresh_all_row_colors()


func _count_visible_items() -> int:
	var count := 0
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		var check_info := {"name": item.get_text(0), "address": str(item.get_metadata(0))}
		if named_only_filter and not _has_friendly_name(check_info):
			continue
		count += 1
	return count


func _update_counts() -> void:
	var total := _tree_items.size()
	var visible := _count_visible_items()
	var named := 0
	for key in _tree_items:
		var item: TreeItem = _tree_items[key]
		if not item.get_text(0).begins_with("Unnamed"):
			named += 1
	counts_changed.emit(visible, total, named)


func _selected_row_text_color() -> Color:
	return _device_list.get_theme_color("font_selected_color", "Tree")


func _apply_row_text_color(item: TreeItem, connected: bool) -> void:
	if _device_list.get_selected() == item:
		var selected_color := _selected_row_text_color()
		for column in range(_device_list.columns):
			item.set_custom_color(column, selected_color)
		return

	if connected:
		for column in range(_device_list.columns):
			item.set_custom_color(column, CONNECTED_TEXT_COLOR)
		return

	for column in range(_device_list.columns):
		item.clear_custom_color(column)


func _flash_row(item: TreeItem, key: String) -> void:
	var connected: bool = _item_states.get(key, {}).get("connected", false)
	for column in range(_device_list.columns):
		item.set_custom_color(column, FLASH_TEXT_COLOR)
	var tw := create_tween()
	tw.tween_interval(0.12)
	tw.tween_callback(func() -> void:
		if is_instance_valid(item):
			_apply_row_text_color(item, connected)
	)


func _device_address(device_info: Dictionary) -> String:
	return device_info.get("address", "")


func _normalized_address(address: String) -> String:
	return Bluetooth.normalize_address(address)


func _is_mac_address(address: String) -> bool:
	return Bluetooth.is_valid_bluetooth_address(address)


func _addresses_match(a: String, b: String) -> bool:
	if a.is_empty() or b.is_empty():
		return false
	var na := _normalized_address(a)
	var nb := _normalized_address(b)
	if na == nb:
		return true
	if _is_mac_address(na) and (b.contains(na.replace(":", "")) or b.contains(na)):
		return true
	if _is_mac_address(nb) and (a.contains(nb.replace(":", "")) or a.contains(nb)):
		return true
	return a == b


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
	var address: String = device_info.get("address", "")
	if name.is_empty():
		return false
	if _is_mac_address(name) or _addresses_match(name, address):
		return false
	if name.begins_with("Unknown Device") or name.begins_with("Unnamed Device"):
		return false
	if name == "HID Gamepad":
		return false
	return true


func _display_name_for(device_info: Dictionary) -> String:
	var name: String = device_info.get("name", "").strip_edges()
	if name.is_empty() or _is_mac_address(name) or _addresses_match(name, _device_address(device_info)):
		return "Unnamed Device"
	return name


func _paired_status_text(paired: bool) -> String:
	return "Yes" if paired else "No"


func _connected_status_text(connected: bool) -> String:
	return "Yes" if connected else "No"