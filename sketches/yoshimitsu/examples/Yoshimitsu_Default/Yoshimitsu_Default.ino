// =============================================================================
//  YOSHIMITSU · Yoshimitsu_Default.ino — the relaxed sketch (Arduino IDE example)
// -----------------------------------------------------------------------------
//  File → Examples → Yoshimitsu → Yoshimitsu_Default
//
//  This sketch is ONLY configuration. The whole Hermetic Shinobi — stances,
//  CRSF, MUSHIN, JIGUANG, the flasher bridge — lives in src/Yoshimitsu.h, and
//  every default in src/Yoshimitsu_Loadout.h (the starting loadout).
//
//  Override anything by defining it BEFORE the include: pick a board type
//  (BOARD_*) and/or override individual pins. Ronin never looks back.
// =============================================================================

// ~~~~~~ Optional overrides ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// #define JIGUANG      0    // hermetically deactivate the 極光 storyteller
// #define YOSHI_GYRO   0    // fly without the Zephyrus gyro link
// #define YOSHI_RGB    0    // RP2040 only — turn OFF the onboard WS2812B aurora

// ~~~~~~ Board type (optional — auto-detected from the chip) ~~~~~~~~~~~
//   RP2040    → BOARD_RP2040_TINY (default) · BOARD_RP2040_ZERO
//   ESP32-S3  → BOARD_S3_WAVESHARE (default)
// #define BOARD_RP2040_ZERO         // e.g. build for a Waveshare RP2040-Zero

// ~~~~~~ Per-pin overrides (arbitrary wiring differences) ~~~~~~~~~~~~~~
// Define only the pins that differ from your board's defaults. Full list:
//   CRSF_UART_NUM, CRSF_TX_PIN, CRSF_RX_PIN, CRSF_BAUD,
//   BRIDGE_UART_NUM, BRIDGE_TX_PIN, BRIDGE_RX_PIN, BRIDGE_BAUD,
//   SERVO_PIN_1..8, RX_BOOT_PIN, RX_PWR_PIN,
//   GYRO_SDA_PIN, GYRO_SCL_PIN, RGB_LED_PIN (RP2040) / LED_PIN (S3)
// #define SERVO_PIN_3    12         // move servo 3 to GPIO12
// #define GYRO_SCL_PIN    9         // rewire the MPU6050 clock line

#include <Yoshimitsu.h>