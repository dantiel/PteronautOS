# HERMETIC MANIFEST — PteronautOS

> *Firmware para ornitóptero biomecânico com servo-asa batente, correndo num
> receptor ESP8285 a 80MHz. O veículo é um pterossauro biomecânico com asa
> batente e leme de crista cefálica.*

## Essência

O PteronautOS é um **fork do ExpressLRS 4.x** transformado em controlador de voo
para ornitóptero. Não é um quad — não há motores, nem ESC, nem Betaflight.
O movimento é gerado por **forma de onda** que pilota dois servos de asa (left/right)
e um servo de leme (crista cefálica). O rádio envia CRSF — o PteronautOS traduz
em batimento alado.

## Arquitectura

```
CRSF (rádio) → rx_main → Ornithopter → waveform → PWM servos
                           ↑
                      Zephyrus (MPU6050 gyro → PID → crest rudder correction)
```

### Módulos

| Módulo | Directório | Função |
|---|---|---|
| **Ornithopter** | `src/lib/Ornithopter/` | Mixer, waveform, filtro de canais |
| **Zephyrus** | `src/lib/Zephyrus/` | MPU6050 + Mahony AHRS + PID duplo |
| **ServoOutput** | `src/lib/ServoOutput/devServoOutput.cpp` | PWM sincronizado ao tick CRSF |
| **Target** | `src/targets/pteronautos-rx.ini` | ESP8285 2400 RX |

## Hardware

- **MCU:** ESP8285 @ 80MHz, 1MB Flash
- **Rádio:** ExpressLRS 2.4GHz (SX1280)
- **Giroscópio:** MPU6050 (GY-521) — I2C GPIO4(SDA)/GPIO5(SCL)
- **Servos:** 3 canais PWM (asa esquerda, asa direita, leme)
- **Build:** `pio run -e PteronautOS_ESP8285_2400_RX`

## Constantes de Compilação

- `-D PTERONAUTOS=1` — flag de identidade do projecto
- `-D ORNITHOPTER_MODE=1` — activa o módulo Ornithopter
- `PLATFORM_ESP8285=1`

## Proibições

- **Nunca** usar alocação dinâmica (`malloc`, `new`) — stack e globais apenas
- **Nunca** usar `delay()` — quebra o timing CRSF
- **Nunca** bloquear o loop principal — tudo assíncrono
- **Nunca** remover o failsafe centrado — servo volta a 1500µs se perder sinal
- **Nunca** exceder 65% RAM ou 60% Flash — margem para expansão futura

## Ethos

1. **O servo é o músculo** — PWM preciso, sem jitter, sincronizado ao tick CRSF
2. **A forma de onda é a alma** — o batimento alado vive nas curvas, não nos ângulos
3. **Failsafe é survival** — se o link cair, as asas centram, o leme centra
4. **Graça na degradação** — se o MPU6050 falhar, o módulo desliga-se sem afectar o voo
5. **Zero dependências externas** — tudo no repositório, sem bibliotecas de terceiros

## Stack

- PlatformIO + ESP8285 Arduino Core
- ExpressLRS 4.x (fork)
- C++17, sem excepções, sem RTTI
- I2C directo (Wire) para MPU6050
- CRSF protocol (canais RC + telemetria)

## Linhagem

Este projecto descende directamente dos sketches em `../Arduino/`. Ver o manifesto
irmão para a história evolutiva completa.
