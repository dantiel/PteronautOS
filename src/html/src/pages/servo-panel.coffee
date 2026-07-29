import renderFn from './servo-panel.lithaml'
import {PteroElement, Fmt, Style, Status, Servo} from '../lib/ptero'
import {i18n} from '../utils/i18n'

###
# Servo Output Panel — Live PWM monitor via /pteronautos/state.
# Sweep button and failsafe config are interactive.
# FEATURE:PTERONAUTOS
###
class ServoPanel extends PteroElement
  pollRate: 500

  @properties:
    servoLeftUs:   {state: true}
    servoRightUs:  {state: true}
    servoRudderUs: {state: true}
    linkUp:        {state: true}
    _sweeping:     {state: true}
    _sweepPos:     {state: true}
    failsafeMode:  {state: true}
    failsafeDelay: {state: true}

  servoLeftUs   = 1500
  servoRightUs  = 1500
  servoRudderUs = 1500
  linkUp        = false
  _sweeping     = false
  _sweepPos     = 0
  failsafeMode  = 'center'
  failsafeDelay = 1000

  # — Apply polled state —
  _applyState: (data) ->
    return unless data.ornithopter
    o = data.ornithopter
    @linkUp        = !!o.link_up
    @servoLeftUs   = Fmt.f0 o.servo_left_us
    @servoRightUs  = Fmt.f0 o.servo_right_us
    @servoRudderUs = Fmt.f0 o.servo_rudder_us

  # — Sweep test animation —
  _doSweep: ->
    return if @_sweeping
    @_sweeping = true
    @_sweepPos = 0
    dir = 1
    step = 30
    tick = =>
      return unless @_sweeping
      @_sweepPos += dir * step
      if @_sweepPos >= 100
        @_sweepPos = 100
        dir = -1
      else if @_sweepPos <= 0
        @_sweepPos = 0
        dir = 1
      # Animate — sweep all channels
      pos = 1000 + (@_sweepPos / 100) * 1000
      @servoLeftUs   = Fmt.f0 pos
      @servoRightUs  = Fmt.f0 pos
      @servoRudderUs = Fmt.f0 pos
      setTimeout(tick, 40) if @_sweeping
    tick()
    # Auto-stop after 3 seconds
    setTimeout(=>
      @_sweeping = false
      @_sweepPos = 0
    , 3000)

  _onSlider: (prop) -> (evt) =>
    @[prop] = parseInt(evt.target.value)

  _onSelect: (prop) -> (evt) =>
    @[prop] = evt.target.value

  render: -> renderFn(this)

  # — Template Helpers —

  _servoChannels: -> Servo.channels
  _liveUs:        (ch) -> Servo.liveUs ch, @servoLeftUs, @servoRightUs, @servoRudderUs
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