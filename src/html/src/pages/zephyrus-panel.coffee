import renderFn from './zephyrus-panel.lithaml'
import {PteroElement, Fmt, Style, Status, API, Zephyrus} from '../lib/ptero'

###
# Zephyrus Gyro Panel — MPU6050 live telemetry via /pteronautos/state.
# PID sliders (Roll/Pitch/Yaw with Feed Forward) and calibrate button are interactive.
# FEATURE:PTERONAUTOS
###
class ZephyrusPanel extends PteroElement
  pollRate: 2000

  @properties:
    compiled:          {state: true}
    gyroEnabled:      {state: true}
    gyroCalibrated:    {state: true}
    _calibrating:      {state: true}
    calibSamples:      {state: true}
    rollDeg:           {state: true}
    pitchDeg:          {state: true}
    yawRate:           {state: true}
    rollCorrection:    {state: true}
    yawCorrection:     {state: true}
    pitchCorrection:   {state: true}
    rudderCorrection:  {state: true}
    boardRotation:     {state: true}
    slewGain:   {state: true}
    wingRollGain:  {state: true}
    wingPitchGain: {state: true}
    wingYawGain:   {state: true}
    uptimeMs:          {state: true}
    # Roll PID
    rollP:   {state: true}
    rollI:   {state: true}
    rollD:   {state: true}
    rollMax: {state: true}
    # Pitch PID
    pitchP:   {state: true}
    pitchI:   {state: true}
    pitchD:   {state: true}
    pitchMax: {state: true}
    # Yaw PID
    yawP:   {state: true}
    yawI:   {state: true}
    yawD:   {state: true}
    yawMax: {state: true}

  compiled          = true
  gyroEnabled      = false
  gyroCalibrated    = false
  _calibrating      = false
  calibSamples      = 0
  rollDeg           = 0
  pitchDeg          = 0
  yawRate           = 0
  rollCorrection    = 0
  yawCorrection     = 0
  pitchCorrection   = 0
  rudderCorrection  = 0
  boardRotation     = 0
  slewGain   = 0
  wingRollGain  = 0
  wingPitchGain = 0
  wingYawGain   = 0
  uptimeMs          = 0
  # Roll — runtime PID gains (units match firmware ZephyrusConfig.h defaults)
  rollP   = 1.2; rollI   = 0.05; rollD   = 0.15; rollMax = 30
  # Pitch
  pitchP  = 1.0; pitchI  = 0.04; pitchD  = 0.12; pitchMax = 25
  # Yaw
  yawP    = 0.8; yawI    = 0.03; yawD    = 0.10; yawMax  = 20

  # Apply polled state
  _applyState: (data) ->
    @uptimeMs = Fmt.f0 data.uptime_ms
    return unless data.zephyrus
    z = data.zephyrus
    @compiled         = if z.compiled? then !!z.compiled else true
    @gyroEnabled      = !!z.enabled
    @gyroCalibrated    = !!z.calibrated
    @_calibrating      = !!z.calibrating
    @calibSamples      = Fmt.f0 z.calib_samples
    @rollDeg           = Fmt.f0 z.roll_deg
    @pitchDeg          = Fmt.f0 z.pitch_deg
    @yawRate           = Fmt.f0 z.yaw_rate
    @rollCorrection    = Fmt.f0 z.roll_correction
    @yawCorrection     = Fmt.f0 z.yaw_correction
    @pitchCorrection   = Fmt.f0 z.pitch_correction
    @rudderCorrection  = Fmt.f0 z.rudder_correction
    @boardRotation     = Fmt.f0 z.board_rotation
    @slewGain   = Fmt.f0 z.slew_gain
    @wingRollGain  = Fmt.f0 z.wing_roll_gain
    @wingPitchGain = Fmt.f0 z.wing_pitch_gain
    @wingYawGain   = Fmt.f0 z.wing_yaw_gain
    if z.pid?
      @rollP  = z.pid.roll.p;   @rollI  = z.pid.roll.i;   @rollD  = z.pid.roll.d;   @rollMax  = z.pid.roll.max
      @pitchP = z.pid.pitch.p;  @pitchI = z.pid.pitch.i;  @pitchD = z.pid.pitch.d;  @pitchMax = z.pid.pitch.max
      @yawP   = z.pid.yaw.p;    @yawI   = z.pid.yaw.i;    @yawD   = z.pid.yaw.d;    @yawMax   = z.pid.yaw.max

  # PID slider — live local echo on drag (no network), persist on release.
  # Splitting input (cheap, local) from change (one POST) keeps the 80MHz loop
  # free of LittleFS-write bursts during slider drags.
  _onPidLocal: (prop) -> (evt) =>
    v = parseFloat evt.target.value
    @[prop] = if isNaN(v) or not isFinite(v) then 0 else v

  _onPidPost: (param) -> (evt) =>
    v = parseFloat evt.target.value
    v = 0 if isNaN(v) or not isFinite(v)
    await fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: "#{param}=#{v}"}

  # Actions
  _doCalibrate: ->
    {ok} = await API.calibrate()
    if ok
      @_calibrating = true
      @calibSamples = 0

  _setOrientation: (evt) ->
    rot = parseInt evt.target.value
    {ok} = await API.setOrientation rot
    @boardRotation = rot if ok

  # Slew gain — sent live to /pteronautos/config on every input
  _onSlew: (evt) ->
    v = parseInt(evt.target.value) || 0
    @slewGain = v
    await fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: "slew_gain=#{v}"}

  # Mesozoic 2-wing stabilizer gain (-100..+100; sign = correction direction).
  # Local echo on drag (no network), single POST on release — mirrors the PID
  # sliders so the 80MHz loop never sees a LittleFS-write burst mid-drag.
  _onWingGainLocal: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value) || 0

  _onWingGainPost: (param) -> (evt) =>
    v = parseInt(evt.target.value) || 0
    await fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: "#{param}=#{v}"}

  # Enable/disable the gyro at runtime. Firmware re-probes the MPU on the
  # false→true edge (see Zephyrus::begin()), so this works without a reboot.
  _toggleGyro: ->
    target = if @gyroEnabled then 0 else 1
    await fetch '/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: "gyro_enabled=#{target}"}

  render: -> renderFn(this)

  # Template Helpers

  _fmt1:            (v) -> Fmt.f1 v
  _fmtDeg:          (v) -> Fmt.deg v
  _fmtDegPS:        (v) -> Fmt.degPS v
  _compiled:        -> !!@compiled
  _badgeStyle:      -> Style.badge Status.color @pollError, @gyroEnabled, not @gyroCalibrated
  _statusText:      -> Status.gyroText @pollError, @gyroEnabled, @gyroCalibrated
  _toggleLabel:     -> if @gyroEnabled then self._t('zephyrus.toggle.disable') else self._t('zephyrus.toggle.enable')
  _uptimeLabel:     -> Fmt.uptime @uptimeMs
  _rotationOptions: -> Zephyrus.rotationOptions
  _isRotation:      (val) -> Zephyrus.isRotation @boardRotation, val
  _calibrateLabel:  -> Status.calibLabel @_calibrating
  _calibDisabled:   -> not @gyroEnabled or @_calibrating
  _calibProgress:   -> Status.calibProgress @calibSamples
  _calibHint:       -> Status.calibHint @gyroEnabled, @_calibrating
  _horizonCaption:  -> Status.horizonCaption @gyroEnabled

  _horizonOuterStyle: -> Style.horizon()
  _horizonLineStyle:  -> Style.horizonLine @rollDeg, @pitchDeg
  _centerDotStyle:    -> Style.centerDot()

customElements.define 'zephyrus-panel', ZephyrusPanel
export default ZephyrusPanel