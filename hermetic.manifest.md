# HERMETIC MANIFEST — PteronautOS

> *O único controlador de voo para ornitópteros em hardware ExpressLRS —
> servo-asa batente **e** modelos com caixa de engrenagens (gearbox).*

## Missão

O PteronautOS é o **controlador de voo de ornitópteros para hardware ExpressLRS** —
o único, por desígnio. Traduz CRSF em movimento alado, quer o batimento venha de
**servos de asa** (forma de onda), quer de **caixa de engrenagens** (motor + cauda).
Não é um quad, nem um flight controller genérico.

**Corre em TODO o hardware ExpressLRS** — com giroscópio ou sem ele. Sem IMU o
módulo Zephyrus desliga-se e o ornitóptero voa puro CRSF→PWM (graceful degradation,
nunca um boot-loop). A prioridade absoluta é **versatilidade, configurabilidade e
consciência de espaço e de runtime** — o alvo é o ESP8285 @ 80 MHz, 1 MB flash.

### Separação de responsabilidades

| Plataforma | Papel |
|---|---|
| **PteronautOS** | O controlador de voo de ornitópteros **único e enxuto** para hardware ELRS. Rate damper + self-level básico, mixer de asa/cauda, failsafe centrado. **Sem GPS, sem VTX, sem pilha de navegação.** |
| **OrniFlight** | FC completo e rico — GPS, VTX, waypoints, blackbox rico, estabilização avançada (ESPELHO, ONDAS, aeroelasticidade) — para hardware RP2040/STM32. |

**O que NÃO pertence ao PteronautOS:** GPS, VTX, waypoints, navegação, fusão de
sensores rica, estabilização avançada. Estas vivem no OrniFlight. O PteronautOS
faz **uma** coisa — voar um ornitóptero num ELRS RX — e fá-lo com margem de
RAM/Flash. Cada feature acrescentada tem de justificar o seu custo no ESP8285;
se não justificar, vai para o OrniFlight.

## Essência

O PteronautOS é um **fork do ExpressLRS 4.x** transformado em controlador de voo
para ornitóptero. Não é um quad — não há motores, nem ESC, nem Betaflight.
O movimento é gerado por **forma de onda** que pilota servos de asa (left/right)
e um servo de leme (crista cefálica), **ou** por **mixer de gearbox** que pilota
motor + cauda. O rádio envia CRSF — o PteronautOS traduz em batimento alado (servo)
ou em tracção alada (gearbox).

## Arquitectura

```
CRSF (rádio) → rx_main → Ornithopter → waveform (servo)  → PWM servos
                              │        └ mixer (gearbox) → PWM motor+cauda
                              ↑
                      Zephyrus (gyro → Mesozoic rate/attitude → correcção)
```

### Módulos

| Módulo | Directório | Função |
|---|---|---|
| **Ornithopter** | `src/lib/Ornithopter/` | Mixer servo + gearbox, waveform, filtro de canais |
| **Zephyrus** | `src/lib/Zephyrus/` | MPU6050/LSM6DS3 + Mesozoic (rate damper + attitude) — **sem Mahony** |
| **ServoOutput** | `src/lib/ServoOutput/devServoOutput.cpp` | PWM sincronizado ao tick CRSF |
| **Target** | `src/targets/pteronautos-rx.ini` | ESP8285 2400 RX |

## Hardware

- **MCU:** ESP8285 @ 80MHz, 1MB Flash (alvo; o firmware corre em todo o hardware ELRS)
- **Rádio:** ExpressLRS 2.4GHz (SX1280)
- **Giroscópio (opcional):** MPU6050 (GY-521) ou LSM6DS3 (GY-LSM6DS3) — I2C GPIO1(SDA)/GPIO3(SCL)
- **Servos:** 3+ canais PWM (asa esquerda, asa direita, leme — ou motor + cauda no gearbox)
- **Build:** `pio run -e PteronautOS_ESP8285_2400_RX`

## Constantes de Compilação

- `-D PTERONAUTOS=1` — flag de identidade do projecto
- `-D ORNITHOPTER_MODE=1` — activa o módulo Ornithopter
- `-D MESOZOIC_ONLY=1` — estabilização rate-only (damper; default)
- `-D MESOZOIC_HORIZON=1` — estabilização self-level (rate + attitude, flap-decoupled)
- `-D MIXER_PROFILE=3..12` — selecciona mixer servo vs gearbox (ver `OrnithopterConfig.h`)
- `-D ZEPHYR_IMU=ZEPHYR_IMU_LSM6DS3` — usa o GY-LSM6DS3 em vez do GY-521 (default: MPU6050)
- `PLATFORM_ESP8285=1`

## Proibições

- **Nunca** usar alocação dinâmica (`malloc`, `new`) — stack e globais apenas
- **Nunca** usar `delay()` — quebra o timing CRSF
- **Nunca** bloquear o loop principal — tudo assíncrono
- **Nunca** remover o failsafe centrado — servo volta a 1500µs se perder sinal
- **Nunca** exceder 65% RAM ou 60% Flash — margem para expansão futura
- **Nunca** acrescentar GPS/VTX/navegação — pertencem ao OrniFlight, não ao PteronautOS

## Ethos

1. **O servo é o músculo** — PWM preciso, sem jitter, sincronizado ao tick CRSF
2. **A forma de onda é a alma** — o batimento alado vive nas curvas, não nos ângulos
3. **Failsafe é survival** — se o link cair, as asas centram, o leme centra
4. **Graça na degradação** — sem IMU, o módulo desliga-se sem afectar o voo
5. **Versátil e enxuto** — um firmware, todo o hardware ELRS, com e sem gyro, servo e gearbox
6. **Consciência de runtime** — cada feature paga o seu custo no ESP8285; senão, é OrniFlight
7. **Zero dependências externas** — tudo no repositório, sem bibliotecas de terceiros
8. **Commits são decisões** — cada commit é intenção consciente, nunca automático; a história conta a alma do código

## Stack

- PlatformIO + ESP8285 Arduino Core
- ExpressLRS 4.x (fork)
- C++17, sem excepções, sem RTTI
- I2C directo (Wire) para MPU6050/LSM6DS3
- CRSF protocol (canais RC + telemetria)

## Linhagem

Este projecto descende directamente dos sketches em `../Arduino/`. Ver o manifesto
irmão para a história evolutiva completa.