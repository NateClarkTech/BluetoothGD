# BluetoothGD

Bluetooth Classic device discovery, pairing, and connection management for Godot 4.4+, via GDExtension.

Use it when your game needs in-app controller setup instead of sending players to OS Bluetooth settings.

## Supported platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Windows 10/11 (x86_64) | Supported | WinRT backend |
| Linux (x86_64) | Supported | BlueZ + D-Bus; needs `bluetoothd`, `libdbus-1.so.3`, adapter permissions |
| macOS | Stub | Not shipped; runtime returns not-implemented |
| Mobile | Planned | Not implemented |

Check at runtime: `Bluetooth.is_bluetooth_available()` and `Bluetooth.get_platform_name()`.

## Quick start

### Use the addon

1. Copy `addons/bluetooth_gd/` into your project.
2. Autoload `addons/bluetooth_gd/example/bluetooth_manager.tscn` as `Bluetooth` (or add a `BluetoothManager` node).
3. Connect signals and call methods:

```gdscript
func _ready() -> void:
    Bluetooth.bluetooth_ready.connect(func(): Bluetooth.start_scan())
    Bluetooth.device_found.connect(func(info): print(info))
    Bluetooth.pairing_failed.connect(func(a, err, code): print(code, err))
```

No EditorPlugin — this is a native GDExtension only.

### Run the demo

1. Open `demo/` in Godot 4.4+.
2. Press F5 (`scenes/controller_pairing.tscn`).

The demo autoloads `scenes/bluetooth_manager.tscn`.

## Shipped binaries (`addons/bluetooth_gd/bin/`)

| Variant | When to use | Approx. Linux size |
|---------|-------------|-------------------|
| `template_debug` | Godot editor | ~21 MB `.so` |
| `template_release` | Exported builds | ~1 MB `.so` |

Include **both** in distributions. Debug builds are large due to debug symbols; release builds are what players need.

Windows `.dll` files are also included for editor (debug) and export (release).

## Linux runtime requirements

- `libdbus-1.so.3` (linked at build time — required for Godot to load the extension)
- BlueZ (`bluetoothd` running)
- User in `bluetooth` group or polkit rights for pair/connect

Prebuilt Linux libraries target **GLIBC_2.14** (built in Ubuntu 20.04 Docker).

## Building from source

```bash
git clone --recurse-submodules https://github.com/NateClarkTech/BluetoothGD.git
cd BluetoothGD
```

**Linux (canonical shipping build):**

```bash
./scripts/build-linux-docker.sh
# SELinux hosts: export DOCKER_VOLUME_OPTS=Z
./scripts/verify-linux-binaries.sh
```

Builds inside Ubuntu 20.04, copies to `addons/bluetooth_gd/bin/` and `demo/addons/bluetooth_gd/bin/`, and verifies `libdbus` linkage plus `dlopen(RTLD_NOW)`.

**Local development:**

```bash
cmake --preset debug && cmake --build --preset debug
cmake --preset release && cmake --build --preset release
```

**Windows:** `pwsh scripts/build-windows.ps1` (close Godot first).

Requirements: CMake 3.17+, C++17, `libdbus-1-dev` on Linux, VS 2019+ on Windows.

## API overview

`BluetoothManager` is a `Node` with signal-driven pairing/scan APIs. See `addons/bluetooth_gd/doc_classes/BluetoothManager.xml` for the full reference.

Common calls: `start_scan()`, `pair_device(address)`, `connect_device(address)`, `get_paired_devices()`, `get_discovered_devices()`.

Device dictionaries include `address`, `name`, `paired`, `connected`, `device_class`, `device_id`, and `rssi` (Linux).

## Known limitations

- Windows/Linux: cannot reliably force-disconnect Bluetooth HID gamepads via `disconnect_device()`.
- Linux: missing `libdbus-1` prevents the extension from loading (not a graceful fallback).
- macOS/mobile: not production-ready.

## CI

GitHub Actions runs unit tests, verifies committed Linux binaries (`verify-linux-binaries.sh`), and rebuilds Linux artifacts with `build-linux-docker.sh` (Ubuntu 20.04).

## Project layout

```
addons/bluetooth_gd/   # Copy into your game (gdextension, bin/, example/, docs)
demo/                  # Reference Godot project
src/                   # C++ GDExtension source
scripts/               # build-linux-docker.sh, verify-linux-binaries.sh, build-windows.ps1
```

## License

[MIT License](LICENSE.md) — Copyright (c) 2026 Nate Clark.