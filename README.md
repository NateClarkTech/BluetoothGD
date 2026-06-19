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
- **Linux / macOS:** Backends compile but return "not implemented" errors at runtime (see [Supported platforms](#supported-platforms)).
- **Mobile:** Not implemented yet; see platform table below.

## Supported platforms

| Platform | Status | Backend | Notes |
|----------|--------|---------|-------|
| **Windows 10/11** (x86_64) | **Fully supported** | WinRT | Primary development target. Tested with Godot 4.7. |
| **Linux** (x86_64) | Stub | BlueZ / D-Bus | Milestone 2. Builds link `dbus-1`; operations emit not-implemented errors. |
| **macOS** (universal) | Stub | IOBluetooth | Milestone 3. Framework linked; operations emit not-implemented errors. |
| **Android** | Planned | Android Bluetooth API | Milestone 4. Requires runtime permissions (`BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN`, location on older API levels) and export plugin integration. Classic pairing availability varies by device and Android version. |
| **iOS** | Planned | Core Bluetooth / External Accessory | Milestone 5. iOS restricts third-party Classic Bluetooth access; gamepad pairing may require MFi / system UI flows. BLE-centric workflows are more feasible than full Classic pairing. |

**Currently fully working:** Windows desktop only.

Mobile platforms are listed because they are first-class targets in the project roadmap, but **no Android or iOS backend ships today**. Use `Bluetooth.get_platform_name()` and `Bluetooth.is_bluetooth_available()` at runtime to gate features per platform.

## Requirements

- [Godot 4.4+](https://godotengine.org/) (demo tested on 4.7)
- [CMake](https://cmake.org/) 3.17+
- C++17 compiler (C++20 on Windows for WinRT coroutines)
- Git with submodules

### Windows build tools

- Visual Studio 2019 or later with the **Desktop development with C++** workload
- **Windows 10/11 SDK** (WinRT / `windowsapp`)

### Linux build tools (stub backend)

- `libdbus-1-dev` (for future BlueZ integration)

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
│       ├── linux/          # BlueZ stub
│       └── macos/          # IOBluetooth stub
├── demo/                   # Godot demo project
│   ├── bluetooth_manager.gdextension
│   ├── bin/                # Compiled native libraries
│   └── scenes/             # Demo UI + autoload scene
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
    Bluetooth.device_found.connect(_on_device_found)
    Bluetooth.scan_started.connect(_on_scan_started)
    Bluetooth.scan_stopped.connect(_on_scan_stopped)
    Bluetooth.pairing_succeeded.connect(_on_pairing_succeeded)
    Bluetooth.pairing_failed.connect(_on_pairing_failed)
    Bluetooth.connection_changed.connect(_on_connection_changed)
    Bluetooth.error_occurred.connect(_on_error_occurred)

func _on_start_scan() -> void:
    Bluetooth.start_scan()

func _on_pair(address: String) -> void:
    Bluetooth.pair_device(address)
```

### API reference

#### Methods

| Method | Description |
|--------|-------------|
| `start_scan()` | Begin discovering nearby Bluetooth devices |
| `stop_scan()` | Stop an active scan |
| `get_discovered_devices() -> Array[Dictionary]` | Cached devices from the current session |
| `pair_device(address: String)` | Pair (bond) with a device |
| `unpair_device(address: String)` | Remove pairing |
| `get_paired_devices() -> Array[Dictionary]` | Known paired devices |
| `is_paired(address: String) -> bool` | Whether a device is paired |
| `connect_device(address: String)` | Request connection |
| `disconnect_device(address: String)` | Request disconnection (best-effort on Windows HID) |
| `is_connected(address: String) -> bool` | Whether a device is connected |
| `normalize_address(address: String) -> String` | Normalize MAC formatting (`AA:BB:CC:DD:EE:FF`) |
| `is_bluetooth_available() -> bool` | Whether the native backend initialized |
| `get_platform_name() -> String` | `"windows"`, `"linux"`, `"macos"`, or `"unknown"` |

#### Signals

| Signal | Payload |
|--------|---------|
| `device_found` | `device_info: Dictionary` |
| `scan_started` | — |
| `scan_stopped` | — |
| `pairing_started` | `address: String` |
| `pairing_succeeded` | `address: String` |
| `pairing_failed` | `address: String`, `error: String` |
| `connection_changed` | `address: String`, `connected: bool`, `message: String` |
| `error_occurred` | `operation: String`, `message: String` |

#### Device dictionary fields

| Field | Type | Description |
|-------|------|-------------|
| `address` | `String` | MAC address when available |
| `name` | `String` | Friendly name |
| `paired` | `bool` | Paired/bonded state |
| `connected` | `bool` | Connection state |
| `trusted` | `bool` | Trusted flag (platform-dependent) |
| `device_class` | `String` | Inferred class (e.g. `"gamepad"`) |
| `device_id` | `String` | Platform device path / ID (Windows AEP) |

## Demo UI

The reference scene (`demo/scenes/controller_pairing.tscn`) includes:

- **Named devices only** filter (on by default) — hides unnamed discovery entries while keeping them cached
- **Smart action button** — label switches between Pair / Connect / Disconnect based on device state
- **Event log** — general scan and pairing activity
- **Advanced log** — connect/disconnect attempts, blocked operations, and errors

## Roadmap

1. **M1 — Windows (WinRT)** — done
2. **M2 — Linux (BlueZ via D-Bus)**
3. **M3 — macOS (IOBluetooth)**
4. **M4 — Android** — permissions, JNI/NDK bridge, export templates
5. **M5 — iOS** — evaluate BLE vs Classic constraints; likely system-assisted pairing flows

## License

License not yet specified. Add a `LICENSE` file before distribution.