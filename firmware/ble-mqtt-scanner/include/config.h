#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEVICE_REGISTRY_CAPACITY
#define DEVICE_REGISTRY_CAPACITY 256
#endif

#ifndef SIGHTING_INTERVAL_MS
#define SIGHTING_INTERVAL_MS 300000UL
#endif

#ifndef SCAN_DURATION_MS
#define SCAN_DURATION_MS 30000UL
#endif

#ifndef LOCAL_LOG_MAX_BYTES
#define LOCAL_LOG_MAX_BYTES (512UL * 1024UL)
#endif

#ifndef PRESENCE_ENTER_RSSI
#define PRESENCE_ENTER_RSSI (-75)
#endif

#ifndef PRESENCE_EXIT_TIMEOUT_MS
#define PRESENCE_EXIT_TIMEOUT_MS 90000UL
#endif

#ifndef PRESENCE_TRACKER_CAPACITY
#define PRESENCE_TRACKER_CAPACITY 128
#endif
