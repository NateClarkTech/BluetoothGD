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
- **Linux missing libdbus:** The extension loads without a hard `libdbus` dependency, but Bluetooth stays unavailable until `libdbus-1` is installed.
- **macOS:** Backend compiles but returns "not implemented" errors at runtime (see [Supported platforms](#supported-platforms)).
- **Mobile:** Not implemented yet; see platform table below.

## Supported platforms

| Platform | Status | Backend | Notes |
|----------|--------|---------|-------|
| **Windows 10/11** (x86_64) | **Supported** | WinRT | Primary development target. Tested with Godot 4.7. |
| **Linux** (x86_64) | **Supported** | BlueZ / D-Bus | Requires `bluetoothd`, `libdbus-1` at runtime, powered adapter, and BlueZ D-Bus permissions (`bluetooth` group or polkit). Missing `libdbus-1` degrades gracefully (`is_bluetooth_available()` → `false`). |
| **macOS** (universal) | Stub | IOBluetooth | Milestone 3. Framework linked; operations emit not-implemented errors. |
| **Android** | Planned | Android Bluetooth API | Milestone 4. Requires runtime permissions (`BLUETOOTH_CONNECT`, `BLUETOOTH_SCAN`, location on older API levels) and export plugin integration. Classic pairing availability varies by device and Android version. |
| **iOS** | Planned | Core Bluetooth / External Accessory | Milestone 5. iOS restricts third-party Classic Bluetooth access; gamepad pairing may require MFi / system UI flows. BLE-centric workflows are more feasible than full Classic pairing. |

**Currently fully working:** Windows and Linux desktop.

Use `Bluetooth.get_platform_name()` and `Bluetooth.is_bluetooth_available()` at runtime to gate features per platform.

## Requirements

- [Godot 4.4+](https://godotengine.org/) (demo tested on 4.4 and 4.7)
- [CMake](https://cmake.org/) 3.17+ (3.23+ for [CMake presets](CMakePresets.json))
- C++17 compiler (C++20 on Windows for WinRT coroutines)
- Git with submodules

### Windows build tools

- Visual Studio 2019 or later with the **Desktop development with C++** workload
- **Windows 10/11 SDK** (WinRT / `windowsapp`)

### Linux build tools

- `libdbus-1-dev` (headers/pkg-config at **build** time only — `libdbus-1` is loaded at runtime via `dlopen`, not linked as `DT_NEEDED`)
- `pkg-config`, Ninja (recommended), and a C++17 compiler
- [Docker](https://docs.docker.com/) (optional — recommended for shipping prebuilt `.so` files via `scripts/build-linux-docker.sh`)
- `bluez` (runtime — `bluetoothd` must be running)
- `libdbus-1-3` / `dbus-libs` (runtime — if missing, the extension still loads but `is_bluetooth_available()` returns `false`)
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

Successful builds copy native libraries into **both**:

- `addons/bluetooth_gd/bin/` — distributable addon
- `demo/addons/bluetooth_gd/bin/` — demo project

Ship **both** variants when distributing the addon:

| Variant | Use |
|---------|-----|
| `template_debug` | Godot editor |
| `template_release` | Exported / release builds |

Output names must match `bluetooth_manager.gdextension` (for example `libbluetooth_manager.linux.template_release.x86_64.so`).

Close Godot before rebuilding — the editor locks native libraries on Windows.

#### Linux prebuilt binaries (recommended for shipping)

Use the Docker helper to build on **Ubuntu 20.04** with GCC 9. This avoids accidentally linking against newer glibc/libstdc++ symbols from a bleeding-edge host.

```bash
./scripts/build-linux-docker.sh
```

The script:

- Initializes `godot-cpp` on the **host** (before Docker)
- Builds **Release** and **Debug** inside `ubuntu:20.04`
- Installs CMake from Kitware (Ubuntu 20.04's CMake 3.16 is too old)
- Sets `GODOTCPP_TARGET` to match each build type
- Prints `GLIBC_*` symbols from the release `.so`

On **openSUSE / Fedora** with SELinux enforcing, the script auto-applies the `:Z` volume label. If `/src` is empty inside the container:

```bash
export DOCKER_VOLUME_OPTS=Z
./scripts/build-linux-docker.sh
```

**Prebuilt Linux runtime requirements** (current 20.04 Docker build):

| Dependency | Notes |
|------------|-------|
| glibc | Symbols up to **GLIBC_2.14** |
| libstdc++.so.6 | Symbols up to **GLIBCXX_3.4.22** |
| libdbus-1.so.3 | Loaded at runtime via `dlopen` — not a hard `DT_NEEDED` dependency |
| BlueZ | `bluetoothd` running; D-Bus permissions for pair/connect |

If `libdbus-1` is missing, the extension still loads but `is_bluetooth_available()` returns `false` and `error_occurred` reports `not_supported`.

Verify symbol requirements after any Linux build:

```bash
SO=addons/bluetooth_gd/bin/libbluetooth_manager.linux.template_release.x86_64.so
objdump -T "$SO" | grep -oE 'GLIBC_[0-9.]+'   | sort -Vu | tail -1
objdump -T "$SO" | grep -oE 'GLIBCXX_[0-9.]+' | sort -Vu | tail -1
ldd "$SO" | grep dbus   # should print nothing
```

Build on an older base image yourself if you need to target an even older distro.

#### Local build (development)

Requires **CMake 3.17+** (presets need **3.23+**). Always set `GODOTCPP_TARGET` to match the build type.

**CMake presets** ([CMakePresets.json](CMakePresets.json)):

```bash
cmake --preset debug
cmake --build --preset debug

cmake --preset release
cmake --build --preset release
```

**Manual configure:**

```bash
# Debug (editor)
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGODOTCPP_TARGET=template_debug
cmake --build build/debug

# Release (export)
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release
cmake --build build/release
```

**Windows (Visual Studio):** set `GODOTCPP_TARGET` at configure time — use separate build directories for debug and release:

```powershell
cmake -S . -B build-debug   -DGODOTCPP_TARGET=template_debug
cmake -S . -B build-release -DGODOTCPP_TARGET=template_release
cmake --build build-debug   --config Debug
cmake --build build-release --config Release
```

### 3. Run the demo

1. Open the `demo/` folder as a Godot project.
2. Press **F5** to run `scenes/controller_pairing.tscn`.
3. Use **Start Scan** to discover devices, select one, then **Pair Device** / **Connect** / **Disconnect** as appropriate.

The demo autoloads `Bluetooth` from `scenes/bluetooth_manager.tscn`. The GDExtension manifest lives at `demo/addons/bluetooth_gd/bluetooth_manager.gdextension`.

## Project layout

```
BluetoothGD/
├── addons/bluetooth_gd/    # Godot addon (copy into your project)
│   ├── bluetooth_manager.gdextension
│   ├── bin/                # Compiled native libraries (post-build copy target)
│   ├── doc_classes/        # Editor class docs
│   └── example/            # Example autoload scene
├── src/                    # GDExtension C++ source
│   ├── bluetooth_manager.* # Godot Node API
│   ├── backend/            # Command/event bridge
│   ├── threading/          # Worker thread + queues
│   └── platform/           # Per-OS backends
│       ├── windows/        # WinRT (fully implemented)
│       ├── linux/          # BlueZ / D-Bus (+ dbus_loader)
│       └── macos/          # IOBluetooth stub
├── demo/                   # Godot demo project
│   ├── addons/bluetooth_gd/  # Same addon layout; bin/ receives post-build copy
│   └── scenes/             # Demo UI + autoload scene
├── doc_classes/            # Source XML for BluetoothManager editor docs
├── tests/                  # Native unit tests (device info helpers)
├── cmake/                  # Build helpers (doc generation, API pin)
├── scripts/                # build-linux-docker.sh (Ubuntu 20.04 container build)
├── godot-cpp/              # Godot C++ bindings (submodule, 4.4 branch)
├── CMakeLists.txt
└── CMakePresets.json       # debug / release Ninja presets
```

## Using in your project

### 1. Copy the addon

Copy the entire `addons/bluetooth_gd/` folder into your Godot project's `addons/` directory. It should contain:

- `bluetooth_manager.gdextension`
- `bin/libbluetooth_manager.<platform>.<template>.<arch>.<ext>` (build all variants you need — debug for editor, release for exported builds)

If you build from source, run CMake first so `addons/bluetooth_gd/bin/` is populated. Adjust library paths in the `.gdextension` file only if you relocate `bin/`.

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
    if not Bluetooth.is_bluetooth_available():
        return  # e.g. missing libdbus on Linux
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
| `implemented` | Whether the backend initialized successfully |
| `requires_libdbus` | Linux only — D-Bus client library is required at runtime |
| `dbus_available` | Linux only — whether `libdbus-1` was loaded |
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
