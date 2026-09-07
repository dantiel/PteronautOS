// esp32s3_flash_bridge.ino — ESP32-S3 pocket flasher for ELRS 8285 receivers
// --------------------------------------------------------------------------
// Turns a ~6 USD ESP32-S3 into a comfortable EP2-class flasher + console:
//   · USB (native CDC)  ↔  UART1  →  esptool writes straight through
//   · GPIO9  drives the receiver's BOOT pad (active low)
//   · GPIO10 gates a P-MOSFET that switches the receiver's 3.3 V rail
//   · BOOT ×2 within 1.5 s  →  flash mode (boot-hold + power-cycle dance)
//   · single BOOT press     →  restart the receiver (run the new firmware)
//   · serial line "FLASH"   →  same as BOOT ×2
//
//  Wiring:
//    S3 3V3   ──► S ── P-MOSFET (e.g. AO3401) ── D ──► RX 3V3
//    S3 GPIO10 ──► gate           (10 kΩ pull-up to 3V3; LOW = power ON)
//    S3 GPIO9  ──► RX BOOT pad
//    S3 GPIO18 ◄── RX TX          (U1 RX)
//    S3 GPIO17 ──► RX RX          (U1 TX)
//    S3 GND     ──► RX GND
//
//  Flash (macOS example):
//    python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
//      --baud 115200 --before no_reset write_flash \
//      --flash_mode dout --flash_size 1MB --flash_freq 40m \
//      0x0 firmware.bin
//  NEVER use --before default_reset over this bridge — the power-cycle IS the reset.
//
//  Target: any ESP32-S3 with native USB (Waveshare Tiny/Micro/Nano, generic devkit).

#include <Arduino.h>

#ifndef BRIDGE_TX_PIN
#define BRIDGE_TX_PIN 17          // S3 U1 TX  → receiver RX
#endif
#ifndef BRIDGE_RX_PIN
#define BRIDGE_RX_PIN 18          // S3 U1 RX  ← receiver TX
#endif
#ifndef RX_PWR_PIN
#define RX_PWR_PIN 10             // P-MOSFET gate (active-low power switch)
#endif
#ifndef RX_BOOT_PIN
#define RX_BOOT_PIN 9             // receiver BOOT pad (active low)
#endif
#ifndef BRIDGE_BAUD
#define BRIDGE_BAUD 115200UL
#endif

#define PRESS_WINDOW_MS 1500
#define DEBOUNCE_MS     40
#define HOLD_OFF_MS     120       // receiver power-off during the dance
#define HOLD_ON_MS      900       // wait inside bootloader after re-power
#define RX_RESTART_MS   700

static uint32_t lastPressMs = 0;
static bool     armed      = false;

static void rxPower(bool on) {
  digitalWrite(RX_PWR_PIN, on ? LOW : HIGH);    // gate LOW = MOSFET on
}

static void bootAssert(bool hold) {
  digitalWrite(RX_BOOT_PIN, hold ? LOW : HIGH);
}

static void restartRx() {
  rxPower(false);
  delay(HOLD_OFF_MS);
  rxPower(true);
  delay(RX_RESTART_MS);
  Serial.println("RX restarted — new firmware should be running.");
}

static void enterFlashMode() {
  Serial.println("FLASH MODE: cycling receiver power with BOOT held...");
  bootAssert(true);                             // 1. hold BOOT low
  rxPower(false);                               // 2. kill power
  delay(HOLD_OFF_MS);
  rxPower(true);                                // 3. power up into bootloader
  delay(HOLD_ON_MS);
  bootAssert(false);                            // 4. release BOOT
  delay(80);
  Serial.println("Receiver in ROM bootloader — run esptool with --before no_reset now.");
}

static void pump() {
  if (Serial1.available() && Serial.availableForWrite()) {
    Serial.write(Serial1.read());
  }
  if (Serial.available() && Serial1.availableForWrite()) {
    Serial1.write(Serial.read());
  }
}

// Command channel: exact "FLASH<CR|LF>" is consumed locally, anything else passes through.
static void handleUsb() {
  static char line[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (n < sizeof(line) - 1) line[n++] = c;
    if (c == '\n' || c == '\r') {
      line[n] = '\0';
      bool isFlash = (n == 6 && strncmp(line, "FLASH", 5) == 0);
      if (isFlash) enterFlashMode();
      else Serial1.write((const uint8_t *)line, n);   // passthrough non-command line
      n = 0;
    }
    if (n >= sizeof(line) - 1) n = 0;                 // overflow guard
  }
}

static void handleBootButton() {
  static uint8_t  last = HIGH;
  static uint32_t stableSince = 0;
  uint8_t now = digitalRead(0);                        // BOOT button, pulled up
  if (now != last) {
    stableSince = millis();
    last = now;
    return;
  }
  if (now == LOW && (millis() - stableSince) > DEBOUNCE_MS) {
    stableSince = millis() + 1000000UL;                // one edge per press
    if (armed && (millis() - lastPressMs) <= PRESS_WINDOW_MS) {
      armed = false;
      enterFlashMode();                                // BOOT ×2 → flash
    } else {
      armed = true;
      lastPressMs = millis();                          // BOOT ×1 → maybe restart
    }
  }
  if (armed && (millis() - lastPressMs) > PRESS_WINDOW_MS) {
    armed = false;
    restartRx();                                       // single press timed out → restart
  }
}

void setup() {
  pinMode(RX_PWR_PIN, OUTPUT);
  pinMode(RX_BOOT_PIN, OUTPUT);
  pinMode(0, INPUT_PULLUP);                            // BOOT button

  bootAssert(false);
  rxPower(true);                                       // receiver powered by default
  delay(50);

  Serial.begin(115200);                                // USB CDC
  Serial1.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);
  Serial.println("ESP32-S3 flash bridge ready. BOOT ×2 = flash mode · BOOT ×1 = restart RX");
}

void loop() {
  handleBootButton();
  handleUsb();
  pump();
}
