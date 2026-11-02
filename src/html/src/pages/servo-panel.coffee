import renderFn from './servo-panel.lithaml'
import {PteroElement, Fmt, Style, Status, Servo} from '../lib/ptero'
import {i18n} from '../utils/i18n'

###
# Servo Output Panel — Live PWM monitor via /pteronautos/state.
# Sweep button and failsafe config are interactive.
# FEATURE:PTERONAUTOS
###
class ServoPanel extends PteroElement
  pollRate: 2000
  # Fetch /pteronautos/config once on mount for pwm_pins + func_map (the real
  # per-pad GPIO + servo-function routing, not the old hardcoded channel table).
  needsConfig: true

  @properties:
    servoLeftUs:   {state: true}
    servoRightUs:  {state: true}
    servoRudderUs: {state: true}
    servoMotorUs:  {state: true}
    linkUp:        {state: true}
    pwmPins:       {state: true}
    funcMap:       {state: true}
    _sweeping:     {state: true}
    _sweepPos:     {state: true}
    failsafeMode:  {state: true}
    failsafeDelay: {state: true}
    _dragIndex:     {state: true}
    _dragOverIndex: {state: true}

  constructor: ->
    super()
    @servoLeftUs   = 1500
    @servoRightUs  = 1500
    @servoRudderUs = 1500
    @servoMotorUs  = 1500
    @linkUp        = false
    # Sensible defaults so the table is never empty before/without the one-shot
    # /pteronautos/config load (which supplies the real per-pad GPIO + func_map).
    @pwmPins       = [0, 1, 3]
    @funcMap       = [1, 2, 3]
    @_sweeping     = false
    @_sweepPos     = 0
    @failsafeMode  = 'center'
    @failsafeDelay = 1000
    @_dragIndex     = -1
    @_dragOverIndex = -1

  # — Static config: real GPIO + servo function per pad, from /pteronautos/config —
  _applyConfig: (data) ->
    o = data?.ornithopter
    return unless o
    # Only override the constructor defaults when the firmware reports real
    # channels. Empty arrays are legitimate (e.g. a companion with no local
    # PWM), so keep the defaults in that case instead of blanking the table.
    @pwmPins = o.pwm_pins if o.pwm_pins?.length
    @funcMap = o.func_map if o.func_map?.length

  # — Apply polled state —
  _applyState: (data) ->
    if data.ornithopter
      o = data.ornithopter
      @linkUp        = !!o.link_up
      @servoLeftUs   = Fmt.f0 o.servo_left_wing_us
      @servoRightUs  = Fmt.f0 o.servo_right_wing_us
      @servoRudderUs = Fmt.f0 o.servo_rudder_us
      @servoMotorUs  = Fmt.f0 o.servo_motor_us
    # Sweep status from firmware
    if data.sweep
      wasSweeping = @_sweeping
      @_sweeping = !!data.sweep.active
      if @_sweeping
        sweepUs = Fmt.f0 data.sweep.us
        @_sweepPos = Math.round((sweepUs - 1000) / 10)
        @servoLeftUs = @servoRightUs = @servoRudderUs = sweepUs

  # — Sweep test — firmware-side triangle wave, auto-stops after 6s —
  _doSweep: ->
    if @_sweeping
      fetch('/pteronautos/sweep', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'state=0'})
      @_sweeping = false
      @_sweepPos = 0
    else
      fetch('/pteronautos/sweep', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'state=1'})
      @_sweeping = true

  _onSlider: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)

  _onSelect: (prop) -> (evt) =>
    @[prop] = evt.target.value

  # — Drag-and-drop servo reorder — re-maps which function drives which pad.
  _onDragStart: (i) -> (evt) =>
    @_dragIndex = i
    evt.dataTransfer?.effectAllowed = 'move'
    try evt.dataTransfer?.setData 'text/plain', String(i)

  _onDragOver: (i) -> (evt) =>
    evt.preventDefault()
    evt.dataTransfer?.dropEffect = 'move'
    @_dragOverIndex = i

  _onDrop: (i) -> (evt) =>
    evt.preventDefault()
    @_reorderServo @_dragIndex, i
    @_dragIndex = -1
    @_dragOverIndex = -1

  _onDragEnd: (evt) =>
    @_dragIndex = -1
    @_dragOverIndex = -1

  _reorderServo: (from, to) ->
    return if from < 0 or to < 0 or from is to
    fm = (@funcMap ? []).slice()
    return if from >= fm.length or to >= fm.length
    [fm[from], fm[to]] = [fm[to], fm[from]]
    @funcMap = fm
    @_persistServoOrder()

  _persistServoOrder: ->
    body = new URLSearchParams()
    body.append 'func_map', (@funcMap ? []).join(',')
    fetch('/pteronautos/config', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body: body.toString()})

  render: -> renderFn(this)

  # — Template Helpers —

  _servoChannels: -> Servo.channels @funcMap, @pwmPins
  _rowStyle:      (i) -> if i is @_dragOverIndex then "background:#3a3a18;outline:1px dashed #da0;cursor:grabbing;" else "cursor:grab;"
  _liveUs:        (ch) -> Servo.liveUs ch, @servoLeftUs, @servoRightUs, @servoRudderUs, @servoMotorUs
  _liveLabel:     (ch) -> Servo.liveLabel @_liveUs ch
  _liveStyle:     (ch) -> Servo.liveStyle @_liveUs ch
  _badgeStyle:    -> Style.badge Status.color @pollError, @linkUp
  _statusText:    -> Status.servoText @pollError, @linkUp
  _footerText:    -> Status.servoFooter @linkUp
  _sweepLabel:    -> if @_sweeping then i18n.t('servo.sweep.stop') else i18n.t('servo.sweep.btn')
  _sweepProgress: -> if @_sweeping then i18n.t('servo.sweep.sweeping', {pos: @_sweepPos}) else ''
  _failsafeOptions: -> [
    {val: 'center', label: i18n.t('servo.failsafe.center')}
    {val: 'hold',   label: i18n.t('servo.failsafe.hold')}
    {val: 'custom', label: i18n.t('servo.failsafe.no_pulses')}
  ]

customElements.define 'servo-panel', ServoPanel
export default ServoPanel