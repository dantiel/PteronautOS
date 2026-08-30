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
    uptimeMs:          {state: true}
    # Roll PID
    rollP:   {state: true}
    rollI:   {state: true}
    rollD:   {state: true}
    rollMax: {state: true}
    rollFF:  {state: true}
    # Pitch PID
    pitchP:   {state: true}
    pitchI:   {state: true}
    pitchD:   {state: true}
    pitchMax: {state: true}
    pitchFF:  {state: true}
    # Yaw PID
    yawP:   {state: true}
    yawI:   {state: true}
    yawD:   {state: true}
    yawMax: {state: true}
    yawFF:  {state: true}

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
  uptimeMs          = 0
  # Roll
  rollP   = 30; rollI   = 5; rollD   = 15; rollMax = 40; rollFF  = 70
  # Pitch
  pitchP  = 35; pitchI  = 5; pitchD  = 15; pitchMax = 40; pitchFF = 70
  # Yaw
  yawP    = 25; yawI    = 3; yawD    = 10; yawMax  = 50; yawFF   = 60

  # Apply polled state
  _applyState: (data) ->
    @uptimeMs = Fmt.f0 data.uptime_ms
    return unless data.zephyrus
    z = data.zephyrus
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
    if z.pid?
      p = z.pid
      @rollP = p.roll_p; @rollI = p.roll_i; @rollD = p.roll_d; @rollMax = p.roll_max; @rollFF = p.roll_ff
      @pitchP = p.pitch_p; @pitchI = p.pitch_i; @pitchD = p.pitch_d; @pitchMax = p.pitch_max; @pitchFF = p.pitch_ff
      @yawP = p.yaw_p; @yawI = p.yaw_i; @yawD = p.yaw_d; @yawMax = p.yaw_max; @yawFF = p.yaw_ff

  # Slider & number input handler — clamps NaN/empty to 0
  _onSlider: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value) || 0

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

  render: -> renderFn(this)

  # Template Helpers

  _fmt1:            (v) -> Fmt.f1 v
  _fmtDeg:          (v) -> Fmt.deg v
  _fmtDegPS:        (v) -> Fmt.degPS v
  _badgeStyle:      -> Style.badge Status.color @pollError, @gyroEnabled, not @gyroCalibrated
  _statusText:      -> Status.gyroText @pollError, @gyroEnabled, @gyroCalibrated
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