# PteronautOS WebUI — Developer Guide

> *The bird has three lungs: CoffeeScript breathes logic, LithAML breathes
> structure, Sass breathes form. Together they sing in the browser.*

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Build Pipeline](#2-build-pipeline)
3. [PteroElement — The Foundation](#3-pteroelement--the-foundation)
4. [i18n System](#4-i18n-system)
5. [Panel Anatomy](#5-panel-anatomy)
6. [LithAML Template DSL](#6-lithaml-template-dsl)
7. [Feature Flags](#7-feature-flags)
8. [Code Principles](#8-code-principles)
9. [Build & Run](#9-build--run)
10. [FAQ / Troubleshooting](#10-faq--troubleshooting)

---

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────┐
│                    index.html                        │
│  <elrs-app>  ← LitElement, hash-based SPA router     │
│    ├── #sidedrawer  (menu)                           │
│    ├── <header>     (language selector)              │
│    ├── <div#main>   (panel outlet)                   │
│    └── <elrs-footer>                                 │
│         ├── <ornithopter-panel>  ← #ornithopter      │
│         ├── <zephyrus-panel>     ← #zephyrus         │
│         ├── <servo-panel>        ← #servo            │
│         ├── <debug-console-panel> ← #debug           │
│         └── ...16 ELRS pages ...                     │
└──────────────────────────────────────────────────────┘
```

### Key Decisions

| Decision | Why |
|----------|-----|
| **Single Page App** (hash routing) | Runs on ESP8285 — no server-side routing. One HTML load, JS handles navigation. |
| **Lit 3.x** | Tiny (5KB), reactive, class-based. Used by upstream ExpressLRS. |
| **Light DOM** | `createRenderRoot() → this`. No Shadow DOM. CSS cascades normally. Simpler debugging. |
| **Polling** | Each panel polls `/pteronautos/state` at 500ms via `PteroElement._doPoll()`. No WebSockets — ESP8285 memory is tight. |
| **Lazy loading** | Pages grouped into `general-group.js` and `advanced-group.js`. Dynamic `import()` keeps initial payload small for ESP SPIFFS. |

### Data Flow

```
ESP8285 Firmware
    │
    │  GET /pteronautos/state     → JSON with ornithopter, zephyrus, servo sections
    │  POST /pteronautos/config   → save mixer profile, stroke ferocity, etc.
    │  POST /pteronautos/calibrate → trigger gyro calibration
    │  GET /config                 → ExpressLRS boot config (version, features)
    │
    ▼
PteroElement._doPoll()
    │
    │  API.fetchState()  →  {data, error}
    │  _applyState(data)  →  sets @properties reactively
    │
    ▼
Lit render()  →  LithAML template  →  HTML in light DOM
```

---

## 2. Build Pipeline

```
Source File                 Vite Plugin            Output
─────────────────────────────────────────────────────────────
*.coffee         ──→  coffeePlugin()       ──→  ESM JavaScript
*.lithaml        ──→  hamlLitPlugin()       ──→  html`...` export
*.{js,css,html}  ──→  featureBlocksPlugin() ──→  feature-gated code
*.sass           ──→  Vite built-in          ──→  CSS
index.html       ──→  inlineStaticHtmlAssets ──→  inlined SVGs
```

### Order Matters

```
enforce: 'pre'  →  coffeePlugin, hamlLitPlugin, featureBlocksPlugin  (run first)
default         →  minifyTemplateLiterals, babelDecoratorsPlugin     (run after)
```

### CoffeeScript → JavaScript

`build-plugins/coffee-plugin.js` uses the `coffeescript` package (v2.7.0).

- Transpiles `.coffee` → bare JS (no IIFE wrapper)
- Converts `module.exports = { ... }` → `export const { ... }` (CJS → ESM)
- No source maps in dev (fast iteration)

**Why CoffeeScript?** The Ornithopter panels use CoffeeScript's clean syntax for
state-heavy reactive components — `@properties`, implicit returns, `->` arrow
functions. It's the project's *lingua franca* and predates the ELRS JS migration.

### LithAML → Lit Template

`build-plugins/haml-lit-plugin.js` + `haml-lit-compiler.js`:

```
.lithaml source                →    Lit render() export
─────────────────────────────────────────────────────────
%section.panel                  →    html`<section class="panel">
  %h2 Title                     →      <h2>Title</h2>
  = self._t('key')              →      ${self._t('key')}
  - if self.enabled             →      ${self.enabled ? html`...` : ''}
```

Output is a default export: `export default (self) => html\`...\``

See [§6 LithAML Template DSL](#6-lithaml-template-dsl) for full syntax.

### Feature Blocks

`build-plugins/feature-blocks-plugin.js` strips feature-gated code at build time:

```html
<!-- FEATURE:PTERONAUTOS -->
<ornithopter-panel></ornithopter-panel>
<!-- /FEATURE:PTERONAUTOS -->
```

When `VITE_FEATURE_PTERONAUTOS=true`, the markers are removed but content stays.
When unset, the entire block is deleted. See [§7 Feature Flags](#7-feature-flags).

### Production Build

```
npm run build:pteronautos
```

Produces:
- `dist/index.html` — minified, inlined assets
- `dist/assets/app-*.js` — all panels, utilities, locales (~77KB gzipped)
- `dist/assets/utils-*.js` — shared utilities chunk
- `headers/web-pteronautos-rx-8285.h` — C header for ESP32 SPIFFS

---

## 3. PteroElement — The Foundation

`src/lib/ptero.coffee` exports the base class every PteronautOS panel extends:

```coffee
import {PteroElement} from '../lib/ptero'

class MyPanel extends PteroElement
  pollRate: 500              # polling interval in ms

  @properties:               # Lit reactive properties (state:true = internal)
    myValue: {state: true}

  _applyState: (data) ->     # override — polled data arrives here
    @myValue = data.foo

  render: -> renderFn(this)  # LithAML template is imported as renderFn
```

### What `PteroElement` Provides

| Feature | How |
|---------|-----|
| **Light DOM** | `createRenderRoot() → this` |
| **Polling** | `connectedCallback()` starts `setInterval(_doPoll, pollRate)` |
| **Error flag** | `this.pollError` (true when `/pteronautos/state` fails) |
| **i18n shortcut** | `this._t('key')` delegates to the i18n singleton |
| **Locale reactivity** | Listens to `window` `locale-changed` event → `requestUpdate()` |
| **Cleanup** | `disconnectedCallback()` stops poll timer, removes listeners |

### Lifecycle

```
constructor()
    │
connectedCallback()
    ├── super.connectedCallback()
    ├── addEventListener('locale-changed', ...)
    ├── _doPoll()                    ← first poll immediately
    └── setInterval(_doPoll, 500)    ← repeat every 500ms
            │
            ├── API.fetchState()     → GET /pteronautos/state
            ├── if error: pollError = true
            └── else: _applyState(data)
                        │
                        └── sets @properties → Lit re-renders
disconnectedCallback()
    ├── super.disconnectedCallback()
    ├── removeEventListener('locale-changed', ...)
    └── clearInterval(pollTimer)
```

### Helper Exports

`ptero.coffee` also exports pure utility modules used in LithAML templates:

| Module | Example | Purpose |
|--------|---------|---------|
| `Fmt` | `Fmt.f0(150)` → `"150"`, `Fmt.pct(75)` → `"75%"` | Number formatting |
| `Style` | `Style.badge('#d4a017')` → inline CSS string | Styling |
| `Status` | `Status.color(pollErr, online)` → `'#2d8'` | State → color |
| `API` | `API.fetchState()`, `API.calibrate()` | Async fetch wrappers |
| `Servo` | `Servo.channels`, `Servo.liveUs()` | Servo channel metadata |

All formatting functions in `Fmt` and `Style` are **λ-curried** (from `essential.js`). This means:

```coffee
Fmt.f1      # a function: (v) -> v.toFixed(1)
Fmt.pct     # a function: (v) -> "#{int(v)}%"
Style.badge '#d4a017'  # a function: (v) -> "background-color:#d4a017;..."
```

This enables point-free composition in LithAML: `= Fmt.f0(self.someValue)`.

---

## 4. i18n System

### Architecture

```
┌──────────────────────────────────────────┐
│           i18n-loader.js                 │
│  Imports 11 locale modules               │
│    ↓                                     │
│  i18n.register(en)  ... i18n.register(ar)│
│    ↓                                     │
│  i18n.init() → reads localStorage        │
│              → sets current locale       │
│              → dispatches 'locale-changed'│
└──────────────┬───────────────────────────┘
               │
     ┌─────────▼─────────┐
     │   i18n.js          │  (singleton I18nEngine)
     │                    │
     │  t(key, params)    │  → translated string with {{param}} interpolation
     │  setLocale(code)   │  → switch language, persist, dispatch
     │  locale            │  → current code ('de', 'ja', ...)
     │  availableLocales  │  → [{code, name, nativeName, dir}, ...]
     │  dir               │  → 'ltr' or 'rtl'
     └────────────────────┘
               │
     ┌─────────▼─────────┐
     │  locale modules    │  (src/locales/{en,de,pt,es,fr,hi,ja,ko,ru,zh,ar}.js)
     │                    │
     │  export default {  │
     │    code: 'de',     │
     │    name: 'German', │
     │    nativeName: 'Deutsch',
     │    dir: 'ltr',     │
     │    keys: {         │
     │      'key': 'Text',│
     │      ...           │
     │    }               │
     │  }                 │
     └────────────────────┘
```

### Usage Patterns

**From CoffeeScript (PteronautOS panels):**

```coffee
# In Lit render → template:
= self._t('ornithopter.panel.title')

# With interpolation:
= self._t('common.saved', {name: 'Profile 3'})
```

**From JavaScript (ELRS pages):**

```js
import {i18n} from "../utils/i18n.js";

// In render:
${i18n.t('elrs.binding.phrase_label')}

// HTML content (needs unsafeHTML):
${unsafeHTML(i18n.t('elrs.binding.override_note'))}
```

**From LithAML (PteronautOS templates):**

```haml
= self._t('ornithopter.panel.title')
```

**Dynamic keys:**

```js
// Array of labels — use getters so they re-evaluate on locale change
get ACTION_OPTIONS() {
    return [
        {value: '1', label: i18n.t('elrs.buttons.actions.arm')},
        {value: '2', label: i18n.t('elrs.buttons.actions.disarm')},
    ];
}
```

### Locale File Format

```js
// src/locales/de.js
export default {
    code: 'de',
    name: 'German',
    nativeName: 'Deutsch',
    dir: 'ltr',
    keys: {
        'ornithopter.panel.title': 'Ornithopter',
        'zephyrus.calibrate.btn': 'Kalibrieren',
        'elrs.binding.phrase_label': 'Binding-Phrase',
        // ...303 elrs keys + ~100 pteronautos keys
    }
}
```

### Adding a New Language

1. Copy `src/locales/en.js` → `src/locales/xx.js`
2. Edit `code`, `name`, `nativeName`, `dir`
3. Translate all `keys` values
4. Add import + registration in `src/utils/i18n-loader.js`

### Language Selector

The `<select>` dropdown in the header calls `i18n.setLocale(code)` which:
1. Switches `_strings` Map to the new locale's keys
2. Sets `document.documentElement.lang` and `dir` (for RTL)
3. Persists to `localStorage`
4. Dispatches `CustomEvent('locale-changed')` on `window`
5. All `PteroElement` subclasses re-render automatically

Browser detection (on first visit): `navigator.language` → match known locale, else fallback `en`.

---

## 5. Panel Anatomy

Every PteronautOS panel follows the same two-file pattern:

```
src/pages/ornithopter-panel.coffee   ← Logic (class, state, handlers)
src/pages/ornithopter-panel.lithaml  ← Template (HTML structure, HAML→Lit)
```

### CoffeeScript File (`ornithopter-panel.coffee`)

```coffee
import renderFn from './ornithopter-panel.lithaml'   # ← template import
import {PteroElement, Fmt, Style, Status} from '../lib/ptero'

class OrnithopterPanel extends PteroElement
  # ── Polling rate ────────────────────────────────────
  pollRate: 500

  # ── Reactive properties ─────────────────────────────
  @properties:
    profileId:     {state: true}
    strokeFerocity:{state: true}

  # ── Default values ──────────────────────────────────
  profileId     = 1
  strokeFerocity = 30

  # ── Poll data handler ───────────────────────────────
  _applyState: (data) ->
    return unless data.ornithopter
    o = data.ornithopter
    @linkUp = o.link_up == true

  # ── Event handlers ──────────────────────────────────
  _onSlider: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)

  _onCheck: (prop) -> (evt) =>
    @[prop] = evt.target.checked

  # ── Render ──────────────────────────────────────────
  render: -> renderFn(this)    # pass `this` to template function

# ── Register as custom element ────────────────────────
customElements.define 'ornithopter-panel', OrnithopterPanel
export default OrnithopterPanel
```

### Key Patterns

**1. Handler factories** — `_onSlider(prop)` returns a handler for that property:

```coffee
_onSlider: (prop) -> (evt) =>
  @[prop] = parseInt(evt.target.value)

# Usage in template:
# %input{type:"range", @input: self._onSlider('strokeFerocity')}
```

**2. Computed properties** — methods that derive display values:

```coffee
_cycleRatingDisplay: -> (Fmt.f3(@cycleRatingSec / 1000.0)) + 's'
_glideAngleLabel: ->
  v = @glideAngleDeg
  if v < 0 then "#{Math.abs(v)}° Dihedral" else if v > 0 then "#{v}° Anhedral" else 'Neutral'
```

**3. Profile helpers** — filter/match profile data:

```coffee
_profilesForKernel: -> @profiles.filter (p) -> p.kernel == @kernel
_currentProfile: -> @profiles[@profileId] or @profiles[1]
_isServoProfile: -> @_currentProfile().kernel == 'servo'
```

**4. Save to firmware:**

```coffee
_saveConfig: ->
  body = JSON.stringify {
    profile_id: @profileId
    stroke_ferocity: @strokeFerocity
  }
  fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/json'}, body}
```

### ELRS Pages (JavaScript)

ELRS default pages follow the same Lit pattern but in plain JS:

```js
import {LitElement, html} from 'lit';
import {customElement, state} from 'lit/decorators.js';
import {i18n} from '../utils/i18n.js';

@customElement('binding-panel')
export class BindingPanel extends LitElement {
    @state() accessor phrase = '';
    @state() accessor uid = '';

    // ...render, handlers, etc.
}
```

These use explicit `i18n.t()` calls and may need `unsafeHTML` for HTML-bearing strings.

---

## 6. LithAML Template DSL

LithAML is a HAML dialect that compiles to Lit `html\`...\`` tagged templates.

### Syntax Reference

| HAML | Lit Output |
|------|------------|
| `%section.panel` | `<section class="panel">` |
| `%h2.gold` | `<h2 class="gold">` |
| `%div#main` | `<div id="main">` |
| `.card` | `<div class="card">` |
| `#unique` | `<div id="unique">` |
| `= self.value` | `${self.value}` (escaped) |
| `!= self.htmlStr` | `${unsafeHTML(self.htmlStr)}` (unescaped) |
| `%input{value: self.x}` | `<input value="${self.x}">` |
| `%input{.value: self.x}` | `<input .value="${self.x}">` (property binding) |
| `%input{@input: self.fn}` | `<input @input="${self.fn}">` (event binding) |
| `%input{?disabled: self.lock}` | `<input ?disabled="${self.lock}">` (boolean attr) |
| `- if self.cond` | `${self.cond ? html\`...\` : ''}` |
| `- else` | (else branch of nearest if) |
| `- elsif self.cond2` | (else-if branch) |
| `- for item of self.list` | `${self.list.map(item => html\`...\`)}` |
| `- for item, idx of self.list` | `${self.list.map((item, idx) => html\`...\`)}` |
| `:plain` | Raw text block (following indented lines) |
| `/ comment` | Skipped (not in output) |
| `text content` | Text node |

### Literal values

Unquoted values in attributes are treated as **dynamic expressions** (e.g. `{value: self.foo}` → `${self.foo}`).
Quoted values are **static strings**: `{type: "range"}` → `type="range"`.

### Nested if/for

Indentation controls nesting. One level = 2 spaces (CoffeeScript convention).

```haml
- if self.enabled
  %section
    %h3 Active
    = self.label
- else
  %p.pale Disabled
```

### Example: Slider with Label

```haml
%div.slider-row
  %label Roll P:
  %input{type:"range", min:"0", max:"100", .value: self.rollP, @input: self._onSlider('rollP')}
  %span.value= Fmt.f0(self.rollP)
```

### Example: Conditional Section

```haml
- if self._hasRudder()
  %section.rudder
    %h3 Rudder
    .slider-row
      %label Yaw Weight
      %input{type:"range", min:"0", max:"100", .value: self.rudderYawWeight, @input: self._onSlider('rudderYawWeight')}
      %span.value= Fmt.f0(self.rudderYawWeight)
```

### Example: Select Dropdown

```haml
%select{@change: self._onKernel}
  - for k of ['servo','gearbox']
    %option{value: k, ?selected: (k == self.kernel)}
      = if k == 'servo' then self._t('ornithopter.kernel.servo') else self._t('ornithopter.kernel.gearbox')
```

---

## 7. Feature Flags

Conditional builds based on environment variables. Defined in `build-plugins/feature-blocks-plugin.js`.

### Build-time Flags (VITE_FEATURE_*)

| Flag | Effect |
|------|--------|
| `VITE_FEATURE_PTERONAUTOS=true` | Includes Ornithopter, Zephyrus, Servo, Debug panels |
| `VITE_FEATURE_IS_TX=true` | TX mode (shows buttons, models; hides connections, serial) |
| `VITE_FEATURE_HAS_SX128X=true` | SX1280 radio support |
| `VITE_FEATURE_IS_8285=true` | ESP8285 target (smaller flash budget) |
| `VITE_FEATURE_HAS_LR1121=true` | LR1121 radio + updater page |

### Syntax

```html
<!-- FEATURE:PTERONAUTOS -->
<li><a href="#ornithopter">Ornithopter</a></li>
<!-- /FEATURE:PTERONAUTOS -->
```

```js
// FEATURE:IS_TX
return html`<buttons-panel></buttons-panel>`;
// /FEATURE:IS_TX
// FEATURE:NOT IS_TX
return html`<connections-panel></connections-panel>`;
// /FEATURE:NOT IS_TX
```

### Inversion

- `FEATURE:NOT IS_TX` — content included when IS_TX is **false**
- `FEATURE:!HAS_LR1121` — same, shorthand

### Matched Flags

`FEATURE:HAS_SUBGHZ` is a derived flag — true when `HAS_LR1121 || HAS_SX127X`.

### Processor applies to

- `index.html` (HTML comments)
- `.js`, `.coffee` (JS/CoffeeScript comments — both `//` line and `/* */` block)
- `.sass`, `.css` (CSS comments — `/* */` block only)

---

## 8. Code Principles

### 1. Pure Functions First

Formatting and styling are pure functions in `ptero.coffee`. No DOM access, no side effects.
All λ-curried for composition.

```coffee
Fmt.pct   # (v) -> "#{int(v)}%"
Fmt.deg   # (v) -> "#{_f1 v}°"
Style.badge '#d4a017'   # (v) -> "background-color:#d4a017;..."
```

### 2. Poll, Don't Subscribe

No WebSockets. Every panel polls `/pteronautos/state` independently.
This keeps the ESP8285 code simple — one HTTP handler, no persistent connections.

### 3. State Flows Down, Events Flow Up

```
Firmware JSON  ──→  _applyState(data)  ──→  @properties  ──→  render()
                                                                    │
User input  ──→  handler(_onSlider, _onCheck)  ──→  @properties  ──┘
                 │
                 └──→  POST /pteronautos/config (on save)
```

### 4. Light DOM, Not Shadow DOM

All styles cascade. The `mui.css` framework defines base styles.
`elrs.sass` (monochrome) and `pteronautos.sass` (Turret Road gold theme) layer on top.
CSS load order in `index.html`:

```
mui.css → elrs.sass → pteronautos.sass → main.sass → icons.sass
```

`main.sass` is **zero-color** — pure layout, no colors. All theming in component Sass files.

### 5. No Memory Allocation in Templates

LithAML templates compile to static template strings. Dynamic content uses `${ }` interpolation.
No `innerHTML` — Lit auto-escapes. For HTML-bearing translations, use `unsafeHTML()` (only from locale strings, never from user input).

### 6. One File Per Panel, Two Extensions

- `.coffee` — logic (extends `PteroElement`)
- `.lithaml` — template (imported as `renderFn`)

ELRS pages use single `.js` files with inline `html\`...\`` templates.

### 7. i18n is an Afterthought — Until It Isn't

Every user-visible string goes through `_t('key')` or `i18n.t('key')`.
Exception: technical values that are language-independent (frequency in Hz, µs values, pin numbers).

---

## 9. Build & Run

### Prerequisites

```bash
# Node v22.17.1 required — the system v14 cannot parse ??= in build plugins
nvm use 22.17.1

# Install dependencies (only once)
cd src/html
npm install
```

### Development

```bash
# PteronautOS RX mode (ESP8285, SX1280, all PteronautOS panels visible)
npm run dev:rx

# URL: http://localhost:5173/
# Default panel: #info (ExpressLRS info page)
# PteronautOS panels: #ornithopter, #zephyrus, #servo, #debug

# With CSS tree-shaking (what the firmware build uses)
npm run dev:rx:css-shake
```

```bash
# ExpressLRS TX mode (no PteronautOS panels)
npm run dev:tx

# Other variants
npm run dev:rx-esp32          # ESP32 RX, no 8285
npm run dev:tx:css-shake      # TX + CSS tree shake
```

### Mock vs Proxy

By default (`npm run dev:rx`), a **mock server** returns fake telemetry.
To test against real hardware:

```bash
VITE_ELRS_PROXY_TARGET=http://192.168.4.1 npm run dev:rx
```

The proxy forwards `/pteronautos/*` and `/config` requests to the ESP8285.

### Production Build

```bash
# PteronautOS firmware build
npm run build:pteronautos

# Output:
#   dist/index.html
#   dist/assets/app-*.js       (~77KB gzipped)
#   dist/assets/utils-*.js
#   headers/web-pteronautos-rx-8285.h   ← C header for ESP32 SPIFFS

# All targets (11 builds)
npm run build:all
```

### Syntax & Lint Checks

```bash
npm run check:syntax     # validates CoffeeScript + LithAML parse
npm run check:lint       # ESLint on JS files
npm run build            # full: check + clean + build
```

### Hot Reload

Vite provides HMR for `.js`, `.coffee`, `.sass`, `.lithaml`.
Changes to `.lithaml` or `.coffee` trigger instant browser updates.

**Caveats:**
- If a `.lithaml` syntax error crashes the compiler, Vite may serve a cached error page.
  Fix the syntax and Vite auto-recovers.
- Language changes (`setLocale`) persist in `localStorage` — clear storage or use an incognito window for fresh tests.

---

## 10. FAQ / Troubleshooting

### Q: "Vite fails with `Unexpected token ??=`"
**A:** You're on Node < 16. Use `nvm use 22.17.1`.

### Q: "Template shows literal `${self.value}` instead of the number"
**A:** The Haml-Lit compiler treated your attribute value as static (quoted) instead of dynamic (unquoted). Use `{value: self.x}` (no quotes around `self.x`).

### Q: "`<input>` value doesn't update when I type"
**A:** You used `{value: self.x}` (HTML attribute, one-time). Use `{.value: self.x}` (Lit property binding, reactive).

### Q: "ELRS pages show English even when I switch language"
**A:** ELRS pages don't extend `PteroElement` — they need manual `locale-changed` listeners. Current workaround: reload the page after switching language.

### Q: "How do I add a new PteronautOS panel?"
**A:**
1. Create `src/pages/foo-panel.coffee` extending `PteroElement`
2. Create `src/pages/foo-panel.lithaml` with LithAML template
3. Add route in `src/app.js`: `case 'foo': return html\`<foo-panel></foo-panel>\``
4. Add menu entry (gated with `<!-- FEATURE:PTERONAUTOS -->`)
5. Add locale keys in `src/locales/en.js` under `foo.*`
6. Ensure `ensureLoadedForRoute` includes `'foo'` in the right group

### Q: "How big is the production SPIFFS binary?"
**A:** ~77KB gzipped for the PteronautOS build. ESP8285 has ~400KB SPIFFS — well within budget.

### Q: "`_t()` works in CoffeeScript but not in LithAML"
**A:** In LithAML use `= self._t('key')`. Don't import i18n directly — templates receive `self` (the component instance).

---

## File Map

```
src/html/
├── index.html                          # SPA shell
├── vite.config.js                      # Build config, plugin order
├── package.json                        # Scripts, deps (Node v22.17.1)
│
├── src/
│   ├── app.js                          # <elrs-app> — router, menu, language selector
│   ├── components/
│   │   ├── elrs-footer.js              # Footer (pterodactyl ASCII gated)
│   │   └── ...
│   ├── lib/
│   │   ├── ptero.coffee                # PteroElement + Fmt/Style/Status/API/Servo
│   │   └── essential.js                # λ-curry, compose, functional primitives
│   ├── utils/
│   │   ├── i18n.js                     # I18nEngine singleton
│   │   ├── i18n-loader.js              # Imports all locales, calls init()
│   │   └── ...
│   ├── locales/
│   │   ├── en.js                       # English (~400 keys)
│   │   ├── de.js, pt.js, ... (10 more) # Translated locales
│   │   └── ...
│   ├── pages/
│   │   ├── ornithopter-panel.coffee    # Ornithopter panel logic
│   │   ├── ornithopter-panel.lithaml   # Ornithopter panel template
│   │   ├── zephyrus-panel.coffee       # Zephyrus gyro panel
│   │   ├── zephyrus-panel.lithaml
│   │   ├── servo-panel.coffee          # Servo PWM monitor
│   │   ├── servo-panel.lithaml
│   │   ├── debug-console-panel.js      # Debug console
│   │   ├── binding-panel.js            # ELRS: binding
│   │   ├── wifi-panel.js               # ELRS: WiFi
│   │   ├── ... (12 more ELRS pages)
│   │   └── ...
│   ├── assets/
│   │   ├── elrs.sass                   # Monochrome-neutral component styles
│   │   ├── pteronautos.sass            # Turret Road gold theme
│   │   ├── main.sass                   # Pure layout, zero color
│   │   ├── icons.sass                  # Icon font classes
│   │   └── mui.css                     # MUI framework (vendored)
│   └── page-groups/
│       ├── general-group.js            # Lazy loads: binding, options, wifi, etc.
│       └── advanced-group.js           # Lazy loads: hardware, zephyrus, servo, etc.
│
├── build-plugins/
│   ├── coffee-plugin.js                # .coffee → ESM JS
│   ├── haml-lit-plugin.js              # .lithaml → Lit template
│   ├── haml-lit-compiler.js            # HAML parser → html`` generator
│   ├── feature-blocks-plugin.js        # FEATURE:NAME conditional compilation
│   ├── babel-decorators-plugin.js      # Lit @customElement decorators
│   ├── css-tree-shake-plugin.js        # Removes unused CSS
│   ├── inline-static-html-assets.js    # Inlines SVGs/icons in production
│   ├── esp32-header-plugin.js          # Produces C header for SPIFFS
│   └── README.md                       # Plugin docs
│
├── dev-plugins/
│   ├── dev-mock-plugin.js              # Fake telemetry for dev
│   └── dev-proxy-plugin.js             # Proxy to real ESP8285
│
├── docs/
│   ├── i18n.md                         # i18n internals reference
│   └── WEBUI_ARCHITECTURE.md           # This document
│
├── headers/                            # Generated C headers for SPIFFS
│   └── web-pteronautos-rx-8285.h
│
└── PROGRESS.md                         # i18n page checklist
```

---

*Thus the WebUI breathes — CoffeeScript in the logic, LithAML in the structure,
Sass in the skin, and eleven voices in the tongue. The bird flies.*
