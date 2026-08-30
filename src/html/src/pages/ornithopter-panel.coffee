import renderFn from './ornithopter-panel.lithaml'
import {PteroElement} from '../lib/ptero'
import {i18n} from '../utils/i18n'

###
# Ornithopter Panel — bench mode: kernel + base configuration.
# Two-tier selector: Kernel (Servo-Drive | Gearbox) → Mixer (profile).
# 8 mixer profiles matching firmware MixerProfile enum:
#   0=SERVO_2WING, 1=SERVO_2WING_1RUD, 2=SERVO_4WING,
#   3=GEARBOX_2VTAIL_1RUD, 4=GEARBOX_1MOT_2VTAIL, 5=GEARBOX_1MOT_2VTAIL_1RUD,
#   6=GEARBOX_1ELE_1RUD, 7=GEARBOX_1MOT_1ELE_1RUD
# Per-flight-profile tuning lives in FlightProfilesPanel (separate tab).
# FEATURE:PTERONAUTOS
###
class OrnithopterPanel extends PteroElement
  pollRate: 2000
  needsConfig: true

  @properties:
    stateLoaded:  {state: true}
    # Selector state
    kernel:       {state: true}  # 'servo' | 'gearbox'
    profileId:    {state: true}  # 0–7
    # Kernel param (global — not per flight profile)
    servoSpeed:   {state: true}
    flapBaseFreq: {state: true}
    servoMinUs:   {state: true}
    servoMaxUs:   {state: true}
    # Per-servo correction (µs trim), indexed by ServoFunc
    servoTrimLeftWing:      {state: true}
    servoTrimRightWing:     {state: true}
    servoTrimRudder:        {state: true}
    servoTrimVtailLeft:     {state: true}
    servoTrimVtailRight:    {state: true}
    servoTrimElevator:      {state: true}
    servoTrimBackLeftWing:  {state: true}
    servoTrimBackRightWing: {state: true}
    # Global mixing params
    rudderYawWeight:    {state: true}
    rudderRollWeight:   {state: true}
    elevonScale:        {state: true}
    motorMinUs:         {state: true}
    motorMaxUs:         {state: true}
    # Gearbox glide mode
    glideMode:          {state: true}
    hallSensorPin:      {state: true}
    ratchetThrottlePct: {state: true}
    ratchetTimeoutMs:   {state: true}
    modelName:          {state: true}

  constructor: ->
    super()
    @stateLoaded          = false
    @kernel               = 'servo'
    @profileId            = 1
    @servoSpeed           = 70
    @flapBaseFreq         = 50
    @servoMinUs           = 988
    @servoMaxUs           = 2012
    @servoTrimLeftWing    = 0
    @servoTrimRightWing   = 0
    @servoTrimRudder      = 0
    @servoTrimVtailLeft   = 0
    @servoTrimVtailRight  = 0
    @servoTrimElevator    = 0
    @servoTrimBackLeftWing  = 0
    @servoTrimBackRightWing = 0
    @rudderYawWeight      = 65
    @rudderRollWeight     = 35
    @elevonScale          = 50
    @motorMinUs           = 1000
    @motorMaxUs           = 2000
    @glideMode            = false
    @hallSensorPin        = 12
    @ratchetThrottlePct   = 15
    @ratchetTimeoutMs     = 500
    @modelName            = ''
    @_pollInFlight        = false
    @_saveInFlight        = false
    @_fieldFocused        = false
    @saveState            = 'idle'   # idle | saving | saved | error
    @saveError            = null
    @_saveResetTimer      = null

  # ── Non-overlapping poll guard ────────────────────────────────────
  _doPoll: ->
    return if @_pollInFlight or @_saveInFlight
    @_pollInFlight = true
    try
      await super()
    finally
      @_pollInFlight = false

  # ── Profile definitions (mirrors firmware MixerProfile enum) ──────
  # PWM pins now: idx0=GPIO9, idx1=GPIO10, idx2=GPIO5 (channels 4/5/6)
  profiles: [
    {id:0, name:'SERVO_2WING',             label:self._t('ornithopter.profile.2wing_no_rudder'),          kernel:'servo',   servos:2, rudder:false, vtail:false, motor:false, map:'L Wing GPIO9 · R Wing GPIO10'}
    {id:1, name:'SERVO_2WING_1RUD',        label:self._t('ornithopter.profile.2wing_rudder'),             kernel:'servo',   servos:3, rudder:true,  vtail:false, motor:false, map:'L Wing GPIO9 · R Wing GPIO10 · Rudder GPIO5'}
    {id:2, name:'SERVO_4WING',             label:self._t('ornithopter.profile.4wing'), kernel:'servo', servos:4, rudder:false, vtail:false, motor:false, map:'L GPIO9 · R GPIO10 · Back-L GPIO5'}
    {id:3, name:'GEARBOX_2VTAIL_1RUD',     label:self._t('ornithopter.profile.vtail_rudder'),             kernel:'gearbox', servos:3, rudder:true,  vtail:true,  motor:false, map:'Rudder GPIO9 · V-Tail L GPIO10 · V-Tail R GPIO5'}
    {id:4, name:'GEARBOX_1MOT_2VTAIL',     label:self._t('ornithopter.profile.motor_2vtail'),            kernel:'gearbox', servos:3, rudder:false, vtail:true,  motor:true,  map:'Motor GPIO9 · V-Tail L GPIO10 · V-Tail R GPIO5'}
    {id:5, name:'GEARBOX_1MOT_2VTAIL_1RUD',label:self._t('ornithopter.profile.motor_vtail_rudder'), kernel:'gearbox', servos:4, rudder:true, vtail:true, motor:true, map:'Rudder GPIO9 · Motor GPIO10 · V-Tail L GPIO5'}
    {id:6, name:'GEARBOX_1ELE_1RUD',       label:self._t('ornithopter.profile.elev_rudder'),           kernel:'gearbox', servos:2, rudder:true,  vtail:false, motor:false, map:'Rudder GPIO9 · Elevator GPIO10'}
    {id:7, name:'GEARBOX_1MOT_1ELE_1RUD',  label:self._t('ornithopter.profile.motor_elev_rudder'),       kernel:'gearbox', servos:3, rudder:true,  vtail:false, motor:true,  map:'Rudder GPIO9 · Motor GPIO10 · Elevator GPIO5'}
  ]

  # ── Apply polled state (bench is config-driven; state only unlocks UI) ──
  _applyState: (data) ->
    @stateLoaded = true if data?.ornithopter

  # ── Static config (fetched once on mount via /pteronautos/config) ──
  _applyConfig: (data) ->
    return unless data?.ornithopter
    o = data.ornithopter
    ft = o.type or 'servo'
    if ft != @kernel
      @kernel = ft
      @profileId = @_firstForKernel(ft)
    @profileId = o.profile_id if o.profile_id?
    @modelName = o.model_name or ''
    @stateLoaded = true
    # Guarded while a number field has focus, so a stale fetch can't clobber
    # a value mid-typing.
    unless @_fieldFocused
      @servoSpeed         = o.servo_speed          if o.servo_speed?
      @flapBaseFreq       = o.flap_base_freq       if o.flap_base_freq?
      @servoMinUs         = o.servo_min_us         if o.servo_min_us?
      @servoMaxUs         = o.servo_max_us         if o.servo_max_us?
      if o.servo_trim?
        t = o.servo_trim
        @servoTrimLeftWing     = t[1] ? 0
        @servoTrimRightWing    = t[2] ? 0
        @servoTrimRudder       = t[3] ? 0
        @servoTrimVtailLeft    = t[5] ? 0
        @servoTrimVtailRight   = t[6] ? 0
        @servoTrimElevator     = t[7] ? 0
        @servoTrimBackLeftWing = t[8] ? 0
        @servoTrimBackRightWing = t[9] ? 0
      @rudderYawWeight    = o.rudder_yaw_weight    if o.rudder_yaw_weight?
      @rudderRollWeight   = o.rudder_roll_weight   if o.rudder_roll_weight?
      @elevonScale        = o.elevon_scale         if o.elevon_scale?
      @motorMinUs         = o.motor_min_us         if o.motor_min_us?
      @motorMaxUs         = o.motor_max_us         if o.motor_max_us?
      @glideMode          = o.glide_mode == true   if o.glide_mode?
      @hallSensorPin      = o.hall_sensor_pin      if o.hall_sensor_pin?
      @ratchetThrottlePct = o.ratchet_throttle_pct if o.ratchet_throttle_pct?
      @ratchetTimeoutMs   = o.ratchet_timeout_ms   if o.ratchet_timeout_ms?

  render: -> renderFn(this)

  # ── Selector handlers ─────────────────────────────────────────────
  _onKernel: (evt) =>
    @kernel = evt.target.value
    @profileId = @_firstForKernel(@kernel)
    @_saveConfig()

  _onMixer: (evt) =>
    @profileId = parseInt(evt.target.value)
    @_saveConfig()

  # ── Profile helpers ───────────────────────────────────────────────
  _firstForKernel: (k) ->
    for p in @profiles
      return p.id if p.kernel == k
    1  # fallback

  _profilesForKernel: ->
    k = @kernel
    @profiles.filter (p) -> p.kernel == k

  _currentProfile: -> @profiles[@profileId] or @profiles[0]

  _servoCount: -> @_currentProfile().servos
  _servoMapStr: -> @_currentProfile().map
  _hasRudder: -> @_currentProfile().rudder
  _hasVtail: -> @_currentProfile().vtail
  _hasMotor: -> @_currentProfile().motor
  _isGearboxProfile: -> @_currentProfile().kernel == 'gearbox'

  # Per-servo trim sliders for the current profile: {prop, label}
  _servoTrimList: ->
    p = @_currentProfile()
    if p.kernel == 'servo'
      out = [{prop: 'servoTrimLeftWing', label: self._t('ornithopter.servo.left_wing')}]
      out.push {prop: 'servoTrimRightWing', label: self._t('ornithopter.servo.right_wing')}
      out.push {prop: 'servoTrimRudder', label: self._t('ornithopter.servo.rudder')} if p.rudder
      out.push {prop: 'servoTrimBackLeftWing', label: self._t('ornithopter.servo.back_left_wing')} if p.id == 2
      out
    else
      out = []
      out.push {prop: 'servoTrimRudder', label: self._t('ornithopter.servo.rudder')} if p.rudder
      if p.vtail
        out.push {prop: 'servoTrimVtailLeft', label: self._t('ornithopter.servo.vtail_left')}
        out.push {prop: 'servoTrimVtailRight', label: self._t('ornithopter.servo.vtail_right')}
      else
        out.push {prop: 'servoTrimElevator', label: self._t('ornithopter.servo.elevator')} if p.id in [6, 7]
      out

  # ── Hall sensor free-pin map per profile ──────────────────────────
  _freeHallPins: ->
    p = @_currentProfile()
    used = [1,2,3,4,5]  # always reserved
    pwmPinMap = {0:0, 3:9, 4:10, 6:16}
    for idx,pin of pwmPinMap
      used.push(pin) if idx < p.servos
    [12,13,14,15]

  _hallPinLabel: (pin) -> "GPIO#{pin}"

  # ── Display formatters ────────────────────────────────────────────
  _servoSpeedLabel: ->
    "#{(@servoSpeed / 1000).toFixed(3)} s/60°"

  _flapBaseFreqLabel: ->
    "#{(@flapBaseFreq / 10).toFixed(1)} Hz"

  # ── Slider / Number / Select handlers ─────────────────────────────
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

  _nudge: (prop, delta) -> (evt) =>
    v = (@[prop] ? 0) + delta
    @[prop] = @_clamp(prop, v)
    @_saveConfig()

  _onFieldFocus: -> @_fieldFocused = true
  _onFieldBlur:  -> @_fieldFocused = false

  _onModelName: (evt) =>
    @modelName = evt.target.value

  _onModelNameCommit: (evt) =>
    @modelName = evt.target.value
    @_saveConfig()

  _onCheck: (prop) -> (evt) =>
    @[prop] = evt.target.checked
    @_saveConfig()

  _onSelect: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)
    @_saveConfig()

  # ── Servo presets ─────────────────────────────────────────────────
  _servoPresets: -> [
    {label: 'KST MS320 (0.07 s/60°, 3.2 kg·cm)',      ms: 70, min: 500, max: 2500}
    {label: 'Blue Arrow D0576HT-MG-HV (0.06 s/60°)',  ms: 60, min: 900, max: 2100}
    {label: 'Blue Arrow AF D43S-6.0-MG (0.09 s/60°)', ms: 90, min: 900, max: 2100}
    {label: 'PTK 4765 (0.08 s/60°)',                  ms: 80, min: 900, max: 2100}
    {label: 'PTK 7350 MG-D (0.06 s/60°)',             ms: 60, min: 900, max: 2100}
    {label: 'PTK 7465 Plus (0.06 s/60°)',             ms: 60, min: 500, max: 2500}
  ]

  _onServoPreset: (evt) ->
    idx = parseInt(evt.target.value, 10)
    return if isNaN(idx)
    p = @_servoPresets()[idx]
    return unless p
    @servoSpeed = @_clamp('servoSpeed', p.ms)
    @servoMinUs = @_clamp('servoMinUs', p.min)
    @servoMaxUs = @_clamp('servoMaxUs', p.max)
    @_saveConfig()

  _boundsFor: (prop) ->
    return [-300, 300] if prop.indexOf('servoTrim') == 0
    switch prop
      when 'servoSpeed'  then [25, 250]
      when 'servoMinUs'  then [500, 1490]
      when 'servoMaxUs'  then [1510, 2500]
      else null

  _clamp: (prop, v) ->
    b = @_boundsFor(prop)
    return v unless b
    Math.min(b[1], Math.max(b[0], v))

  # ── Save kernel + base config ─────────────────────────────────────
  _saveConfig: ->
    return if @_saveInFlight
    @_saveInFlight = true
    @saveState = 'saving'
    @saveError = null
    clearTimeout @_saveResetTimer if @_saveResetTimer
    body = new URLSearchParams()
    body.append 'profile_id',           parseInt(@profileId)
    body.append 'servo_speed',          parseInt(@servoSpeed)
    body.append 'flap_base_freq',       parseInt(@flapBaseFreq)
    body.append 'servo_min_us',         parseInt(@servoMinUs)
    body.append 'servo_max_us',         parseInt(@servoMaxUs)
    body.append 'servo_trim_1',         parseInt(@servoTrimLeftWing)
    body.append 'servo_trim_2',         parseInt(@servoTrimRightWing)
    body.append 'servo_trim_3',         parseInt(@servoTrimRudder)
    body.append 'servo_trim_5',         parseInt(@servoTrimVtailLeft)
    body.append 'servo_trim_6',         parseInt(@servoTrimVtailRight)
    body.append 'servo_trim_7',         parseInt(@servoTrimElevator)
    body.append 'servo_trim_8',         parseInt(@servoTrimBackLeftWing)
    body.append 'servo_trim_9',         parseInt(@servoTrimBackRightWing)
    body.append 'rudder_yaw_weight',    parseInt(@rudderYawWeight)
    body.append 'rudder_roll_weight',   parseInt(@rudderRollWeight)
    body.append 'elevon_scale',         parseInt(@elevonScale)
    body.append 'motor_min_us',         parseInt(@motorMinUs)
    body.append 'motor_max_us',         parseInt(@motorMaxUs)
    body.append 'glide_mode',           if @glideMode then '1' else '0'
    body.append 'hall_sensor_pin',      parseInt(@hallSensorPin)
    body.append 'ratchet_throttle_pct', parseInt(@ratchetThrottlePct)
    body.append 'ratchet_timeout_ms',   parseInt(@ratchetTimeoutMs)
    body.append 'model_name',           @modelName or ''
    try
      res = await fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()}
      if res and res.ok
        @saveState = 'saved'
      else
        @saveState = 'error'
        @saveError = "HTTP #{res?.status ? '?'}"
    catch e
      @saveState = 'error'
      @saveError = e?.message ? self._t('app.error.network')
    finally
      @_saveInFlight = false
      @_saveResetTimer = setTimeout (=> @saveState = 'idle'), 2500

  # ── Save feedback / UI lock helpers ────────────────────────────────
  _saving: -> @_saveInFlight
  _uiLocked: -> !@stateLoaded or @_saveInFlight
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


customElements.define 'ornithopter-panel', OrnithopterPanel
export default OrnithopterPanel