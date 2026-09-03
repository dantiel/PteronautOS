###
# Ptero.coffee — PteronautOS WebUI Helper Library
# Functional first, pure CoffeeScript, built on essential.js.
# Every function is λ-curried, composable, and side-effect-free
# where possible. State-dependent helpers receive state as arguments.
#
# Usage:
#   import * as _ from '../lib/essential'
#   import {Fmt, Style, Status, API, PteroElement} from '../lib/ptero'
###

import {LitElement} from 'lit'
import * as _ from './essential'
import {i18n} from '../utils/i18n'

# ── Type-safe numeric coercion ────────────────────────────────────
# Essential's `λ` is curry — we use it to make all formatters point-free capable.
{λ, compose, partial, K, extend, flip} = _

num = (v) -> Number(v) ? 0
int = (v) -> Math.round(num v)
clamp = λ (lo, hi, v) -> Math.max(lo, Math.min(hi, num v))
clamp01 = clamp(0, 1)
clamp100 = clamp(0, 100)

# ═══════════════════════════════════════════════════════════════════
# Fmt — Pure formatting functions
# All take numbers, return strings. λ-curried for composition.
# ═══════════════════════════════════════════════════════════════════

_f1 = λ (v) -> num(v).toFixed 1
_f2 = λ (v) -> num(v).toFixed 2
_f3 = λ (v) -> num(v).toFixed 3

Fmt =
  f0: λ (v) -> int(v).toString()
  f1: _f1
  f2: _f2
  f3: _f3

  deg:    compose (λ (s) -> "#{s}°"),   _f1
  degPS:  compose (λ (s) -> "#{s}°/s"), _f1
  us:     λ (v) -> "#{int(v)}µs"
  usLive: λ (v) -> if int(v) is 1500 then '1500µs' else "#{int(v)}µs"
  pct:    λ (v) -> "#{int(v)}%"
  hz:     λ (v) -> "#{_f1 v} Hz"
  uptime: λ (ms) -> "#{Math.floor(num(ms) / 1000)}s"

  # Convert 1000-2000µs servo range to percentage -100%..+100%
  usToPct: λ (us) -> int(((num(us) - 1500) / 500) * 100)
  usToBarWidth: λ (us) -> clamp100(((num(us) - 1000) / 1000) * 100)
  usToBarColor: λ (us) ->
    u = num us
    return '#d4a017' if u > 1550
    return '#8b6914' if u < 1450
    '#888'

# ═══════════════════════════════════════════════════════════════════
# Style — CSS style string generators
# Pure functions that return inline style strings.
# ═══════════════════════════════════════════════════════════════════

Style =
  # Inline badge: colored background, white text
  badge: λ (color) ->
    "background-color:#{color};display:inline-block;padding:4px 12px;border-radius:3px;color:#fff;font-weight:600;"

  # Bar fill for live PWM display
  barFill: λ (pct, color = '#d4a017') ->
    "background:#{color};height:8px;border-radius:4px;width:#{clamp100(pct)}%;transition:width 0.15s ease-out;"

  barTrack: K "background:#333;height:8px;border-radius:4px;position:relative;overflow:hidden;"

  # Monospace data style
  mono: λ (color = '#d4a017') ->
    "color:#{color};font-family:'SF Mono','Fira Code',monospace;font-size:13px;"

  # Dim label style
  dim: K "color:#888;font-size:11px;"

  # Value card
  valueGold: K "font-size:24px;font-weight:700;color:#d4a017;font-family:'SF Mono',monospace;"

  # Center alignment
  center: K "text-align:center;"

  # Disabled hint
  hint: K "color:#888;font-style:italic;"

  # Horizon outer shell
  horizon: K(
    "width:200px;height:200px;margin:0 auto;overflow:hidden;" +
    "background:linear-gradient(180deg,#3a6fc2 0%,#3a6fc2 50%,#5c3d0e 50%,#6b4c1e 100%);" +
    "border-radius:50%;border:3px solid #d4a017;position:relative;" +
    "box-shadow:0 0 20px rgba(212,160,23,0.12),inset 0 0 30px rgba(0,0,0,0.5);"
  )

  # Horizon line transform (roll + pitch)
  horizonLine: λ (rollDeg, pitchDeg) ->
    r = clamp(-45, 45, rollDeg)
    p = clamp(-40, 40, pitchDeg * 2)
    "transform:rotate(#{r}deg) translateY(#{p}px);" +
    "width:100%;height:2px;background:#fff;" +
    "position:absolute;top:50%;left:0;transition:transform 0.15s ease-out;"

  # Center dot on horizon line
  centerDot: K(
    "position:absolute;top:-7px;left:50%;margin-left:-7px;" +
    "width:14px;height:14px;background:#d4a017;border-radius:50%;border:2px solid #fff;"
  )

  # Pto glass card
  glass: K(
    "background:rgba(28,28,34,0.85);backdrop-filter:blur(8px);" +
    "-webkit-backdrop-filter:blur(8px);border:1px solid rgba(212,160,23,0.15);"
  )

# ═══════════════════════════════════════════════════════════════════
# Status — State classifiers (pure functions)
# Take state booleans, return display strings and colors.
# ═══════════════════════════════════════════════════════════════════

Status =
  # Tri-state color: error → amber, online → green, offline → grey
  color: λ (pollErr, online, warn = false) ->
    return '#c84' if pollErr
    return '#da0' if warn
    return '#2d8' if online
    '#888'

  # Servo status text (uses i18n keys)
  servoText: λ (pollErr, linkUp) ->
    return i18n.t 'servo.status.api_offline' if pollErr
    return i18n.t 'servo.status.link_active' if linkUp
    i18n.t 'servo.status.no_link'

  # Ornithopter status text (uses i18n keys)
  orniText: λ (pollErr, linkUp) ->
    return i18n.t 'status.orni.api_offline' if pollErr
    return i18n.t 'status.orni.link_active' if linkUp
    i18n.t 'status.orni.need_link'

  # Gyro status text (uses i18n keys)
  gyroText: λ (pollErr, enabled, calibrated) ->
    return i18n.t 'status.gyro.api_offline' if pollErr
    return i18n.t 'status.gyro.active' if enabled and calibrated
    return i18n.t 'status.gyro.detected' if enabled
    i18n.t 'status.gyro.disabled'

  # Calibration label (uses i18n keys)
  calibLabel: λ (calibrating) ->
    if calibrating then i18n.t 'zephyrus.calibrate.calibrating' else i18n.t 'zephyrus.calibrate.btn'

  # Calibration progress (uses i18n key)
  calibProgress: λ (samples) -> i18n.t 'zephyrus.calibrate.sampling', {samples: num samples}

  # Calibration hint text (uses i18n keys)
  calibHint: λ (enabled, calibrating) ->
    return i18n.t 'zephyrus.calibrate.hint_available' unless enabled
    return i18n.t 'zephyrus.calibrate.hint_hold' if calibrating
    i18n.t 'zephyrus.calibrate.hint_ready'

  # Footer text for servo panel (uses i18n keys)
  servoFooter: λ (linkUp) ->
    if linkUp
      i18n.t 'servo.footer.live'
    else
      i18n.t 'servo.footer.static'

  # Horizon caption (uses i18n keys)
  horizonCaption: λ (enabled) ->
    if enabled then i18n.t 'zephyrus.attitude.live' else i18n.t 'zephyrus.attitude.waiting'

# ═══════════════════════════════════════════════════════════════════
# API — Async fetch helpers
# ═══════════════════════════════════════════════════════════════════

delay = (ms) -> new Promise (resolve) -> setTimeout resolve, ms

# A completed browser fetch can race the ESPAsyncWebServer response destructor.
# Retry only transient busy/network failures; permanent HTTP errors return at once.
fetchJsonWithRetry = (url, attempts = 6) ->
  lastError = null
  for attempt in [0...attempts]
    try
      resp = await fetch url
      return await resp.json() if resp.ok
      throw new Error "HTTP #{resp.status}" unless resp.status in [429, 503]
      lastError = new Error "HTTP #{resp.status}"
    catch e
      lastError = e
    await delay 150 * (attempt + 1) if attempt + 1 < attempts
  throw lastError

API =
  # Fetch pteronautos state JSON. Returns {data, error}.
  fetchState: ->
    try
      {data: await fetchJsonWithRetry('/pteronautos/state'), error: null}
    catch e
      {data: null, error: e}

  # Fetch static pteronautos config (fetched once per panel mount, not polled).
  fetchConfig: ->
    try
      {data: await fetchJsonWithRetry('/pteronautos/config'), error: null}
    catch e
      {data: null, error: e}

  # POST to /pteronautos/calibrate  
  calibrate: ->
    try
      resp = await fetch '/pteronautos/calibrate', {method: 'POST'}
      ok: resp.ok
    catch e
      ok: false

  # POST board rotation to /pteronautos/orientation
  setOrientation: (rotation) ->
    try
      body = new URLSearchParams()
      body.append 'rotation', String rotation
      resp = await fetch '/pteronautos/orientation', {
        method: 'POST'
        headers: {'Content-Type': 'application/x-www-form-urlencoded'}
        body
      }
      ok: resp.ok
    catch e
      ok: false

  # Ping endpoint
  ping: ->
    try
      resp = await fetch '/pteronautos/ping'
      data = await resp.json()
      data.ok is true
    catch e
      false

# ═══════════════════════════════════════════════════════════════════
# Servo — Static channel definitions
# ═══════════════════════════════════════════════════════════════════

Servo =
  # Channel definitions for servo panel table
  _makeCh: (ch, name, pin, pinLabel, center = 1500, min = 1000, max = 2000) ->
    {ch, name, pin, pinLabel, center, min, max
     centerLabel: "#{center}µs", minLabel: "#{min}µs", maxLabel: "#{max}µs"
     failsafeLabel: "#{center}µs"}

  # Lookup live microsecond value by channel name
  liveUs: λ (ch, servoLeftUs, servoRightUs, servoRudderUs) ->
    switch ch.name
      when 'Left Wing'    then num servoLeftUs
      when 'Right Wing'   then num servoRightUs
      when 'Crest Rudder' then num servoRudderUs
      else 1500

  # Live display label (highlights non-neutral)
  liveLabel: λ (liveUs) -> if liveUs is 1500 then '1500µs' else "#{liveUs}µs"

  # Style for live column
  liveStyle: λ (liveUs) ->
    changed = liveUs isnt 1500
    "color:#{if changed then '#d4a017' else '#888'};font-family:monospace;"

# Defined after Servo exists to avoid "undefined is not an object" in JS literal
Servo.channels = [
  Servo._makeCh 1, 'Left Wing',    0,  'GPIO0'
  Servo._makeCh 2, 'Right Wing',   1,  'GPIO1'
  Servo._makeCh 3, 'Crest Rudder', 3,  'GPIO3'
  Servo._makeCh 4, 'AUX 4',        9,  'GPIO9'
  Servo._makeCh 5, 'AUX 5',       10,  'GPIO10'
]

# ═══════════════════════════════════════════════════════════════════
# Zephyrus — Gyro-specific helpers
# ═══════════════════════════════════════════════════════════════════

Zephyrus =
  # Board rotation options for dropdown
  rotationOptions: [
    {val: '0', label: 'DEFAULT — Flat, pins forward, chip up'}
    {val: '1', label: 'YAW 90° — Rotated 90° clockwise (pins right)'}
    {val: '2', label: 'YAW 180° — Rotated 180° (pins backward)'}
    {val: '3', label: 'YAW 270° — Rotated 270° (pins left)'}
    {val: '4', label: 'UPSIDE DOWN — Flipped over (chip down)'}
    {val: '5', label: 'VERTICAL — Pins forward, board vertical'}
    {val: '6', label: 'VERTICAL — Pins right, board vertical'}
  ]

  # Check if a rotation value matches current setting
  isRotation: λ (current, val) -> int(current) is int(val)

# ═══════════════════════════════════════════════════════════════════
# PteroElement — Base class for all PteronautOS LitElements
# ═══════════════════════════════════════════════════════════════════

class PteroElement extends LitElement
  # ── Default polling rate in ms ──
  pollRate: 500
  pollError: false
  # Subclasses that read static config set this true — triggers a one-shot
  # /pteronautos/config fetch on mount instead of polling config every 2s.
  needsConfig: false
  _pollTimer: null
  _boundOnLocale: null

  # ── All PteronautOS panels render into light DOM ──
  createRenderRoot: -> this

  # ── i18n shortcut: delegates to i18n singleton ──
  _t: (key, params = {}) -> i18n.t(key, params)

  # ── Start the polling loop and listen for locale changes ──
  connectedCallback: ->
    super.connectedCallback()
    @_boundOnLocale = => @requestUpdate()
    window.addEventListener 'locale-changed', @_boundOnLocale
    @_pollTimer = setInterval (=> @_doPoll?()), @pollRate
    @_loadConfig?() if @needsConfig

  # ── Stop the polling loop and locale listener ──
  disconnectedCallback: ->
    super.disconnectedCallback()
    if @_boundOnLocale
      window.removeEventListener 'locale-changed', @_boundOnLocale
      @_boundOnLocale = null
    if @_pollTimer
      clearInterval @_pollTimer
      @_pollTimer = null

  # ── Fetch state and apply. Override _applyState in subclasses. ──
  _doPoll: ->
    {data, error} = await API.fetchState()
    if error
      @pollError = true
    else
      @pollError = false
      @_applyState data

  # ── One-shot static config fetch. Override _applyConfig in subclasses. ──
  _loadConfig: ->
    {data, error} = await API.fetchConfig()
    @_applyConfig data unless error

  # ── Override me ──
  _applyState: (data) -> # no-op
  _applyConfig: (data) -> # no-op

export {Fmt, Style, Status, API, Servo, Zephyrus, PteroElement}
