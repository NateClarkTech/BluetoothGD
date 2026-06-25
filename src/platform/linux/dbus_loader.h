#pragma once

#include <godot_cpp/variant/string.hpp>

namespace bluetooth {

bool ensure_dbus_library_loaded(godot::String *p_error = nullptr);
bool is_dbus_library_loaded();

} // namespace bluetooth