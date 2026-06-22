# BluetoothGD

Cross-platform **Bluetooth Classic** pairing and connection management for Godot 4.x, implemented as a GDExtension. Discover controllers, pair/unpair devices, and track connection state from GDScript — with a reference demo UI for gamepad pairing workflows.

Built for games that need in-app Bluetooth setup (for example, pairing an Xbox or PlayStation controller without sending players to the OS settings app).

## Features

- Device discovery via platform-native Bluetooth APIs
- Pair, unpair, connect, and disconnect by MAC address or Windows device ID
- Paired and connected state tracking with live updates
- Signal-driven API suitable for UI binding
- Worker-thread backend so Bluetooth I/O does not block the main thread
- Reference demo with device list, smart action button, event log, and advanced operational log

### Windows-specific capabilities

The Windows backend uses **WinRT** (`Windows.Devices.Enumeration`, `DevicePairing`, HID enumeration) and includes:

- Discovery across Classic Bluetooth, BLE association endpoints, and HID gamepads
- Connection detection for HID gamepads (Xbox, DualSense, and similar) when Classic `ConnectionStatus` is unavailable
- Detailed error messages (HRESULT, operation context, device IDs)

### Known limitations

- **Windows HID disconnect:** `disconnect_device()` cannot force-disconnect Bluetooth HID gamepads. Power off the controller or remove the device in Windows Bluetooth settings for a full disconnect.
- **Linux HID disconnect:** `disconnect_device()` may not keep Bluetooth HID gamepads disconnected if they reconnect when powered on.
- **macOS:** Backend compiles but returns "not implemented" errors at runtime (see [Supported platforms](#supported-platforms)).
- **Mobile:** Not implemented yet; see platform table below.

## Supported platforms

| Platform | Status | Backend | Notes |
|----------|--------|---------|-------|
| **Windows 10/11** (x86_64) | **Supported** | WinRT | Primary development target. Tested with Godot 4.7. |
| **Linux** (x86_64) | **Supported** | BlueZ / D-Bus | Requires `bluetoothd`, powered adapter, and BlueZ D-Bus permissions (`bluetooth` group or polkit). |
| **macOS** (universal) | Stub | IOBluetooth | Milestone 3. Framework linked; operations emit not-implemented errors. |
| **Android** | Planned | Android Bluetooth API | Milestone 4. Requires runtime permissions (`BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN`, location on older API levels) and export plugin integration. Classic pairing availability varies by device and Android version. |
| **iOS** | Planned | Core Bluetooth / External Accessory | Milestone 5. iOS restricts third-party Classic Bluetooth access; gamepad pairing may require MFi / system UI flows. BLE-centric workflows are more feasible than full Classic pairing. |

**Currently fully working:** Windows and Linux desktop.

Use `Bluetooth.get_platform_name()` and `Bluetooth.is_bluetooth_available()` at runtime to gate features per platform.

## Requirements

- [Godot 4.4+](https://godotengine.org/) (demo tested on 4.4 and 4.7)
- [CMake](https://cmake.org/) 3.17+
- C++17 compiler (C++20 on Windows for WinRT coroutines)
- Git with submodules

### Windows build tools

- Visual Studio 2019 or later with the **Desktop development with C++** workload
- **Windows 10/11 SDK** (WinRT / `windowsapp`)

### Linux build tools

- `libdbus-1-dev`
- `bluez` (runtime — `bluetoothd` must be running)
- User in the `bluetooth` group (or equivalent polkit rights) for pair/connect operations

### macOS build tools (stub backend)

- Xcode command-line tools

## Quick start

### 1. Clone and initialize submodules

```bash
git clone <repository-url> BluetoothGD
cd BluetoothGD
git submodule update --init --recursive
```

### 2. Build the extension

```bash
cmake -S . -B build
cmake --build build --config Debug
```

On Windows with Visual Studio:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Built libraries are copied to `demo/bin/` (for example `libbluetooth_manager.windows.template_debug.x86_64.dll`).

Close Godot before rebuilding — the editor locks the DLL on Windows.

### 3. Run the demo

1. Open the `demo/` folder as a Godot project.
2. Press **F5** to run `scenes/controller_pairing.tscn`.
3. Use **Start Scan** to discover devices, select one, then **Pair Device** / **Connect** / **Disconnect** as appropriate.

The demo autoloads `Bluetooth` from `scenes/bluetooth_manager.tscn`. The GDExtension manifest lives at `demo/bluetooth_manager.gdextension`.

## Project layout

```
BluetoothGD/
├── src/                    # GDExtension C++ source
│   ├── bluetooth_manager.* # Godot Node API
│   ├── backend/            # Command/event bridge
│   ├── threading/          # Worker thread + queues
│   └── platform/           # Per-OS backends
│       ├── windows/        # WinRT (fully implemented)
│       ├── linux/          # BlueZ / D-Bus
│       └── macos/          # IOBluetooth stub
├── demo/                   # Godot demo project
│   ├── bluetooth_manager.gdextension
│   ├── bin/                # Compiled native libraries (gitignored)
│   └── scenes/             # Demo UI + autoload scene
├── doc_classes/            # Editor API docs (BluetoothManager.xml)
├── tests/                  # Native unit tests (device info helpers)
├── cmake/                  # Build helpers (doc generation)
├── godot-cpp/              # Godot C++ bindings (submodule, 4.4 branch)
└── CMakeLists.txt
```

## Using in your project

### 1. Copy extension artifacts

Copy into your Godot project:

- `bluetooth_manager.gdextension`
- `bin/libbluetooth_manager.<platform>.<template>.<arch>.<ext>`

Adjust library paths in the `.gdextension` file if your `bin/` location differs.

### 2. Add a BluetoothManager node

Add a `BluetoothManager` node to your scene tree, or autoload it (as the demo does).

### 3. Connect signals and call methods

```gdscript
func _ready() -> void:
    Bluetooth.bluetooth_ready.connect(_on_bluetooth_ready)
    Bluetooth.device_found.connect(_on_device_found)
    Bluetooth.scan_started.connect(_on_scan_started)
    Bluetooth.scan_stopped.connect(_on_scan_stopped)
    Bluetooth.pairing_succeeded.connect(_on_pairing_succeeded)
    Bluetooth.pairing_failed.connect(_on_pairing_failed)
    Bluetooth.pairing_pin_requested.connect(_on_pairing_pin_requested)
    Bluetooth.connection_changed.connect(_on_connection_changed)
    Bluetooth.error_occurred.connect(_on_error_occurred)

func _on_bluetooth_ready() -> void:
    # Backend initialized and first paired-device sync completed.
    pass

func _on_start_scan() -> void:
    Bluetooth.start_scan({"named_only": false, "gamepads_only": false})

func _on_pair(address: String) -> void:
    Bluetooth.pair_device(address)

func _on_pairing_pin_requested(address: String) -> void:
    Bluetooth.confirm_pairing("0000")  # or reject_pairing() / cancel_pairing()

func _on_pairing_failed(address: String, error: String, error_code: int) -> void:
    print(Bluetooth.get_error_code_name(error_code), error)
```

### API reference

#### Methods

| Method | Description |
|--------|-------------|
| `start_scan(options: Dictionary = {})` | Begin discovering nearby Bluetooth devices (see [scan options](#scan-options)) |
| `stop_scan()` | Stop an active scan |
| `get_discovered_devices() -> Array` | Cached devices from the current session |
| `pair_device(address: String)` | Pair (bond) with a device by MAC or platform identifier |
| `pair_device_by_id(device_id: String)` | Pair using the platform `device_id` (useful on Windows before a MAC is resolved) |
| `unpair_device(address: String)` | Remove pairing |
| `refresh_paired_devices()` | Re-query paired devices from the native backend |
| `get_paired_devices() -> Array` | Known paired devices |
| `is_paired(address: String) -> bool` | Whether a device is paired |
| `connect_device(address: String)` | Request connection |
| `disconnect_device(address: String)` | Request disconnection (best-effort on Windows HID) |
| `is_connected(address: String) -> bool` | Whether a device is connected |
| `confirm_pairing(pin: String = "")` | Accept an interactive pairing request (PIN when requested) |
| `reject_pairing()` | Reject the current interactive pairing request |
| `cancel_pairing()` | Cancel the in-progress pairing ceremony |
| `normalize_address(address: String) -> String` | Normalize MAC formatting (`AA:BB:CC:DD:EE:FF`) |
| `is_valid_bluetooth_address(address: String) -> bool` | Whether a string is a 6-byte MAC address |
| `can_unpair_while_connected() -> bool` | Platform policy for unpairing connected devices (`true` on Windows and Linux) |
| `is_bluetooth_available() -> bool` | Whether the native backend initialized |
| `is_radio_on() -> bool` | Whether the Bluetooth radio is powered on |
| `get_platform_name() -> String` | `"windows"`, `"linux"`, `"macos"`, or `"unknown"` |
| `get_capabilities() -> Dictionary` | Platform capability flags (see [capabilities](#capabilities)) |
| `get_error_code_name(error_code: int) -> String` | Stable name for signal error codes (see [error codes](#error-codes)) |

#### Signals

| Signal | Payload |
|--------|---------|
| `bluetooth_ready` | — (emitted once after init + first paired-device sync) |
| `scan_started` | — |
| `scan_stopped` | — |
| `device_found` | `device_info: Dictionary` |
| `device_removed` | `address: String` |
| `devices_refreshed` | — (batch sync after `refresh_paired_devices`) |
| `pairing_started` | `address: String` |
| `pairing_succeeded` | `address: String` |
| `pairing_failed` | `address: String`, `error: String`, `error_code: int` |
| `pairing_confirmation_requested` | `address: String`, `kind: String` |
| `pairing_pin_requested` | `address: String` |
| `pairing_display_pin` | `address: String`, `pin: String` |
| `connection_changed` | `address: String`, `connected: bool`, `message: String` |
| `error_occurred` | `operation: String`, `message: String`, `error_code: int` |

#### Device dictionary fields

| Field | Type | Description |
|-------|------|-------------|
| `address` | `String` | MAC address when known; otherwise a platform identifier |
| `name` | `String` | Friendly name (may be empty for unnamed discovery entries) |
| `paired` | `bool` | Paired/bonded state |
| `connected` | `bool` | Connection state |
| `trusted` | `bool` | Trusted flag (primarily Linux) |
| `device_class` | `String` | Inferred class (e.g. `"gamepad"`, `"unknown"`) |
| `device_id` | `String` | Platform device path / ID (Windows AEP, Linux D-Bus object path) |
| `rssi` | `int` | Signal strength in dBm when available (Linux scan) |

#### Scan options

Optional `options` dictionary for `start_scan()`:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `named_only` | `bool` | `false` | Omit devices without a friendly name from backend scan results |
| `gamepads_only` | `bool` | `false` | Only report gamepad-class devices |
| `min_rssi` | `int` | `-127` | Minimum RSSI threshold (Linux) |
| `timeout_seconds` | `int` | `0` | Scan timeout hint in seconds |

The demo applies its **Named devices only** filter in the UI layer (via `DeviceListPresenter`), independent of these backend options.

#### Capabilities

`get_capabilities()` returns a dictionary of platform flags. Common keys:

| Key | Description |
|-----|-------------|
| `platform` | Same as `get_platform_name()` |
| `implemented` | Whether the backend is fully implemented |
| `supports_ble` | BLE discovery support |
| `supports_device_id` | `pair_device_by_id()` and `device_id` fields are usable |
| `supports_rssi` | RSSI available during scan (Linux) |
| `can_disconnect_hid` | Whether HID gamepads can be force-disconnected (`false` on Windows/Linux) |
| `can_unpair_while_connected` | Whether unpair is allowed while connected |
| `needs_pin_ui` | Whether interactive PIN/confirmation UI may be required |

#### Error codes

Use `get_error_code_name(error_code)` with `pairing_failed` and `error_occurred`. Known values:

`none`, `device_not_found`, `radio_off`, `not_supported`, `pin_required`, `pairing_rejected`, `permission_denied`, `operation_timeout`, `invalid_address`, `already_in_progress`, `unknown`

## Demo UI

The reference scene (`demo/scenes/controller_pairing.tscn`) includes:

- **Named devices only** filter (on by default) — hides unnamed discovery entries while keeping them cached
- **Smart action button** — label switches between Pair / Connect / Disconnect based on device state
- **Event log** — general scan and pairing activity
- **Advanced log** — connect/disconnect attempts, blocked operations, and errors

## Roadmap

1. **M1 — Windows (WinRT)** — done
2. **M2 — Linux (BlueZ via D-Bus)** — done
3. **M3 — macOS (IOBluetooth)**
4. **M4 — Android** — permissions, JNI/NDK bridge, export templates
5. **M5 — iOS** — evaluate BLE vs Classic constraints; likely system-assisted pairing flows

## License

[MIT License](LICENSE.md) — Copyright (c) 2026 Nate Clark.
