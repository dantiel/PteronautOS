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
#include "MiniMDNS.h"
#include <user_interface.h>   // struct rst_info + REASON_* for the heap-safe crash endpoint
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

#include <cstdarg>
#include "config.h"

#include "devWIFI.h"          // LoadOrnithopterConfig() declaration
#include "PteroWebHandlers.h"

#if defined(PTERONAUTOS)
static void GetPteronautosSettings(AsyncWebServerRequest *request)
{
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
    auto *response = new (std::nothrow) AsyncJsonResponse(false);
    if (!response) { request->client()->close(true); return; }
    JsonObject root = response->getRoot().as<JsonObject>();
    populateSettingsJson(root["settings"].to<JsonObject>(), root);
    sendJsonResponse(request, response);
}

static void GetPteronautosRxConfig(AsyncWebServerRequest *request)
{
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
    auto *response = new (std::nothrow) AsyncJsonResponse(false);
    if (!response) { request->client()->close(true); return; }
    JsonObject root = response->getRoot().as<JsonObject>();
    JsonObject cfg = root["config"].to<JsonObject>();
    const auto uid = cfg["uid"].to<JsonArray>();
    copyArray(UID, UID_LEN, uid);
#if defined(TARGET_RX)
    populateRxConfigJson(cfg);
#endif
    sendJsonResponse(request, response);
}
#endif // defined(PTERONAUTOS)

#if defined(PTERONAUTOS)
static void GetPteronautosDebug(AsyncWebServerRequest *request)
{
    auto *response = new (std::nothrow) AsyncJsonResponse();
    if (!response) { request->client()->close(true); return; }
    JsonObject root = response->getRoot().to<JsonObject>();
    root["firmware"] = "PteronautOS";

    sendJsonResponse(request, response);
}

#ifdef ZEPHYRUS_ENABLED
#include <Wire.h>
#endif

static void GetPteronautosI2cScan(AsyncWebServerRequest *request)
{
    auto *response = new (std::nothrow) AsyncJsonResponse();
    if (!response) { request->client()->close(true); return; }
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

    sendJsonResponse(request, response);
}

static void GetPteronautosSystem(AsyncWebServerRequest *request)
{
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
    auto *response = new (std::nothrow) AsyncJsonResponse();
    if (!response) { request->client()->close(true); return; }
    JsonObject root = response->getRoot().to<JsonObject>();

    root["firmware"] = "PteronautOS";
    root["version"] = VERSION;
    root["git_commit"] = commit;
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

    sendJsonResponse(request, response);
}
// ── Blackbox dump ──────────────────────────────────────────────────────────
// GET /pteronautos/blackbox → JSON: always-on fault register + (under
// ZEPHYR_BLACKBOX) the last ~0.77s ring buffer of the stabilizer hot path.
// Fetched on-demand only (never polled), so a manual String build is lighter
// on the fragmented heap than an AsyncJsonResponse tree.
static void GetPteronautosBlackbox(AsyncWebServerRequest *request)
{
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
#if !defined(ZEPHYRUS_ENABLED)
    // No ESP-side gyro in this build (EP2 + RP2040: the companion owns the
    // gyro) — there is no Zephyrus fault register to report.
    request->send(200, "application/json", "{\"ok\":true,\"compiled\":false}");
#else
    String out;
#ifdef ZEPHYR_BLACKBOX
    out.reserve(4096);
    out += "{\"ok\":true,\"compiled\":true,\"period_ms\":3,\"samples\":";
    out += String((int)ZEPHYR_BB_SAMPLES);
    out += ",\"count\":";
    out += String((int)zephyrus.bbCount);
    out += ",\"faults\":{\"nan\":";
    out += String((int)zephyrus.faultNan);
    out += ",\"i2c_fail\":";
    out += String((int)zephyrus.faultI2cFail);
    out += ",\"bus_recover\":";
    out += String((int)zephyrus.faultBusRecover);
    out += ",\"reinit\":";
    out += String((int)zephyrus.faultReinit);
    out += ",\"last_reason\":";
    out += String((int)zephyrus.lastFaultReason);
    out += "},\"frames\":[";
    uint16_t n = zephyrus.bbCount;
    uint16_t start = (n < ZEPHYR_BB_SAMPLES) ? 0 : zephyrus.bbHead;
    for (uint16_t i = 0; i < n; i++) {
        const ZephyrBbFrame &f = zephyrus.bbRing[(start + i) % ZEPHYR_BB_SAMPLES];
        if (i) out += ',';
        out += '[';
        out += String((int)f.tMs); out += ',';
        out += String((int)f.rxLsb); out += ',';
        out += String((int)f.ryLsb); out += ',';
        out += String((int)f.rollCorrLsb); out += ',';
        out += String((int)f.pitchCorrLsb); out += ',';
        out += String((int)f.flags); out += ',';
        out += String((int)f.i2cFail);
        out += ']';
    }
    out += "]}";
#else
    out = "{\"ok\":true,\"compiled\":false,\"faults\":{\"nan\":";
    out += String((int)zephyrus.faultNan);
    out += ",\"i2c_fail\":";
    out += String((int)zephyrus.faultI2cFail);
    out += ",\"bus_recover\":";
    out += String((int)zephyrus.faultBusRecover);
    out += ",\"reinit\":";
    out += String((int)zephyrus.faultReinit);
    out += ",\"last_reason\":";
    out += String((int)zephyrus.lastFaultReason);
    out += "}}";
#endif
    request->send(200, "application/json", out);
#endif
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
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
    // Telemetry only. The ~90-field static config moved to GetPteronautosConfig
    // (fetched once per panel mount) so the 2s poll no longer rebuilds it.
    auto *response = new (std::nothrow) AsyncJsonResponse(false);
    if (!response) { request->client()->close(true); return; }
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
    // Compile-time AHRS mode — replaces the old hardcoded "Mahony" panel label.
#if defined(MESOZOIC_HORIZON)
    zeph["ahrs"] = "Mesozoic HORIZON";
#elif defined(MESOZOIC_ONLY)
    zeph["ahrs"] = "Mesozoic rate-only";
#else
    zeph["ahrs"] = "Mahony AHRS";
#endif
#if defined(ZEPHYRUS_ENABLED)
    zeph["compiled"]          = true;
    zeph["enabled"]           = zephyrus.gyroEnabled;
    zeph["connected"]         = zephyrus.enabled;   // WHO_AM_I matched (MPU physically present)
    zeph["calibrated"]        = zephyrus.calibrated;
    zeph["calibrating"]       = zephyrus._calibrating;
    zeph["calib_samples"]     = zephyrus._calibCount;
    zeph["pitch_deg"]         = zephyrus.pitchDeg;
    zeph["roll_deg"]          = zephyrus.rollDeg;
    zeph["yaw_rate"]          = zephyrus.yawRate;
    zeph["roll_correction"]   = zephyrus.rollCorrection;
    zeph["yaw_correction"]    = zephyrus.yawCorrection;
    zeph["pitch_correction"]  = zephyrus.pitchCorrection;
    zeph["board_rotation"]    = zephyrus.boardRotation;
    zeph["slew_gain"] = (int)zephyrus.slewGain;
    zeph["gyro_lpf"]         = zephyrus.gyroLpfShift;
    zeph["mode"]             = (int)zephyrus.gyroMode;
    zeph["mode_name"]        = (zephyrus.gyroMode == GYRO_MODE_HORIZON) ? "HORIZON"
                             : (zephyrus.gyroMode == GYRO_MODE_RATE)    ? "RATE" : "OFF";
    zeph["wing_roll_gain"]      = (int)ornithopter.wingRollGain;
    zeph["wing_pitch_gain"]     = (int)ornithopter.wingPitchGain;
    zeph["wing_pitch_fer_gain"] = (int)ornithopter.wingPitchFerGain;
    zeph["wing_pitch_skew_gain"] = (int)ornithopter.wingPitchSkewGain;
    zeph["wing_roll_skew_gain"]  = (int)ornithopter.wingRollSkewGain;
    zeph["wing_yaw_skew_gain"]   = (int)ornithopter.wingYawSkewGain;
    zeph["wing_yaw_gain"]       = (int)ornithopter.wingYawGain;
    zeph["roll_attitude_correction"]  = zephyrus.rollAttitudeCorrection;
    zeph["pitch_attitude_correction"] = zephyrus.pitchAttitudeCorrection;
    zeph["fault_nan"]          = zephyrus.faultNan;
    zeph["fault_i2c_fail"]     = zephyrus.faultI2cFail;
    zeph["fault_bus_recover"]  = zephyrus.faultBusRecover;
    zeph["fault_reinit"]       = zephyrus.faultReinit;
    zeph["fault_last_reason"]  = zephyrus.lastFaultReason;

    // Runtime-tunable PID gains — read back by the panel sliders
    JsonObject pid = zeph["pid"].to<JsonObject>();
    JsonObject roll  = pid["roll"].to<JsonObject>();
    roll["p"] = zephyrus.rollKp;  roll["i"] = zephyrus.rollKi;
    roll["d"] = zephyrus.rollKd;  roll["max"] = zephyrus.rollImax;
    JsonObject pitch = pid["pitch"].to<JsonObject>();
    pitch["p"] = zephyrus.pitchKp; pitch["i"] = zephyrus.pitchKi;
    pitch["d"] = zephyrus.pitchKd; pitch["max"] = zephyrus.pitchImax;
    JsonObject yaw = pid["yaw"].to<JsonObject>();
    yaw["p"] = zephyrus.yawKp; yaw["i"] = zephyrus.yawKi;
    yaw["d"] = zephyrus.yawKd; yaw["max"] = zephyrus.yawImax;
#else
    zeph["compiled"]          = false;
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
    zeph["board_rotation"]    = 0;
    zeph["wing_roll_gain"]    = 0;
    zeph["wing_pitch_gain"]   = 0;
    zeph["wing_yaw_gain"]     = 0;
#endif

    JsonObject sweep = root["sweep"].to<JsonObject>();
    sweep["active"] = isServoSweepActive();
    sweep["us"]     = getServoSweepUs();

    sendJsonResponse(request, response);
}

static void GetPteronautosConfig(AsyncWebServerRequest *request)
{
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
    // Static config — only changes via POST /pteronautos/config. Fetched once
    // per panel mount, not polled. Keeps the 2s state poll lean on the heap.
    auto *response = new (std::nothrow) AsyncJsonResponse(false);
    if (!response) { request->client()->close(true); return; }
    JsonObject root = response->getRoot().as<JsonObject>();
    JsonObject orni = root["ornithopter"].to<JsonObject>();

    // Mixer profile is compile-time (MIXER_PROFILE build flag). config.GetModelId()
    // is the ELRS model-match id and has nothing to do with the ornithopter kernel —
    // reporting it as profile_id made the panel show "gearbox" for a servo build.
    uint8_t pid = (uint8_t)ACTIVE_PROFILE;
    orni["profile_id"]          = pid;
    orni["type"]                = PROFILE_IS_GEARBOX ? "gearbox" : "servo";
    orni["kernel_fixed"]        = false;  // profile is runtime-switchable
    orni["pwm_outputs"]         = GPIO_PIN_PWM_OUTPUTS_COUNT;  // local servo pads (0 = companion)
    // Per-pad GPIO + servo function, in natural silkscreen order (index 0 = first
    // broken-out pad). Lets the Servo panel show the real pin for each channel so
    // users can correlate with the Hardware (pwm_outputs) editor.
    JsonArray pwmPins = orni["pwm_pins"].to<JsonArray>();
    JsonArray funcMap = orni["func_map"].to<JsonArray>();
    for (int ch = 0; ch < PROFILE.servoCount && ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
    {
        pwmPins.add(GPIO_PIN_PWM_OUTPUTS[ch]);
        funcMap.add(ornithopter.funcMap[ch]);
    }
    orni["model_name"]          = ornithopter.modelName;

#ifdef ZEPHYRUS_ENABLED
    orni["cadence_gain"]        = (int)ornithopter.cadenceGain;
    orni["ferocity_d_gain"]     = (int)ornithopter.ferocityDGain;
    orni["balance_gain"]        = (int)ornithopter.balanceGain;
    orni["ferocity_p_gain"]     = (int)ornithopter.ferocityPGain;
    orni["anchor_gain"]         = (int)ornithopter.anchorGain;
    orni["resonance_gain"]      = (int)ornithopter.resonanceGain;
    orni["ssff_gain"]           = (int)ornithopter.ssffGain;
    orni["aero_glide_coeff"]    = (int)ornithopter.aeroGlideCoeff;
    orni["aero_flap_coeff"]     = (int)ornithopter.aeroFlapCoeff;
    // Mesozoic 2-wing stabilizer gains
    orni["wing_roll_gain"]      = (int)ornithopter.wingRollGain;
    orni["wing_pitch_gain"]     = (int)ornithopter.wingPitchGain;
    orni["wing_pitch_fer_gain"] = (int)ornithopter.wingPitchFerGain;
    orni["wing_pitch_skew_gain"] = (int)ornithopter.wingPitchSkewGain;
    orni["wing_roll_skew_gain"]  = (int)ornithopter.wingRollSkewGain;
    orni["wing_yaw_skew_gain"]   = (int)ornithopter.wingYawSkewGain;
    orni["wing_yaw_gain"]       = (int)ornithopter.wingYawGain;
#endif

    // Runtime waveform/mixer config fields
    orni["stroke_ferocity"]     = (int)ornithopter.strokeFerocity;
    orni["return_ferocity"]     = (int)ornithopter.returnFerocity;
    orni["stroke_skew"]         = (int)ornithopter.strokeSkew;
    orni["return_skew"]         = (int)ornithopter.returnSkew;
    orni["throttle_thrust_shape_mix"] = (int)ornithopter.throttleThrustShapeMix;
    orni["throttle_thrust_expo"] = (int)ornithopter.throttleThrustExpo;
    orni["aileron_skew_mix"]   = (int)ornithopter.aileronSkewMix;
    orni["throttle_skew_rate_mix"] = (int)ornithopter.throttleSkewRateMix;
    orni["aileron_skew_rate_mix"] = (int)ornithopter.aileronSkewRateMix;
    orni["glide_angle_deg"]     = ornithopter.glideAngleDeg;
    orni["flapping_angle_deg"]  = (int)ornithopter.flappingAngleDeg;
    orni["aileron_scale"]       = (int)ornithopter.aileronScale;
    orni["elevator_scale"]      = (int)ornithopter.elevatorScale;
    orni["rudder_yaw_weight"]   = (int)ornithopter.rudderYawWeight;
    orni["rudder_roll_weight"]  = (int)ornithopter.rudderRollWeight;
    orni["rudder_ferocity_range"] = (int)ornithopter.rudderFerocityRange;
    orni["rudder_amplitude_differential"] = (int)ornithopter.rudderAmplitudeDifferential;
    orni["elevator_ferocity_mix"] = (int)ornithopter.elevatorFerocityMix;
    orni["elevator_ferocity_rate_mix"] = (int)ornithopter.elevatorFerocityRateMix;
    orni["elevator_antigravity_mix"] = (int)ornithopter.elevatorAntigravityMix;
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

    // Glitch Filter — CRSF channel glitch filter (values in CRSF raw 172–1811)
    orni["glitch_active"]        = ornithopter.glitchActive;
    orni["glitch_delta_max"]     = (int)ornithopter.glitchDeltaMax;
    orni["glitch_delta_arm"]     = (int)ornithopter.glitchDeltaArm;
    orni["glitch_delta_persist"] = (int)ornithopter.glitchDeltaPersist;
    orni["glitch_simult_threshold"] = (int)ornithopter.glitchSimultThreshold;

    // Flight profiles (3 × tuning param sets)
    JsonArray fpArr = orni["flight_profiles"].to<JsonArray>();
    for (int i = 0; i < FLIGHT_PROFILE_COUNT; ++i) {
        JsonObject p = fpArr.createNestedObject();
        p["name"]                  = ornithopter.flightProfiles[i].name;
        p["stroke_ferocity"]       = (int)ornithopter.flightProfiles[i].strokeFerocity;
        p["return_ferocity"]       = (int)ornithopter.flightProfiles[i].returnFerocity;
        p["stroke_skew"]           = (int)ornithopter.flightProfiles[i].strokeSkew;
        p["return_skew"]           = (int)ornithopter.flightProfiles[i].returnSkew;
        p["throttle_thrust_shape_mix"] = (int)ornithopter.flightProfiles[i].throttleThrustShapeMix;
        p["throttle_thrust_expo"]  = (int)ornithopter.flightProfiles[i].throttleThrustExpo;
        p["aileron_skew_mix"]     = (int)ornithopter.flightProfiles[i].aileronSkewMix;
        p["throttle_skew_rate_mix"] = (int)ornithopter.flightProfiles[i].throttleSkewRateMix;
        p["aileron_skew_rate_mix"] = (int)ornithopter.flightProfiles[i].aileronSkewRateMix;
        p["glide_angle_deg"]       = ornithopter.flightProfiles[i].glideAngleDeg;
        p["flapping_angle_deg"]    = (int)ornithopter.flightProfiles[i].flappingAngleDeg;
        p["rudder_ferocity_range"] = (int)ornithopter.flightProfiles[i].rudderFerocityRange;
        p["rudder_amplitude_differential"] = (int)ornithopter.flightProfiles[i].rudderAmplitudeDifferential;
        p["elevator_ferocity_mix"] = (int)ornithopter.flightProfiles[i].elevatorFerocityMix;
        p["elevator_ferocity_rate_mix"] = (int)ornithopter.flightProfiles[i].elevatorFerocityRateMix;
        p["elevator_antigravity_mix"] = (int)ornithopter.flightProfiles[i].elevatorAntigravityMix;
        p["throttle_frequency_mix"] = (int)ornithopter.flightProfiles[i].throttleFrequencyMix;
        p["ferocity_shape_mix"] = (int)ornithopter.flightProfiles[i].ferocityShapeMix;
    }

    sendJsonResponse(request, response);
}

static void GetPteronautosPing(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", "{\"ok\":true,\"firmware\":\"PteronautOS\"}");
}

// ── Unguarded diagnostic endpoint ─────────────────────────────────
// Served even when the heap guard is firing on the heavy JSON handlers, so a
// crash/reboot loop can be diagnosed without ever reaching a "healthy" state.
// Bounded formatting; request->send still allocates an HTTP response.
static void GetPteronautosDiag(AsyncWebServerRequest *request)
{
    char buf[384];
#if defined(PLATFORM_ESP8266)
    snprintf_P(buf, sizeof(buf),
        PSTR("{\"ok\":true,\"heap_low\":%d,\"free_heap\":%d,\"max_free_block\":%d,\"uptime_ms\":%u,\"reset_reason\":\"%s\",\"heap_before_wifi\":%u,\"heap_after_services\":%u,\"http_slice_bytes\":512}"),
        heapCriticallyLow() ? 1 : 0,
        (int)ESP.getFreeHeap(), (int)ESP.getMaxFreeBlockSize(),
        (unsigned)millis(), ESP.getResetReason().c_str(),
        (unsigned)heapBeforeWifi, (unsigned)heapAfterServices);
#else
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"heap_low\":0,\"free_heap\":%d,\"uptime_ms\":%u,\"reset_reason\":\"%s\"}",
        (int)ESP.getFreeHeap(), (unsigned)millis(), ESP.getResetReason().c_str());
#endif
    request->send(200, "application/json", buf);
}

// ── Crash-context dump (raw text) ─────────────────────────────────
// Reads the SDK rst_info struct DIRECTLY into a stack buffer — zero heap
// allocation. The old path called ESP.getResetInfo() (returns a String) and
// request->send(..., String) deep-copied it a second time: the ONE endpoint
// that must keep working while the heap is critically low was doing the most
// allocation of any handler. Read the struct, snprintf into buf, send buf —
// same heap profile as GetPteronautosDiag (which never crashes).
static void GetPteronautosCrash(AsyncWebServerRequest *request)
{
#if defined(PLATFORM_ESP8266)
    const struct rst_info *ri = ESP.getResetInfoPtr();
    const char *reason;
    switch (ri->reason) {
        case REASON_DEFAULT_RST:      reason = "Power On"; break;
        case REASON_WDT_RST:          reason = "Hardware Watchdog"; break;
        case REASON_EXCEPTION_RST:    reason = "Exception"; break;
        case REASON_SOFT_WDT_RST:     reason = "Software Watchdog"; break;
        case REASON_SOFT_RESTART:     reason = "Software/System restart"; break;
        case REASON_DEEP_SLEEP_AWAKE: reason = "Deep-Sleep Wake"; break;
        case REASON_EXT_SYS_RST:      reason = "External System"; break;
        default:                      reason = "Unknown"; break;
    }
    char buf[224];
    snprintf(buf, sizeof(buf),
        "Fatal exception:%u flag:%u (%s) epc1:0x%08x epc2:0x%08x epc3:0x%08x excvaddr:0x%08x depc:0x%08x",
        (unsigned)ri->exccause, (unsigned)ri->reason, reason,
        (unsigned)ri->epc1, (unsigned)ri->epc2, (unsigned)ri->epc3,
        (unsigned)ri->excvaddr, (unsigned)ri->depc);
    request->send(200, "text/plain", buf);
#else
    request->send(200, "text/plain", "no crash context on this platform");
#endif
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

// Float form-param reader — used for runtime-tunable PID gains (small floats
// like Kp/Ki/Kd). NaN is rejected so a malformed POST can't poison the loop.
static float _pteroParamFloat(AsyncWebServerRequest *request, const char* key, float deflt)
{
    if (request->hasParam(key, true)) {
        float v = request->getParam(key, true)->value().toFloat();
        if (!isnan(v)) return v;
    }
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
                f.print(F("\\u00"));
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

    f.print(F("{\"flight_profiles\":["));
    for (int i = 0; i < FLIGHT_PROFILE_COUNT; ++i) {
        if (i) f.print(',');
        f.print(F("{\"name\":"));
        _pteroPrintJsonString(f, ornithopter.flightProfiles[i].name);
        f.print(F(",\"stroke_ferocity\":"));
        f.print((int)ornithopter.flightProfiles[i].strokeFerocity);
        f.print(F(",\"return_ferocity\":"));
        f.print((int)ornithopter.flightProfiles[i].returnFerocity);
        f.print(F(",\"stroke_skew\":"));
        f.print((int)ornithopter.flightProfiles[i].strokeSkew);
        f.print(F(",\"return_skew\":"));
        f.print((int)ornithopter.flightProfiles[i].returnSkew);
        f.print(F(",\"throttle_thrust_shape_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].throttleThrustShapeMix);
        f.print(F(",\"throttle_thrust_expo\":"));
        f.print((int)ornithopter.flightProfiles[i].throttleThrustExpo);
        f.print(F(",\"aileron_skew_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].aileronSkewMix);
        f.print(F(",\"throttle_skew_rate_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].throttleSkewRateMix);
        f.print(F(",\"aileron_skew_rate_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].aileronSkewRateMix);
        f.print(F(",\"glide_angle_deg\":"));
        f.print((int)ornithopter.flightProfiles[i].glideAngleDeg);
        f.print(F(",\"flapping_angle_deg\":"));
        f.print((int)ornithopter.flightProfiles[i].flappingAngleDeg);
        f.print(F(",\"rudder_ferocity_range\":"));
        f.print((int)ornithopter.flightProfiles[i].rudderFerocityRange);
        f.print(F(",\"rudder_amplitude_differential\":"));
        f.print((int)ornithopter.flightProfiles[i].rudderAmplitudeDifferential);
        f.print(F(",\"elevator_ferocity_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].elevatorFerocityMix);
        f.print(F(",\"elevator_ferocity_rate_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].elevatorFerocityRateMix);
        f.print(F(",\"elevator_antigravity_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].elevatorAntigravityMix);
        f.print(F(",\"throttle_frequency_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].throttleFrequencyMix);
        f.print(F(",\"ferocity_shape_mix\":"));
        f.print((int)ornithopter.flightProfiles[i].ferocityShapeMix);
        f.print('}');
    }
    f.print(F("],\"active_flight_profile\":"));
    f.print((int)ornithopter.activeFlightProfile);
    f.print(F(",\"rudder_yaw_weight\":"));
    f.print((int)ornithopter.rudderYawWeight);
    f.print(F(",\"rudder_roll_weight\":"));
    f.print((int)ornithopter.rudderRollWeight);
    f.print(F(",\"aileron_scale\":"));
    f.print((int)ornithopter.aileronScale);
    f.print(F(",\"elevator_scale\":"));
    f.print((int)ornithopter.elevatorScale);
    f.print(F(",\"elevon_scale\":"));
    f.print((int)ornithopter.elevonScale);
    f.print(F(",\"motor_min_us\":"));
    f.print((int)ornithopter.motorMinUs);
    f.print(F(",\"motor_max_us\":"));
    f.print((int)ornithopter.motorMaxUs);
    f.print(F(",\"glide_mode\":"));
    f.print(ornithopter.glideMode ? "true" : "false");
    f.print(F(",\"hall_sensor_pin\":"));
    f.print((int)ornithopter.hallSensorPin);
    f.print(F(",\"ratchet_throttle_pct\":"));
    f.print((int)ornithopter.ratchetThrottlePct);
    f.print(F(",\"ratchet_timeout_ms\":"));
    f.print((int)ornithopter.ratchetTimeoutMs);
    f.print(F(",\"servo_speed\":"));
    f.print((int)ornithopter.servoSpeed);
    f.print(F(",\"flap_base_freq\":"));
    f.print((int)ornithopter.flapBaseFreq);
    f.print(F(",\"servo_min_us\":"));
    f.print((int)ornithopter.servoMinUs);
    f.print(F(",\"servo_max_us\":"));
    f.print((int)ornithopter.servoMaxUs);
    f.print(F(",\"servo_trim\":["));
    for (int i = 0; i < SF_COUNT; ++i) {
        if (i) f.print(',');
        f.print((int)ornithopter.servoTrimUs[i]);
    }
    f.print(F("],\"func_map\":["));
    for (int i = 0; i < ORNI_MAX_OUTPUTS; ++i) {
        if (i) f.print(',');
        f.print((int)ornithopter.funcMap[i]);
    }
    f.print(F("],\"profile_id\":"));
    f.print((int)activeProfile);
    f.print(F(",\"model_name\":"));
    _pteroPrintJsonString(f, ornithopter.modelName);
    f.print(F(",\"glitch_active\":"));
    f.print(ornithopter.glitchActive ? "true" : "false");
    f.print(F(",\"glitch_delta_max\":"));
    f.print((int)ornithopter.glitchDeltaMax);
    f.print(F(",\"glitch_delta_arm\":"));
    f.print((int)ornithopter.glitchDeltaArm);
    f.print(F(",\"glitch_delta_persist\":"));
    f.print((int)ornithopter.glitchDeltaPersist);
    f.print(F(",\"glitch_simult_threshold\":"));
    f.print((int)ornithopter.glitchSimultThreshold);
#ifdef ZEPHYRUS_ENABLED
    f.print(F(",\"gyro_enabled\":"));
    f.print(zephyrus.gyroEnabled ? "true" : "false");
    f.print(F(",\"slew_gain\":"));
    f.print((int)zephyrus.slewGain);
    f.print(F(",\"gyro_lpf\":"));
    f.print((int)zephyrus.gyroLpfShift);
    f.print(F(",\"board_rotation\":"));
    f.print((int)zephyrus.boardRotation);
    f.print(F(",\"wing_roll_gain\":"));
    f.print((int)ornithopter.wingRollGain);
    f.print(F(",\"wing_pitch_gain\":"));
    f.print((int)ornithopter.wingPitchGain);
    f.print(F(",\"wing_pitch_fer_gain\":"));
    f.print((int)ornithopter.wingPitchFerGain);
    f.print(F(",\"wing_pitch_skew_gain\":"));
    f.print((int)ornithopter.wingPitchSkewGain);
    f.print(F(",\"wing_roll_skew_gain\":"));
    f.print((int)ornithopter.wingRollSkewGain);
    f.print(F(",\"wing_yaw_skew_gain\":"));
    f.print((int)ornithopter.wingYawSkewGain);
    f.print(F(",\"wing_yaw_gain\":"));
    f.print((int)ornithopter.wingYawGain);
    f.print(F(",\"pid_roll_p\":"));
    f.print(zephyrus.rollKp, 3);
    f.print(F(",\"pid_roll_i\":"));
    f.print(zephyrus.rollKi, 3);
    f.print(F(",\"pid_roll_d\":"));
    f.print(zephyrus.rollKd, 3);
    f.print(F(",\"pid_roll_max\":"));
    f.print(zephyrus.rollImax, 1);
    f.print(F(",\"pid_pitch_p\":"));
    f.print(zephyrus.pitchKp, 3);
    f.print(F(",\"pid_pitch_i\":"));
    f.print(zephyrus.pitchKi, 3);
    f.print(F(",\"pid_pitch_d\":"));
    f.print(zephyrus.pitchKd, 3);
    f.print(F(",\"pid_pitch_max\":"));
    f.print(zephyrus.pitchImax, 1);
    f.print(F(",\"pid_yaw_p\":"));
    f.print(zephyrus.yawKp, 3);
    f.print(F(",\"pid_yaw_i\":"));
    f.print(zephyrus.yawKi, 3);
    f.print(F(",\"pid_yaw_d\":"));
    f.print(zephyrus.yawKd, 3);
    f.print(F(",\"pid_yaw_max\":"));
    f.print(zephyrus.yawImax, 1);
#endif
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
            if (p["name"].is<const char*>()) {
                const char *nm = p["name"].as<const char*>();
                uint8_t j = 0;
                for (; j < ORNI_PROFILE_NAME_MAX && nm[j]; ++j) dst.name[j] = nm[j];
                dst.name[j] = '\0';
            }
            if (p["stroke_ferocity"].is<int>())       dst.strokeFerocity      = p["stroke_ferocity"].as<int>();
            if (p["return_ferocity"].is<int>())       dst.returnFerocity      = p["return_ferocity"].as<int>();
            if (p["stroke_skew"].is<int>()) {
                int32_t sk = p["stroke_skew"].as<int>();
                if (sk < -100) sk = -100;
                if (sk >  100) sk =  100;
                dst.strokeSkew = sk;
            }
            if (p["return_skew"].is<int>()) {
                int32_t sk = p["return_skew"].as<int>();
                if (sk < -100) sk = -100;
                if (sk >  100) sk =  100;
                dst.returnSkew = sk;
            }
            if (p["throttle_thrust_shape_mix"].is<int>()) {
                int32_t mix = p["throttle_thrust_shape_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.throttleThrustShapeMix = mix;
            }
            if (p["throttle_thrust_expo"].is<int>()) {
                int32_t expo = p["throttle_thrust_expo"].as<int>();
                if (expo < (int)ORNI_THRUST_EXPO_MIN) expo = (int)ORNI_THRUST_EXPO_MIN;
                if (expo > (int)ORNI_THRUST_EXPO_MAX) expo = (int)ORNI_THRUST_EXPO_MAX;
                dst.throttleThrustExpo = expo;
            }
            if (p["aileron_skew_mix"].is<int>()) {
                int32_t mix = p["aileron_skew_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.aileronSkewMix = mix;
            }
            if (p["throttle_skew_rate_mix"].is<int>()) {
                int32_t mix = p["throttle_skew_rate_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.throttleSkewRateMix = mix;
            }
            if (p["aileron_skew_rate_mix"].is<int>()) {
                int32_t mix = p["aileron_skew_rate_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.aileronSkewRateMix = mix;
            }
            if (p["glide_angle_deg"].is<int>())       dst.glideAngleDeg       = (int8_t)p["glide_angle_deg"].as<int>();
                        if (p["flapping_angle_deg"].is<int>()) {
                            int32_t fa = p["flapping_angle_deg"].as<int>();
                            if (fa < -15) fa = -15;
                            if (fa >  15) fa =  15;
                            dst.flappingAngleDeg = (int8_t)fa;
                        }
            if (p["rudder_ferocity_range"].is<int>()) dst.rudderFerocityRange = p["rudder_ferocity_range"].as<int>();
            if (p["rudder_amplitude_differential"].is<int>()) dst.rudderAmplitudeDifferential = p["rudder_amplitude_differential"].as<int>();
            if (p["elevator_ferocity_mix"].is<int>()) dst.elevatorFerocityMix = p["elevator_ferocity_mix"].as<int>();
            if (p["elevator_ferocity_rate_mix"].is<int>()) {
                int32_t mix = p["elevator_ferocity_rate_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.elevatorFerocityRateMix = mix;
            }
            if (p["elevator_antigravity_mix"].is<int>()) {
                int32_t mix = p["elevator_antigravity_mix"].as<int>();
                if (mix < 0) mix = 0;
                if (mix > 100) mix = 100;
                dst.elevatorAntigravityMix = mix;
            }
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
    if (o["aileron_scale"].is<int>())         ornithopter.aileronScale         = o["aileron_scale"].as<int>();
    if (o["elevator_scale"].is<int>())        ornithopter.elevatorScale        = o["elevator_scale"].as<int>();
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
    // Runtime servo routing — apply after profile_id so it overrides the profile
    // default (drag-and-drop reorder in the Servo Output tab).
    if (o["func_map"].is<JsonArray>()) {
        JsonArray fmArr = o["func_map"].as<JsonArray>();
        uint8_t order[ORNI_MAX_OUTPUTS];
        uint8_t n = 0;
        for (int i = 0; i < ORNI_MAX_OUTPUTS && i < (int)fmArr.size(); ++i)
            order[n++] = (uint8_t)fmArr[i].as<int>();
        ornithopter.setServoOrder(order, n);
    }
    // Glitch Filter — CRSF channel glitch filter (clamped so a corrupt config can't
    // freeze every stick move or pass every glitch).
    if (o["glitch_active"].is<bool>())   ornithopter.glitchActive = o["glitch_active"].as<bool>();
    if (o["glitch_delta_max"].is<int>()) {
        int32_t v = o["glitch_delta_max"].as<int>();
        if (v < 20) v = 20; else if (v > 500) v = 500;
        ornithopter.glitchDeltaMax = (uint16_t)v;
    }
    if (o["glitch_delta_arm"].is<int>()) {
        int32_t v = o["glitch_delta_arm"].as<int>();
        if (v < 100) v = 100; else if (v > 1639) v = 1639;
        ornithopter.glitchDeltaArm = (uint16_t)v;
    }
    if (o["glitch_delta_persist"].is<int>()) {
        int32_t v = o["glitch_delta_persist"].as<int>();
        if (v < 20) v = 20; else if (v > 1000) v = 1000;
        ornithopter.glitchDeltaPersist = (uint16_t)v;
    }
    if (o["glitch_simult_threshold"].is<int>()) {
        int32_t v = o["glitch_simult_threshold"].as<int>();
        if (v < 2) v = 2; else if (v > STK_COUNT) v = STK_COUNT;
        ornithopter.glitchSimultThreshold = (uint8_t)v;
    }
#ifdef ZEPHYRUS_ENABLED
    if (o["gyro_enabled"].is<bool>()) {
        zephyrus.gyroEnabled = o["gyro_enabled"].as<bool>();
    }
    if (o["slew_gain"].is<int>()) {
        float sg = (float)o["slew_gain"].as<int>();
        if (sg < 0.0f) sg = 0.0f;
        if (sg > 100.0f) sg = 100.0f;
        zephyrus.slewGain = sg;
    }
    if (o["gyro_lpf"].is<int>()) {
        int v = o["gyro_lpf"].as<int>();
        if (v < 0) v = 0; else if (v > 6) v = 6;
        zephyrus.gyroLpfShift = (uint8_t)v;
    }
    if (o["board_rotation"].is<int>()) {
        int v = o["board_rotation"].as<int>();
        if (v >= 0 && v <= 6) zephyrus.setBoardRotation((uint8_t)v);
    }
    if (o["wing_roll_gain"].is<int>()) {
        int v = o["wing_roll_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingRollGain = (float)v;
    }
    if (o["wing_pitch_gain"].is<int>()) {
        int v = o["wing_pitch_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingPitchGain = (float)v;
    }
    if (o["wing_pitch_fer_gain"].is<int>()) {
        int v = o["wing_pitch_fer_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingPitchFerGain = (float)v;
    }
    if (o["wing_pitch_skew_gain"].is<int>()) {
        int v = o["wing_pitch_skew_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingPitchSkewGain = (float)v;
    }
    if (o["wing_roll_skew_gain"].is<int>()) {
        int v = o["wing_roll_skew_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingRollSkewGain = (float)v;
    }
    if (o["wing_yaw_skew_gain"].is<int>()) {
        int v = o["wing_yaw_skew_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingYawSkewGain = (float)v;
    }
    if (o["wing_yaw_gain"].is<int>()) {
        int v = o["wing_yaw_gain"].as<int>();
        if (v < -100) v = -100; else if (v > 100) v = 100;
        ornithopter.wingYawGain = (float)v;
    }
    // Runtime PID gains — clamped to sane envelopes so a corrupt config can't
    // destabilize the loop (Kp ≤10, Ki/Kd ≤2, Imax ≤100).
    if (o["pid_roll_p"].is<float>())  { float v = o["pid_roll_p"].as<float>();  if (v >= 0 && v <= 8) zephyrus.rollKp = v; }
    if (o["pid_roll_i"].is<float>())  { float v = o["pid_roll_i"].as<float>();  if (v >= 0 && v <= 2)  zephyrus.rollKi = v; }
    if (o["pid_roll_d"].is<float>())  { float v = o["pid_roll_d"].as<float>();  if (v >= 0 && v <= 2)  zephyrus.rollKd = v; }
    if (o["pid_roll_max"].is<float>()){ float v = o["pid_roll_max"].as<float>(); if (v >= 0 && v <= 100) zephyrus.rollImax = v; }
    if (o["pid_pitch_p"].is<float>()) { float v = o["pid_pitch_p"].as<float>(); if (v >= 0 && v <= 8) zephyrus.pitchKp = v; }
    if (o["pid_pitch_i"].is<float>()) { float v = o["pid_pitch_i"].as<float>(); if (v >= 0 && v <= 2)  zephyrus.pitchKi = v; }
    if (o["pid_pitch_d"].is<float>()) { float v = o["pid_pitch_d"].as<float>(); if (v >= 0 && v <= 2)  zephyrus.pitchKd = v; }
    if (o["pid_pitch_max"].is<float>()){ float v = o["pid_pitch_max"].as<float>(); if (v >= 0 && v <= 100) zephyrus.pitchImax = v; }
    if (o["pid_yaw_p"].is<float>())   { float v = o["pid_yaw_p"].as<float>();   if (v >= 0 && v <= 8) zephyrus.yawKp = v; }
    if (o["pid_yaw_i"].is<float>())   { float v = o["pid_yaw_i"].as<float>();   if (v >= 0 && v <= 2)  zephyrus.yawKi = v; }
    if (o["pid_yaw_d"].is<float>())   { float v = o["pid_yaw_d"].as<float>();   if (v >= 0 && v <= 2)  zephyrus.yawKd = v; }
    if (o["pid_yaw_max"].is<float>()) { float v = o["pid_yaw_max"].as<float>(); if (v >= 0 && v <= 100) zephyrus.yawImax = v; }
#endif

    // Restore active flight profile and apply its params to the mixer.
    if (o["active_flight_profile"].is<int>())
        ornithopter.applyFlightProfile((uint8_t)o["active_flight_profile"].as<int>());
    else
        ornithopter.applyFlightProfile(ornithopter.activeFlightProfile);
}

static void PostPteronautosConfig(AsyncWebServerRequest *request)
{
    // NO heap guard here: this path is heap-free. Params are read as String
    // references (no copy) and SaveOrnithopterConfig() streams straight to
    // LittleFS via PteroConfigWriter (no JsonDocument, no output buffer). The
    // form-urlencoded POST body is *already* sitting in heap when this handler
    // runs, so max_free_block naturally dips below HEAP_MAX_BLOCK_WATER — but
    // the save needs none of that space. Guarding it (as before) produced a
    // permanent false 503 and made flight-profile saves impossible.
    int fp = -1;
    if (request->hasParam("flight_profile", true)) {
        const String &slot = request->getParam("flight_profile", true)->value();
        if (slot.length() != 1 || slot[0] < '0' || slot[0] >= '0' + FLIGHT_PROFILE_COUNT) {
            request->send(400, "application/json", "{\"ok\":false,\"saved\":false,\"error\":\"Invalid flight profile slot\"}");
            return;
        }
        fp = slot[0] - '0';
    }
    // Copy one flight-profile slot into another (WebUI "copy" button).
    if (fp >= 0 && request->hasParam("copy_profile_from", true)) {
        const String &srcStr = request->getParam("copy_profile_from", true)->value();
        if (srcStr.length() == 1 && srcStr[0] >= '0' && srcStr[0] < '0' + FLIGHT_PROFILE_COUNT) {
            ornithopter.copyFlightProfile((uint8_t)fp, (uint8_t)(srcStr[0] - '0'));
        }
    }
    // Rename a flight-profile slot.
    if (request->hasParam("name", true)) {
        String nm = request->getParam("name", true)->value();
        nm.trim();
        ornithopter.setFlightProfileName(fp >= 0 ? (uint8_t)fp : ornithopter.activeFlightProfile, nm.c_str());
    }
    // Mixer profile — runtime-switchable
    int pid = _pteroParamInt(request, "profile_id", -1);
    if (pid >= 0) setOrnithopterProfile((uint8_t)pid);
    // Servo routing — comma-separated ServoFunc tags (drag-and-drop reorder in
    // the Servo Output tab). Re-maps which function drives which output pad.
    if (request->hasParam("func_map", true)) {
        const String &fm = request->getParam("func_map", true)->value();
        uint8_t order[ORNI_MAX_OUTPUTS];
        uint8_t n = 0;
        int start = 0;
        while (n < ORNI_MAX_OUTPUTS) {
            int comma = fm.indexOf(',', start);
            String tok = (comma < 0) ? fm.substring(start) : fm.substring(start, comma);
            tok.trim();
            if (tok.length() == 0) break;
            order[n++] = (uint8_t)tok.toInt();
            if (comma < 0) break;
            start = comma + 1;
        }
        ornithopter.setServoOrder(order, n);
    }
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
    // Mesozoic 2-wing stabilizer gains (-100..+100; sign = correction direction)
    int wingRoll      = _pteroParamInt(request, "wing_roll_gain",      (int)ornithopter.wingRollGain);
    int wingPitch     = _pteroParamInt(request, "wing_pitch_gain",     (int)ornithopter.wingPitchGain);
    int wingPitchFer  = _pteroParamInt(request, "wing_pitch_fer_gain", (int)ornithopter.wingPitchFerGain);
    int wingPitchSkew = _pteroParamInt(request, "wing_pitch_skew_gain", (int)ornithopter.wingPitchSkewGain);
    int wingRollSkew  = _pteroParamInt(request, "wing_roll_skew_gain",  (int)ornithopter.wingRollSkewGain);
    int wingYawSkew   = _pteroParamInt(request, "wing_yaw_skew_gain",   (int)ornithopter.wingYawSkewGain);
    int wingYaw       = _pteroParamInt(request, "wing_yaw_gain",       (int)ornithopter.wingYawGain);
    if (wingRoll      < -100) wingRoll      = -100; else if (wingRoll      > 100) wingRoll      = 100;
    if (wingPitch     < -100) wingPitch     = -100; else if (wingPitch     > 100) wingPitch     = 100;
    if (wingPitchFer  < -100) wingPitchFer  = -100; else if (wingPitchFer  > 100) wingPitchFer  = 100;
    if (wingPitchSkew < -100) wingPitchSkew = -100; else if (wingPitchSkew > 100) wingPitchSkew = 100;
    if (wingRollSkew  < -100) wingRollSkew  = -100; else if (wingRollSkew  > 100) wingRollSkew  = 100;
    if (wingYawSkew   < -100) wingYawSkew   = -100; else if (wingYawSkew   > 100) wingYawSkew   = 100;
    if (wingYaw       < -100) wingYaw       = -100; else if (wingYaw       > 100) wingYaw       = 100;
    ornithopter.wingRollGain      = (float)wingRoll;
    ornithopter.wingPitchGain     = (float)wingPitch;
    ornithopter.wingPitchFerGain  = (float)wingPitchFer;
    ornithopter.wingPitchSkewGain = (float)wingPitchSkew;
    ornithopter.wingRollSkewGain  = (float)wingRollSkew;
    ornithopter.wingYawSkewGain   = (float)wingYawSkew;
    ornithopter.wingYawGain       = (float)wingYaw;
    int gv = _pteroParamInt(request, "gyro_enabled", -1);
    if (gv >= 0) {
        zephyrus.gyroEnabled = (gv == 1);
        if (gv == 1) zephyrus.reprobe();   // force a fresh MPU probe on enable
    }
    float slewGain = (float)_pteroParamInt(request, "slew_gain", (int)zephyrus.slewGain);
    if (slewGain < 0.0f) slewGain = 0.0f;
    if (slewGain > 100.0f) slewGain = 100.0f;
    zephyrus.slewGain = slewGain;
    int glpf = _pteroParamInt(request, "gyro_lpf", (int)zephyrus.gyroLpfShift);
    if (glpf < 0) glpf = 0; else if (glpf > 6) glpf = 6;
    zephyrus.gyroLpfShift = (uint8_t)glpf;
    int brot = _pteroParamInt(request, "board_rotation", (int)zephyrus.boardRotation);
    if (brot >= 0 && brot <= 6) zephyrus.setBoardRotation((uint8_t)brot);
    // Runtime PID gains — clamped to the same envelopes as LoadOrnithopterConfig
    float rp  = _pteroParamFloat(request, "pid_roll_p",  zephyrus.rollKp);   if (rp  < 0) rp  = 0; else if (rp  > 8)   rp  = 8;   zephyrus.rollKp  = rp;
    float ri  = _pteroParamFloat(request, "pid_roll_i",  zephyrus.rollKi);   if (ri  < 0) ri  = 0; else if (ri  > 2)   ri  = 2;   zephyrus.rollKi  = ri;
    float rd  = _pteroParamFloat(request, "pid_roll_d",  zephyrus.rollKd);   if (rd  < 0) rd  = 0; else if (rd  > 2)   rd  = 2;   zephyrus.rollKd  = rd;
    float rmx = _pteroParamFloat(request, "pid_roll_max", zephyrus.rollImax); if (rmx < 0) rmx = 0; else if (rmx > 100) rmx = 100; zephyrus.rollImax = rmx;
    float pp  = _pteroParamFloat(request, "pid_pitch_p", zephyrus.pitchKp);  if (pp  < 0) pp  = 0; else if (pp  > 8)   pp  = 8;   zephyrus.pitchKp  = pp;
    float pi  = _pteroParamFloat(request, "pid_pitch_i", zephyrus.pitchKi);  if (pi  < 0) pi  = 0; else if (pi  > 2)   pi  = 2;   zephyrus.pitchKi  = pi;
    float pd  = _pteroParamFloat(request, "pid_pitch_d", zephyrus.pitchKd);  if (pd  < 0) pd  = 0; else if (pd  > 2)   pd  = 2;   zephyrus.pitchKd  = pd;
    float pmx = _pteroParamFloat(request, "pid_pitch_max", zephyrus.pitchImax); if (pmx < 0) pmx = 0; else if (pmx > 100) pmx = 100; zephyrus.pitchImax = pmx;
    float yp  = _pteroParamFloat(request, "pid_yaw_p",  zephyrus.yawKp);     if (yp  < 0) yp  = 0; else if (yp  > 8)   yp  = 8;   zephyrus.yawKp   = yp;
    float yi  = _pteroParamFloat(request, "pid_yaw_i",  zephyrus.yawKi);     if (yi  < 0) yi  = 0; else if (yi  > 2)   yi  = 2;   zephyrus.yawKi   = yi;
    float yd  = _pteroParamFloat(request, "pid_yaw_d",  zephyrus.yawKd);     if (yd  < 0) yd  = 0; else if (yd  > 2)   yd  = 2;   zephyrus.yawKd   = yd;
    float ymx = _pteroParamFloat(request, "pid_yaw_max", zephyrus.yawImax);  if (ymx < 0) ymx = 0; else if (ymx > 100) ymx = 100; zephyrus.yawImax = ymx;
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
    float   rudRng= (float)_pteroParamInt(request, "rudder_ferocity_range", (int)defaults.rudderFerocityRange);
    float   rudAmpDiff = (float)_pteroParamInt(request, "rudder_amplitude_differential", (int)defaults.rudderAmplitudeDifferential);
    float   elevFerMix = (float)_pteroParamInt(request, "elevator_ferocity_mix", (int)defaults.elevatorFerocityMix);
    float   elevFerRateMix = (float)_pteroParamInt(request, "elevator_ferocity_rate_mix", (int)defaults.elevatorFerocityRateMix);
    if (elevFerRateMix < 0.0f) elevFerRateMix = 0.0f;
    if (elevFerRateMix > 100.0f) elevFerRateMix = 100.0f;
    float   elevAntiGravMix = (float)_pteroParamInt(request, "elevator_antigravity_mix", (int)defaults.elevatorAntigravityMix);
    if (elevAntiGravMix < 0.0f) elevAntiGravMix = 0.0f;
    if (elevAntiGravMix > 100.0f) elevAntiGravMix = 100.0f;
    float   thrThrustShape = (float)_pteroParamInt(request, "throttle_thrust_shape_mix", (int)defaults.throttleThrustShapeMix);
    if (thrThrustShape < 0.0f) thrThrustShape = 0.0f;
    if (thrThrustShape > 100.0f) thrThrustShape = 100.0f;
    float   thrThrustExpo = (float)_pteroParamInt(request, "throttle_thrust_expo", (int)defaults.throttleThrustExpo);
    if (thrThrustExpo < ORNI_THRUST_EXPO_MIN) thrThrustExpo = ORNI_THRUST_EXPO_MIN;
    if (thrThrustExpo > ORNI_THRUST_EXPO_MAX) thrThrustExpo = ORNI_THRUST_EXPO_MAX;
    float   thrFreqMix = (float)_pteroParamInt(request, "throttle_frequency_mix", (int)defaults.throttleFrequencyMix);
    if (thrFreqMix < 0.0f) thrFreqMix = 0.0f;
    if (thrFreqMix > 100.0f) thrFreqMix = 100.0f;
    float   ferShapeMix = (float)_pteroParamInt(request, "ferocity_shape_mix", (int)defaults.ferocityShapeMix);
    if (ferShapeMix < 0.0f) ferShapeMix = 0.0f;
    if (ferShapeMix > 100.0f) ferShapeMix = 100.0f;
    float   stSkew = (float)_pteroParamInt(request, "stroke_skew", (int)defaults.strokeSkew);
    float   rtSkew = (float)_pteroParamInt(request, "return_skew", (int)defaults.returnSkew);
    if (stSkew < ORNI_SKEW_MIN) stSkew = ORNI_SKEW_MIN;
    if (stSkew > ORNI_SKEW_MAX) stSkew = ORNI_SKEW_MAX;
    if (rtSkew < ORNI_SKEW_MIN) rtSkew = ORNI_SKEW_MIN;
    if (rtSkew > ORNI_SKEW_MAX) rtSkew = ORNI_SKEW_MAX;
    float   ailSkewMix = (float)_pteroParamInt(request, "aileron_skew_mix", (int)defaults.aileronSkewMix);
    if (ailSkewMix < 0.0f) ailSkewMix = 0.0f;
    if (ailSkewMix > 100.0f) ailSkewMix = 100.0f;
    float   thrSkewRateMix = (float)_pteroParamInt(request, "throttle_skew_rate_mix", (int)defaults.throttleSkewRateMix);
    if (thrSkewRateMix < 0.0f) thrSkewRateMix = 0.0f;
    if (thrSkewRateMix > 100.0f) thrSkewRateMix = 100.0f;
    float   ailSkewRateMix = (float)_pteroParamInt(request, "aileron_skew_rate_mix", (int)defaults.aileronSkewRateMix);
    if (ailSkewRateMix < 0.0f) ailSkewRateMix = 0.0f;
    if (ailSkewRateMix > 100.0f) ailSkewRateMix = 100.0f;

    if (fp >= 0 && fp < FLIGHT_PROFILE_COUNT) {
        // Write to a specific flight-profile slot (and apply live if active).
        ornithopter.setFlightProfileParams((uint8_t)fp, sf, rf, glide, flapAng, rudRng, rudAmpDiff, elevFerMix, thrThrustExpo, thrThrustShape, thrFreqMix, ferShapeMix, stSkew, rtSkew, ailSkewMix, thrSkewRateMix, ailSkewRateMix, elevFerRateMix, elevAntiGravMix);
    } else {
        // Legacy/global path: apply to live fields + store into active profile.
        ornithopter.strokeFerocity      = sf;
        ornithopter.returnFerocity      = rf;
        ornithopter.glideAngleDeg       = glide;
        ornithopter.flappingAngleDeg    = flapAng;
        ornithopter.rudderFerocityRange = rudRng;
        ornithopter.rudderAmplitudeDifferential = rudAmpDiff;
        ornithopter.elevatorFerocityMix = elevFerMix;
        ornithopter.elevatorFerocityRateMix = elevFerRateMix;
        ornithopter.elevatorAntigravityMix = elevAntiGravMix;
        ornithopter.throttleThrustShapeMix = thrThrustShape;
        ornithopter.throttleThrustExpo = thrThrustExpo;
        ornithopter.throttleFrequencyMix = thrFreqMix;
        ornithopter.ferocityShapeMix = ferShapeMix;
        ornithopter.strokeSkew       = stSkew;
        ornithopter.returnSkew       = rtSkew;
        ornithopter.aileronSkewMix      = ailSkewMix;
        ornithopter.throttleSkewRateMix = thrSkewRateMix;
        ornithopter.aileronSkewRateMix = ailSkewRateMix;
        ornithopter.setFlightProfileParams(ornithopter.activeFlightProfile, sf, rf, glide, flapAng, rudRng, rudAmpDiff, elevFerMix, thrThrustExpo, thrThrustShape, thrFreqMix, ferShapeMix, stSkew, rtSkew, ailSkewMix, thrSkewRateMix, ailSkewRateMix, elevFerRateMix, elevAntiGravMix);
    }

    // Global mixer params (not per-profile)
    ornithopter.rudderYawWeight    = (float)_pteroParamInt(request, "rudder_yaw_weight",  (int)ornithopter.rudderYawWeight);
    ornithopter.rudderRollWeight   = (float)_pteroParamInt(request, "rudder_roll_weight", (int)ornithopter.rudderRollWeight);
    ornithopter.aileronScale       = (float)_pteroParamInt(request, "aileron_scale",      (int)ornithopter.aileronScale);
    ornithopter.elevatorScale      = (float)_pteroParamInt(request, "elevator_scale",     (int)ornithopter.elevatorScale);
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

    // Glitch Filter — CRSF channel glitch filter (runtime-configurable)
    {
        bool ga = _pteroParamInt(request, "glitch_active", ornithopter.glitchActive ? 1 : 0) != 0;
        int32_t dMax = _pteroParamInt(request, "glitch_delta_max", ornithopter.glitchDeltaMax);
        int32_t dArm = _pteroParamInt(request, "glitch_delta_arm", ornithopter.glitchDeltaArm);
        int32_t dPers = _pteroParamInt(request, "glitch_delta_persist", ornithopter.glitchDeltaPersist);
        int32_t lSim = _pteroParamInt(request, "glitch_simult_threshold", ornithopter.glitchSimultThreshold);
        if (dMax < 20)  dMax = 20;   else if (dMax > 500)  dMax = 500;
        if (dArm < 100) dArm = 100;  else if (dArm > 1639) dArm = 1639;
        if (dPers < 20) dPers = 20;  else if (dPers > 1000) dPers = 1000;
        if (lSim < 2)   lSim = 2;    else if (lSim > STK_COUNT) lSim = STK_COUNT;
        ornithopter.configureGlitchFilter(ga, (uint16_t)dMax, (uint16_t)dArm, (uint16_t)dPers, (uint8_t)lSim);
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
    if (heapCriticallyLow()) { serveHeapBusy(request); return; }
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

// POST /pteronautos/orientation — form-urlencoded: rotation=0..6
// Runtime MPU board-rotation change + persist. The WebUI API.setOrientation
// posts here (this route was previously missing → 404 → the panel selector
// silently no-oped). setBoardRotation remaps axes on the next update() tick.
#ifdef ZEPHYRUS_ENABLED
static void PostPteronautosOrientation(AsyncWebServerRequest *request)
{
    int rot = _pteroParamInt(request, "rotation", (int)zephyrus.boardRotation);
    if (rot < 0) rot = 0;
    if (rot > 6) rot = 6;
    zephyrus.setBoardRotation((uint8_t)rot);
    SaveOrnithopterConfig();
    request->send(200, "application/json", "{\"ok\":1}");
}
#endif

static void GetPteronautosStick(AsyncWebServerRequest *request)
{
    PostPteronautosStick(request); // same response
}
#endif // defined(PTERONAUTOS)


#if defined(PTERONAUTOS)
void registerPteronautosHandlers(AsyncWebServer &server)
{
    // Fallback/retry for recovery mode. Normal receiver startup restores before
    // outputs start; never allocate the config document inside a state poll.
    LoadOrnithopterConfig();

    server.on("/pteronautos/state", HTTP_GET, GetPteronautosState);
    server.on("/pteronautos/state/", HTTP_GET, GetPteronautosState);
    server.on("/pteronautos/ping", HTTP_GET, GetPteronautosPing);
    server.on("/pteronautos/diag", HTTP_GET, GetPteronautosDiag);
    server.on("/pteronautos/crash", HTTP_GET, GetPteronautosCrash);
    server.on("/pteronautos/i2cscan", HTTP_GET, GetPteronautosI2cScan);
    server.on("/pteronautos/config", HTTP_GET, GetPteronautosConfig);
    server.on("/pteronautos/config", HTTP_POST, PostPteronautosConfig);
    server.on("/pteronautos/settings", HTTP_GET, GetPteronautosSettings);
    server.on("/pteronautos/rx-config", HTTP_GET, GetPteronautosRxConfig);
    server.on("/pteronautos/sweep", HTTP_POST, PostPteronautosSweep);
    server.on("/pteronautos/sweep/status", HTTP_GET, GetPteronautosSweepStatus);
    server.on("/pteronautos/backup", HTTP_GET, GetPteronautosBackup);
    {
        auto *backupHandler = new AsyncCallbackJsonWebHandler("/pteronautos/backup", PostPteronautosBackup);
        backupHandler->setMaxContentLength(16384);
        server.addHandler(backupHandler);
    }
    server.on("/pteronautos/stick", HTTP_GET, GetPteronautosStick);
    server.on("/pteronautos/stick", HTTP_POST, PostPteronautosStick);
#ifdef ZEPHYRUS_ENABLED
    server.on("/pteronautos/orientation", HTTP_POST, PostPteronautosOrientation);
#endif
    server.on("/pteronautos/blackbox", HTTP_GET, GetPteronautosBlackbox);
}
#endif // defined(PTERONAUTOS)