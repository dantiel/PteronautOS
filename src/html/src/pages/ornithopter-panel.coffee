import renderFn from './ornithopter-panel.lithaml'
import {PteroElement, Fmt, Style, Status} from '../lib/ptero'

###
# Ornithopter Panel — live ornithopter telemetry via /pteronautos/state.
# Two-tier selector: Kernel (Servo-Drive | Gearbox) → Mixer (profile).
# 8 mixer profiles matching firmware MixerProfile enum:
#   0=SERVO_2WING, 1=SERVO_2WING_1RUD, 2=SERVO_4WING,
#   3=GEARBOX_2VTAIL_1RUD, 4=GEARBOX_1MOT_2VTAIL, 5=GEARBOX_1MOT_2VTAIL_1RUD,
#   6=GEARBOX_1ELE_1RUD, 7=GEARBOX_1MOT_1ELE_1RUD
# FEATURE:PTERONAUTOS
###
class OrnithopterPanel extends PteroElement
  pollRate: 500

  @properties:
    orniEnabled:   {state: true}
    linkUp:        {state: true}
    voiceThrottle: {state: true}
    voiceCadence:  {state: true}
    voiceAileron:  {state: true}
    voiceElevator: {state: true}
    voiceRudder:   {state: true}
    # Selector state
    kernel:        {state: true}  # 'servo' | 'gearbox'
    profileId:     {state: true}  # 0–7
    # Servo waveform config (servo profiles only)
    strokeFerocity:    {state: true}
    returnFerocity:    {state: true}
    glideAngleDeg:     {state: true}   # ORNI_GLIDE_ANGLE_DEG
    cycleRatingSec:    {state: true}   # ORNI_CYCLE_RATING
    # Mixing params (all profiles)
    aileronScale:       {state: true}
    elevatorScale:      {state: true}
    rudderYawWeight:    {state: true}
    rudderRollWeight:   {state: true}
    rudderFerocityRange:{state: true}
    elevonScale:        {state: true}
    motorMinUs:         {state: true}
    motorMaxUs:         {state: true}
    # Gearbox glide mode
    glideMode:          {state: true}
    hallSensorPin:      {state: true}
    ratchetThrottlePct: {state: true}
    ratchetTimeoutMs:   {state: true}

  orniEnabled   = false
  linkUp        = false
  voiceThrottle = 1500
  voiceCadence  = 0
  voiceAileron  = 0
  voiceElevator = 0
  voiceRudder   = 0
  kernel        = 'servo'
  profileId     = 1
  strokeFerocity     = 30
  returnFerocity     = 50
  glideAngleDeg      = -4
  cycleRatingSec     = 70   # ms → 0.070s
  aileronScale       = 40
  elevatorScale      = 60
  rudderYawWeight    = 65
  rudderRollWeight   = 35
  rudderFerocityRange= 50
  elevonScale        = 50
  motorMinUs         = 1000
  motorMaxUs         = 2000
  glideMode          = false
  hallSensorPin      = 12
  ratchetThrottlePct = 15
  ratchetTimeoutMs   = 500

  # ── Profile definitions (mirrors firmware MixerProfile enum) ──────
  profiles: [
    {id:0, name:'SERVO_2WING',              label:'2-Wing',              kernel:'servo',  servos:2, rudder:false, vtail:false, motor:false, map:'Left Wing, Right Wing'}
    {id:1, name:'SERVO_2WING_1RUD',         label:'2-Wing + Rudder',     kernel:'servo',  servos:3, rudder:true,  vtail:false, motor:false, map:'Left Wing, Right Wing, Rudder'}
    {id:2, name:'SERVO_4WING',              label:'4-Wing',              kernel:'servo',  servos:4, rudder:false, vtail:false, motor:false, map:'Front L, Front R, Back L, Back R'}
    {id:3, name:'GEARBOX_2VTAIL_1RUD',      label:'V-Tail + Rudder',     kernel:'gearbox',servos:3, rudder:true,  vtail:true,  motor:false, map:'Rudder, V-Tail L, V-Tail R'}
    {id:4, name:'GEARBOX_1MOT_2VTAIL',      label:'Motor + V-Tail',      kernel:'gearbox',servos:3, rudder:false, vtail:true,  motor:true,  map:'Motor, V-Tail L, V-Tail R'}
    {id:5, name:'GEARBOX_1MOT_2VTAIL_1RUD', label:'Motor + V-Tail + Rud',kernel:'gearbox',servos:4, rudder:true,  vtail:true,  motor:true,  map:'Rudder, Motor, V-Tail L, V-Tail R'}
    {id:6, name:'GEARBOX_1ELE_1RUD',        label:'Elevator + Rudder',   kernel:'gearbox',servos:2, rudder:true,  vtail:false, motor:false, map:'Rudder, Elevator'}
    {id:7, name:'GEARBOX_1MOT_1ELE_1RUD',   label:'Motor + Elev + Rud',  kernel:'gearbox',servos:3, rudder:true,  vtail:false, motor:true,  map:'Rudder, Motor, Elevator'}
  ]

  # ── Apply polled state ────────────────────────────────────────────
  _applyState: (data) ->
    return unless data.ornithopter
    o = data.ornithopter
    @linkUp        = o.link_up == true
    @orniEnabled   = o.orni_enabled == true
    @voiceThrottle = Fmt.f0 o.voice_throttle
    @voiceCadence  = Fmt.f0 o.voice_cadence
    @voiceAileron  = Fmt.f0 o.voice_aileron
    @voiceElevator = Fmt.f0 o.voice_elevator
    @voiceRudder   = Fmt.f0 o.voice_rudder
    ft = o.type or 'servo'
    if ft != @kernel
      @kernel = ft
      @profileId = @_firstForKernel(ft)
    if o.profile_id?
      @profileId = o.profile_id

  # ── Slider / Select handlers ──────────────────────────────────────
  _onSlider: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)

  _onCheck: (prop) -> (evt) =>
    @[prop] = evt.target.checked

  _onSelect: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)

  _onKernel: (evt) =>
    @kernel = evt.target.value
    @profileId = @_firstForKernel(@kernel)

  _onMixer: (evt) =>
    @profileId = parseInt(evt.target.value)

  render: -> renderFn(this)

  # ── Profile helpers ───────────────────────────────────────────────
  _firstForKernel: (k) ->
    for p in @profiles
      return p.id if p.kernel == k
    1  # fallback

  _profilesForKernel: ->
    k = @kernel
    @profiles.filter (p) -> p.kernel == k

  _currentProfile: -> @profiles[@profileId] or @profiles[1]

  _saveConfig: ->
    body = JSON.stringify {
      kernel: @kernel
      profile_id: @profileId
      stroke_ferocity: @strokeFerocity
      return_ferocity: @returnFerocity
      glide_angle_deg: @glideAngleDeg
      cycle_rating_ms: @cycleRatingSec
      aileron_scale: @aileronScale
      elevator_scale: @elevatorScale
      rudder_yaw_weight: @rudderYawWeight
      rudder_roll_weight: @rudderRollWeight
      rudder_ferocity_range: @rudderFerocityRange
      elevon_scale: @elevonScale
      motor_min_us: @motorMinUs
      motor_max_us: @motorMaxUs
      glide_mode: @glideMode
      hall_sensor_pin: @hallSensorPin
      ratchet_throttle_pct: @ratchetThrottlePct
      ratchet_timeout_ms: @ratchetTimeoutMs
    }
    fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/json'}, body}

  _kernelLabel: ->
    if @kernel == 'servo' then 'Servo-Drive' else 'Gearbox'

  _servoCount: -> @_currentProfile().servos
  _servoMapStr: -> @_currentProfile().map
  _isServoProfile: -> @_currentProfile().kernel == 'servo'
  _hasRudder: -> @_currentProfile().rudder
  _hasVtail: -> @_currentProfile().vtail
  _hasMotor: -> @_currentProfile().motor
  _isGearboxProfile: -> @_currentProfile().kernel == 'gearbox'

  # ── Hall sensor free-pin map per profile ──────────────────────────
  # Reserved across all: GPIO1(UART TX),3(UART RX),2(radio_rst),4(I2C SDA),5(radio_busy)
  # PWM indices used vary by profile — free pins = GPIO12,13,14,15 always available
  _freeHallPins: ->
    p = @_currentProfile()
    used = [1,2,3,4,5]  # always reserved
    # Map used PWM pins from funcMap (NONE slots are free for hall sensor)
    # Standard ESP8285 PWM pin layout: idx0=GPIO0, idx3=GPIO9, idx4=GPIO10, idx6=GPIO16
    pwmPinMap = {0:0, 3:9, 4:10, 6:16}
    for idx,pin of pwmPinMap
      used.push(pin) if idx < p.servos  # crude: if profile uses this many servos, these pins are taken
    # Always offer GPIO12,13,14,15 as hall sensor candidates
    [12,13,14,15]

  _hallPinLabel: (pin) -> "GPIO#{pin}"

  # ── Display formatters ────────────────────────────────────────────
  _cycleRatingDisplay: -> (Fmt.f3(@cycleRatingSec / 1000.0)) + 's'

  _strokeLabel: ->
    if @strokeFerocity < 5 then 'Pure Sine' else if @strokeFerocity > 80 then 'Aggressive' else 'Moderate'

  _returnLabel: ->
    if @returnFerocity > 70 then 'Fast Upstroke' else if @returnFerocity < 30 then 'Slow Upstroke' else 'Normal'

  _glideAngleLabel: ->
    v = @glideAngleDeg
    if v < 0 then "#{Math.abs(v)}° Dihedral" else if v > 0 then "#{v}° Anhedral" else 'Neutral'

  # ── Style/Status helpers ──────────────────────────────────────────
  _badgeStyle: -> Style.badge Status.color @pollError, @linkUp
  _statusText: -> Status.orniText @pollError, @linkUp


customElements.define 'ornithopter-panel', OrnithopterPanel
export default OrnithopterPanel