# BluetoothGD

Bluetooth Classic pairing and connection management for Godot 4.4+ (GDExtension).

## Install

1. Copy `addons/bluetooth_gd/` into your Godot project.
2. Autoload `example/bluetooth_manager.tscn` as `Bluetooth`, or add a `BluetoothManager` node to a scene.
3. Connect signals and call methods from GDScript.

This is a **GDExtension addon** — there is no EditorPlugin and nothing to enable under **Project → Plugins**.

## Binaries in `bin/`

| File | Use |
|------|-----|
| `*.template_debug.*` | Godot editor (~21 MB Linux debug `.so`) |
| `*.template_release.*` | Exported games (~1 MB Linux release `.so`) |

Ship both variants. The debug library is large because it includes symbols for editor debugging.

## Platforms

- **Windows 10/11** (x86_64) — supported
- **Linux** (x86_64) — supported (`bluetoothd`, `libdbus-1.so.3`, BlueZ permissions)
- **macOS** — not included (backend stub only)

## Docs and demo

- API reference: [repository README](https://github.com/NateClarkTech/BluetoothGD/blob/main/README.md)
- Example scan UI: `example/scan_demo.tscn`
- Full demo project: open the `demo/` folder from the repository root

## License

[MIT](LICENSE.md)