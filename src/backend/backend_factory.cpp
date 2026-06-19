#include "bluetooth_backend.h"

#if defined(_WIN32)
#include "../platform/windows/windows_backend.h"
#elif defined(__APPLE__)
#include "../platform/macos/macos_backend.h"
#elif defined(__linux__)
#include "../platform/linux/linux_backend.h"
#endif

namespace bluetooth {

BluetoothBackend *create_platform_backend() {
#if defined(_WIN32)
	return new WindowsBackend();
#elif defined(__APPLE__)
	return new MacOSBackend();
#elif defined(__linux__)
	return new LinuxBackend();
#else
	return nullptr;
#endif
}

} // namespace bluetooth