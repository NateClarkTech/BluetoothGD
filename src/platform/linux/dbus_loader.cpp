#include "dbus_loader.h"

#include <dlfcn.h>

namespace bluetooth {

namespace {

void *dbus_handle = nullptr;

constexpr const char *DBUS_SONAMES[] = {
	"libdbus-1.so.3",
	"libdbus-1.so",
	nullptr,
};

} // namespace

bool ensure_dbus_library_loaded(godot::String *p_error) {
	if (dbus_handle != nullptr) {
		return true;
	}

	dlerror();
	for (int i = 0; DBUS_SONAMES[i] != nullptr; ++i) {
		dbus_handle = dlopen(DBUS_SONAMES[i], RTLD_NOW | RTLD_GLOBAL);
		if (dbus_handle != nullptr) {
			return true;
		}
	}

	const char *dlerr = dlerror();
	godot::String message =
			"libdbus-1 is not installed or could not be loaded. "
			"Install the D-Bus client library (e.g. libdbus-1-3 on Debian/Ubuntu, dbus-libs on Fedora).";
	if (dlerr != nullptr && dlerr[0] != '\0') {
		message += " dlopen: " + godot::String(dlerr);
	}
	if (p_error != nullptr) {
		*p_error = message;
	}
	return false;
}

bool is_dbus_library_loaded() {
	return dbus_handle != nullptr;
}

} // namespace bluetooth