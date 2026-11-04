#pragma once

#include <stdint.h>
#include <ArduinoJson.h>

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncJsonResponse;

// Largest-contiguous-block water mark shared between devWIFI.cpp and the
// PteronautOS handler TU. Lives here so heapCriticallyLow's default argument
// is visible to both translation units.
static constexpr uint32_t HEAP_MAX_BLOCK_WATER = 4000;

// Shared helpers de-static'd from devWIFI.cpp so the PteronautOS handlers can
// reuse upstream code instead of duplicating it.
bool heapCriticallyLow(uint32_t water = HEAP_MAX_BLOCK_WATER);
void serveHeapBusy(AsyncWebServerRequest *request);
void sendJsonResponse(AsyncWebServerRequest *request, AsyncJsonResponse *response);
void populateRxConfigJson(JsonObject cfg);
void populateSettingsJson(JsonObject settings, const JsonObject json);

extern const char VERSION[];

#if defined(PLATFORM_ESP8266)
extern uint32_t heapBeforeWifi;
extern uint32_t heapAfterServices;
#endif

// Registers every /pteronautos/* route on `server` and restores the flight
// profile config (LoadOrnithopterConfig). Called once from startServices().
void registerPteronautosHandlers(AsyncWebServer &server);
