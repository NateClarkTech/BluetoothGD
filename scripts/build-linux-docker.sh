#!/usr/bin/env bash
# Build Linux x86_64 GDExtension libraries inside Ubuntu 20.04 for broad glibc
# compatibility (glibc ~2.31). Copies artifacts to addons/bluetooth_gd/bin/ and
# demo/addons/bluetooth_gd/bin/ via CMake POST_BUILD.
#
# Usage (from repository root):
#   ./scripts/build-linux-docker.sh
#
# Requires: Docker
#
# Note: Ubuntu 20.04 ships CMake 3.16; this script installs CMake from Kitware APT
# (focal) then uses explicit -S/-B paths (build/release, build/debug).
#
# SELinux (openSUSE / Fedora): relabels the repo mount with :Z when enforcing.
# Override with: export DOCKER_VOLUME_OPTS=z   (shared label)
#            or: export DOCKER_VOLUME_OPTS=    (disable relabeling)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -f "${ROOT}/CMakeLists.txt" ]]; then
	echo "ERROR: ${ROOT}/CMakeLists.txt not found. Run this script from the repository."
	exit 1
fi

# Default :Z on SELinux-enforcing hosts so bind mounts are visible inside the container.
if [[ ! -v DOCKER_VOLUME_OPTS ]]; then
	if command -v getenforce >/dev/null 2>&1 && [[ "$(getenforce)" == "Enforcing" ]]; then
		DOCKER_VOLUME_OPTS=Z
	fi
fi
DOCKER_VOLUME_OPTS="${DOCKER_VOLUME_OPTS:-}"

echo "=== Repository: ${ROOT} ==="
if [[ -n "${DOCKER_VOLUME_OPTS}" ]]; then
	echo "=== Docker volume option: :${DOCKER_VOLUME_OPTS} ==="
fi

# Initialize submodules on the host — git in the container often fails on SELinux
# systems (cannot read .git through the bind mount).
if [[ -d "${ROOT}/.git" ]]; then
	echo "=== Updating godot-cpp submodule (host) ==="
	git -C "${ROOT}" submodule update --init --recursive
else
	echo "WARNING: No .git directory — assuming godot-cpp is already present."
fi

if [[ ! -f "${ROOT}/godot-cpp/CMakeLists.txt" ]]; then
	echo "ERROR: godot-cpp is missing. Clone with submodules:"
	echo "  git clone --recurse-submodules <repository-url>"
	exit 1
fi

VOLUME_MOUNT="${ROOT}:/src"
if [[ -n "${DOCKER_VOLUME_OPTS}" ]]; then
	VOLUME_MOUNT="${VOLUME_MOUNT}:${DOCKER_VOLUME_OPTS}"
fi

docker run --rm \
	-v "${VOLUME_MOUNT}" \
	-w /src \
	ubuntu:20.04 \
	bash -c '
		set -euo pipefail

		if [[ ! -f /src/CMakeLists.txt ]]; then
			echo "ERROR: /src/CMakeLists.txt is not visible inside the container."
			echo "This is usually SELinux blocking the bind mount on openSUSE/Fedora."
			echo "Retry on the host with:"
			echo "  export DOCKER_VOLUME_OPTS=Z"
			echo "  ./scripts/build-linux-docker.sh"
			echo ""
			echo "Contents of /src:"
			ls -la /src || true
			exit 1
		fi

		export DEBIAN_FRONTEND=noninteractive

		apt-get update
		apt-get install -y \
			build-essential \
			ca-certificates \
			gnupg \
			wget \
			git \
			ninja-build \
			python3 \
			pkg-config \
			libdbus-1-dev

		# Ubuntu 20.04 ships CMake 3.16; this project requires 3.17+.
		wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
			| gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg
		echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main" \
			> /etc/apt/sources.list.d/kitware.list
		apt-get update
		apt-get install -y --no-install-recommends cmake

		echo "=== CMake $(cmake --version | head -1) ==="

		# Avoid stale CMake cache mixing debug/release godot-cpp bindings.
		rm -rf build/release build/debug

		echo "=== Release build ==="
		cmake -S . -B build/release -G Ninja \
			-DCMAKE_BUILD_TYPE=Release \
			-DGODOTCPP_TARGET=template_release
		cmake --build build/release -j"$(nproc)"

		echo "=== Debug build ==="
		cmake -S . -B build/debug -G Ninja \
			-DCMAKE_BUILD_TYPE=Debug \
			-DGODOTCPP_TARGET=template_debug
		cmake --build build/debug -j"$(nproc)"

		RELEASE_SO="addons/bluetooth_gd/bin/libbluetooth_manager.linux.template_release.x86_64.so"
		DEBUG_SO="addons/bluetooth_gd/bin/libbluetooth_manager.linux.template_debug.x86_64.so"

		echo "=== Build finished ==="
		ls -lh "${RELEASE_SO}" "${DEBUG_SO}"

		echo "=== Minimum GLIBC (release) ==="
		objdump -T "${RELEASE_SO}" | grep GLIBC | sed "s/.*GLIBC_/GLIBC_/" | sort -Vu
	'