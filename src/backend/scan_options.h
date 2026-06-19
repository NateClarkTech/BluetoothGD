#pragma once

namespace bluetooth {

struct ScanOptions {
	bool named_only = false;
	bool gamepads_only = false;
	int min_rssi = -127;
	int timeout_seconds = 0;
};

} // namespace bluetooth