# Build Windows x86_64 GDExtension libraries (Release + Debug). CMake POST_BUILD copies
# artifacts to addons/bluetooth_gd/bin/ and demo/addons/bluetooth_gd/bin/; this script
# also syncs and verifies both locations after building.
#
# Usage (from repository root, in a VS Developer terminal):
#   .\scripts\build-windows.ps1
#
# Requires: CMake 3.23+ (for presets), MSVC 2019+ with Desktop C++ workload and Windows SDK.
# Close Godot before building — the editor locks native libraries.

#Requires -Version 5.1

$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not (Test-Path (Join-Path $Root 'CMakeLists.txt'))) {
	Write-Error "CMakeLists.txt not found at $Root. Run this script from the repository."
}

Write-Host "=== Repository: $Root ==="

if (Test-Path (Join-Path $Root '.git')) {
	Write-Host '=== Updating godot-cpp submodule (host) ==='
	git -C $Root submodule update --init --recursive
	if ($LASTEXITCODE -ne 0) {
		Write-Error 'git submodule update failed.'
	}
} else {
	Write-Warning 'No .git directory — assuming godot-cpp is already present.'
}

if (-not (Test-Path (Join-Path $Root 'godot-cpp/CMakeLists.txt'))) {
	Write-Error @"
godot-cpp is missing. Clone with submodules:
  git clone --recurse-submodules <repository-url>
"@
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
	Write-Error 'cmake not found in PATH. Install CMake 3.23+ and ensure it is on PATH.'
}

$cmakeVersion = (cmake --version | Select-Object -First 1)
Write-Host "=== $cmakeVersion ==="

if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
	Write-Error 'ninja not found in PATH. Open a Visual Studio Developer terminal or install Ninja.'
}

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
	Write-Error 'cl.exe not found in PATH. Open a Visual Studio Developer terminal before running this script.'
}

Write-Host '=== Generator: Ninja ==='

$ReleaseDir = Join-Path $Root 'build/release'
$DebugDir = Join-Path $Root 'build/debug'
$AddonBin = Join-Path $Root 'addons/bluetooth_gd/bin'
$DemoBin = Join-Path $Root 'demo/addons/bluetooth_gd/bin'

# Avoid stale CMake cache mixing debug/release godot-cpp bindings.
foreach ($dir in @($ReleaseDir, $DebugDir)) {
	if (Test-Path $dir) {
		Remove-Item -LiteralPath $dir -Recurse -Force
	}
}

$jobs = $env:NUMBER_OF_PROCESSORS
if (-not $jobs) { $jobs = 4 }

function Invoke-CMakePreset {
	param(
		[string]$PresetName
	)

	& cmake --preset $PresetName
	if ($LASTEXITCODE -ne 0) {
		Write-Error "CMake configure failed for preset '$PresetName'"
	}

	& cmake --build --preset $PresetName -j $jobs
	if ($LASTEXITCODE -ne 0) {
		Write-Error "CMake build failed for preset '$PresetName'"
	}
}

Write-Host '=== Release build ==='
Invoke-CMakePreset -PresetName 'release'

Write-Host '=== Debug build ==='
Invoke-CMakePreset -PresetName 'debug'

$ReleaseDll = 'libbluetooth_manager.windows.template_release.x86_64.dll'
$DebugDll = 'libbluetooth_manager.windows.template_debug.x86_64.dll'

function Copy-ToBoth {
	param(
		[string]$Source,
		[string]$Name
	)

	if (-not (Test-Path $Source)) {
		Write-Error "Missing build output $Source"
	}

	Copy-Item -LiteralPath $Source -Destination (Join-Path $AddonBin $Name) -Force
	Copy-Item -LiteralPath $Source -Destination (Join-Path $DemoBin $Name) -Force
}

Write-Host '=== Syncing binaries to addon and demo bin/ ==='
Copy-ToBoth -Source (Join-Path $ReleaseDir "bin/$ReleaseDll") -Name $ReleaseDll
Copy-ToBoth -Source (Join-Path $DebugDir "bin/$DebugDll") -Name $DebugDll

Write-Host '=== Build finished ==='
Get-Item (Join-Path $AddonBin $ReleaseDll), (Join-Path $AddonBin $DebugDll) |
	Format-Table Name, Length, LastWriteTime -AutoSize
Get-Item (Join-Path $DemoBin $ReleaseDll), (Join-Path $DemoBin $DebugDll) |
	Format-Table Name, Length, LastWriteTime -AutoSize

Write-Host '=== Verifying addon and demo binaries match ==='
foreach ($name in @($ReleaseDll, $DebugDll)) {
	$addonHash = (Get-FileHash (Join-Path $AddonBin $name) -Algorithm MD5).Hash
	$demoHash = (Get-FileHash (Join-Path $DemoBin $name) -Algorithm MD5).Hash
	if ($addonHash -ne $demoHash) {
		Write-Error "Checksum mismatch for $name"
	}
	Write-Host "${name}: OK ($addonHash)"
}

Write-Host '=== Done ==='