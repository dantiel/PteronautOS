#pragma once

#include "device.h"

extern device_t WIFI_device;

// Restore flight profiles after LittleFS is mounted, before starting outputs.
void LoadOrnithopterConfig();
