import renderFn from './flight-profiles-panel.lithaml'
import {PteroElement, Fmt} from '../lib/ptero'
import {i18n} from '../utils/i18n'

# One schema for the request snapshot and the successfully saved cache.
PROFILE_FIELDS =
  strokeFerocity: 'stroke_ferocity'
  returnFerocity: 'return_ferocity'
  glideAngleDeg: 'glide_angle_deg'
  flappingAngleDeg: 'flapping_angle_deg'
  aileronScale: 'aileron_scale'
  elevatorScale: 'elevator_scale'
  rudderFerocityRange: 'rudder_ferocity_range'
  rudderAmplitudeDifferential: 'rudder_amplitude_differential'
  elevatorFerocityMix: 'elevator_ferocity_mix'
  throttleFerocityMix: 'throttle_ferocity_mix'
  throttleFrequencyMix: 'throttle_frequency_mix'
  ferocityShapeMix: 'ferocity_shape_mix'

###
# Flight Profiles Panel — per-profile tuning + channel test.
# Live telemetry via /pteronautos/state; flight profiles via /pteronautos/config.
# 3 flight profiles (0..2) switchable in flight via CH7 (3-pos).
# FEATURE:PTERONAUTOS
###
class FlightProfilesPanel extends PteroElement
  pollRate: 2000
  needsConfig: true

  @properties:
    configLoaded:        {state: true}
    saveState:           {state: true}
    saveError:           {state: true}
    stateLoaded:         {state: true}
    linkUp:              {state: true}
    voiceThrottle:       {state: true}
    voiceFreq:           {state: true}
    voiceProfile:        {state: true}
    voiceAileron:        {state: true}
    voiceElevator:       {state: true}
    voiceRudder:         {state: true}
    voiceArm:            {state: true}
    servoLeftUs:         {state: true}
    servoRightUs:        {state: true}
    servoRudderUs:       {state: true}
    kernel:              {state: true}  # 'servo' | 'gearbox'
    # Flight profile editing
    editProfile:         {state: true}  # 0..2 — which profile the sliders edit
    activeFlightProfile: {state: true}  # 0..2 — selected by PROFILE channel
    flightProfiles:      {state: true}
    # Per-flight-profile tuning (sliders edit editProfile)
    strokeFerocity:      {state: true}
    returnFerocity:      {state: true}
    glideAngleDeg:       {state: true}
    flappingAngleDeg:    {state: true}
    aileronScale:        {state: true}
    elevatorScale:       {state: true}
    rudderFerocityRange: {state: true}
    rudderAmplitudeDifferential: {state: true}
    elevatorFerocityMix: {state: true}
    throttleFerocityMix: {state: true}
    throttleFrequencyMix: {state: true}
    ferocityShapeMix:     {state: true}
    # Virtual stick
    stickOverride:       {state: true}
    stickChannels:       {state: true}

  constructor: ->
    super()
    @configLoaded         = false
    @stateLoaded          = false
    @linkUp               = false
    @voiceThrottle        = 1500
    @voiceFreq            = 1500
    @voiceProfile         = 992
    @voiceAileron         = 0
    @voiceElevator        = 0
    @voiceRudder          = 0
    @voiceArm             = 0
    @servoLeftUs          = 1500
    @servoRightUs         = 1500
    @servoRudderUs        = 1500
    @kernel               = 'servo'
    @editProfile          = 1
    @activeFlightProfile  = 1
    @flightProfiles       = [
      {strokeFerocity:30, returnFerocity:50, glideAngleDeg:-4, flappingAngleDeg:0, aileronScale:40, elevatorScale:60, rudderFerocityRange:50, rudderAmplitudeDifferential:0, elevatorFerocityMix:0, throttleFerocityMix:0, throttleFrequencyMix:0, ferocityShapeMix:0}
      {strokeFerocity:50, returnFerocity:50, glideAngleDeg:-4, flappingAngleDeg:0, aileronScale:40, elevatorScale:60, rudderFerocityRange:50, rudderAmplitudeDifferential:0, elevatorFerocityMix:0, throttleFerocityMix:0, throttleFrequencyMix:0, ferocityShapeMix:0}
      {strokeFerocity:70, returnFerocity:50, glideAngleDeg: 2, flappingAngleDeg:0, aileronScale:40, elevatorScale:60, rudderFerocityRange:50, rudderAmplitudeDifferential:0, elevatorFerocityMix:0, throttleFerocityMix:0, throttleFrequencyMix:0, ferocityShapeMix:0}
    ]
    @strokeFerocity       = 30
    @returnFerocity       = 50
    @glideAngleDeg        = -4
    @flappingAngleDeg     = 0
    @aileronScale         = 40
    @elevatorScale        = 60
    @rudderFerocityRange  = 50
    @rudderAmplitudeDifferential = 0
    @elevatorFerocityMix  = 0
    @throttleFerocityMix  = 0
    @throttleFrequencyMix = 0
    @ferocityShapeMix      = 0
    @stickOverride        = false
    @ratchetTimeoutMs     = 500
    # CRSF range (172–1811), neutral = 992. Throttle at glide (below flap
    # threshold) so the channel test starts at rest — steering is visible.
    @stickChannels        = [992, 992, 172, 992, 1811, 1500, 992]
    @_stickTimers         = {}
    @_stickPending        = {}
    @_pollInFlight        = false
    @_saveInFlight        = false
    @_saveQueued          = false
    @_unsavedProfiles     = {}
    @_profileSaveErrors   = {}
    @_fieldFocused        = false
    @_stickDragActive     = false
    @_stickDragTimer      = null
    @saveState            = 'idle'   # idle | saving | saved | error
    @saveError            = null
    @_saveResetTimer      = null

  # ── Non-overlapping poll guard ────────────────────────────────────
  # ESP8285 has ~23KB free heap. A 2s state poll builds a JSON doc; if it
  # overlaps a previous slow response (or a slider POST), heap exhausts and
  # the radio reboots. Skip a poll when one is in flight.
  _doPoll: ->
    return if @_pollInFlight or @_stickDragActive or @_saveInFlight
    @_pollInFlight = true
    try
      await super()
    finally
      @_pollInFlight = false

  # While a stick slider is being dragged, hold the state poll.
  _markStickDragActive: ->
    @_stickDragActive = true
    clearTimeout @_stickDragTimer if @_stickDragTimer
    @_stickDragTimer = setTimeout (=> @_stickDragActive = false), 400

  # ── Channel test: CH1–CH7 (PWM µs view over CRSF stickChannels) ──
  channelDefs: [
    {ch:1, stk:0, fn:'Aileron',  snap:true}
    {ch:2, stk:1, fn:'Elevator', snap:true}
    {ch:3, stk:2, fn:'Throttle', snap:false}
    {ch:4, stk:3, fn:'Rudder',   snap:true}
    {ch:5, stk:4, fn:'Arm',      snap:false}
    {ch:6, stk:5, fn:'Freq',     snap:false}
    {ch:7, stk:6, fn:'Profile',  snap:false}
  ]

  _crsfToPwm: (raw) -> Math.round(1000 + (raw - 172) * 1000 / 1639)

  _pwmToCrsf: (us) -> Math.round(172 + (us - 1000) * 1639 / 1000)

  _stickPwm: (def) -> @_crsfToPwm(@stickChannels[def.stk] ? 992)

  _chnSliderStyle: ->
    "-webkit-appearance:slider-vertical;appearance:slider-vertical;writing-mode:vertical-lr;direction:rtl;width:20px;height:150px;margin:6px 0;cursor:pointer;accent-color:#9cf;"

  # ── Apply polled state ────────────────────────────────────────────
  _applyState: (data) ->
    return unless data.ornithopter
    o = data.ornithopter
    @stateLoaded          = true
    @linkUp               = o.link_up == true
    @voiceThrottle        = Fmt.f0 o.voice_throttle
    @voiceFreq            = Fmt.f0 o.voice_freq        if o.voice_freq?
    @voiceProfile         = Fmt.f0 o.voice_profile     if o.voice_profile?
    @voiceAileron         = Fmt.f0 o.voice_aileron
    @voiceElevator        = Fmt.f0 o.voice_elevator
    @voiceRudder          = Fmt.f0 o.voice_rudder
    @voiceArm             = Fmt.f0 o.voice_arm         if o.voice_arm?
    @servoLeftUs          = Fmt.f0 o.servo_left_wing_us  if o.servo_left_wing_us?
    @servoRightUs         = Fmt.f0 o.servo_right_wing_us if o.servo_right_wing_us?
    @servoRudderUs        = Fmt.f0 o.servo_rudder_us     if o.servo_rudder_us?
    @activeFlightProfile  = o.active_flight_profile if o.active_flight_profile?

  # ── Static config (fetched once on mount via /pteronautos/config) ──
  _applyConfig: (data) ->
    return unless data?.ornithopter
    o = data.ornithopter
    return unless Array.isArray(o.flight_profiles) and o.flight_profiles.length == 3
    @kernel = o.type or 'servo'
    if o.stick_override?
      @stickOverride = o.stick_override == true
    if o.stick_channels?
      @stickChannels = o.stick_channels.slice()
    if o.ratchet_timeout_ms?
      @ratchetTimeoutMs = o.ratchet_timeout_ms
    if o.flight_profiles?
      @flightProfiles = o.flight_profiles.map (p) =>
        strokeFerocity:      p.stroke_ferocity ? 30
        returnFerocity:      p.return_ferocity ? 50
        glideAngleDeg:       p.glide_angle_deg ? -4
        flappingAngleDeg:    p.flapping_angle_deg ? 0
        aileronScale:        p.aileron_scale ? 40
        elevatorScale:       p.elevator_scale ? 60
        rudderFerocityRange: p.rudder_ferocity_range ? 50
        rudderAmplitudeDifferential: p.rudder_amplitude_differential ? 0
        elevatorFerocityMix: p.elevator_ferocity_mix ? 0
        throttleFerocityMix: p.throttle_ferocity_mix ? 0
        throttleFrequencyMix: p.throttle_frequency_mix ? 0
        ferocityShapeMix: p.ferocity_shape_mix ? 0
      @_loadEditProfile() unless @_fieldFocused
      @configLoaded = true

  render: -> renderFn(this)

  # ── Flight profile editing ────────────────────────────────────────
  _onEditProfile: (evt) =>
    return if @_uiLocked()
    clearTimeout @_saveResetTimer if @_saveResetTimer
    @editProfile = parseInt(evt.target.value)
    @_loadEditProfile()

  # Load the selected edit profile's params into the slider buffer.
  _loadEditProfile: ->
    p = @_unsavedProfiles[@editProfile] or @flightProfiles[@editProfile] or @flightProfiles[1]
    @saveState = if @_unsavedProfiles[@editProfile] then 'error' else 'idle'
    @saveError = @_profileSaveErrors[@editProfile] or null
    @strokeFerocity      = p.strokeFerocity
    @returnFerocity      = p.returnFerocity
    @glideAngleDeg       = p.glideAngleDeg
    @flappingAngleDeg    = p.flappingAngleDeg
    @aileronScale        = p.aileronScale
    @elevatorScale       = p.elevatorScale
    @rudderFerocityRange = p.rudderFerocityRange
    @rudderAmplitudeDifferential = p.rudderAmplitudeDifferential
    @elevatorFerocityMix = p.elevatorFerocityMix
    @throttleFerocityMix = p.throttleFerocityMix
    @throttleFrequencyMix = p.throttleFrequencyMix
    @ferocityShapeMix = p.ferocityShapeMix

  # ── Virtual Stick (channel test) ──────────────────────────────────
  _onStickToggle: =>
    @stickOverride = not @stickOverride
    @_sendStickOverride()

  _onStickInput: (def) -> (evt) =>
    us = parseInt(evt.target.value)
    raw = @_pwmToCrsf(us)
    @stickOverride = true   # moving a slider auto-enables override
    @_markStickDragActive()
    @_sendStickChannel(def.stk, raw)

  _onStickRelease: (def) -> (evt) =>
    @_markStickDragActive()
    return unless def.snap
    @_sendStickChannelImmediate(def.stk, @_pwmToCrsf(1500))

  _sendStickChannelNow: (idx, val) ->
    arr = @stickChannels.slice()
    arr[idx] = val
    @stickChannels = arr
    body = new URLSearchParams()
    body.append "ch#{idx}", val
    body.append 'ratchet_timeout_ms', parseInt(@ratchetTimeoutMs)
    try
      await fetch '/pteronautos/stick', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()}
    catch

  _sendStickChannel: (idx, val) ->
    @_stickPending[idx] = val
    clearTimeout @_stickTimers[idx] if @_stickTimers[idx]
    @_stickTimers[idx] = setTimeout (=>
      @_stickTimers[idx] = null
      @_sendStickChannelNow idx, @_stickPending[idx]
    ), 50

  _sendStickChannelImmediate: (idx, val) ->
    clearTimeout @_stickTimers[idx] if @_stickTimers[idx]
    @_stickTimers[idx] = null
    @_sendStickChannelNow idx, val

  _sendStickOverride: ->
    body = new URLSearchParams()
    body.append 'override', if @stickOverride then '1' else '0'
    for v,i in @stickChannels
      body.append "ch#{i}", v
    try
      await fetch '/pteronautos/stick', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()}
    catch

  # ── Save flight-profile params ────────────────────────────────────
  _saveConfig: ->
    return unless @configLoaded
    if @_saveInFlight
      @_saveQueued = true
      return
    # Capture both the slot and values before yielding to the network.
    profileIndex = @editProfile
    return unless profileIndex in [0, 1, 2]
    snapshot = {}
    for prop of PROFILE_FIELDS
      value = Number(@[prop])
      bounds = @_boundsFor(prop) or [0, 100]
      unless Number.isInteger(value) and bounds[0] <= value <= bounds[1]
        @saveState = 'error'
        @saveError = "Invalid value: #{prop}"
        return
      snapshot[prop] = value
    @_saveInFlight = true
    @saveState = 'saving'
    @saveError = null
    clearTimeout @_saveResetTimer if @_saveResetTimer
    body = new URLSearchParams()
    body.append 'flight_profile', profileIndex
    for prop, key of PROFILE_FIELDS
      body.append key, snapshot[prop]
    try
      res = await fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()}
      result = await res.json()
      unless res.ok and result?.saved == true
        throw new Error(result?.error or "HTTP #{res.status}: save not confirmed")
      profiles = @flightProfiles.slice()
      profiles[profileIndex] = snapshot
      @flightProfiles = profiles
      delete @_unsavedProfiles[profileIndex]
      delete @_profileSaveErrors[profileIndex]
      @saveState = 'saved'
    catch e
      @saveState = 'error'
      @saveError = e?.message ? @_t('app.error.network')
      # Retain the attempted values per slot even if the reply was lost after
      # a successful device write. Switching profiles must not resurrect an
      # older confirmed snapshot and silently overwrite these values later.
      @_unsavedProfiles[profileIndex] = snapshot
      @_profileSaveErrors[profileIndex] = @saveError
    finally
      @_saveInFlight = false
      if @_saveQueued
        @_saveQueued = false
        @_saveConfig()
      else if @saveState == 'saved'
        @_saveResetTimer = setTimeout (=> @saveState = 'idle'), 2500

  # ── Slider / Number handlers ──────────────────────────────────────
  _onSlider: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)

  _onSliderCommit: (evt) =>
    @_saveConfig()

  _onNumberInput: (prop) -> (evt) =>
    v = parseInt(evt.target.value, 10)
    @[prop] = v unless isNaN(v)

  _onNumberCommit: (prop) -> (evt) =>
    @[prop] = @_clamp(prop, @[prop])
    @_saveConfig()

  _boundsFor: (prop) ->
    switch prop
      when 'glideAngleDeg'    then [-15, 15]
      when 'flappingAngleDeg' then [-15, 15]
      else null

  _clamp: (prop, v) ->
    b = @_boundsFor(prop)
    return v unless b
    Math.min(b[1], Math.max(b[0], v))

  _onFieldFocus: -> @_fieldFocused = true
  _onFieldBlur:  -> @_fieldFocused = false

  # ── Display formatters ────────────────────────────────────────────
  _strokeLabel: ->
    if @strokeFerocity < 5 then self._t('ornithopter.waveform.stroke_sine') else if @strokeFerocity > 80 then self._t('ornithopter.waveform.stroke_aggressive') else self._t('ornithopter.waveform.stroke_moderate')

  _returnLabel: ->
    if @returnFerocity > 70 then self._t('ornithopter.waveform.return_fast') else if @returnFerocity < 30 then self._t('ornithopter.waveform.return_slow') else self._t('ornithopter.waveform.return_normal')

  _glideAngleLabel: ->
    v = @glideAngleDeg
    if v < 0 then "#{Math.abs(v)}° #{self._t('ornithopter.angle.dihedral')}" else if v > 0 then "#{v}° #{self._t('ornithopter.angle.anhedral')}" else self._t('ornithopter.angle.neutral')

  # ── Save feedback / UI lock helpers ────────────────────────────────
  _saving: -> @_saveInFlight
  _uiLocked: -> !@configLoaded or @_saveInFlight
  _isServoProfile: -> @kernel == 'servo'
  _saveStateText: ->
    switch @saveState
      when 'saving' then @_t('ornithopter.saving')
      when 'saved'  then @_t('ornithopter.saved')
      when 'error'  then "#{@_t('ornithopter.save_error')} #{@saveError ? ''}"
      else ''
  _saveStateStyle: ->
    switch @saveState
      when 'saving' then 'color:#d4a017;'
      when 'saved'  then 'color:#4caf50;'
      when 'error'  then 'color:#e05555;'
      else ''


customElements.define 'flight-profiles-panel', FlightProfilesPanel
export default FlightProfilesPanel
