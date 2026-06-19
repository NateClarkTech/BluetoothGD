# BluetoothGD — Post-MVP Improvement Plan

**Status:** Phases A–F below are **implemented** (verified 2026-06-19). This doc remains the architecture reference and session bootstrap.  
**Purpose:** Ground truth for new sessions — do not trust stale conversation summaries over this file.  
**Last verified:** 2026-06-19 against repo at `BluetoothGD/` (Godot 4.7 demo, CMake build).

---

## 1. Ground truth — what exists today

### Project purpose

Cross-platform **Bluetooth Classic** pairing/connect GDExtension for Godot 4.x, with a reference demo UI for gamepad pairing workflows.

### Supported platforms (runtime)

| Platform | Status | Backend |
|----------|--------|---------|
| Windows 10/11 x86_64 | **Working** | WinRT (`src/platform/windows/`) |
| Linux x86_64 | **Working** | BlueZ via D-Bus (`src/platform/linux/`) |
| macOS | Stub | Emits not-implemented errors |
| Android / iOS | Not started | — |

### Build (verified)

```bash
cd BluetoothGD
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
# Output: demo/bin/libbluetooth_manager.linux.template_debug.x86_64.so (platform suffix varies)
```

**Linux deps:** `libdbus-1-dev`, runtime `bluetoothd`, user in `bluetooth` group (or polkit).  
**Windows deps:** VS + Windows SDK, C++20 for WinRT.

**CMake note:** D-Bus is required only on Linux (`if(UNIX AND NOT APPLE)` in `CMakeLists.txt`). Windows/macOS configures do not need `libdbus-1-dev`.

### Architecture (actual code paths)

```
GDScript (demo)
    ↕ signals + method calls (main thread)
BluetoothManager (Node autoload, src/bluetooth_manager.cpp)
    ↕ command queue / event queue
WorkerThread (src/threading/worker_thread.cpp)
    ↕ BluetoothBackend interface
Platform backend (windows_backend / linux_backend / macos stub)
```

- **Commands** (main → worker): `INITIALIZE`, `START_SCAN`, `STOP_SCAN`, `PAIR_DEVICE`, `UNPAIR_DEVICE`, `CONNECT_DEVICE`, `DISCONNECT_DEVICE`, `REFRESH_PAIRED_DEVICES`, `SHUTDOWN`
- **Events** (worker → main, drained in `BluetoothManager::_process`): see `src/backend/bluetooth_events.h`
- **Linux D-Bus poll:** `backend->poll()` on worker idle loop (100 ms command timeout)

### Public GDScript API (autoload `Bluetooth`)

**Methods:** `start_scan`, `stop_scan`, `get_discovered_devices`, `get_paired_devices`, `refresh_paired_devices`, `pair_device`, `unpair_device`, `connect_device`, `disconnect_device`, `is_paired`, `is_connected`, `normalize_address`, `is_valid_bluetooth_address`, `can_unpair_while_connected`, `is_bluetooth_available`, `get_platform_name`

**Signals:** `device_found`, `device_removed`, `devices_refreshed`, `bluetooth_ready`, `scan_started`, `scan_stopped`, `pairing_started`, `pairing_succeeded`, `pairing_failed`, `connection_changed`, `error_occurred`

See `README.md` for full payload tables.

**Device dictionary keys** (`DeviceInfo::to_dictionary` in `src/bluetooth_device_info.h`):  
`address`, `name`, `paired`, `connected`, `trusted`, `device_class`, `device_id`

### Demo UI (`demo/scenes/controller_pairing.gd` + `demo/scripts/device_list_presenter.gd`)

- **Tree** device list: columns **Name | Address | Type | Paired | Connected** (Yes/No)
- **Sort order:** connected → paired-not-connected → other; then named → discovery order
- **Named-only filter** (default on): hides devices without a “friendly” name
- **Data source:** `BluetoothManager` only (no parallel `_device_cache`); presenter does incremental Tree updates
- **Startup:** waits for `bluetooth_ready` before enabling scan
- **Unpair policy:** `Bluetooth.can_unpair_while_connected()` (not platform string checks)
- **Logs:** capped at 500 lines; collapsible advanced log

### Linux backend highlights (`src/platform/linux/`)

- Pairing agent at `/org/bluetoothgd/agent`, registered on `/org/bluez`
- Scan: silent `GetManagedObjects` snapshot → `PAIRED_DEVICES_UPDATED` batch sync, then `Adapter1.StartDiscovery` + D-Bus matches for `InterfacesAdded`, `InterfacesRemoved`, and `PropertiesChanged`
- Agent **rejects** PIN/passkey — “Just Works” pairing only
- `refresh_paired_devices()` → silent enumerate + `PAIRED_DEVICES_UPDATED` (no per-device spam)
- `unpair_device`: disconnect if connected, `Adapter1.RemoveDevice`, emit `DEVICE_REMOVED`
- `Paired=false` in `PropertiesChanged` updates via `DEVICE_FOUND`; removal via `InterfacesRemoved` / unpair only
- `disconnect_device`: emits `connection_changed(false)` only when BlueZ confirms disconnected

### Windows backend highlights

- `handle_device_removed` emits `DEVICE_REMOVED` to `BluetoothManager`
- Scan start: silent snapshots + `PAIRED_DEVICES_UPDATED`, then live watchers for discovery
- `refresh_paired_devices()`: silent enumerate + batch sync
- HID gamepads: connect state may differ from Classic `ConnectionStatus` (README documents limitations)

---

## 2. What the original MVP plan finished (do NOT re-implement)

These are **done**. A new session should not treat them as open work:

- [x] Linux BlueZ backend via `libdbus-1` (not shell scripts)
- [x] Worker-thread command/event model with `poll()` for D-Bus signals
- [x] BlueZ agent registration fix (`/org/bluez` path)
- [x] D-Bus dict parsing + `blocking_depth` re-entrancy guard
- [x] Demo UI scaling (`canvas_items` stretch), copy-paste logs (`TextEdit`)
- [x] Tree columns + sort (connected > paired > other)
- [x] `clear_unhelpful_device_name()` — don’t show MAC as friendly name
- [x] Linux auto-disconnect before unpair
- [x] README platform table updated for Linux support

---

## 3. Stale assumptions to avoid

| Wrong assumption | Reality |
|------------------|---------|
| “Phases A–F are still open” | Implemented — see section 1 and `README.md` API table |
| “Demo uses `_device_cache`” | Removed; `DeviceListPresenter` reads `Bluetooth` + incremental Tree |
| “Scan start floods `device_found`” | Fixed: silent snapshot + `devices_refreshed` on both platforms |
| “`Paired=false` means device_removed on Linux” | Fixed: only updates paired column; removal via unpair / `InterfacesRemoved` |
| “`is_paired`/`is_connected` hit D-Bus from GDScript” | They read `BluetoothManager` HashMaps on main thread only |
| “macOS works” | Stub only |
| “Bulk refresh emits per-device `device_removed`” | `sync_devices_from_snapshot` replaces cache; emits `devices_refreshed` only |

---

## 4. Problem summary (prioritized)

### P0 — Correctness & API completeness

1. **No device removal lifecycle** — unpair / `InterfacesRemoved` / Windows `Removed` do not propagate to GDScript or `BluetoothManager` maps consistently.
2. **Triple cache** — backend + `BluetoothManager` + demo `_device_cache` with redundant sync (`_sync_cache_from_bluetooth`, `_refresh_cache_connection_flags`, `_resolve_device_flags`).
3. **Startup race** — demo `_ready()` lists devices before worker `INITIALIZE` + `REFRESH_PAIRED_DEVICES` finish.
4. **Linux disconnect signal semantics** — `disconnect_device` emits `connection_changed(false)` even when `Device1.Disconnect` fails (message mentions failure; bool does not).

### P1 — Performance & event churn

5. **Full Tree rebuild** on every `device_found` during scan.
6. **`refresh_paired_devices` floods UI** — `enumerate_devices(true, true)` force-emits N × `device_found`.
7. **Repeated flag resolution** — sort comparator and row build call `_resolve_device_flags` / `is_paired` per device multiple times per refresh.

### P2 — Cross-platform API hygiene

8. **Platform checks in demo GDScript** (`get_platform_name() == "linux"` for unpair).
9. **`pair_device`/`connect_device` always `normalize_address`** — may be wrong for opaque Windows `device_id` strings (normalize is MAC-oriented).
10. **`PAIRED_DEVICES_UPDATED` unused** — batch refresh signal defined but not exposed.

### P3 — Demo UX & maintainability

11. **Logs unbounded** — memory growth in long sessions.
12. **No in-flight operation UI** — buttons stay enabled during pair/connect.
13. **`controller_pairing.gd` ~650 lines** — presentation + cache + actions intertwined.
14. **Minor:** `_current_device_action` stored but unused; `Bluetooth.call(...)` instead of typed methods.

### P4 — Future / docs

15. Linux PIN pairing not supported (agent rejects) — document or implement.
16. No project tests or CI.
17. Optional UI polish (feedback on button press, row highlight on connect) — demo only, not core extension.

---

## 5. Implementation phases (PR-sized)

Execute in order unless noted. Each phase has **acceptance criteria** and **files to touch**.

---

### Phase A — Device lifecycle signals (P0)

**Goal:** One authoritative removal/unpair path from native → `BluetoothManager` → GDScript.

#### A1. Add events and signals

- Add `EventType::DEVICE_REMOVED` (or reuse and implement `PAIRED_DEVICES_UPDATED` — prefer explicit `device_removed`).
- `BluetoothManager`: handle event → erase from `discovered_devices` and `paired_devices` → emit `device_removed(address: String)`.
- Bind method + `ADD_SIGNAL` in `_bind_methods()`.
- Update `README.md` API table.

**Files:**  
`src/backend/bluetooth_events.h`, `src/bluetooth_manager.cpp`, `src/bluetooth_manager.h`, `README.md`

#### A2. Linux — emit removal

- Add D-Bus match for `InterfacesRemoved` on ObjectManager (mirror `InterfacesAdded` in `start_scan` / `stop_scan`).
- Implement `handle_interfaces_removed` — when `org.bluez.Device1` path removed, emit `DEVICE_REMOVED` with normalized address.
- After successful `unpair_device` (`Adapter1.RemoveDevice`), emit `DEVICE_REMOVED` (don’t rely only on signal race).

**Files:**  
`src/platform/linux/linux_backend.cpp`, `linux_backend.h`, possibly `bluez_dbus.cpp`

#### A3. Windows — propagate removal

- `handle_device_removed`: emit `DEVICE_REMOVED` to event queue (lookup address from cache before erase).

**Files:**  
`src/platform/windows/windows_backend.cpp`

#### A4. Pairing state from PropertiesChanged (Linux)

- When `Paired` flips in `handle_properties_changed`, emit `pairing_succeeded` or `device_removed` / update maps as appropriate (today only `connection_changed` is emitted from that handler).

**Files:**  
`src/platform/linux/linux_backend.cpp`, `src/bluetooth_manager.cpp`

**Acceptance criteria:**

- [ ] Unpair from demo removes row without manual “Refresh Paired”
- [ ] Linux: device removed in system settings disappears from list
- [ ] Windows: watcher `Removed` updates list
- [ ] No duplicate rows after remove + rescan

---

### Phase B — Single source of truth in demo (P0 + P1)

**Goal:** Demo reads from `BluetoothManager` only; delete parallel cache logic where possible.

#### B1. Add `backend_ready` or use existing signals

Option A (minimal): demo defers first populate:

```gdscript
func _ready() -> void:
    ...
    if Bluetooth.is_bluetooth_available():
        _refresh_device_list()
    else:
        Bluetooth.error_occurred.connect(_on_backend_error_once, CONNECT_ONE_SHOT)
    Bluetooth.device_found.connect(...) # already exists — first burst from refresh populates list
```

Option B (cleaner): new signal `bluetooth_ready` emitted once after successful init + first `REFRESH_PAIRED_DEVICES` completes (requires C++ change).

**Files:**  
`demo/scenes/controller_pairing.gd`, optionally `src/bluetooth_manager.cpp`

#### B2. Slim demo data layer

- Replace `_device_cache` with:
  - `_discovery_order: Dictionary` (address → int) for stable sort only, OR
  - sort by `name`/`address` within rank (drop discovery order entirely).
- `_refresh_device_list()` builds from `get_discovered_devices()` + merge `get_paired_devices()` (single function, no `_refresh_cache_connection_flags`).
- Trust `paired`/`connected` on dictionaries updated by signals; call `is_paired`/`is_connected` only when handling user action (not per-row per-frame).

**Files:**  
`demo/scenes/controller_pairing.gd`

#### B3. Incremental Tree updates

- Keep `address → TreeItem` map.
- On `device_found`: update existing item or create one; resort only when rank changes (or throttle full resort to 150 ms during scan).
- On `device_removed`: `item.free()` / erase from map.
- On `connection_changed` / `pairing_succeeded`: update columns 3–4 and reposition if rank changed.

**Files:**  
`demo/scenes/controller_pairing.gd`

**Acceptance criteria:**

- [ ] First launch populates paired devices without empty flash (or shows “Initializing…” until ready)
- [ ] Scan with 20+ devices does not stutter badly (subjective: no full clear per device)
- [ ] Removing `_device_cache` does not break selection or sort

---

### Phase C — Batch refresh & less event noise (P1)

**Goal:** Stop N× `device_found` on every `refresh_paired_devices`.

#### C1. Silent sync vs notify

- Change `refresh_paired_devices()` to `enumerate_devices(false, false)` by default.
- Emit one `PAIRED_DEVICES_UPDATED` (or `devices_refreshed`) after enumerate completes.
- `BluetoothManager`: on that event, replace or merge manager maps from backend — **requires backend → manager bulk sync API** OR backend emits full snapshot event.

**Design choice (pick one in implementation):**

| Approach | Pros | Cons |
|----------|------|------|
| **Snapshot event** (`Array` of devices in one event) | Simple manager update | Large payloads |
| **Pull model** (`sync_from_backend()` on worker, manager stores mirror) | Quiet wire | More C++ plumbing |

**Files:**  
`src/platform/linux/linux_backend.cpp`, `src/platform/windows/windows_backend.cpp`, `src/bluetooth_manager.cpp`, `src/backend/bluetooth_events.h`

**Acceptance criteria:**

- [ ] Init + post-unpair refresh does not spam main log with one line per cached device
- [ ] UI still updates after refresh

---

### Phase D — API hygiene (P2)

#### D1. `can_unpair_while_connected() -> bool`

- Windows: `false` (or true if backend gains auto-disconnect later)
- Linux: `true`
- Demo uses this instead of `get_platform_name() == "linux"`

#### D2. Address vs device ID

- Add `is_valid_bluetooth_address` binding to GDScript (already in C++ inline).
- `pair_device` / `connect_device`: only normalize when address is valid MAC; pass through opaque IDs unchanged.

#### D3. Disconnect truthfulness (Linux)

- Emit `connection_changed(false)` only if disconnect succeeded OR BlueZ reports `Connected=false` after property read.
- On failure: `error_occurred` only, or `connection_changed` with a new optional `success` parameter (breaking change — document in README).

**Files:**  
`src/bluetooth_manager.cpp`, `src/platform/linux/linux_backend.cpp`, `demo/scenes/controller_pairing.gd`, `README.md`

---

### Phase E — Demo UX & code structure (P3)

#### E1. Extract presenter script

- `demo/scripts/device_list_presenter.gd` — Tree columns, sort, filter, incremental update
- `controller_pairing.gd` — scan buttons, logs, action handlers

#### E2. Operational UX

- Disable action buttons while operation pending (track pending address + op enum).
- Cap logs (e.g. 500 lines) or “Clear log” button.
- Selection restore via normalized address match on rebuild.

#### E3. Light polish (godot-polish — demo appropriate)

- `punch_ui()` on button press (scale tween) for Pair/Connect/Unpair.
- Brief `modulate` flash on row when `connection_changed` fires.
- No shaders/particles needed — utility demo.

**Files:**  
`demo/scenes/controller_pairing.gd`, `demo/scenes/controller_pairing.tscn`, new script under `demo/scripts/`

---

### Phase F — Build, test, CI (P4)

#### F1. CMake guard

```cmake
if(UNIX AND NOT APPLE)
  pkg_check_modules(DBUS REQUIRED dbus-1)
endif()
```

Move D-Bus include/link into that block only.

#### F2. Unit tests (C++)

Test without D-Bus:

- `normalize_address`, `is_valid_bluetooth_address`, `addresses_match`
- `clear_unhelpful_device_name`, `infer_device_class`, `canonical_device_key` logic

#### F3. Smoke test

- Godot headless loads demo scene without script errors.
- Optional: mock `BluetoothManager` autoload for presenter tests.

#### F4. CI

- GitHub Actions: Linux + Windows build matrix, upload `.so` / `.dll` artifact.

**Files:**  
`CMakeLists.txt`, new `tests/` directory, `.github/workflows/build.yml`

---

## 6. Suggested PR stack (Graphite-style)

```
main
 └── pr/A-device-lifecycle-signals
      └── pr/B-demo-single-source-of-truth
           └── pr/C-batch-refresh
                └── pr/D-api-hygiene
                     └── pr/E-demo-ux-refactor
                          └── pr/F-ci-and-tests
```

Phases A and B can start in parallel only if B uses new `device_removed` signal from A — **serialize A before B** for demo work.

---

## 7. Verification checklist (run after each phase)

```bash
# Build
cmake --build build --parallel

# Godot smoke
godot --path demo --headless --quit-after 1

# Manual hardware (Linux)
groups | grep bluetooth   # must pass for pair/connect
bluetoothctl show         # powered yes
# Run demo: scan → pair → connect → disconnect → unpair
```

**Regression matrix:**

| Action | Windows | Linux |
|--------|---------|-------|
| Scan lists devices | ✓ test | ✓ test |
| Pair gamepad | ✓ test | ✓ test |
| Connect / disconnect | ✓ test | ✓ test |
| Unpair removes from list | ✓ after Phase A | ✓ after Phase A |
| Named-only filter | ✓ | ✓ |
| Sort: connected on top | ✓ | ✓ |

---

## 8. File map (quick navigation)

| Area | Path |
|------|------|
| GDExtension entry | `src/register_types.cpp` |
| Public API | `src/bluetooth_manager.cpp`, `.h` |
| Device model helpers | `src/bluetooth_device_info.h` |
| Events / commands | `src/backend/bluetooth_events.h`, `bluetooth_commands.h` |
| Worker | `src/threading/worker_thread.cpp` |
| Linux BlueZ | `src/platform/linux/linux_backend.cpp`, `bluez_dbus.cpp`, `bluez_agent.cpp` |
| Windows WinRT | `src/platform/windows/windows_backend.cpp` |
| Demo UI | `demo/scenes/controller_pairing.gd`, `.tscn` |
| Autoload | `demo/scenes/bluetooth_manager.tscn` → class `BluetoothManager` |
| Extension binary | `demo/bin/libbluetooth_manager.*` |
| User docs | `README.md` |

---

## 9. Out of scope for this plan

- macOS IOBluetooth implementation
- Android / iOS export
- BLE-only workflows
- In-app PIN entry UI (unless spun out as Phase G after agent work)
- Godot InputMap / `InputJoy` integration — pairing only, not gamepad input routing

---

## 10. Session bootstrap prompt

Copy into a new agent session:

```
Read docs/IMPROVEMENT_PLAN.md in BluetoothGD. Implement Phase A (device lifecycle signals)
unless I specify otherwise. Verify ground truth in section 1 before coding; ignore stale
conversation summaries. Build with CMake, smoke-test with Godot headless, update README
for any new signals.
```

Replace `Phase A` with the target phase as needed.