import renderFn from './joypad-panel.lithaml'
import {PteroElement, Fmt} from '../lib/ptero'
import {i18n} from '../utils/i18n'

# Stick channel indices (match firmware `StickCh` enum in Ornithopter.h)
CH_AILERON  = 0
CH_ELEVATOR = 1
CH_THROTTLE = 2
CH_RUDDER   = 3
CH_ARM      = 4
CH_FREQ     = 5
CH_PROFILE  = 6

CRSF_MIN    = 172
CRSF_CENTER = 992
CRSF_MAX    = 1811

# Knob travel from pad centre (px) — pads are 130px, knob 44px.
KNOB_TRAVEL = 38

### 
# Joypad Panel — touchstick flight control for bench piloting.
# Two thumbsticks (true multi-touch via TouchEvent identifiers):
#   LEFT  = rudder (X, spring)  + throttle (Y, ratchet-hold)
#   RIGHT = aileron (X, spring) + elevator (Y, spring)
# Plus flight-profile switcher, flap-speed (frequency) modifier and arm.
# Drives the firmware virtual-stick endpoint POST /pteronautos/stick.
# FEATURE:PTERONAUTOS
###
class JoypadPanel extends PteroElement
  pollRate: 2000

  @properties:
    stickOverride: {state: true}
    arm:           {state: true}
    profile:       {state: true}   # 0..2
    speedUs:       {state: true}   # flap frequency channel (µs 1000..2000)
    leftX:         {state: true}   # -1..1
    leftY:         {state: true}
    rightX:        {state: true}
    rightY:        {state: true}
    linkUp:        {state: true}
    servoLeftUs:   {state: true}
    servoRightUs:  {state: true}
    servoRudderUs: {state: true}
    voiceArm:      {state: true}
    activeProfile: {state: true}

  constructor: ->
    super()
    @stickOverride = false
    @arm           = false
    @profile       = 1
    @speedUs       = 1500
    @leftX         = 0
    @leftY         = -1        # throttle rests at glide (bottom)
    @rightX        = 0
    @rightY        = 0
    @linkUp        = false
    @servoLeftUs   = 1500
    @servoRightUs  = 1500
    @servoRudderUs = 1500
    @voiceArm      = 0
    @activeProfile = 1
    @_channelTimers   = {}
    @_channelPending  = {}
    @_touches         = new Map()
    @_pollInFlight    = false

  # ── Wire the thumbsticks after first render ──────────────────────
  firstUpdated: ->
    @_pads = @querySelectorAll '.joy-pad'
    @_pads.forEach (pad) =>
      pad.addEventListener 'touchstart',  ((e) => @_touchStart pad, e), {passive: false}
      pad.addEventListener 'touchmove',   ((e) => @_touchMove pad, e),  {passive: false}
      pad.addEventListener 'touchend',    ((e) => @_touchEnd pad, e),   {passive: false}
      pad.addEventListener 'touchcancel', ((e) => @_touchEnd pad, e),   {passive: false}
      pad.addEventListener 'mousedown',   ((e) => @_mouseDown pad, e)

  # ── Touch plumbing — one identifier per pad ⇒ true multi-touch ────
  _touchStart: (pad, e) ->
    e.preventDefault()
    return if @_touches.has pad
    for t in e.changedTouches
      if pad.contains t.target
        @_touches.set pad, t.identifier
        @_updateStick pad, t.clientX, t.clientY
        break

  _touchMove: (pad, e) ->
    e.preventDefault()
    id = @_touches.get pad
    return unless id?
    for t in e.changedTouches
      if t.identifier is id
        @_updateStick pad, t.clientX, t.clientY
        break

  _touchEnd: (pad, e) ->
    id = @_touches.get pad
    return unless id?
    for t in e.changedTouches
      if t.identifier is id
        @_touches.delete pad
        @_releaseStick pad
        break

  _mouseDown: (pad, e) ->
    e.preventDefault()
    @_updateStick pad, e.clientX, e.clientY
    move = (ev) => @_updateStick pad, ev.clientX, ev.clientY
    up = =>
      window.removeEventListener 'mousemove', move
      window.removeEventListener 'mouseup', up
      @_releaseStick pad
    window.addEventListener 'mousemove', move
    window.addEventListener 'mouseup', up

  _updateStick: (pad, cx, cy) ->
    rect = pad.getBoundingClientRect()
    radius = rect.width / 2
    nx = (cx - (rect.left + radius)) / radius
    ny = ((rect.top + radius) - cy) / radius
    len = Math.sqrt nx*nx + ny*ny
    if len > 1
      nx /= len
      ny /= len
    @_onStickMove pad.getAttribute('data-stick'), nx, ny

  _onStickMove: (stick, nx, ny) ->
    @_ensureOverride()
    if stick is 'left'
      @leftX = nx
      @leftY = ny
      @_sendChannel CH_RUDDER,   @_axisToCrsf(nx)
      @_sendChannel CH_THROTTLE, @_axisToCrsf(ny)
    else
      @rightX = nx
      @rightY = ny
      @_sendChannel CH_AILERON,  @_axisToCrsf(nx)
      @_sendChannel CH_ELEVATOR, @_axisToCrsf(ny)

  _releaseStick: (pad) ->
    stick = pad.getAttribute 'data-stick'
    if stick is 'left'
      # rudder springs to centre; throttle holds its last position
      @leftX = 0
      @_sendChannel CH_RUDDER, CRSF_CENTER
    else
      @rightX = 0
      @rightY = 0
      @_sendChannel CH_AILERON,  CRSF_CENTER
      @_sendChannel CH_ELEVATOR, CRSF_CENTER

  # ── CRSF helpers ─────────────────────────────────────────────────
  _axisToCrsf: (v) -> Math.round CRSF_MIN + (v + 1) * 500 * 1639 / 1000

  _profileRaw: (p) ->
    switch p
      when 0 then 172
      when 2 then 1811
      else 992

  # ── Sending (debounced per channel, non-overlapping) ─────────────
  _ensureOverride: ->
    return if @stickOverride
    @stickOverride = true
    @_sendStickOverride()

  _sendChannel: (idx, val) ->
    @_channelPending[idx] = val
    clearTimeout @_channelTimers[idx] if @_channelTimers[idx]
    @_channelTimers[idx] = setTimeout (=>
      @_channelTimers[idx] = null
      @_sendChannelNow idx, @_channelPending[idx]
    ), 40

  _sendChannelNow: (idx, val) ->
    body = new URLSearchParams()
    body.append 'override', '1'
    body.append "ch#{idx}", val
    try
      await fetch '/pteronautos/stick', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()}
    catch

  _sendStickOverride: ->
    body = new URLSearchParams()
    body.append 'override', if @stickOverride then '1' else '0'
    body.append 'ch0', @_axisToCrsf(@rightX)
    body.append 'ch1', @_axisToCrsf(@rightY)
    body.append 'ch2', @_axisToCrsf(@leftY)
    body.append 'ch3', @_axisToCrsf(@leftX)
    body.append 'ch4', if @arm then 1811 else 992
    body.append 'ch5', @speedUs
    body.append 'ch6', @_profileRaw(@profile)
    try
      await fetch '/pteronautos/stick', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()}
    catch

  # ── Control handlers ─────────────────────────────────────────────
  _onOverrideToggle: ->
    @stickOverride = not @stickOverride
    @_sendStickOverride()

  _onArmToggle: ->
    @arm = not @arm
    @_ensureOverride()
    @_sendChannel CH_ARM, if @arm then 1811 else 992

  _onProfileButton: (n) -> =>
    @profile = n
    @_ensureOverride()
    @_sendChannel CH_PROFILE, @_profileRaw(n)

  _onSpeedInput: (evt) ->
    @speedUs = parseInt evt.target.value
    @_ensureOverride()
    @_sendChannel CH_FREQ, @speedUs

  # ── Poll (guarded, live servo readout) ───────────────────────────
  _doPoll: ->
    return if @_pollInFlight
    @_pollInFlight = true
    try
      await super()
    finally
      @_pollInFlight = false

  _applyState: (data) ->
    return unless data.ornithopter
    o = data.ornithopter
    @linkUp        = o.link_up == true
    @servoLeftUs   = Fmt.f0 o.servo_left_wing_us  if o.servo_left_wing_us?
    @servoRightUs  = Fmt.f0 o.servo_right_wing_us if o.servo_right_wing_us?
    @servoRudderUs = Fmt.f0 o.servo_rudder_us     if o.servo_rudder_us?
    @voiceArm      = Fmt.f0 o.voice_arm           if o.voice_arm?
    @activeProfile = o.active_flight_profile      if o.active_flight_profile?

  # ── Template helpers ─────────────────────────────────────────────
  _knobStyle: (x, y) ->
    "transform:translate(#{Math.round(x*KNOB_TRAVEL)}px,#{Math.round(y*KNOB_TRAVEL)}px);"

  _leftKnobStyle:  -> @_knobStyle @leftX,  @leftY
  _rightKnobStyle: -> @_knobStyle @rightX, @rightY

  _overrideDot: ->
    if @stickOverride then 'pto-status-dot pto-status-dot--warn' else 'pto-status-dot pto-status-dot--off'

  _armClass: -> if @arm then 'ptero-btn ptero-btn--armed' else 'ptero-btn'

  _armLabel: -> if @arm then i18n.t('joypad.disarm') else i18n.t('joypad.arm')

  _profileBtnClass: (n) -> if @profile is n then 'pto-profile-btn is-active' else 'pto-profile-btn'

  _profileOptions: -> [0,1,2].map (n) => {val: n, label: i18n.t('joypad.profile_n', {n})}

  _throttlePct: -> Math.round ((@leftY + 1) * 50)

  _armed: -> Number(@voiceArm) > 992

  render: -> renderFn(this)

customElements.define 'joypad-panel', JoypadPanel
export default JoypadPanel
