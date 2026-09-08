/*
  YOSHIMITSU · Yoshimitsu_Padraos.h — Valores Padrão Internos

  Every default of the Hermetic Shinobi lives here as *_PADRAO values.
  The sketch overrides anything by defining it before the include;
  src/Yoshimitsu.h resolves names against these padrões. Update the
  library dongle without ever re-touching your config sketch.

  Strategy: Gralha Azul (o-grande-codigo-da-gralha-azul).
*/

#ifndef YOSHIMITSU_PADRAOS_H
#define YOSHIMITSU_PADRAOS_H

/* ── Feature gates ─────────────────────────────────────────────── */
#define JIGUANG_PADRAO             1
#define YOSHI_GYRO_PADRAO          1

/* ── Waveshare RP2040-Tiny / RP2040-Zero (same pinout) ────────── */
#define RP2040_TINY_CRSF_UART_NUM_PADRAO    0    // UART0 = Serial1
#define RP2040_TINY_CRSF_TX_PIN_PADRAO      0    // UART0 TX (wired for completeness)
#define RP2040_TINY_CRSF_RX_PIN_PADRAO      1    // UART0 RX ← receiver CRSF TX
#define RP2040_TINY_CRSF_BAUD_PADRAO        420000UL
#define RP2040_TINY_BRIDGE_UART_NUM_PADRAO  1    // UART1 = Serial2
#define RP2040_TINY_BRIDGE_TX_PIN_PADRAO    8    // UART1 TX → receiver RX
#define RP2040_TINY_BRIDGE_RX_PIN_PADRAO    9    // UART1 RX ← receiver TX
#define RP2040_TINY_BRIDGE_BAUD_PADRAO      115200UL
#define RP2040_TINY_SERVO_PIN_1_PADRAO      2
#define RP2040_TINY_SERVO_PIN_2_PADRAO      3
#define RP2040_TINY_SERVO_PIN_3_PADRAO      4
#define RP2040_TINY_SERVO_PIN_4_PADRAO     -1
#define RP2040_TINY_SERVO_PIN_5_PADRAO     -1
#define RP2040_TINY_SERVO_PIN_6_PADRAO     -1
#define RP2040_TINY_SERVO_PIN_7_PADRAO     -1
#define RP2040_TINY_SERVO_PIN_8_PADRAO     -1
#define RP2040_TINY_RX_BOOT_PIN_PADRAO      5    // receiver GPIO0 (active low)
#define RP2040_TINY_RX_PWR_PIN_PADRAO       6    // P-MOSFET gate (LOW = receiver powered)
#define RP2040_TINY_GYRO_SDA_PIN_PADRAO     10   // MPU6050 SDA (optional, Wire)
#define RP2040_TINY_GYRO_SCL_PIN_PADRAO     11   // MPU6050 SCL
#define RP2040_TINY_RGB_LED_PIN_PADRAO      16   // onboard WS2812B

/* ── Waveshare ESP32-S3-Tiny / -Micro / -Nano ─────────────────── */
#define S3_WAVESHARE_CRSF_RX_PIN_PADRAO     44   // UART1 RX ← receiver TX
#define S3_WAVESHARE_CRSF_TX_PIN_PADRAO     43   // UART1 TX → receiver RX
#define S3_WAVESHARE_CRSF_BAUD_PADRAO       420000UL
#define S3_WAVESHARE_BRIDGE_RX_PIN_PADRAO   18   // UART2 RX ← receiver TX
#define S3_WAVESHARE_BRIDGE_TX_PIN_PADRAO   17   // UART2 TX → receiver RX
#define S3_WAVESHARE_BRIDGE_BAUD_PADRAO     115200UL
#define S3_WAVESHARE_SERVO_PIN_1_PADRAO     1
#define S3_WAVESHARE_SERVO_PIN_2_PADRAO     2
#define S3_WAVESHARE_SERVO_PIN_3_PADRAO     3
#define S3_WAVESHARE_SERVO_PIN_4_PADRAO     4
#define S3_WAVESHARE_SERVO_PIN_5_PADRAO     5
#define S3_WAVESHARE_SERVO_PIN_6_PADRAO     6
#define S3_WAVESHARE_SERVO_PIN_7_PADRAO     7
#define S3_WAVESHARE_SERVO_PIN_8_PADRAO     8
#define S3_WAVESHARE_RX_PWR_PIN_PADRAO      10   // P-MOSFET gate (LOW = receiver powered)
#define S3_WAVESHARE_RX_BOOT_PIN_PADRAO     9    // receiver BOOT pad (active low)
#define S3_WAVESHARE_GYRO_SDA_PIN_PADRAO    15   // MPU6050 SDA (optional, Wire)
#define S3_WAVESHARE_GYRO_SCL_PIN_PADRAO    16   // MPU6050 SCL
#define S3_WAVESHARE_LED_PIN_PADRAO         21   // status LED (-1 disables)

/* ── BOARD_CUSTOM fallbacks (used when the sketch defines no pins) ── */
// RP2040 side
#define CUSTOM_RP2040_CRSF_UART_NUM_PADRAO   0
#define CUSTOM_RP2040_CRSF_TX_PIN_PADRAO     0
#define CUSTOM_RP2040_CRSF_RX_PIN_PADRAO     1
#define CUSTOM_RP2040_CRSF_BAUD_PADRAO       420000UL
#define CUSTOM_RP2040_BRIDGE_UART_NUM_PADRAO 1
#define CUSTOM_RP2040_BRIDGE_TX_PIN_PADRAO   8
#define CUSTOM_RP2040_BRIDGE_RX_PIN_PADRAO   9
#define CUSTOM_RP2040_BRIDGE_BAUD_PADRAO     115200UL
#define CUSTOM_RP2040_SERVO_PIN_1_PADRAO   2
#define CUSTOM_RP2040_SERVO_PIN_2_PADRAO   3
#define CUSTOM_RP2040_SERVO_PIN_3_PADRAO   4
#define CUSTOM_RP2040_SERVO_PIN_4_PADRAO   -1
#define CUSTOM_RP2040_SERVO_PIN_5_PADRAO   -1
#define CUSTOM_RP2040_SERVO_PIN_6_PADRAO   -1
#define CUSTOM_RP2040_SERVO_PIN_7_PADRAO   -1
#define CUSTOM_RP2040_SERVO_PIN_8_PADRAO   -1
#define CUSTOM_RP2040_RX_BOOT_PIN_PADRAO     5
#define CUSTOM_RP2040_RX_PWR_PIN_PADRAO      6
#define CUSTOM_RP2040_GYRO_SDA_PIN_PADRAO    10
#define CUSTOM_RP2040_GYRO_SCL_PIN_PADRAO    11
#define CUSTOM_RP2040_RGB_LED_PIN_PADRAO     16
// S3 side
#define CUSTOM_S3_CRSF_RX_PIN_PADRAO         44
#define CUSTOM_S3_CRSF_TX_PIN_PADRAO         43
#define CUSTOM_S3_CRSF_BAUD_PADRAO           420000UL
#define CUSTOM_S3_BRIDGE_RX_PIN_PADRAO       18
#define CUSTOM_S3_BRIDGE_TX_PIN_PADRAO       17
#define CUSTOM_S3_BRIDGE_BAUD_PADRAO         115200UL
#define CUSTOM_S3_SERVO_PIN_1_PADRAO       1
#define CUSTOM_S3_SERVO_PIN_2_PADRAO       2
#define CUSTOM_S3_SERVO_PIN_3_PADRAO       3
#define CUSTOM_S3_SERVO_PIN_4_PADRAO       4
#define CUSTOM_S3_SERVO_PIN_5_PADRAO       5
#define CUSTOM_S3_SERVO_PIN_6_PADRAO       6
#define CUSTOM_S3_SERVO_PIN_7_PADRAO       7
#define CUSTOM_S3_SERVO_PIN_8_PADRAO       8
#define CUSTOM_S3_RX_PWR_PIN_PADRAO          10
#define CUSTOM_S3_RX_BOOT_PIN_PADRAO         9
#define CUSTOM_S3_GYRO_SDA_PIN_PADRAO        15
#define CUSTOM_S3_GYRO_SCL_PIN_PADRAO        16
#define CUSTOM_S3_LED_PIN_PADRAO             21

/* ── CRSF / servo constants ────────────────────────────────────── */
#define SERVO_COUNT_MAX_PADRAO      8
#define CHANNEL_COUNT_PADRAO        16
#define CRSF_RC_TYPE_PADRAO         0x16
#define CRSF_PAYLOAD_PADRAO         22    // 16 ch × 11 bit = 22 bytes
#define RAW_MIN_PADRAO              172   // CRSF numeric window (PteronautOS)
#define RAW_MAX_PADRAO              1811
#define PWM_MIN_PADRAO              988
#define PWM_MAX_PADRAO              2012
#define FAILSAFE_MS_PADRAO          500
#define BRIDGE_BURST_PADRAO         8     // max bytes per direction per pass

/* ── Timing ────────────────────────────────────────────────────── */
#define HOLD_OFF_MS_PADRAO          120   // receiver power-off during the dance
#define HOLD_ON_MS_PADRAO           900   // wait inside bootloader after re-power
#define RX_RESTART_MS_PADRAO        700   // settle after a plain restart
#define SETTLE_MS_PADRAO            80    // BOOT release settle
#define BOOT_PRINT_MS_PADRAO        150   // USB settle gate before the boot banner
#define PRESS_WINDOW_MS_PADRAO      1500  // button (ESP32)
#define DEBOUNCE_MS_PADRAO          40
#define LONG_PRESS_MS_PADRAO        2000
#define RESET_TAP_WINDOW_SEC_PADRAO 2     // buttonless RESET-tap (RP2040)

/* ── Zephyrus gyro link ────────────────────────────────────────── */
#define MPU_ADDR_PADRAO             0x68
#define MPU_WHOAMI_PADRAO           0x75
#define MPU_PWR_PADRAO              0x6B
#define MPU_GYRO_CFG_PADRAO         0x1B
#define MPU_GZ_H_PADRAO             0x47
#define GYRO_SCALE_LSB_PADRAO       131   // ±250 dps
#define GYRO_GAIN_PADRAO            4     // µs of correction per dps (tune me)
#define GYRO_CORRECTION_SERVO_PADRAO 2    // servo index fed by the gyro (crest)
#define ARM_CHANNEL_PADRAO          4     // CRSF channel that arms (0-based)

#endif  // YOSHIMITSU_PADRAOS_H
