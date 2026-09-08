// =============================================================================
//  YOSHIMITSU · yoshimitsu.ino — the config shell (Gralha Azul strategy)
// -----------------------------------------------------------------------------
//  This sketch is ONLY configuration. The whole Hermetic Shinobi — stances,
//  CRSF, MUSHIN, JIGUANG, the flasher bridge — lives in src/Yoshimitsu.h, and
//  every default in src/Yoshimitsu_Padraos.h. Update the dongle: install this
//  folder as an Arduino library, `git pull` (or drop a new release), and this
//  file stays untouched.
//
//  Override anything by defining it BEFORE the include. The guarded block
//  below mirrors the active pin map; edit values there (or define BOARD_CUSTOM
//  and set your own pins). Ronin never looks back.
// =============================================================================

// ── Optional overrides ───────────────────────────────────────────────
// #define JIGUANG      0    // hermetically deactivate the 極光 storyteller
// #define YOSHI_GYRO   0    // fly without the Zephyrus gyro link
// #define YOSHI_RGB    1    // force the WS2812B aurora on RP2040

// ── BOARD_CUSTOM: the single source of truth when your wiring differs ──
#ifdef BOARD_CUSTOM
  #define CRSF_UART_NUM   0        // 0 = Serial1 (UART0), 1 = Serial2 (UART1)  [RP2040]
  #define CRSF_TX_PIN     0        // RP2040 UART0 TX | S3 UART1 TX
  #define CRSF_RX_PIN     1        // RP2040 UART0 RX | S3 UART1 RX
  #define CRSF_BAUD       420000UL
  #define BRIDGE_UART_NUM 1        // RP2040 only
  #define BRIDGE_TX_PIN   8        // RP2040 UART1 TX | S3 UART2 TX (17)
  #define BRIDGE_RX_PIN   9        // RP2040 UART1 RX | S3 UART2 RX (18)
  #define BRIDGE_BAUD     115200UL
  #define SERVO_PIN_1     2
  #define SERVO_PIN_2     3
  #define SERVO_PIN_3     4
  #define SERVO_PIN_4    -1
  #define SERVO_PIN_5    -1
  #define SERVO_PIN_6    -1
  #define SERVO_PIN_7    -1
  #define SERVO_PIN_8    -1
  #define RX_BOOT_PIN     5        // receiver GPIO0 (active low) | S3 (9)
  #define RX_PWR_PIN      6        // P-MOSFET gate (LOW = receiver powered) | S3 (10)
  #define GYRO_SDA_PIN    10       // MPU6050 SDA (optional, Wire) | S3 (15)
  #define GYRO_SCL_PIN    11       // MPU6050 SCL | S3 (16)
  #define RGB_LED_PIN     16       // onboard WS2812B (RP2040)
  #define LED_PIN         21       // status LED, -1 disables (S3)
#endif

#include <Yoshimitsu.h>
