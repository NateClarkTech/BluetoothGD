#!/usr/bin/env bash
# Verify Linux GDExtension binaries load correctly (libdbus linked, RTLD_NOW).
# Used by CI and after local/Docker builds.
#
# Usage:
#   ./scripts/verify-linux-binaries.sh
#   ./scripts/verify-linux-binaries.sh path/to/libbluetooth_manager.linux.template_release.x86_64.so

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RELEASE_SO="${1:-${ROOT}/addons/bluetooth_gd/bin/libbluetooth_manager.linux.template_release.x86_64.so}"
DEBUG_SO="${ROOT}/addons/bluetooth_gd/bin/libbluetooth_manager.linux.template_debug.x86_64.so"

require_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required tool: $1" >&2
		exit 1
	fi
}

verify_extension_so() {
	local so_path="$1"
	local label="$2"

	echo "=== Verify ${label}: ${so_path} ==="
	if [[ ! -f "${so_path}" ]]; then
		echo "ERROR: missing ${so_path}" >&2
		exit 1
	fi

	if ! readelf -d "${so_path}" | grep -q "Shared library: \[libdbus-1.so.3\]"; then
		echo "ERROR: ${so_path} does not list libdbus-1.so.3 in DT_NEEDED." >&2
		readelf -d "${so_path}" | grep NEEDED || true
		exit 1
	fi

	if ! ldd "${so_path}" | grep -q "libdbus-1.so.3 =>"; then
		echo "ERROR: ${so_path} cannot resolve libdbus-1.so.3." >&2
		ldd "${so_path}" || true
		exit 1
	fi

	python3 - "${so_path}" <<'PY'
import ctypes
import os
import sys

so_path = sys.argv[1]
rtld_now = getattr(os, "RTLD_NOW", getattr(ctypes, "RTLD_NOW", 0x2))
ctypes.CDLL(so_path, mode=rtld_now)
print(f"dlopen(RTLD_NOW) OK: {so_path}")
PY

	local max_glibc max_glibcxx
	max_glibc="$(objdump -T "${so_path}" | grep -oE "GLIBC_[0-9.]+" | sort -Vu | tail -1)"
	max_glibcxx="$(objdump -T "${so_path}" | grep -oE "GLIBCXX_[0-9.]+" | sort -Vu | tail -1)"
	echo "  libdbus DT_NEEDED: OK"
	echo "  dlopen(RTLD_NOW): OK"
	echo "  max GLIBC symbol: ${max_glibc:-unknown}"
	echo "  max GLIBCXX symbol: ${max_glibcxx:-unknown}"
}

require_cmd readelf
require_cmd ldd
require_cmd objdump
require_cmd python3

verify_extension_so "${RELEASE_SO}" "release"
verify_extension_so "${DEBUG_SO}" "debug"

echo "=== Linux binary verification passed ==="