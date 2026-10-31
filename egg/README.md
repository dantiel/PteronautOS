# 🥚 Pteronaut-Egg

Ein **minimaler WiFi-Bootstrap** für den ESP8285 (1 MB Flash). Seine einzige
Aufgabe: ein volles PteronautOS-Image über WiFi annehmen und via
`ESP8266 Updater` (`U_FLASH`) stagen. Kein CRSF, kein WebUI, kein ArduinoJson,
kein littlefs — nur AP-Modus + roher TCP-Server + die Core-`Updater`-Klasse.

## Warum das Ei existiert

Der 8285 hat eine einzige Sketch-Region von **991 216 B** (Linker
`elrs.flash.1m.ld`). Ein volles PteronautOS-Image ist **636 064 B** groß. Ein
laufendes Image kann daher nur ein Image nachladen, das in seinen *freien*
Staging-Space passt:

```
freier Space = 995 328 − (laufende Größe + 4096 aufgerundet)
```

| laufend | freier Space | kann nachladen |
|---|---|---|
| **Egg** (289 KB) | **680 KB** | Full (636 KB) ✅ |
| **Full** (636 KB) | **348 KB** | Egg (289 KB) ✅ |
| Full (636 KB) | 348 KB | Slim (607 KB) ❌ |

Das Ei ist **klein genug** (296 159 B ≈ 289 KB, Ziel ≤ 320 KB), dass der
Zyklus **Egg → Full → Egg** unendlich oft schließt — rein über WiFi, ohne
FTDI, ohne RP2040-Begleiter.

## Bauen

```sh
pio run -d egg
# → egg/.pio/build/egg/firmware.bin  (~289 KB)
```

Das Ei nutzt exakt denselben Linker wie PteronautOS (`elrs.flash.1m.ld`), damit
littlefs/eeprom/rfcal/wifi bei einem `U_FLASH`-Wechsel an derselben Adresse
landen. Beide Dateien **byte-identisch halten** — eine Änderung immer in beiden
(`src/elrs.flash.1m.ld` und `egg/elrs.flash.1m.ld`).

## Einmalige Erstinstallation

Das Ei selbst braucht **einen** UART-Flash (es ist kein Firmware-Update, sondern
der Bootstrap):

```sh
# FTDI oder Yoshimitsu-Begleiter ('bl' → ESP.rebootIntoUartDownloadMode())
esptool.py -p <port> -b 460800 write_flash 0x0 egg/.pio/build/egg/firmware.bin
```

## Der Zwei-Phasen-Zyklus (danach rein WiFi)

1. **Egg läuft** → öffnet AP `PTERONAUT-EGG` (Passwort `hatchnatural`) →
   `http://pteronaut-egg.local` (Fallback `http://192.168.4.1`) →
   `firmware.bin` (636 KB) auf das ASCII-Ei ziehen (Drag&Drop) oder klicken →
   Ei staget und rebootet in volles PteronautOS.
2. **Voll läuft** → bestehende WebUI → Firmware-Upload → `egg.bin` (277 KB)
   hochladen („Flash Anyway" bei Target-Mismatch) → rebootet ins Ei.

Pro Update: **2 Uploads, ~913 KB WiFi-Traffic, null Hardware.**

## Brick-Risiko (ehrlich)

Wie bei jedem OTA: flasht man über das Ei ein **kaputtes** Voll-Image (kein
WiFi mehr), braucht man doch wieder UART. Das Ei reduziert das Risiko nicht
unter OTA-Niveau — es entfernt nur das Kabel aus dem *normalen* Update-Pfad.

## Sicherheit

Der AP ist passwortgeschützt (`hatchnatural`). Da ein offener AP es jedem in
Reichweite erlaubte, Firmware zu flashen, ist ein Passwort Pflicht. Bei Bedarf
in `src/main.cpp` (`AP_SSID`/`AP_PASS`/`AP_HOST`) ändern.

## mDNS

Das Ei antwortet auf `http://pteronaut-egg.local` (Bonjour/mDNS). Fallback,
falls der Client kein mDNS kann (z. B. manches Android): `http://192.168.4.1`.