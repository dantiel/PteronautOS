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

  @properties:
    servoLeftUs:   {state: true}
    servoRightUs:  {state: true}
    servoRudderUs: {state: true}
    linkUp:        {state: true}
    _sweeping:     {state: true}
    _sweepPos:     {state: true}
    failsafeMode:  {state: true}
    failsafeDelay: {state: true}

  constructor: ->
    super()
    @servoLeftUs   = 1500
    @servoRightUs  = 1500
    @servoRudderUs = 1500
    @linkUp        = false
    @_sweeping     = false
    @_sweepPos     = 0
    @failsafeMode  = 'center'
    @failsafeDelay = 1000

  # — Apply polled state —
  _applyState: (data) ->
    if data.ornithopter
      o = data.ornithopter
      @linkUp        = !!o.link_up
      @servoLeftUs   = Fmt.f0 o.servo_left_wing_us
      @servoRightUs  = Fmt.f0 o.servo_right_wing_us
      @servoRudderUs = Fmt.f0 o.servo_rudder_us
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