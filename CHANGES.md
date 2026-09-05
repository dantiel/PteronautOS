# PteronautOS - Änderungszusammenfassung

## 🎯 Kernel-Erweiterungen: Zwei neue Flight-Profile-Parameter

### 1. Throttle-Frequency Coupling (`throttleFrequencyMix`)
**Parameter:** `throttle_frequency_mix` (0-100%)

**Funktion:** Blendet den unabhängigen CH6-Frequenzbefehl Richtung Throttle.

- **0%** (Default): CH6 steuert die Frequenz unabhängig
- **100%**: Frequenz folgt Throttle-Amplitude, beide skalieren zusammen
- **Dazwischen:** Kontinuierliche Mischung

**Use-Case:** Natürlicheres Flugverhalten wenn Frequenz und Amplitude korreliert sind.

**Implementierung:**
- Neue Helper-Funktion `orniThrottleFrequencyCommand()` in `OrnithopterConfig.h`
- Clamp-Helper `orniClamp01()` für normierte Werte
- Arduino-frei, direkt unit-testbar

---

### 2. Ferocity-Shape Mix (`ferocityShapeMix`)
**Parameter:** `ferocity_shape_mix` (0-100%)

**Funktion:** Transmutiert die Wellenform des Flappings.

- **0%**: Ursprüngliches GralhaAzul-Trapez (Plateau + Cosine-Ramp)
- **100%**: Abgerundete Pyramide (konstante Geschwindigkeit im Mittelteil, sanfte Umkehr)
- **Dazwischen:** Kontinuierliche Überblendung

**Besonderheit:** Die "Spitzheit" koppelt an `ferocity` → hohe Ferocity = direkter, pyramidal; niedrige = sinus-ähnlich.

**Mathematik:**
- `pointK = kMaxPoint * (2*ferocity01 - ferocity01²)` → eased mapping
- `pointed = asin(pointK * cos(π*t)) / asin(pointK)` → sichere Umkehr

**Resultat:** Belastbare Unendlichkeiten werden vermieden, endliche Beschleunigung bei Umkehr.

---

## 🧪 Test-Suite & CI

### Neue Unit-Tests (`src/test/profile-save/`)
1. **`test-writer.cpp`** - Config-Writer Roundtrip
2. **`test-throttle-frequency.cpp`** - Throttle-Frequency Blend-Tests
3. **`test-ferocity-shape.cpp`** - Shape-Mix Boundary-Tests

### Test-Script für WebUI (`src/html/scripts/`)
- **`test-flight-profiles.mjs`** - Node.js CLI für Profile-Save-API-Tests

**Aufbau:**
- Arduino-Mocks für `Print`, `File`, `LittleFS`
- Host-gcc kompilierbar, läuft ohne Hardware

---

## 🎨 Documentation & Assets

### Neue Docs-Assets
- **`docs/assets/fonts/turret-road-*`** - Custom Monospace-Font für Codeblocks
- **`docs/assets/js/ferocity-waveform.*`** - Interaktive Ferocity-Visualisierung (CoffeeScript)
- **`docs/scripts/monospace_turret_road.py`** - Font-Subset-Generator

### Doku-Updates
- **`docs/documentation/index.haml`** - Neue Ferocity-Shape-Dokumentation
- **`docs/tutorials/flashing/index.haml`** - WiFi-Passwort-Hinweis (`flynatural`)
- **`docs/assets/css/style.sass`** - Font-Integration, Syntax-Highlighting-Refinements

---

## 🌐 WebUI-Erweiterungen

### Flight-Profiles-Panel
- Zwei neue Slider: `throttleFrequencyMix`, `ferocityShapeMix`
- Verbesserte Save-State-Logik:
  - `_unsavedProfiles` Tracking
  - `_profileSaveErrors` Visualisierung
  - `_saveQueued` Debouncing
- Schema-Konstante `PROFILE_FIELDS` für konsistente JSON-Keys

### i18n (11 Sprachen)
Neue Keys in allen Locales:
- `ornithopter.throttle_frequency_mix.title`
- `ornithopter.throttle_frequency_mix.desc`
- `ornithopter.ferocity_shape_mix.title`
- `ornithopter.ferocity_shape_mix.desc`

---

## ⚙️ Firmware-Changes

### `Ornithopter.h/cpp`
- Neue Member: `throttleFrequencyMix`, `ferocityShapeMix`
- `setFlightProfileParams()` erweitert um 2 Parameter
- Flight-Profile-Defaults angepasst (12 Felder)

### `OrnithopterWaveform.h`
- `shapeWave()` erhält `shapeMixPercent` Parameter
- Fast-Path für `ferocity >= 7.999` entfernt (jetzt durch Shape-Mix kontrolliert)
-bergerende Konstanten: `kMaxDwell = 0.98f`, `kMaxPoint = 0.98f`

### `devWIFI.cpp`
- JSON-Keys für neue Parameter: `throttle_frequency_mix`, `ferocity_shape_mix`
- Save-Endpoint `/pteronautos/config` verarbeitet neue Felder

---

## 📊 Statistik

```
43 files changed, 31741 insertions(+), 29006 deletions(-)
```

**Kategorisierung:**
- Firmware (C++): 11 Dateien
- WebUI (CoffeeScript/LitHaml): 2 Dateien  
- i18n (11 Sprachen): 11 Dateien
- Docs (HTML/HAML/SASS): 12 Dateien
- Assets/Scripts: 7 Dateien

---

## 🔗 Commit-Message

```
feat(kernel+webui): add throttle-frequency coupling and ferocity-shape mixing

- New flight-profile params: throttleFrequencyMix (CH6-throttle blend),
  ferocityShapeMix (plateau/pyramidal waveform)
- Unit-test suite for profile save/load (src/test/profile-save/)
- WebUI sliders + i18n for new params (11 languages)
- Interactive ferocity-waveform visualization in docs
- Custom Turret-Road Mono font for codeblocks

Throttle-Frequency: 0% = independent CH6, 100% = locked to amplitude.
Ferocity-Shape: 0% = trapezoidal plateau, 100% = rounded pyramidal stroke.

Files: 43 changed, +31741/-29006
Tests: test-writer, test-throttle-frequency, test-ferocity-shape
```
