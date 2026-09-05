#include "device.h"

#include "deferred.h"

#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "PteroConfigWriter.h"

#if defined(PLATFORM_ESP32)
#include <esp_wifi.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <soc/uart_pins.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#define wifi_mode_t WiFiMode_t
#endif
#include <DNSServer.h>

#include <set>
#include <StreamString.h>

#include <ESPAsyncWebServer.h>

#include "common.h"
#include "rxtx_intf.h"
#include "POWERMGNT.h"
#include "FHSS.h"
#include "hwTimer.h"
#include "logging.h"
#include "options.h"
#include "helpers.h"
#include "devButton.h"
#include "devAnalogVbat.h"

#ifdef ZEPHYRUS_ENABLED
#include "../Zephyrus/ZephyrusFilter.h"
#endif
#ifdef ORNITHOPTER_MODE
#include "../Ornithopter/OrnithopterFilter.h"
#endif

#if defined(TARGET_RX)
#include "VbatCalibration.h"
#endif
#if defined(TARGET_RX) && defined(PLATFORM_ESP32)
#include "devVTXSPI.h"
#endif

#include "WebContent.h"

#include <cstdarg>
#include "config.h"
#if defined(RADIO_LR1121)
#include "lr1121.h"
#endif

#if defined(TARGET_TX)
#include "wifiJoystick.h"

extern void setButtonColors(uint8_t b1, uint8_t b2);
#endif

static char station_ssid[33];
static char station_password[65];

static bool wifiStarted = false;
bool webserverPreventAutoStart = false;

static wl_status_t laststatus = WL_IDLE_STATUS;
volatile WiFiMode_t wifiMode = WIFI_OFF;
static volatile WiFiMode_t changeMode = WIFI_OFF;
static volatile unsigned long changeTime = 0;

static const byte DNS_PORT = 53;
static IPAddress netMsk(255, 255, 255, 0);
static DNSServer dnsServer;
static IPAddress ipAddress;

#if defined(TARGET_RX)
#include "TcpMspConnector.h"
TcpMspConnector wifi2tcp;
#endif

#if defined(PLATFORM_ESP8266)
static bool scanComplete = false;
#endif

static AsyncWebServer server(80);
static bool servicesStarted = false;
static constexpr uint32_t STALE_WIFI_SCAN = 20000;
static uint32_t lastScanTimeMS = 0;

static bool target_seen = false;
static uint8_t target_pos = 0;
static String target_found;
static bool target_complete = false;
static bool force_update = false;
static uint32_t totalSize;

static const char VERSION[] = {LATEST_VERSION, 0};

void setWifiUpdateMode()
{
  // No need to ExitBindingMode(), the radio will be stopped stopped when start the Wifi service.
  // Need to change this before the mode change event so the LED is updated
  InBindingMode = false;
  setConnectionState(wifiUpdate);
}

/** Is this an IP? */
static boolean isIp(const String& str)
{
  for (size_t i = 0; i < str.length(); i++)
  {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9'))
    {
      return false;
    }
  }
  return true;
}

/** IP to String? */
static String toStringIp(const IPAddress& ip)
{
  String res = "";
  for (int i = 0; i < 3; i++)
  {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

static bool captivePortal(AsyncWebServerRequest *request)
{
  if (!isIp(request->host()) && request->host() != (String(wifi_hostname) + ".local"))
  {
    DBGLN("Request redirected to captive portal");
    request->redirect(String("http://") + toStringIp(request->client()->localIP()));
    return true;
  }
  return false;
}

static void WebUpdateSendContent(AsyncWebServerRequest *request)
{
  for (size_t i=0 ; i<WEB_ASSETS_COUNT ; i++) {
    if (request->url().equals(WEB_ASSETS[i].path)) {
      AsyncWebServerResponse *response = request->beginResponse(200, WEB_ASSETS[i].content_type, WEB_ASSETS[i].data, WEB_ASSETS[i].size);
      response->addHeader("Content-Encoding", "gzip");
      if (request->url().equals("/index.html")) {
        response->addHeader("Cache-Control", "no-cache");
      } else {
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
      }
      request->send(response);
      return;
    }
  }
  request->send(404, "text/plain", "File not found");
}

static void WebUpdateHandleRoot(AsyncWebServerRequest *request)
{
  if (captivePortal(request))
  { // If captive portal redirect instead of displaying the page.
    return;
  }
  force_update = request->hasArg("force");
  if (connectionState == hardwareUndefined)
  {
    request->redirect("/index.html#hardware");
  }
  else
  {
    request->redirect("/index.html");
  }
}

static void putFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  static File file;
  static size_t bytes;
  if (!file ||
    // Request URI starts with a / and LittleFS File::name() does not include it, ESP32 doesn't have File::fullName()
    strcmp(&request->url().c_str()[1], file.name()) != 0)
  {
    file = LittleFS.open(request->url(), "w");
    bytes = 0;
  }
  file.write(data, len);
  bytes += len;
  if (bytes == total)
  {
    file.close();
  }
}

static void getFile(AsyncWebServerRequest *request)
{
  if (request->url() == "/options.json") {
    request->send(200, "application/json", getOptions());
  } else if (request->url() == "/hardware.json") {
    request->send(200, "application/json", getHardware());
  } else {
    request->send(LittleFS, request->url().c_str(), "text/plain", true);
  }
}

static void HandleReboot(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "Kill -9, no more CPU time!");
  response->addHeader("Connection", "close");
  request->send(response);
  scheduleRebootTime(200);
}

static void HandleReset(AsyncWebServerRequest *request)
{
  if (request->hasArg("hardware")) {
    LittleFS.remove("/hardware.json");
  }
  if (request->hasArg("options")) {
    LittleFS.remove("/options.json");
#if defined(TARGET_RX)
    config.SetModelId(255);
    config.SetForceTlmOff(false);
    config.Commit();
#endif
  }
  if (request->hasArg("lr1121")) {
    LittleFS.remove("/lr1121.txt");
  }
  if (request->hasArg("model") || request->hasArg("config")) {
    config.SetDefaults(true);
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "Reset complete, rebooting...");
  response->addHeader("Connection", "close");
  request->send(response);
  scheduleRebootTime(100);
}

static void UpdateSettings(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (firmwareOptions.flash_discriminator != json["flash-discriminator"].as<uint32_t>()) {
    request->send(409, "text/plain", "Mismatched device identifier, refresh the page and try again.");
    return;
  }

  File file = LittleFS.open("/options.json", "w");
  serializeJson(json, file);
  file.close();
  String options;
  serializeJson(json, options);
  setOptions(options);
  request->send(200);
}

static const char *GetConfigUidType(const JsonObject json)
{
#if defined(TARGET_RX)
  if (config.GetBindStorage() == BINDSTORAGE_VOLATILE)
    return "Volatile";
  if (config.GetBindStorage() == BINDSTORAGE_RETURNABLE && config.IsOnLoan())
    return "Loaned";
  if (config.GetIsBound())
    return "Bound";
  return "Not Bound";
#else
  if (firmwareOptions.hasUID)
  {
    if (json["options"]["customised"] | false)
      return "Overridden";
    else
      return "Flashed";
  }
  return "Not set (using MAC address)";
#endif
}

static int8_t wifi_GetClientRssi()
{
  if (wifiMode == WIFI_STA)
    return WiFi.RSSI();

#if defined(PLATFORM_ESP32)
  // If AP mode, only return an RSSI if there is just one client connected
  // This could take the request's IP address, find it in tcpip_adapter_get_sta_list(), match it by MAC to ap_sta_list,
  // but there should just be one client
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK)
  {
    if (staList.num == 1)
      return staList.sta[0].rssi;
  }
#endif
  // ESP8266 doesn't seem to store connected station RSSI :/

  return 0;
}

#if defined(TARGET_RX)
static uint8_t getDefinedVoltageSourceCount()
{
    uint8_t count = 0;
    if (hardware_pin(HARDWARE_vbat) != UNDEF_PIN)
        ++count;
#if defined(PLATFORM_ESP32)
    if (hardware_pin(HARDWARE_vsrc1) != UNDEF_PIN)
        ++count;
    if (hardware_pin(HARDWARE_vsrc2) != UNDEF_PIN)
        ++count;
    if (hardware_pin(HARDWARE_vsrc3) != UNDEF_PIN)
        ++count;
#endif
    return count;
}

static void populateVoltageSampleJson(JsonObject root, const voltage_source_sample_t &sample)
{
  root["rawMax"] = sample.rawMax;
  root["adcMedian"] = sample.adcMedian;
  root["saturated"] = sample.saturated;
  root["hasReading"] = sample.hasReading;
}

static void SampleVoltageSources(AsyncWebServerRequest *request, JsonVariant &json)
{
  JsonArray requests = json["requests"].as<JsonArray>();
  if (requests.isNull())
  {
    request->send(400, "text/plain", "Voltage sample batch requests are required");
    return;
  }

  auto *response = new AsyncJsonResponse();
  JsonObject root = response->getRoot().to<JsonObject>();
  JsonObject samplesRoot = root["samples"].to<JsonObject>();

  bool sampledAny = false;
  Vbat_setCalibrationActive(true);
  for (JsonVariant requestItem : requests)
  {
    uint8_t sourceIdx = 0;
    const char *sourceId = requestItem["source"] | "";
    if (!VbatCalibration_findSource(sourceId, &sourceIdx) || !VbatCalibration_isSourceDefined(sourceIdx))
      continue;

    voltage_source_config_t source {};
    VbatCalibration_getSourceConfig(sourceIdx, &source);
    int atten = requestItem["atten"] | source.atten;
    uint8_t samples = requestItem["samples"] | 24;

    voltage_source_sample_t sample {};
    if (!VbatCalibration_sampleSource(sourceIdx, atten, samples, &sample))
      continue;

    JsonObject sampleRoot = samplesRoot[source.id].to<JsonObject>();
    populateVoltageSampleJson(sampleRoot, sample);
    sampledAny = true;
  }
  Vbat_setCalibrationActive(false);

  if (!sampledAny)
  {
    delete response;
    request->send(400, "text/plain", "No valid voltage sample batch requests");
    return;
  }

  response->setLength();
  request->send(response);
}
#endif

static void GetConfiguration(AsyncWebServerRequest *request)
{
  const bool exportMode = request->hasArg("export");
  auto *response = new AsyncJsonResponse();
  const auto json = response->getRoot();

  if (!exportMode)
  {
    JsonDocument options;
    deserializeJson(options, getOptions());
    json["options"] = options;
  }

  const auto cfg = json["config"].to<JsonObject>();
  const auto uid = cfg["uid"].to<JsonArray>();
  copyArray(UID, UID_LEN, uid);

#if defined(TARGET_TX)
  int button_count = 0;
  if (GPIO_PIN_BUTTON != UNDEF_PIN)
    button_count = 1;
  if (GPIO_PIN_BUTTON2 != UNDEF_PIN)
    button_count = 2;
  for (int button=0 ; button<button_count ; button++)
  {
    const tx_button_color_t *buttonColor = config.GetButtonActions(button);
    const auto btn = cfg["button-actions"][button].to<JsonObject>();
    if (hardware_int(button == 0 ? HARDWARE_button_led_index : HARDWARE_button2_led_index) != -1) {
      btn["color"] = buttonColor->val.color;
    }
    for (int pos=0 ; pos<button_GetActionCnt() ; pos++)
    {
      const auto action = btn["action"][pos].to<JsonObject>();
      action["is-long-press"] = buttonColor->val.actions[pos].pressType ? true : false;
      action["count"] = buttonColor->val.actions[pos].count;
      action["action"] = buttonColor->val.actions[pos].action;
    }
  }
  if (exportMode)
  {
    cfg["fan-mode"] = config.GetFanMode();
    cfg["power-fan-threshold"] = config.GetPowerFanThreshold();
    cfg["motion-mode"] = config.GetMotionMode();

    const auto vtxAdmin = cfg["vtx-admin"].to<JsonObject>();
    vtxAdmin["band"] = config.GetVtxBand();
    vtxAdmin["channel"] = config.GetVtxChannel();
    vtxAdmin["pitmode"] = config.GetVtxPitmode();
    vtxAdmin["power"] = config.GetVtxPower();

    const auto backpack = cfg["backpack"].to<JsonObject>();
    backpack["disabled"] = config.GetBackpackDisable();
    backpack["dvr-start-delay"] = config.GetDvrStartDelay();
    backpack["dvr-stop-delay"] = config.GetDvrStopDelay();
    backpack["dvr-aux-channel"] = config.GetDvrAux();
    backpack["telemetry-mode"] = config.GetBackpackTlmMode();

    for (int model = 0 ; model < CONFIG_TX_MODEL_CNT ; model++)
    {
      const model_config_t &modelConfig = config.GetModelConfig(model);
      String strModel(model);
      const auto modelJson = cfg["model"][strModel].to<JsonObject>();
      modelJson["packet-rate"] = modelConfig.rate;
      modelJson["telemetry-ratio"] = modelConfig.tlm;
      modelJson["switch-mode"] = modelConfig.switchMode;
      modelJson["link-mode"] = modelConfig.linkMode;
      modelJson["model-match"] = modelConfig.modelMatch;
      modelJson["tx-antenna"] = modelConfig.txAntenna;
      modelJson["ptr-start-chan"] = modelConfig.ptrStartChannel;
      modelJson["ptr-enable-chan"] = modelConfig.ptrEnableChannel;
      const auto power = cfg["power"].to<JsonObject>();
      power["max-power"] = modelConfig.power;
      power["dynamic-power"] = modelConfig.dynamicPower;
      power["boost-channel"] = modelConfig.boostChannel;
    }
  }
#endif /* TARGET_TX */

  if (!exportMode)
  {
    const auto settings = json["settings"].to<JsonObject>();
    #if defined(TARGET_RX)
    cfg["serial-protocol"] = config.GetSerialProtocol();
    #if defined(PLATFORM_ESP32)
    if ((GPIO_PIN_SERIAL1_RX != UNDEF_PIN && GPIO_PIN_SERIAL1_TX != UNDEF_PIN) || GPIO_PIN_PWM_OUTPUTS_COUNT > 0)
    {
      cfg["serial1-protocol"] = config.GetSerial1Protocol();
    }
    #endif
    cfg["sbus-failsafe"] = config.GetFailsafeMode();
    cfg["modelid"] = config.GetModelId();
    cfg["force-tlm"] = config.GetForceTlmOff();
    cfg["vbind"] = config.GetBindStorage();
    for (int ch=0; ch<GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
      const auto channel = cfg["pwm"][ch].to<JsonObject>();
      channel["config"] = config.GetPwmChannel(ch)->raw;
      channel["pin"] = GPIO_PIN_PWM_OUTPUTS[ch];
      uint8_t features = 0;
      auto pin = GPIO_PIN_PWM_OUTPUTS[ch];
      if (!OPT_PWM_OUT_ONLY)
      {
        if (pin == U0TXD_GPIO_NUM) features |= 1;  // SerialTX supported
        else if (pin == U0RXD_GPIO_NUM) features |= 2;  // SerialRX supported
        else if (pin == GPIO_PIN_SCL) features |= 4;  // I2C SCL supported (only on this pin)
        else if (pin == GPIO_PIN_SDA) features |= 8;  // I2C SDA supported (only on this pin)
        else if (GPIO_PIN_SCL == UNDEF_PIN || GPIO_PIN_SDA == UNDEF_PIN) features |= 12; // Both I2C SCL/SDA supported (on any pin)
      }
      #if defined(PLATFORM_ESP32)
      if (pin != 0) features |= 16; // DShot supported on all pins but GPIO0
      if (!OPT_PWM_OUT_ONLY)
      {
        if (pin == GPIO_PIN_SERIAL1_RX) features |= 32;  // SERIAL1 RX supported (only on this pin)
        else if (pin == GPIO_PIN_SERIAL1_TX) features |= 64;  // SERIAL1 TX supported (only on this pin)
        else if ((GPIO_PIN_SERIAL1_RX == UNDEF_PIN || GPIO_PIN_SERIAL1_TX == UNDEF_PIN) &&
                 (!(features & 1) && !(features & 2))) features |= 96; // Both Serial1 RX/TX supported (on any pin if not already featured for Serial 1)
      }
      #endif
      channel["features"] = features;
    }
    if (GPIO_PIN_RCSIGNAL_RX != UNDEF_PIN && GPIO_PIN_RCSIGNAL_TX != UNDEF_PIN)
    {
        settings["has_serial_pins"] = true;
    }
    #endif
    settings["product_name"] = product_name;
    settings["lua_name"] = device_name;
    settings["uidtype"] = GetConfigUidType(json);
    settings["ssid"] = station_ssid;
    settings["mode"] = wifiMode == WIFI_STA ? "STA" : "AP";
    settings["wifi_dbm"] = wifi_GetClientRssi();
    settings["custom_hardware"] = hardware_flag(HARDWARE_customised);
    settings["target"] = &target_name[4];
    settings["version"] = VERSION;
    settings["git-commit"] = commit;
#if defined(TARGET_TX)
    settings["module-type"] = "TX";
#endif
#if defined(TARGET_RX)
    settings["module-type"] = "RX";
    settings["voltage_source_count"] = getDefinedVoltageSourceCount();
#endif
#if defined(RADIO_SX128X)
    settings["radio-type"] = "SX128X";
    settings["has_low_band"] = false;
    settings["has_high_band"] = true;
    settings["reg_domain_high"] = FHSSconfig->domain;
#elif defined(RADIO_SX127X)
    settings["radio-type"] = "SX127X";
    settings["has_low_band"] = true;
    settings["has_high_band"] = false;
    settings["reg_domain_low"] = FHSSconfig->domain;
#elif defined(RADIO_LR1121)
    settings["radio-type"] = "LR1121";
    settings["has_low_band"] = POWER_OUTPUT_VALUES_COUNT != 0;
    settings["has_high_band"] = POWER_OUTPUT_VALUES_DUAL_COUNT != 0;
    settings["reg_domain_low"] = FHSSconfig->domain;
    settings["reg_domain_high"] = FHSSconfigDualBand->domain;
#endif
  }

  response->setLength();
  request->send(response);
}

static void GetPteronautosDebug(AsyncWebServerRequest *request)
{
    auto *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    root["firmware"] = "PteronautOS";

    response->setLength();
    request->send(response);
}

#ifdef ZEPHYRUS_ENABLED
#include <Wire.h>
#endif

static void GetPteronautosI2cScan(AsyncWebServerRequest *request)
{
    auto *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();

    root["firmware"] = "PteronautOS";

#ifdef ZEPHYRUS_ENABLED
    // --- Pin diagnostics ---
    root["i2c_sda_pin"] = ZEPHYR_I2C_SDA;
    root["i2c_scl_pin"] = ZEPHYR_I2C_SCL;

    // Read raw GPIO state before touching I2C
    pinMode(ZEPHYR_I2C_SDA, INPUT);
    pinMode(ZEPHYR_I2C_SCL, INPUT);
    int sdaBefore = digitalRead(ZEPHYR_I2C_SDA);
    int sclBefore = digitalRead(ZEPHYR_I2C_SCL);
    root["sda_raw_before"] = sdaBefore;
    root["scl_raw_before"] = sclBefore;

    // Now init I2C and scan
    Wire.begin(ZEPHYR_I2C_SDA, ZEPHYR_I2C_SCL);
    Wire.setClock(100000);
    delay(5);

    JsonArray devices = root["devices"].to<JsonArray>();
    JsonArray errors  = root["errors"].to<JsonArray>();
    uint8_t found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            char hex[8];
            snprintf(hex, sizeof(hex), "0x%02X", addr);
            devices.add(hex);
            found++;
        } else if (err == 2) {
            // NACK on address — expected for empty addresses, log only first few
            if (addr <= 5 || (addr >= 0x66 && addr <= 0x6A)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "0x%02X:NACK", addr);
                errors.add(buf);
            }
        }
    }

    root["found"] = found;

    // Post-scan GPIO read
    int sdaAfter = digitalRead(ZEPHYR_I2C_SDA);
    int sclAfter = digitalRead(ZEPHYR_I2C_SCL);
    root["sda_raw_after"] = sdaAfter;
    root["scl_raw_after"] = sclAfter;

    // Diagnostic summary
    if (found == 0 && sdaBefore == 0 && sclBefore == 0) {
        root["diagnosis"] = "BOTH pins LOW — check GY-521 VCC/GND or pull-up resistors";
    } else if (found == 0 && sdaBefore == 0) {
        root["diagnosis"] = "SDA LOW — SDA wire loose, shorted, or missing pull-up";
    } else if (found == 0 && sclBefore == 0) {
        root["diagnosis"] = "SCL LOW — SCL wire loose, shorted, or missing pull-up";
    } else if (found == 0) {
        root["diagnosis"] = "Pins HIGH but no devices — check SDA/SCL not swapped, MPU address (0x68), or faulty GY-521";
    } else if (found > 0) {
        root["diagnosis"] = "OK";
    }
#else
    root["found"] = 0;
    root["error"] = "Zephyrus not compiled (ZEPHYRUS_ENABLED=0)";
#endif

    response->setLength();
    request->send(response);
}

static void GetPteronautosSystem(AsyncWebServerRequest *request)
{
    auto *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();

    root["firmware"] = "PteronautOS";
    root["version"] = "0.1.0";
    root["build_date"] = __DATE__;
    root["build_time"] = __TIME__;
    root["uptime_ms"] = millis();

#if defined(PLATFORM_ESP8266)
    root["free_heap"] = ESP.getFreeHeap();
    root["cpu_freq_mhz"] = 80;
#elif defined(PLATFORM_ESP32)
    root["free_heap"] = ESP.getFreeHeap();
    root["cpu_freq_mhz"] = 240;
#endif

    root["wifi_rssi"] = WiFi.RSSI();
    root["wifi_channel"] = WiFi.channel();
    root["wifi_mode"] = (WiFi.getMode() == WIFI_AP) ? "AP" : "STA";

    // Flash size
#if defined(PLATFORM_ESP8266)
    root["flash_size_kb"] = ESP.getFlashChipSize() / 1024;
    root["flash_speed_mhz"] = ESP.getFlashChipSpeed() / 1000000;
#endif

    response->setLength();
    request->send(response);
}
// ── Servo sweep externs (defined in devServoOutput.cpp) ──────────
extern void startServoSweep();
extern void stopServoSweep();
extern bool isServoSweepActive();
extern uint16_t getServoSweepUs();

// State endpoint — AsyncJsonResponse (same proven pattern as GetPteronautosSystem).
// ArduinoJson v7 auto-grows from heap. No stack pressure, no format-string bugs.
void LoadOrnithopterConfig();
static bool SaveOrnithopterConfig();

static void GetPteronautosState(AsyncWebServerRequest *request)
{
    // Telemetry only. The ~90-field static config moved to GetPteronautosConfig
    // (fetched once per panel mount) so the 2s poll no longer rebuilds it.
    auto *response = new AsyncJsonResponse(false);
    JsonObject root = response->getRoot().as<JsonObject>();

    // System diagnostics (cheap; no float formatting).
    root["uptime_ms"] = millis();
#if defined(PLATFORM_ESP8266)
    root["free_heap"] = (int)ESP.getFreeHeap();
#endif
    root["reset_reason"] = ESP.getResetReason().c_str();

    JsonObject orni = root["ornithopter"].to<JsonObject>();
    orni["servo_left_wing_us"]  = ornithopter.funcValue(SF_LEFT_WING);
    orni["servo_right_wing_us"] = ornithopter.funcValue(SF_RIGHT_WING);
    orni["servo_rudder_us"]     = ornithopter.funcValue(SF_RUDDER);
    orni["servo_motor_us"]      = ornithopter.funcValue(SF_MOTOR);
    orni["voice_throttle"]      = ornithopter.voiceThrottle;
    orni["voice_freq"]          = ornithopter.voiceFreq;
    orni["voice_profile"]       = ornithopter.voiceProfile;
    orni["voice_aileron"]       = ornithopter.voiceAileron;
    orni["voice_elevator"]      = ornithopter.voiceElevator;
    orni["voice_rudder"]        = ornithopter.voiceRudder;
    orni["voice_arm"]           = ornithopter.voiceArm;
    orni["link_up"]             = ornithopter.linkUp;
    orni["enabled"]             = ornithopter.enabled;
    orni["connection_state"]    = connectionState;
    orni["active_flight_profile"] = ornithopter.activeFlightProfile;
    orni["active_profile"] = (uint8_t)activeProfile;
    orni["servo_count"] = PROFILE.servoCount;

    JsonObject zeph = root["zephyrus"].to<JsonObject>();
#if defined(ZEPHYRUS_ENABLED)
    zeph["enabled"]           = zephyrus.gyroEnabled;
    zeph["calibrated"]        = zephyrus.calibrated;
    zeph["calibrating"]       = zephyrus._calibrating;
    zeph["calib_samples"]     = zephyrus._calibCount;
    zeph["pitch_deg"]         = zephyrus.pitchDeg;
    zeph["roll_deg"]          = zephyrus.rollDeg;
    zeph["yaw_rate"]          = zephyrus.yawRate;
    zeph["roll_correction"]   = zephyrus.rollCorrection;
    zeph["yaw_correction"]    = zephyrus.yawCorrection;
    zeph["pitch_correction"]  = zephyrus.pitchCorrection;
    zeph["rudder_correction"] = zephyrus.rudderCorrection;
    zeph["board_rotation"]    = zephyrus.boardRotation;
#else
    zeph["enabled"]           = false;
    zeph["calibrated"]        = false;
    zeph["calibrating"]       = false;
    zeph["calib_samples"]     = 0;
    zeph["pitch_deg"]         = 0.0f;
    zeph["roll_deg"]          = 0.0f;
    zeph["yaw_rate"]          = 0.0f;
    zeph["roll_correction"]   = 0.0f;
    zeph["yaw_correction"]    = 0.0f;
    zeph["pitch_correction"]  = 0.0f;
    zeph["rudder_correction"] = 0.0f;
    zeph["board_rotation"]    = 0;
#endif

    JsonObject sweep = root["sweep"].to<JsonObject>();
    sweep["active"] = isServoSweepActive();
    sweep["us"]     = getServoSweepUs();

    response->setLength();
    request->send(response);
}

static void GetPteronautosConfig(AsyncWebServerRequest *request)
{
    // Static config — only changes via POST /pteronautos/config. Fetched once
    // per panel mount, not polled. Keeps the 2s state poll lean on the heap.
    auto *response = new AsyncJsonResponse(false);
    JsonObject root = response->getRoot().as<JsonObject>();
    JsonObject orni = root["ornithopter"].to<JsonObject>();

    // Mixer profile is compile-time (MIXER_PROFILE build flag). config.GetModelId()
    // is the ELRS model-match id and has nothing to do with the ornithopter kernel —
    // reporting it as profile_id made the panel show "gearbox" for a servo build.
    uint8_t pid = (uint8_t)ACTIVE_PROFILE;
    orni["profile_id"]          = pid;
    orni["type"]                = PROFILE_IS_GEARBOX ? "gearbox" : "servo";
    orni["kernel_fixed"]        = false;  // profile is runtime-switchable
    orni["model_name"]          = ornithopter.modelName;

    orni["cadence_gain"]        = (int)ornithopter.cadenceGain;
    orni["ferocity_d_gain"]     = (int)ornithopter.ferocityDGain;
    orni["balance_gain"]        = (int)ornithopter.balanceGain;
    orni["ferocity_p_gain"]     = (int)ornithopter.ferocityPGain;
    orni["anchor_gain"]         = (int)ornithopter.anchorGain;
    orni["resonance_gain"]      = (int)ornithopter.resonanceGain;
    orni["ssff_gain"]           = (int)ornithopter.ssffGain;
    orni["aero_glide_coeff"]    = (int)ornithopter.aeroGlideCoeff;
    orni["aero_flap_coeff"]     = (int)ornithopter.aeroFlapCoeff;

    // Runtime waveform/mixer config fields
    orni["stroke_ferocity"]     = (int)ornithopter.strokeFerocity;
    orni["return_ferocity"]     = (int)ornithopter.returnFerocity;
    orni["glide_angle_deg"]     = ornithopter.glideAngleDeg;
    orni["flapping_angle_deg"]  = (int)ornithopter.flappingAngleDeg;
    orni["aileron_scale"]       = (int)ornithopter.aileronScale;
    orni["elevator_scale"]      = (int)ornithopter.elevatorScale;
    orni["rudder_yaw_weight"]   = (int)ornithopter.rudderYawWeight;
    orni["rudder_roll_weight"]  = (int)ornithopter.rudderRollWeight;
    orni["rudder_ferocity_range"] = (int)ornithopter.rudderFerocityRange;
    orni["rudder_amplitude_differential"] = (int)ornithopter.rudderAmplitudeDifferential;
    orni["elevator_ferocity_mix"] = (int)ornithopter.elevatorFerocityMix;
    orni["throttle_ferocity_mix"] = (int)ornithopter.throttleFerocityMix;
    orni["throttle_frequency_mix"] = (int)ornithopter.throttleFrequencyMix;
    orni["ferocity_shape_mix"] = (int)ornithopter.ferocityShapeMix;
    orni["elevon_scale"]        = (int)ornithopter.elevonScale;
    // Kernel-specific pulse params: only emit what the active kernel uses.
    if (PROFILE_IS_GEARBOX)
    {
        orni["motor_min_us"] = ornithopter.motorMinUs;
        orni["motor_max_us"] = ornithopter.motorMaxUs;
    }
    else
    {
        orni["servo_min_us"] = ornithopter.servoMinUs;
        orni["servo_max_us"] = ornithopter.servoMaxUs;
    }
    orni["glide_mode"]          = ornithopter.glideMode;
    orni["hall_sensor_pin"]     = ornithopter.hallSensorPin;
    orni["ratchet_throttle_pct"] = ornithopter.ratchetThrottlePct;
    orni["ratchet_timeout_ms"]  = ornithopter.ratchetTimeoutMs;
    orni["servo_speed"]         = (int)ornithopter.servoSpeed;
    orni["flap_base_freq"]      = (int)ornithopter.flapBaseFreq;
    JsonArray trimArr = orni["servo_trim"].to<JsonArray>();
    for (int i = 0; i < SF_COUNT; ++i) trimArr.add((int)ornithopter.servoTrimUs[i]);

    // Virtual stick override
    orni["stick_override"]      = ornithopter.stickOverride;
    JsonArray stickArr = orni["stick_channels"].to<JsonArray>();
    for (int i = 0; i < STK_COUNT; ++i) stickArr.add(ornithopter.stickChannels[i]);

    // Flight profiles (3 × tuning param sets)
    JsonArray fpArr = orni["flight_profiles"].to<JsonArray>();
    for (int i = 0; i < FLIGHT_PROFILE_COUNT; ++i) {
        JsonObject p = fpArr.createNestedObject();
        p["stroke_ferocity"]       = (int)ornithopter.flightProfiles[i].strokeFerocity;
        p["return_ferocity"]       = (int)ornithopter.flightProfiles[i].returnFerocity;
        p["glide_angle_deg"]       = ornithopter.flightProfiles[i].glideAngleDeg;
        p["flapping_angle_deg"]    = (int)ornithopter.flightProfiles[i].flappingAngleDeg;
        p["aileron_scale"]         = (int)ornithopter.flightProfiles[i].aileronScale;
        p["elevator_scale"]        = (int)ornithopter.flightProfiles[i].elevatorScale;
        p["rudder_ferocity_range"] = (int)ornithopter.flightProfiles[i].rudderFerocityRange;
        p["rudder_amplitude_differential"] = (int)ornithopter.flightProfiles[i].rudderAmplitudeDifferential;
        p["elevator_ferocity_mix"] = (int)ornithopter.flightProfiles[i].elevatorFerocityMix;
        p["throttle_ferocity_mix"] = (int)ornithopter.flightProfiles[i].throttleFerocityMix;
        p["throttle_frequency_mix"] = (int)ornithopter.flightProfiles[i].throttleFrequencyMix;
        p["ferocity_shape_mix"] = (int)ornithopter.flightProfiles[i].ferocityShapeMix;
    }

    response->setLength();
    request->send(response);
}

static void GetPteronautosPing(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", "{\"ok\":true,\"firmware\":\"PteronautOS\"}");
}

// ── Ornithopter Config Endpoint ──────────────────────────────────
// ESPAsyncWebServer auto-parses form-urlencoded POST bodies into
// request->params() (post params). Use hasParam/getParam — a body
// handler is NEVER called for form-urlencoded in this fork, so the
// previous manual-body approach silently received an empty body.
static int _pteroParamInt(AsyncWebServerRequest *request, const char* key, int deflt)
{
    if (request->hasParam(key, true))
        return request->getParam(key, true)->value().toInt();
    return deflt;
}

// ── Runtime config persistence (LittleFS /pteronautos.conf) ──────
// Ornithopter params are RAM-only otherwise; save survives reboot.
static bool _pteroConfigLoaded = false;

// Print a JSON string literal with escaping (used for user-supplied strings
// like the model name, which may contain quotes/backslashes).
static void _pteroPrintJsonString(Print &f, const char *s)
{
    f.print('"');
    if (s)
    {
        for (const char *p = s; *p; ++p)
        {
            char c = *p;
            if ((uint8_t)c < 0x20) {
                const char hex[] = "0123456789abcdef";
                f.print("\\u00");
                f.print(hex[((uint8_t)c) >> 4]);
                f.print(hex[((uint8_t)c) & 15]);
                continue;
            }
            if (c == '"' || c == '\\') f.print('\\');
            f.print(c);
        }
    }
    f.print('"');
}

static bool SaveOrnithopterConfig()
{
    // Stream JSON straight to LittleFS instead of building a heap JsonDocument.
    // The ESP8285 has ~23KB free heap; a save POST that overlaps the 2s state
    // poll used to exhaust it (ArduinoJson v7 auto-grows) and reboot the radio.
    // Direct File::print writes are heap-free — literals live in flash.
    PteroConfigWriter f(LittleFS, "/pteronautos.conf", "/pteronautos.conf.tmp");
    if (!f) return false;

    f.print("{\"flight_profiles\":[");
    for (int i = 0; i < FLIGHT_PROFILE_COUNT; ++i) {
        if (i) f.print(',');
        f.print("{\"stroke_ferocity\":");
        f.print((int)ornithopter.flightProfiles[i].strokeFerocity);
        f.print(",\"return_ferocity\":");
        f.print((int)ornithopter.flightProfiles[i].returnFerocity);
        f.print(",\"glide_angle_deg\":");
        f.print((int)ornithopter.flightProfiles[i].glideAngleDeg);
        f.print(",\"flapping_angle_deg\":");
        f.print((int)ornithopter.flightProfiles[i].flappingAngleDeg);
        f.print(",\"aileron_scale\":");
        f.print((int)ornithopter.flightProfiles[i].aileronScale);
        f.print(",\"elevator_scale\":");
        f.print((int)ornithopter.flightProfiles[i].elevatorScale);
        f.print(",\"rudder_ferocity_range\":");
        f.print((int)ornithopter.flightProfiles[i].rudderFerocityRange);
        f.print(",\"rudder_amplitude_differential\":");
        f.print((int)ornithopter.flightProfiles[i].rudderAmplitudeDifferential);
        f.print(",\"elevator_ferocity_mix\":");
        f.print((int)ornithopter.flightProfiles[i].elevatorFerocityMix);
        f.print(",\"throttle_ferocity_mix\":");
        f.print((int)ornithopter.flightProfiles[i].throttleFerocityMix);
        f.print(",\"throttle_frequency_mix\":");
        f.print((int)ornithopter.flightProfiles[i].throttleFrequencyMix);
        f.print(",\"ferocity_shape_mix\":");
        f.print((int)ornithopter.flightProfiles[i].ferocityShapeMix);
        f.print('}');
    }
    f.print("],\"active_flight_profile\":");
    f.print((int)ornithopter.activeFlightProfile);
    f.print(",\"rudder_yaw_weight\":");
    f.print((int)ornithopter.rudderYawWeight);
    f.print(",\"rudder_roll_weight\":");
    f.print((int)ornithopter.rudderRollWeight);
    f.print(",\"elevon_scale\":");
    f.print((int)ornithopter.elevonScale);
    f.print(",\"motor_min_us\":");
    f.print((int)ornithopter.motorMinUs);
    f.print(",\"motor_max_us\":");
    f.print((int)ornithopter.motorMaxUs);
    f.print(",\"glide_mode\":");
    f.print(ornithopter.glideMode ? "true" : "false");
    f.print(",\"hall_sensor_pin\":");
    f.print((int)ornithopter.hallSensorPin);
    f.print(",\"ratchet_throttle_pct\":");
    f.print((int)ornithopter.ratchetThrottlePct);
    f.print(",\"ratchet_timeout_ms\":");
    f.print((int)ornithopter.ratchetTimeoutMs);
    f.print(",\"servo_speed\":");
    f.print((int)ornithopter.servoSpeed);
    f.print(",\"flap_base_freq\":");
    f.print((int)ornithopter.flapBaseFreq);
    f.print(",\"servo_min_us\":");
    f.print((int)ornithopter.servoMinUs);
    f.print(",\"servo_max_us\":");
    f.print((int)ornithopter.servoMaxUs);
    f.print(",\"servo_trim\":[");
    for (int i = 0; i < SF_COUNT; ++i) {
        if (i) f.print(',');
        f.print((int)ornithopter.servoTrimUs[i]);
    }
    f.print("],\"profile_id\":");
    f.print((int)activeProfile);
    f.print(",\"model_name\":");
    _pteroPrintJsonString(f, ornithopter.modelName);
    f.print('}');
    return f.commit();
}

void LoadOrnithopterConfig()
{
    if (_pteroConfigLoaded) return;
    if (!LittleFS.exists("/pteronautos.conf")) return;
    File f = LittleFS.open("/pteronautos.conf", "r");
    if (!f) return;
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    if (!doc.is<JsonObject>()) return;
    _pteroConfigLoaded = true;
    JsonObject o = doc.as<JsonObject>();

    // Per-flight-profile tuning
    if (o["flight_profiles"].is<JsonArray>()) {
        JsonArray fpArr = o["flight_profiles"].as<JsonArray>();
        for (int i = 0; i < FLIGHT_PROFILE_COUNT && i < (int)fpArr.size(); ++i) {
            JsonObject p = fpArr[i].as<JsonObject>();
            FlightProfileParams &dst = ornithopter.flightProfiles[i];
            if (p["stroke_ferocity"].is<int>())       dst.strokeFerocity      = p["stroke_ferocity"].as<int>();
            if (p["return_ferocity"].is<int>())       dst.returnFerocity      = p["return_ferocity"].as<int>();
            if (p["glide_angle_deg"].is<int>())       dst.glideAngleDeg       = (int8_t)p["glide_angle_deg"].as<int>();
                        if (p["flapping_angle_deg"].is<int>()) {
                            int32_t fa = p["flapping_angle_deg"].as<int>();
                            if (fa < -15) fa = -15;
                            if (fa >  15) fa =  15;
                            dst.flappingAngleDeg = (int8_t)fa;
                        }
            if (p["aileron_scale"].is<int>())         dst.aileronScale        = p["aileron_scale"].as<int>();
            if (p["elevator_scale"].is<int>())        dst.elevatorScale       = p["elevator_scale"].as<int>();
            if (p["rudder_ferocity_range"].is<int>()) dst.rudderFerocityRange = p["rudder_ferocity_range"].as<int>();
            if (p["rudder_amplitude_differential"].is<int>()) dst.rudderAmplitudeDifferential = p["rudder_amplitude_differential"].as<int>();
            if (p["elevator_ferocity_mix"].is<int>()) dst.elevatorFerocityMix = p["elevator_ferocity_mix"].as<int>();
            if (p["throttle_ferocity_mix"].is<int>()) dst.throttleFerocityMix = p["throttle_ferocity_mix"].as<int>();
            if (p["throttle_frequency_mix"].is<int>()) {
                int32_t mix = p["throttle_frequency_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.throttleFrequencyMix = mix;
            }
            if (p["ferocity_shape_mix"].is<int>()) {
                int32_t mix = p["ferocity_shape_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.ferocityShapeMix = mix;
            }
        }
    }

    // Global mixer params
    if (o["rudder_yaw_weight"].is<int>())     ornithopter.rudderYawWeight      = o["rudder_yaw_weight"].as<int>();
    if (o["rudder_roll_weight"].is<int>())    ornithopter.rudderRollWeight     = o["rudder_roll_weight"].as<int>();
    if (o["elevon_scale"].is<int>())          ornithopter.elevonScale          = o["elevon_scale"].as<int>();
    if (o["motor_min_us"].is<int>())          ornithopter.motorMinUs           = (uint16_t)o["motor_min_us"].as<int>();
    if (o["motor_max_us"].is<int>())          ornithopter.motorMaxUs           = (uint16_t)o["motor_max_us"].as<int>();
    if (o["glide_mode"].is<bool>())           ornithopter.glideMode            = o["glide_mode"].as<bool>();
    if (o["hall_sensor_pin"].is<int>())       ornithopter.hallSensorPin        = (uint8_t)o["hall_sensor_pin"].as<int>();
    if (o["ratchet_throttle_pct"].is<int>())  ornithopter.ratchetThrottlePct   = (uint8_t)o["ratchet_throttle_pct"].as<int>();
    if (o["ratchet_timeout_ms"].is<int>())    ornithopter.ratchetTimeoutMs     = (uint16_t)o["ratchet_timeout_ms"].as<int>();
    if (o["servo_speed"].is<int>()) {
        int32_t spd = o["servo_speed"].as<int>();
        if (spd < ORNI_SERVO_SPEED_MIN_MS) spd = ORNI_SERVO_SPEED_MIN_MS;
        if (spd > ORNI_SERVO_SPEED_MAX_MS) spd = ORNI_SERVO_SPEED_MAX_MS;
        ornithopter.servoSpeed = (uint16_t)spd;
    }
    if (o["flap_base_freq"].is<int>()) {
        int32_t bf = o["flap_base_freq"].as<int>();
        if (bf < ORNI_FLAP_BASE_FREQ_MIN_DHZ) bf = ORNI_FLAP_BASE_FREQ_MIN_DHZ;
        if (bf > ORNI_FLAP_BASE_FREQ_MAX_DHZ) bf = ORNI_FLAP_BASE_FREQ_MAX_DHZ;
        ornithopter.flapBaseFreq = (uint16_t)bf;
    }
    if (o["servo_min_us"].is<int>())          ornithopter.servoMinUs           = (uint16_t)o["servo_min_us"].as<int>();
    if (o["servo_max_us"].is<int>())          ornithopter.servoMaxUs           = (uint16_t)o["servo_max_us"].as<int>();
    if (ornithopter.servoMinUs < ORNI_SERVO_ABS_MIN_US) ornithopter.servoMinUs = ORNI_SERVO_ABS_MIN_US;
    if (ornithopter.servoMinUs > 1490) ornithopter.servoMinUs = 1490;
    if (ornithopter.servoMaxUs < 1510) ornithopter.servoMaxUs = 1510;
    if (ornithopter.servoMaxUs > ORNI_SERVO_ABS_MAX_US) ornithopter.servoMaxUs = ORNI_SERVO_ABS_MAX_US;
    if (ornithopter.servoMaxUs <= ornithopter.servoMinUs) ornithopter.servoMaxUs = ornithopter.servoMinUs + 1;
    if (o["servo_trim"].is<JsonArray>()) {
        JsonArray trimArr = o["servo_trim"].as<JsonArray>();
        for (int i = 0; i < SF_COUNT && i < (int)trimArr.size(); ++i) {
            int32_t t = trimArr[i].as<int>();
            if (t < -300) t = -300;
            if (t >  300) t =  300;
            ornithopter.servoTrimUs[i] = (int16_t)t;
        }
    }
    if (o["profile_id"].is<int>())            setOrnithopterProfile((uint8_t)o["profile_id"].as<int>());
    if (o["model_name"].is<const char*>())
        strlcpy(ornithopter.modelName, o["model_name"].as<const char*>(), sizeof(ornithopter.modelName));

    // Restore active flight profile and apply its params to the mixer.
    if (o["active_flight_profile"].is<int>())
        ornithopter.applyFlightProfile((uint8_t)o["active_flight_profile"].as<int>());
    else
        ornithopter.applyFlightProfile(ornithopter.activeFlightProfile);
}

static void PostPteronautosConfig(AsyncWebServerRequest *request)
{
    int fp = -1;
    if (request->hasParam("flight_profile", true)) {
        const String &slot = request->getParam("flight_profile", true)->value();
        if (slot.length() != 1 || slot[0] < '0' || slot[0] >= '0' + FLIGHT_PROFILE_COUNT) {
            request->send(400, "application/json", "{\"ok\":false,\"saved\":false,\"error\":\"Invalid flight profile slot\"}");
            return;
        }
        fp = slot[0] - '0';
    }
    // Mixer profile — runtime-switchable
    int pid = _pteroParamInt(request, "profile_id", -1);
    if (pid >= 0) setOrnithopterProfile((uint8_t)pid);
    // Zephyrus / Ondas gains
#ifdef ZEPHYRUS_ENABLED
    ornithopter.cadenceGain     = (float)_pteroParamInt(request, "cadence_gain",       (int)ornithopter.cadenceGain);
    ornithopter.ferocityDGain   = (float)_pteroParamInt(request, "ferocity_d_gain",    (int)ornithopter.ferocityDGain);
    ornithopter.balanceGain     = (float)_pteroParamInt(request, "balance_gain",       (int)ornithopter.balanceGain);
    ornithopter.ferocityPGain   = (float)_pteroParamInt(request, "ferocity_p_gain",    (int)ornithopter.ferocityPGain);
    ornithopter.anchorGain      = (float)_pteroParamInt(request, "anchor_gain",        (int)ornithopter.anchorGain);
    ornithopter.resonanceGain   = (float)_pteroParamInt(request, "resonance_gain",     (int)ornithopter.resonanceGain);
    ornithopter.ssffGain        = (float)_pteroParamInt(request, "ssff_gain",          (int)ornithopter.ssffGain);
    ornithopter.aeroGlideCoeff  = (float)_pteroParamInt(request, "aero_glide_coeff",   (int)ornithopter.aeroGlideCoeff);
    ornithopter.aeroFlapCoeff   = (float)_pteroParamInt(request, "aero_flap_coeff",    (int)ornithopter.aeroFlapCoeff);
    int gv = _pteroParamInt(request, "gyro_enabled", -1);
    if (gv >= 0) zephyrus.gyroEnabled = (gv == 1);
#endif
    // Missing fields in a partial profile save belong to the requested slot,
    // not whichever profile the transmitter happens to have active.
    const FlightProfileParams &defaults = ornithopter.flightProfiles[fp >= 0 ? fp : ornithopter.activeFlightProfile];
    float   sf    = (float)_pteroParamInt(request, "stroke_ferocity",       (int)defaults.strokeFerocity);
    float   rf    = (float)_pteroParamInt(request, "return_ferocity",       (int)defaults.returnFerocity);
    int8_t  glide = (int8_t)_pteroParamInt(request, "glide_angle_deg",      defaults.glideAngleDeg);
        int8_t  flapAng = (int8_t)_pteroParamInt(request, "flapping_angle_deg",  defaults.flappingAngleDeg);
        if (flapAng < -15) flapAng = -15;
        if (flapAng >  15) flapAng =  15;
    float   ail   = (float)_pteroParamInt(request, "aileron_scale",         (int)defaults.aileronScale);
    float   elev  = (float)_pteroParamInt(request, "elevator_scale",        (int)defaults.elevatorScale);
    float   rudRng= (float)_pteroParamInt(request, "rudder_ferocity_range", (int)defaults.rudderFerocityRange);
    float   rudAmpDiff = (float)_pteroParamInt(request, "rudder_amplitude_differential", (int)defaults.rudderAmplitudeDifferential);
    float   elevFerMix = (float)_pteroParamInt(request, "elevator_ferocity_mix", (int)defaults.elevatorFerocityMix);
    float   thrFerMix  = (float)_pteroParamInt(request, "throttle_ferocity_mix", (int)defaults.throttleFerocityMix);
    float   thrFreqMix = (float)_pteroParamInt(request, "throttle_frequency_mix", (int)defaults.throttleFrequencyMix);
    if (thrFreqMix < 0.0f) thrFreqMix = 0.0f;
    if (thrFreqMix > 100.0f) thrFreqMix = 100.0f;
    float   ferShapeMix = (float)_pteroParamInt(request, "ferocity_shape_mix", (int)defaults.ferocityShapeMix);
    if (ferShapeMix < 0.0f) ferShapeMix = 0.0f;
    if (ferShapeMix > 100.0f) ferShapeMix = 100.0f;

    if (fp >= 0 && fp < FLIGHT_PROFILE_COUNT) {
        // Write to a specific flight-profile slot (and apply live if active).
        ornithopter.setFlightProfileParams((uint8_t)fp, sf, rf, glide, flapAng, ail, elev, rudRng, rudAmpDiff, elevFerMix, thrFerMix, thrFreqMix, ferShapeMix);
    } else {
        // Legacy/global path: apply to live fields + store into active profile.
        ornithopter.strokeFerocity      = sf;
        ornithopter.returnFerocity      = rf;
        ornithopter.glideAngleDeg       = glide;
        ornithopter.flappingAngleDeg    = flapAng;
        ornithopter.aileronScale        = ail;
        ornithopter.elevatorScale       = elev;
        ornithopter.rudderFerocityRange = rudRng;
        ornithopter.rudderAmplitudeDifferential = rudAmpDiff;
        ornithopter.elevatorFerocityMix = elevFerMix;
        ornithopter.throttleFerocityMix = thrFerMix;
        ornithopter.throttleFrequencyMix = thrFreqMix;
        ornithopter.ferocityShapeMix = ferShapeMix;
        ornithopter.setFlightProfileParams(ornithopter.activeFlightProfile, sf, rf, glide, flapAng, ail, elev, rudRng, rudAmpDiff, elevFerMix, thrFerMix, thrFreqMix, ferShapeMix);
    }

    // Global mixer params (not per-profile)
    ornithopter.rudderYawWeight    = (float)_pteroParamInt(request, "rudder_yaw_weight",  (int)ornithopter.rudderYawWeight);
    ornithopter.rudderRollWeight   = (float)_pteroParamInt(request, "rudder_roll_weight", (int)ornithopter.rudderRollWeight);
    ornithopter.elevonScale        = (float)_pteroParamInt(request, "elevon_scale",       (int)ornithopter.elevonScale);
    ornithopter.motorMinUs         = (uint16_t)_pteroParamInt(request, "motor_min_us",    ornithopter.motorMinUs);
    ornithopter.motorMaxUs         = (uint16_t)_pteroParamInt(request, "motor_max_us",    ornithopter.motorMaxUs);
    ornithopter.glideMode          = _pteroParamInt(request, "glide_mode", ornithopter.glideMode ? 1 : 0) != 0;
    ornithopter.hallSensorPin      = (uint8_t)_pteroParamInt(request, "hall_sensor_pin",  ornithopter.hallSensorPin);
    ornithopter.ratchetThrottlePct = (uint8_t)_pteroParamInt(request, "ratchet_throttle_pct", ornithopter.ratchetThrottlePct);
    ornithopter.ratchetTimeoutMs   = (uint16_t)_pteroParamInt(request, "ratchet_timeout_ms", ornithopter.ratchetTimeoutMs);
    {
        int32_t spd = _pteroParamInt(request, "servo_speed", ornithopter.servoSpeed);
        if (spd < ORNI_SERVO_SPEED_MIN_MS) spd = ORNI_SERVO_SPEED_MIN_MS;
        if (spd > ORNI_SERVO_SPEED_MAX_MS) spd = ORNI_SERVO_SPEED_MAX_MS;
        ornithopter.servoSpeed = (uint16_t)spd;
        int32_t bf = _pteroParamInt(request, "flap_base_freq", ornithopter.flapBaseFreq);
        if (bf < ORNI_FLAP_BASE_FREQ_MIN_DHZ) bf = ORNI_FLAP_BASE_FREQ_MIN_DHZ;
        if (bf > ORNI_FLAP_BASE_FREQ_MAX_DHZ) bf = ORNI_FLAP_BASE_FREQ_MAX_DHZ;
        ornithopter.flapBaseFreq = (uint16_t)bf;
    }

    // Servo sweep pulse limits (kernel-level, wing 0°..180° map)
    {
        int32_t mn = _pteroParamInt(request, "servo_min_us", ornithopter.servoMinUs);
        int32_t mx = _pteroParamInt(request, "servo_max_us", ornithopter.servoMaxUs);
        if (mn < ORNI_SERVO_ABS_MIN_US) mn = ORNI_SERVO_ABS_MIN_US;
        if (mn > 1490) mn = 1490;
        if (mx < 1510) mx = 1510;
        if (mx > ORNI_SERVO_ABS_MAX_US) mx = ORNI_SERVO_ABS_MAX_US;
        if (mx <= mn) mx = mn + 1;
        ornithopter.servoMinUs = (uint16_t)mn;
        ornithopter.servoMaxUs = (uint16_t)mx;
    }

    // Per-servo correction (trim), signed µs, keyed by ServoFunc index.
    for (int i = 0; i < SF_COUNT; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "servo_trim_%d", i);
        int32_t t = _pteroParamInt(request, key, ornithopter.servoTrimUs[i]);
        if (t < -300) t = -300;
        if (t >  300) t =  300;
        ornithopter.servoTrimUs[i] = (int16_t)t;
    }

    // User-facing model identifier (shown in Overview, included in backup).
    if (request->hasParam("model_name", true)) {
        String mn = request->getParam("model_name", true)->value();
        mn.trim();
        if (mn.length() >= sizeof(ornithopter.modelName))
            mn = mn.substring(0, sizeof(ornithopter.modelName) - 1);
        strlcpy(ornithopter.modelName, mn.c_str(), sizeof(ornithopter.modelName));
    }

    if (!SaveOrnithopterConfig()) {
        request->send(500, "application/json", "{\"ok\":false,\"saved\":false,\"error\":\"Settings applied in RAM, but saving to flash failed. Previous saved configuration retained; retry saving.\"}");
        return;
    }
    request->send(200, "application/json", "{\"ok\":true,\"saved\":true}");
}

// ── Servo Sweep Endpoints ────────────────────────────────────────
static void PostPteronautosSweep(AsyncWebServerRequest *request)
{
    bool start = false;
    if (request->hasParam("state", true))
        start = (request->getParam("state", true)->value() == "1");
    if (start)
        startServoSweep();
    else
        stopServoSweep();
    request->send(200, "application/json", "{\"ok\":true}");
}

static void GetPteronautosSweepStatus(AsyncWebServerRequest *request)
{
    char sbuf[64];
    snprintf(sbuf, sizeof(sbuf), "{\"active\":%u,\"us\":%u}",
        isServoSweepActive(), getServoSweepUs());
    request->send(200, "application/json", String(sbuf));
}

// ── Full Config Backup / Restore ────────────────────────────────
// GET  /pteronautos/backup — one JSON {meta, options, config, pteronautos}
// POST /pteronautos/backup — restore that same shape, then reboot.
static void GetPteronautosBackup(AsyncWebServerRequest *request)
{
    // RxConfig portion (EEPROM) — exactly the fields UpdateConfiguration consumes.
    JsonDocument cfgDoc;
    JsonObject cfg = cfgDoc.to<JsonObject>();
#if defined(TARGET_RX)
    cfg["serial-protocol"] = config.GetSerialProtocol();
    cfg["sbus-failsafe"]  = config.GetFailsafeMode();
    cfg["modelid"]        = config.GetModelId();
    cfg["force-tlm"]      = config.GetForceTlmOff();
    cfg["vbind"]          = config.GetBindStorage();
    JsonArray uid = cfg["uid"].to<JsonArray>();
    copyArray(config.GetUID(), UID_LEN, uid);
    JsonArray pwm = cfg["pwm"].to<JsonArray>();
    for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
        pwm.add(config.GetPwmChannel(ch)->raw);
#endif
    String cfgStr;
    serializeJson(cfg, cfgStr);

    // Ornithopter portion — raw /pteronautos.conf is already valid JSON.
    String pteroStr = "{}";
    if (LittleFS.exists("/pteronautos.conf")) {
        File f = LittleFS.open("/pteronautos.conf", "r");
        if (f) {
            pteroStr = f.readString();
            f.close();
        }
    }

    String out;
    out.reserve(160 + cfgStr.length() + pteroStr.length() + getOptions().length());
    out += "{\"meta\":{\"type\":\"pteronautos\",\"version\":1,\"target\":\"";
    out += (const char *)&target_name[4];
    out += "\",\"firmware\":\"";
    out += VERSION;
    out += "\",\"model_name\":";
    out += '"';
    for (const char *p = ornithopter.modelName; *p; ++p)
    {
        char c = *p;
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    out += "},\"options\":";
    out += getOptions();
    out += ",\"config\":";
    out += cfgStr;
    out += ",\"pteronautos\":";
    out += pteroStr;
    out += "}";

    request->send(200, "application/json", out);
}

static void PostPteronautosBackup(AsyncWebServerRequest *request, JsonVariant &json)
{
    // 1) firmware options → /options.json (+ rebuild cached options string).
    if (json["options"].is<JsonObject>()) {
        JsonObject opts = json["options"].as<JsonObject>();
        // Keep this device's current discriminator so the restored file is
        // honoured on next boot (options_LoadFromFlashOrFile compares it).
        opts["flash-discriminator"] = firmwareOptions.flash_discriminator;
        File f = LittleFS.open("/options.json", "w");
        if (f) {
            serializeJson(opts, f);
            f.close();
        }
        String s;
        serializeJson(opts, s);
        setOptions(s);
    }

    // 2) RxConfig → EEPROM.
#if defined(TARGET_RX)
    if (json["config"].is<JsonObject>()) {
        JsonObject cfg = json["config"].as<JsonObject>();
        if (cfg["serial-protocol"].is<int>()) config.SetSerialProtocol((eSerialProtocol)cfg["serial-protocol"].as<int>());
        if (cfg["sbus-failsafe"].is<int>())  config.SetFailsafeMode((eFailsafeMode)cfg["sbus-failsafe"].as<int>());
        long modelid = cfg["modelid"] | 255;
        if (modelid < 0 || modelid > 63) modelid = 255;
        config.SetModelId((uint8_t)modelid);
        config.SetForceTlmOff((cfg["force-tlm"] | false) != 0);
        config.SetBindStorage((rx_config_bindstorage_t)(cfg["vbind"] | 0));

        JsonArray uid = cfg["uid"].as<JsonArray>();
        if (!uid.isNull()) {
            uint8_t newUid[UID_LEN] = { 0 };
            size_t juidLen = constrain(uid.size(), 0, UID_LEN);
            copyArray(uid, &newUid[UID_LEN - juidLen], juidLen);
            config.SetUID(newUid);
            memcpy(UID, newUid, UID_LEN);
        }

        JsonArray pwm = cfg["pwm"].as<JsonArray>();
        if (!pwm.isNull()) {
            for (uint32_t ch = 0; ch < pwm.size(); ch++) {
                rx_config_pwm_t pwmChannel;
                pwmChannel.raw = pwm[ch];
                if (OPT_PWM_OUT_ONLY &&
                    (pwmChannel.val.mode == somSerial || pwmChannel.val.mode == somSCL || pwmChannel.val.mode == somSDA ||
                     pwmChannel.val.mode == somSerial1RX || pwmChannel.val.mode == somSerial1TX))
                {
                    pwmChannel.val.mode = som50Hz;
                }
                config.SetPwmChannelRaw(ch, pwmChannel.raw);
            }
        }
        config.Commit();
    }
#endif

    // 3) Ornithopter config → /pteronautos.conf + live reload.
    if (json["pteronautos"].is<JsonObject>()) {
        PteroConfigWriter f(LittleFS, "/pteronautos.conf", "/pteronautos.conf.tmp");
        serializeJson(json["pteronautos"], f);
        if (!f.commit()) {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"Receiver settings may have changed, but the PteronautOS configuration could not be saved. Import incomplete; retry.\"}");
            return;
        }
        _pteroConfigLoaded = false;
        LoadOrnithopterConfig();
    }

    request->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    scheduleRebootTime(500);
}

// ── Virtual Stick Endpoint ───────────────────────────────────────
// POST /pteronautos/stick  — form-urlencoded: override=1&ch0=1500&ch1=1500&...
// GET  /pteronautos/stick  — returns JSON with override status + channel values

static void PostPteronautosStick(AsyncWebServerRequest *request)
{
    // Form-urlencoded POST bodies are auto-parsed into post params
    // (body handlers are never invoked for form-urlencoded in this fork).
    if (request->hasParam("override", true))
        ornithopter.setStickOverride(request->getParam("override", true)->value() == "1");

    for (int ch = 0; ch < STK_COUNT; ++ch)
    {
        char key[5];
        snprintf(key, sizeof(key), "ch%d", ch);
        if (request->hasParam(key, true))
        {
            uint16_t val = (uint16_t)request->getParam(key, true)->value().toInt();
            if (val >= 172 && val <= 1811)
                ornithopter.setStickChannel((uint8_t)ch, val);
        }
    }

    // Minimal static response — the panel fires this fetch without reading the
    // body. Building a snprintf + heap String per POST fragmented the ESP8285
    // heap; under the 2s state poll this exhausted it and rebooted the radio.
    request->send(200, "application/json", "{\"ok\":1}");
}

static void GetPteronautosStick(AsyncWebServerRequest *request)
{
    PostPteronautosStick(request); // same response
}

#if defined(TARGET_TX)
static void UpdateConfiguration(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (json["button-actions"].is<JsonVariant>()) {
    const JsonArray &array = json["button-actions"].as<JsonArray>();
    for (size_t button=0 ; button<array.size() ; button++)
    {
      tx_button_color_t action;
      for (int pos=0 ; pos<button_GetActionCnt() ; pos++)
      {
        action.val.actions[pos].pressType = array[button]["action"][pos]["is-long-press"];
        action.val.actions[pos].count = array[button]["action"][pos]["count"];
        action.val.actions[pos].action = array[button]["action"][pos]["action"];
      }
      action.val.color = array[button]["color"];
      config.SetButtonActions(button, &action);
    }
  }
  config.Commit();
  request->send(200, "text/plain", "Import/update complete");
}

static void ImportConfiguration(AsyncWebServerRequest *request, JsonVariant &json)
{
  if (json["config"].is<JsonVariant>())
  {
    json = json["config"];
  }

  if (json["fan-mode"].is<JsonVariant>()) config.SetFanMode(json["fan-mode"]);
  if (json["power-fan-threshold"].is<JsonVariant>()) config.SetPowerFanThreshold(json["power-fan-threshold"]);
  if (json["motion-mode"].is<JsonVariant>()) config.SetMotionMode(json["motion-mode"]);

  if (json["vtx-admin"].is<JsonObject>())
  {
    const auto vtxAdmin = json["vtx-admin"].as<JsonObject>();
    if (vtxAdmin["band"].is<JsonVariant>()) config.SetVtxBand(vtxAdmin["band"]);
    if (vtxAdmin["channel"].is<JsonVariant>()) config.SetVtxChannel(vtxAdmin["channel"]);
    if (vtxAdmin["pitmode"].is<JsonVariant>()) config.SetVtxPitmode(vtxAdmin["pitmode"]);
    if (vtxAdmin["power"].is<JsonVariant>()) config.SetVtxPower(vtxAdmin["power"]);
  }

  if (json["backpack"].is<JsonVariant>())
  {
    const auto backpack = json["backpack"].as<JsonObject>();
    if (backpack["disabled"].is<JsonVariant>()) config.SetBackpackDisable(backpack["disabled"]);
    if (backpack["dvr-start-delay"].is<JsonVariant>()) config.SetDvrStartDelay(backpack["dvr-start-delay"]);
    if (backpack["dvr-stop-delay"].is<JsonVariant>()) config.SetDvrStopDelay(backpack["dvr-stop-delay"]);
    if (backpack["dvr-aux-channel"].is<JsonVariant>()) config.SetDvrAux(backpack["dvr-aux-channel"]);
    if (backpack["telemetry-mode"].is<JsonVariant>()) config.SetBackpackTlmMode(backpack["telemetry-mode"]);
  }

  if (json["model"].is<JsonVariant>())
  {
    for(JsonPair kv : json["model"].as<JsonObject>())
    {
      const uint8_t model = atoi(kv.key().c_str());
      const auto modelJson = kv.value().as<JsonObject>();

      config.SetModelId(model);
      if (modelJson["packet-rate"].is<JsonVariant>()) config.SetRate(modelJson["packet-rate"]);
      if (modelJson["telemetry-ratio"].is<JsonVariant>()) config.SetTlm(modelJson["telemetry-ratio"]);
      if (modelJson["switch-mode"].is<JsonVariant>()) config.SetSwitchMode(modelJson["switch-mode"]);
      if (modelJson["link-mode"].is<JsonVariant>()) config.SetLinkMode(modelJson["link-mode"]);
      if (modelJson["model-match"].is<JsonVariant>()) config.SetModelMatch(modelJson["model-match"]);
      if (modelJson["tx-antenna"].is<JsonVariant>()) config.SetAntennaMode(modelJson["tx-antenna"]);
      if (modelJson["ptr-start-chan"].is<JsonVariant>()) config.SetPTRStartChannel(modelJson["ptr-start-chan"]);
      if (modelJson["ptr-enable-chan"].is<JsonVariant>()) config.SetPTREnableChannel(modelJson["ptr-enable-chan"]);
      if (modelJson["power"].is<JsonVariant>())
      {
        if (modelJson["power"]["max-power"].is<JsonVariant>()) config.SetPower(modelJson["power"]["max-power"]);
        if (modelJson["power"]["dynamic-power"].is<JsonVariant>()) config.SetDynamicPower(modelJson["power"]["dynamic-power"]);
        if (modelJson["power"]["boost-channel"].is<JsonVariant>()) config.SetBoostChannel(modelJson["power"]["boost-channel"]);
      }
      // have to commit after each model is updated
      config.Commit();
    }
  }

  UpdateConfiguration(request, json);
}

static void WebUpdateButtonColors(AsyncWebServerRequest *request, JsonVariant &json)
{
  int button1Color = json[0].as<int>();
  int button2Color = json[1].as<int>();
  DBGLN("%d %d", button1Color, button2Color);
  setButtonColors(button1Color, button2Color);
  request->send(200);
}
#else
/**
 * @brief: Copy uid to config if changed
*/
static void JsonUidToConfig(JsonVariant &json)
{
  const auto juid = json["uid"].as<JsonArray>();
  size_t juidLen = constrain(juid.size(), 0, UID_LEN);
  uint8_t newUid[UID_LEN] = { 0 };

  // Copy only as many bytes as were included, right-justified
  // This supports 6-digit UID as well as 4-digit (OTA bound) UID
  copyArray(juid, &newUid[UID_LEN-juidLen], juidLen);

  if (memcmp(newUid, config.GetUID(), UID_LEN) != 0)
  {
    config.SetUID(newUid);
    config.Commit();
    // Also copy it to the global UID in case the page is reloaded
    memcpy(UID, newUid, UID_LEN);
  }
}
static void UpdateConfiguration(AsyncWebServerRequest *request, JsonVariant &json)
{
  uint8_t protocol = json["serial-protocol"] | 0;
  config.SetSerialProtocol((eSerialProtocol)protocol);

#if defined(PLATFORM_ESP32)
  uint8_t protocol1 = json["serial1-protocol"] | 0;
  config.SetSerial1Protocol((eSerial1Protocol)protocol1);
#endif

  uint8_t failsafe = json["sbus-failsafe"] | 0;
  config.SetFailsafeMode((eFailsafeMode)failsafe);

  long modelid = json["modelid"] | 255;
  if (modelid < 0 || modelid > 63) modelid = 255;
  config.SetModelId((uint8_t)modelid);

  long forceTlm = json["force-tlm"] | false;
  config.SetForceTlmOff(forceTlm != 0);

  config.SetBindStorage((rx_config_bindstorage_t)(json["vbind"] | 0));
  JsonUidToConfig(json);

  JsonArray pwm = json["pwm"].as<JsonArray>();
  for(uint32_t channel = 0 ; channel < pwm.size() ; channel++)
  {
    rx_config_pwm_t pwmChannel;
    pwmChannel.raw = pwm[channel];
    if (OPT_PWM_OUT_ONLY &&
        (pwmChannel.val.mode == somSerial || pwmChannel.val.mode == somSCL || pwmChannel.val.mode == somSDA ||
         pwmChannel.val.mode == somSerial1RX || pwmChannel.val.mode == somSerial1TX))
    {
      pwmChannel.val.mode = som50Hz;
    }
    //DBGLN("PWMch(%u)=%u", channel, val);
    config.SetPwmChannelRaw(channel, pwmChannel.raw);
  }

  config.Commit();
  request->send(200, "text/plain", "Configuration updated");
}
#endif

static void WebUpdateSendNetworks(AsyncWebServerRequest *request)
{
  int numNetworks = WiFi.scanComplete();
  if (numNetworks >= 0 && millis() - lastScanTimeMS < STALE_WIFI_SCAN) {
    DBGLN("Found %d networks", numNetworks);
    std::set<String> vs;
    String s="[";
    for(int i=0 ; i<numNetworks ; i++) {
      String w = WiFi.SSID(i);
      DBGLN("found %s", w.c_str());
      if (vs.find(w)==vs.end() && w.length()>0) {
        if (!vs.empty()) s += ",";
        s += "\"" + w + "\"";
        vs.insert(w);
      }
    }
    s+="]";
    request->send(200, "application/json", s);
  } else {
    if (WiFi.scanComplete() != WIFI_SCAN_RUNNING)
    {
      #if defined(PLATFORM_ESP8266)
      scanComplete = false;
      WiFi.scanNetworksAsync([](int){
        scanComplete = true;
      });
      #else
      WiFi.scanNetworks(true);
      #endif
      lastScanTimeMS = millis();
    }
    request->send(204, "application/json", "[]");
  }
}

static void sendResponse(AsyncWebServerRequest *request, const String &msg, WiFiMode_t mode) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", msg);
  response->addHeader("Connection", "close");
  request->send(response);
  changeTime = millis();
  changeMode = mode;
}

static void WebUpdateAccessPoint(AsyncWebServerRequest *request)
{
  DBGLN("Starting Access Point");
  String msg = String("Access Point starting, please connect to access point '") + wifi_ap_ssid + "' with password '" + wifi_ap_password + "'";
  sendResponse(request, msg, WIFI_AP);
}

static void WebUpdateConnect(AsyncWebServerRequest *request)
{
  DBGLN("Connecting to network");
  String msg = String("Connecting to network '") + station_ssid + "', connect to http://" +
    wifi_hostname + ".local from a browser on that network";
  sendResponse(request, msg, WIFI_STA);
}

static void WebUpdateSetHome(AsyncWebServerRequest *request)
{
  String ssid = request->arg("network");
  String password = request->arg("password");
  String onInterval = request->arg("wifi-on-interval");

  DBGLN("Setting network %s", ssid.c_str());
  strcpy(station_ssid, ssid.c_str());
  strcpy(station_password, password.c_str());
  if (request->hasArg("save")) {
    strlcpy(firmwareOptions.home_wifi_ssid, ssid.c_str(), sizeof(firmwareOptions.home_wifi_ssid));
    strlcpy(firmwareOptions.home_wifi_password, password.c_str(), sizeof(firmwareOptions.home_wifi_password));
    firmwareOptions.wifi_auto_on_interval = (onInterval.isEmpty() ? -1 : onInterval.toInt()) * 1000;
    saveOptions();
  }
  WebUpdateConnect(request);
}

static void WebUpdateForget(AsyncWebServerRequest *request)
{
  DBGLN("Forget network");
  String onInterval = request->arg("wifi-on-interval");
  firmwareOptions.home_wifi_ssid[0] = 0;
  firmwareOptions.home_wifi_password[0] = 0;
  firmwareOptions.wifi_auto_on_interval = (onInterval.isEmpty() ? -1 : onInterval.toInt()) * 1000;
  saveOptions();
  station_ssid[0] = 0;
  station_password[0] = 0;
  String msg = String("Home network forgotten, please connect to access point '") + wifi_ap_ssid + "' with password '" + wifi_ap_password + "'";
  sendResponse(request, msg, WIFI_AP);
}

static void WebUpdateHandleNotFound(AsyncWebServerRequest *request)
{
  if (captivePortal(request))
  { // If captive portal redirect instead of displaying the error page.
    return;
  }
  String message = F("File Not Found\n\n");
  message += F("URI: ");
  message += request->url();
  message += F("\nMethod: ");
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += request->args();
  message += F("\n");

  for (uint8_t i = 0; i < request->args(); i++)
  {
    message += String(F(" ")) + request->argName(i) + F(": ") + request->arg(i) + F("\n");
  }
  AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", message);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "-1");
  request->send(response);
}

static void corsPreflightResponse(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(204, "text/plain");
  request->send(response);
}

static void WebUploadResponseHandler(AsyncWebServerRequest *request) {
  if (target_seen || Update.hasError()) {
    String msg;
    if (!Update.hasError() && Update.end()) {
      DBGLN("Update complete, rebooting");
      msg = String("{\"status\": \"ok\", \"msg\": \"Update complete. ");
      #if defined(TARGET_RX)
        msg += "Please wait for the LED to resume blinking before disconnecting power.\"}";
      #else
        msg += "Please wait for a few seconds while the device reboots.\"}";
      #endif
      scheduleRebootTime(200);
    } else {
      StreamString p = StreamString();
      if (Update.hasError()) {
        Update.printError(p);
      } else {
        p.println("Not enough data uploaded!");
      }
      p.trim();
      DBGLN("Failed to upload firmware: %s", p.c_str());
      msg = String("{\"status\": \"error\", \"msg\": \"") + p + "\"}";
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", msg);
    response->addHeader("Connection", "close");
    request->send(response);
  } else {
    String message = String("{\"status\": \"mismatch\", \"msg\": \"<b>Current target:</b> ") + (const char *)&target_name[4] + ".<br>";
    if (target_found.length() != 0) {
      message += "<b>Uploaded image:</b> " + target_found + ".<br/>";
    }
    message += "<br/>It looks like you are flashing firmware with a different name to the current  firmware.  This sometimes happens because the hardware was flashed from the factory with an early version that has a different name. Or it may have even changed between major releases.";
    message += "<br/><br/>Please double check you are uploading the correct target, then proceed with 'Flash Anyway'.\"}";
    request->send(200, "application/json", message);
  }
}

static void WebUploadDataHandler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  force_update = force_update || request->hasArg("force");
  if (index == 0) {
    #if defined(TARGET_TX) && defined(PLATFORM_ESP32)
      WifiJoystick::StopJoystickService();
    #endif

    size_t filesize = request->header("X-FileSize").toInt();
    DBGLN("Update: '%s' size %u", filename.c_str(), filesize);
    #if defined(PLATFORM_ESP8266)
    Update.runAsync(true);
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    DBGLN("Free space = %u", maxSketchSpace);
    UNUSED(maxSketchSpace); // for warning
    #endif
    if (!Update.begin(filesize, U_FLASH)) { // pass the size provided
      Update.printError(LOGGING_UART);
    }
    target_seen = false;
    target_found.clear();
    target_complete = false;
    target_pos = 0;
    totalSize = 0;
  }
  if (len) {
    DBGVLN("writing %d", len);
    if (Update.write(data, len) == len) {
      if (force_update || (totalSize == 0 && *data == 0x1F))
        target_seen = true;
      if (!target_seen) {
        for (size_t i=0 ; i<len ;i++) {
          if (!target_complete && (target_pos >= 4 || target_found.length() > 0)) {
            if (target_pos == 4) {
              target_found.clear();
            }
            if (data[i] == 0 || target_found.length() > 50) {
              target_complete = true;
            }
            else {
              target_found += (char)data[i];
            }
          }
          if (data[i] == target_name[target_pos]) {
            ++target_pos;
            if (target_pos >= target_name_size) {
              target_seen = true;
            }
          }
          else {
            target_pos = 0; // Startover
          }
        }
      }
      totalSize += len;
    } else {
      DBGLN("write failed to write %d", len);
    }
  }
}

static void WebUploadForceUpdateHandler(AsyncWebServerRequest *request) {
  target_seen = true;
  if (request->arg("action").equals("confirm")) {
    WebUploadResponseHandler(request);
  } else {
    #if defined(PLATFORM_ESP32)
      Update.abort();
    #endif
    request->send(200, "application/json", "{\"status\": \"ok\", \"msg\": \"Update cancelled\"}");
  }
}

#if defined(TARGET_TX) && defined(PLATFORM_ESP32)
static void WebUdpControl(AsyncWebServerRequest *request)
{
  const String &action = request->arg("action");
  if (action.equals("joystick_begin"))
  {
    WifiJoystick::StartSending(request->client()->remoteIP(),
      request->arg("interval").toInt(), request->arg("channels").toInt());
    request->send(200, "text/plain", "ok");
  }
  else if (action.equals("joystick_end"))
  {
    WifiJoystick::StopSending();
    request->send(200, "text/plain", "ok");
  }
}
#endif

static size_t firmwareOffset = 0;
static size_t getFirmwareChunk(uint8_t *data, size_t len, size_t pos)
{
  uint8_t *dst;
  uint8_t alignedBuffer[7];
  if ((uintptr_t)data % 4 != 0)
  {
    // If data is not aligned, read aligned byes using the local buffer and hope the next call will be aligned
    dst = (uint8_t *)((uint32_t)alignedBuffer / 4 * 4);
    len = 4;
  }
  else
  {
    // Otherwise just make sure len is a multiple of 4 and smaller than a sector
    dst = data;
    len = constrain((len / 4) * 4, 4, SPI_FLASH_SEC_SIZE);
  }

  ESP.flashRead(firmwareOffset + pos, (uint32_t *)dst, len);

  // If using local stack buffer, move the 4 bytes into the passed buffer
  // data is known to not be aligned so it is moved byte-by-byte instead of as uint32_t*
  if ((void *)dst != (void *)data)
  {
    for (unsigned b=len; b>0; --b)
      *data++ = *dst++;
  }
  return len;
}

static void WebUpdateGetFirmware(AsyncWebServerRequest *request) {
  #if defined(PLATFORM_ESP32)
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) {
      firmwareOffset = running->address;
  }
  #endif
  const size_t firmwareTrailerSize = 4096;  // max number of bytes for the options/hardware layout json
  AsyncWebServerResponse *response = request->beginResponse("application/octet-stream", (size_t)ESP.getSketchSize() + firmwareTrailerSize, &getFirmwareChunk);
  String filename = String("attachment; filename=\"") + (const char *)&target_name[4] + "_" + VERSION + ".bin\"";
  response->addHeader("Content-Disposition", filename);
  request->send(response);
}

static void HandleContinuousWave(AsyncWebServerRequest *request) {
  if (request->hasArg("radio")) {
    SX12XX_Radio_Number_t radio = request->arg("radio").toInt() == 1 ? SX12XX_Radio_1 : SX12XX_Radio_2;

#if defined(RADIO_LR1121)
    bool setSubGHz = false;
    setSubGHz = request->arg("subGHz").toInt() == 1;
#endif

    AsyncWebServerResponse *response = request->beginResponse(204);
    response->addHeader("Connection", "close");
    request->send(response);

    Radio.TXdoneCallback = [](){};
    Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq());

    POWERMGNT::init();
    POWERMGNT::setPower(POWERMGNT::getMinPower());

#if defined(RADIO_LR1121)
    Radio.startCWTest(setSubGHz ? FHSSconfig->freq_center : FHSSconfigDualBand->freq_center, radio);
#else
    Radio.startCWTest(FHSSconfig->freq_center, radio);
#if defined(RADIO_SX127X)
    deferExecutionMillis(50, [radio](){ Radio.cwRepeat(radio); });
#endif
#endif
  } else {
    int radios = (GPIO_PIN_NSS_2 == UNDEF_PIN) ? 1 : 2;
    request->send(200, "application/json", String("{\"radios\": ") + radios + ", \"center\": "+ FHSSconfig->freq_center +
#if defined(RADIO_LR1121)
            ", \"center2\": "+ FHSSconfigDualBand->freq_center +
#endif
            "}");
  }
}

static bool initialize()
{
  wifiStarted = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  #if defined(PLATFORM_ESP8266)
  WiFi.forceSleepBegin();
  #endif
  registerButtonFunction(ACTION_START_WIFI, [](){
    setWifiUpdateMode();
  });
  return true;
}

static void startWiFi(unsigned long now)
{
  if (wifiStarted) {
    return;
  }

  if (connectionState < FAILURE_STATES) {
    hwTimer::stop();
#if defined(TARGET_RX) && defined(PLATFORM_ESP32)
    disableVTxSpi();
#endif

    // Set transmit power to minimum
    POWERMGNT::setPower(MinPower);

    setWifiUpdateMode();

    DBGLN("Stopping Radio");
    Radio.End();
  }

  DBGLN("Begin Webupdater");

  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  strcpy(station_ssid, firmwareOptions.home_wifi_ssid);
  strcpy(station_password, firmwareOptions.home_wifi_password);
  if (station_ssid[0] == 0) {
    changeTime = now;
    changeMode = WIFI_AP;
  }
  else {
    changeTime = now;
    changeMode = WIFI_STA;
  }
  laststatus = WL_DISCONNECTED;
  wifiStarted = true;
}

static void startMDNS()
{
  if (!MDNS.begin(wifi_hostname))
  {
    DBGLN("Error starting mDNS");
    return;
  }

  String options = "-DAUTO_WIFI_ON_INTERVAL=" + (firmwareOptions.wifi_auto_on_interval == -1 ? "-1" : String(firmwareOptions.wifi_auto_on_interval / 1000));

  #if defined(TARGET_TX)
  if (firmwareOptions.unlock_higher_power)
  {
    options += " -DUNLOCK_HIGHER_POWER";
  }
  options += " -DTLM_REPORT_INTERVAL_MS=" + String(firmwareOptions.tlm_report_interval);
  options += " -DFAN_MIN_RUNTIME=" + String(firmwareOptions.fan_min_runtime);
  #endif
server.addHandler(new AsyncCallbackJsonWebHandler("/options.json", UpdateSettings));
{
  auto *backupHandler = new AsyncCallbackJsonWebHandler("/pteronautos/backup", PostPteronautosBackup);
  backupHandler->setMaxContentLength(16384);
  server.addHandler(backupHandler);
}
#if defined(TARGET_RX)
  if (firmwareOptions.lock_on_first_connection)
  {
    options += " -DLOCK_ON_FIRST_CONNECTION";
  }
  options += " -DRCVR_UART_BAUD=" + String(firmwareOptions.uart_baud);
  #endif

  String instance = String(wifi_hostname) + "_" + WiFi.macAddress();
  instance.replace(":", "");
  #if defined(PLATFORM_ESP8266)
    // We have to do it differently on ESP8266 as setInstanceName has the side-effect of chainging the hostname!
    MDNS.setInstanceName(wifi_hostname);
    MDNSResponder::hMDNSService service = MDNS.addService(instance.c_str(), "http", "tcp", 80);
    MDNS.addServiceTxt(service, "vendor", "elrs");
    MDNS.addServiceTxt(service, "target", (const char *)&target_name[4]);
    MDNS.addServiceTxt(service, "device", (const char *)device_name);
    MDNS.addServiceTxt(service, "product", (const char *)product_name);
    MDNS.addServiceTxt(service, "version", VERSION);
    MDNS.addServiceTxt(service, "options", options.c_str());
    MDNS.addServiceTxt(service, "type", "rx");
    // If the probe result fails because there is another device on the network with the same name
    // use our unique instance name as the hostname. A better way to do this would be to use
    // MDNSResponder::indexDomain and change wifi_hostname as well.
    MDNS.setHostProbeResultCallback([instance](const char* p_pcDomainName, bool p_bProbeResult) {
      if (!p_bProbeResult) {
        WiFi.hostname(instance);
        MDNS.setInstanceName(instance);
      }
    });
  #else
    MDNS.setInstanceName(instance);
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "vendor", "elrs");
    MDNS.addServiceTxt("http", "tcp", "target", (const char *)&target_name[4]);
    MDNS.addServiceTxt("http", "tcp", "device", (const char *)device_name);
    MDNS.addServiceTxt("http", "tcp", "product", (const char *)product_name);
    MDNS.addServiceTxt("http", "tcp", "version", VERSION);
    MDNS.addServiceTxt("http", "tcp", "options", options.c_str());
  #if defined(TARGET_TX)
    MDNS.addServiceTxt("http", "tcp", "type", "tx");
  #else
    MDNS.addServiceTxt("http", "tcp", "type", "rx");
  #endif
  #endif

  #if defined(TARGET_TX) && defined(PLATFORM_ESP32)
    MDNS.addService("elrs", "udp", JOYSTICK_PORT);
    MDNS.addServiceTxt("elrs", "udp", "device", (const char *)device_name);
    MDNS.addServiceTxt("elrs", "udp", "version", String(JOYSTICK_VERSION).c_str());
  #endif
}

static void addCaptivePortalHandlers()
{
    // Windows 11 captive portal workaround
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
        request->redirect("http://logout.net");
    });

    // A 404 stops win 10 keep calling this repeatedly and panicking the esp32
    server.on("/wpad.dat", [](AsyncWebServerRequest *request) {
        request->send(404);
    });

    // Firefox captive portal call home
    server.on("/success.txt", [](AsyncWebServerRequest *request) {
        request->send(200);
    });

    // URIs that should redirect to WebUpdateHandleRoot
    const char* rootUris[] = {
        "/",                             // Actual root
        "/generate_204",                 // Android
        "/gen_204",                      // Android
        "/library/test/success.html",    // Apple call home
        "/hotspot-detect.html",          // Apple call home
        "/connectivity-check.html",      // Ubuntu
        "/check_network_status.txt",     // Ubuntu
        "/ncsi.txt",                     // Windows call home
        "/canonical.html",               // Firefox captive portal call home
        "/fwlink",                       // Microsoft
        "/redirect"                      // Microsoft redirect
    };

    for (const char* uri : rootUris)
        server.on(uri, WebUpdateHandleRoot);
}

static void startServices()
{
  if (servicesStarted) {
    #if defined(PLATFORM_ESP32)
      MDNS.end();
      startMDNS();
    #endif
    return;
  }

  for (auto asset : WEB_ASSETS)
  {
      server.on(asset.path, WebUpdateSendContent);
  }
  server.on("/networks.json", WebUpdateSendNetworks);
  server.on("/sethome", WebUpdateSetHome);
  server.on("/forget", WebUpdateForget);
  server.on("/connect", WebUpdateConnect);
  server.on("/config", HTTP_GET, GetConfiguration);
  server.on("/access", WebUpdateAccessPoint);
  server.on("/firmware.bin", WebUpdateGetFirmware);

  server.on("/update", HTTP_POST, WebUploadResponseHandler, WebUploadDataHandler);
  server.on("/update", HTTP_OPTIONS, corsPreflightResponse);
  server.on("/forceupdate", WebUploadForceUpdateHandler);
  server.on("/forceupdate", HTTP_OPTIONS, corsPreflightResponse);
  server.on("/cw", HandleContinuousWave);

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Max-Age", "600");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  // Fallback/retry for recovery mode. Normal receiver startup restores before
  // outputs start; never allocate the config document inside a state poll.
  LoadOrnithopterConfig();

  server.on("/pteronautos/state", HTTP_GET, GetPteronautosState);
  server.on("/pteronautos/state/", HTTP_GET, GetPteronautosState);
  server.on("/pteronautos/ping", HTTP_GET, GetPteronautosPing);
  server.on("/pteronautos/config", HTTP_GET, GetPteronautosConfig);
  server.on("/pteronautos/config", HTTP_POST, PostPteronautosConfig);
  server.on("/pteronautos/sweep", HTTP_POST, PostPteronautosSweep);
  server.on("/pteronautos/sweep/status", HTTP_GET, GetPteronautosSweepStatus);
  server.on("/pteronautos/backup", HTTP_GET, GetPteronautosBackup);
  server.on("/pteronautos/stick", HTTP_GET, GetPteronautosStick);
  server.on("/pteronautos/stick", HTTP_POST, PostPteronautosStick);
  server.on("/hardware.json", HTTP_GET | HTTP_POST, getFile, nullptr, putFile);
  server.on("/options.json", HTTP_GET, getFile);
  server.on("/reboot", HandleReboot);
  server.on("/reset", HandleReset);
  #if defined(TARGET_TX) && defined(PLATFORM_ESP32)
    server.on("/udpcontrol", HTTP_POST, WebUdpControl);
  #endif

  server.addHandler(new AsyncCallbackJsonWebHandler("/config", UpdateConfiguration));
  server.addHandler(new AsyncCallbackJsonWebHandler("/options.json", UpdateSettings));
  #if defined(TARGET_RX)
    server.addHandler(new AsyncCallbackJsonWebHandler("/voltage-sample", SampleVoltageSources));
  #endif
  #if defined(TARGET_TX)
    server.addHandler(new AsyncCallbackJsonWebHandler("/buttons", WebUpdateButtonColors));
    auto *handler = new AsyncCallbackJsonWebHandler("/import", ImportConfiguration);
    handler->setMaxContentLength(32768);
    server.addHandler(handler);
  #endif

  #if defined(RADIO_LR1121)
    server.on("/lr1121", HTTP_OPTIONS, corsPreflightResponse);
    addLR1121Handlers(server);
  #endif

  addCaptivePortalHandlers();

  server.onNotFound(WebUpdateHandleNotFound);

  server.begin();

  dnsServer.start(DNS_PORT, "*", ipAddress);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);

  startMDNS();

  #if defined(TARGET_TX) && defined(PLATFORM_ESP32)
    WifiJoystick::StartJoystickService();
  #endif

  servicesStarted = true;
  DBGLN("HTTPUpdateServer ready! Open http://%s.local in your browser", wifi_hostname);
  #if defined(TARGET_RX)
  wifi2tcp.begin();
  #endif
}

static void HandleWebUpdate()
{
  unsigned long now = millis();
  wl_status_t status = WiFi.status();

  if (status != laststatus && wifiMode == WIFI_STA) {
    DBGLN("WiFi status %d", status);
    switch(status) {
      case WL_NO_SSID_AVAIL:
      case WL_CONNECT_FAILED:
      case WL_CONNECTION_LOST:
        changeTime = now;
        changeMode = WIFI_AP;
        break;
      case WL_DISCONNECTED: // try reconnection
        changeTime = now;
        break;
      default:
        break;
    }
    laststatus = status;
  }
  if (status != WL_CONNECTED && wifiMode == WIFI_STA && (now - changeTime) > 30000) {
    changeTime = now;
    changeMode = WIFI_AP;
    DBGLN("Connection failed %d", status);
  }
  if (changeMode != wifiMode && changeMode != WIFI_OFF && (now - changeTime) > 500) {
    switch(changeMode) {
      case WIFI_AP:
        DBGLN("Changing to AP mode");
        WiFi.disconnect();
        wifiMode = WIFI_AP;
        #if defined(PLATFORM_ESP32)
        WiFi.setHostname(wifi_hostname); // hostname must be set before the mode is set to STA
        #endif
        WiFi.mode(wifiMode);
        #if defined(PLATFORM_ESP8266)
        WiFi.setHostname(wifi_hostname); // hostname must be set before the mode is set to STA
        #endif
        changeTime = now;
        #if defined(PLATFORM_ESP8266)
        WiFi.setOutputPower(13.5);
        WiFi.setPhyMode(WIFI_PHY_MODE_11N);
        #elif defined(PLATFORM_ESP32)
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        #endif
        WiFi.softAPConfig(ipAddress, ipAddress, netMsk);
        WiFi.softAP(wifi_ap_ssid, wifi_ap_password);
        startServices();
        break;
      case WIFI_STA:
        DBGLN("Connecting to network '%s'", station_ssid);
        wifiMode = WIFI_STA;
        #if defined(PLATFORM_ESP32)
        WiFi.setHostname(wifi_hostname); // hostname must be set before the mode is set to STA
        #endif
        WiFi.mode(wifiMode);
        #if defined(PLATFORM_ESP8266)
        WiFi.setHostname(wifi_hostname); // hostname must be set after the mode is set to STA
        #endif
        changeTime = now;
        #if defined(PLATFORM_ESP8266)
        WiFi.setOutputPower(13.5);
        WiFi.setPhyMode(WIFI_PHY_MODE_11N);
        #elif defined(PLATFORM_ESP32)
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
        WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
        #endif
        WiFi.begin(station_ssid, station_password);
        startServices();
      default:
        break;
    }
    #if defined(PLATFORM_ESP8266)
      MDNS.notifyAPChange();
    #endif
    changeMode = WIFI_OFF;
  }

  #if defined(PLATFORM_ESP8266)
  if (scanComplete)
  {
    WiFi.mode(wifiMode);
    scanComplete = false;
  }
  #endif

  if (servicesStarted)
  {
    dnsServer.processNextRequest();
    #if defined(PLATFORM_ESP8266)
      MDNS.update();
    #endif

    #if defined(TARGET_TX) && defined(PLATFORM_ESP32)
      WifiJoystick::Loop(now);
    #endif
  }
}

static int start()
{
  ipAddress.fromString(wifi_ap_address);
#if defined(PTERONAUTOS)
  // timeout() owns the full grace-period schedule (respecting the WebUI
  // wifi_auto_on_interval). Starting it immediately avoids a double wait
  // (start() delay + timeout() grace period).
  return DURATION_IMMEDIATELY;
#else
  return firmwareOptions.wifi_auto_on_interval;
#endif
}

static int event()
{
  if (connectionState == wifiUpdate || connectionState > FAILURE_STATES)
  {
    if (!wifiStarted) {
      startWiFi(millis());
      return DURATION_IMMEDIATELY;
    }
  }
  else if (wifiStarted)
  {
    wifiStarted = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    #if defined(PLATFORM_ESP8266)
    WiFi.forceSleepBegin();
    #endif
  }
  return DURATION_IGNORE;
}

static int timeout()
{
  if (wifiStarted)
  {
    HandleWebUpdate();
#if defined(PLATFORM_ESP8266)
    // When in STA mode, a small delay reduces power use from 90mA to 30mA when idle
    // In AP mode, it doesn't seem to make a measurable difference, but does not hurt
    // Only done on 8266 as the ESP32 runs a throttled task
    if (!Update.isRunning())
      delay(1);
    return DURATION_IMMEDIATELY;
#else
    // All the web traffic is async apart from changing modes and MSP2WIFI
    // No need to run balls-to-the-wall; the wifi runs on this core too (0)
    return 2;
#endif
  }

  #if defined(TARGET_TX)
  // if webupdate was requested before or .wifi_auto_on_interval has elapsed but uart is not detected
  // start webupdate, there might be wrong configuration flashed.
  if(firmwareOptions.wifi_auto_on_interval != -1 && webserverPreventAutoStart == false && connectionState < wifiUpdate && !wifiStarted){
    DBGLN("No CRSF ever detected, starting WiFi");
    setWifiUpdateMode();
    return DURATION_IMMEDIATELY;
  }
  #elif defined(TARGET_RX)
  #if !defined(PTERONAUTOS)
  if (!webserverPreventAutoStart && (connectionState == disconnected))
  {
    static bool pastAutoInterval = false;
    // If InBindingMode then wait at least 60 seconds before going into wifi.
    if (!InBindingMode || pastAutoInterval)
    {
      setWifiUpdateMode();
      return DURATION_IMMEDIATELY;
    }
    pastAutoInterval = true;
    return 60000;  // wait 60s in binding mode before forcing WiFi
  }
  #endif
  #if defined(PTERONAUTOS)
  // PteronautOS WiFi startup: radio grace period before WiFi.
  // ESP8285 cannot run WiFi + SX1280 simultaneously — radio gets first chance.
  if (!wifiStarted)
  {
    static uint32_t bootTime = 0;
    if (bootTime == 0) bootTime = millis();

    // Radio hardware failure → WiFi immediately (no point waiting)
    if (connectionState > MODE_STATES)
    {
      DBGLN("PteroWiFi: radio hw failure, starting WiFi");
      setWifiUpdateMode();
      return DURATION_IMMEDIATELY;
    }

    // Connected → never auto-start WiFi
    if (connectionState == connected)
    {
      return DURATION_NEVER;
    }

    // Respect the WebUI wifi_auto_on_interval (ms). -1 = never auto-start.
    int32_t interval = firmwareOptions.wifi_auto_on_interval;
    if (interval == -1)
    {
      return DURATION_NEVER;
    }
    if (interval <= 0) interval = 30000;   // default 30s grace

    if (millis() - bootTime < (uint32_t)interval)
    {
      return 1000;
    }

    // Grace period expired, no link → WiFi fallback
    DBGLN("PteroWiFi: starting after %us (state=%d)",
          (unsigned int)((millis() - bootTime) / 1000), connectionState);
    setWifiUpdateMode();
    return DURATION_IMMEDIATELY;
  }
  #endif
  #endif
  return DURATION_NEVER;
}

device_t WIFI_device = {
  .initialize = initialize,
  .start = start,
  .event = event,
  .timeout = timeout,
  .subscribe = EVENT_CONNECTION_CHANGED
};
