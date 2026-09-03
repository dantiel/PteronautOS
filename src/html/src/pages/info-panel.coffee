import {LitElement} from 'lit'
import renderFn from './info-panel.lithaml'
import {elrsState, formatBand, formatWifiRssi} from '../utils/state'
import {i18n} from '../utils/i18n'
import {API} from '../lib/ptero'

# Mixer profile labels — index = profile_id (0..7), mirrors OrnithopterConfig.h
MIXER_PROFILE_LABELS = -> [
  self._t('ornithopter.profile.2wing_no_rudder')
  self._t('ornithopter.profile.2wing_rudder')
  self._t('ornithopter.profile.4wing')
  self._t('ornithopter.profile.vtail_rudder')
  self._t('ornithopter.profile.motor_2vtail')
  self._t('ornithopter.profile.motor_vtail_rudder')
  self._t('ornithopter.profile.elev_rudder')
  self._t('ornithopter.profile.motor_elev_rudder')
]

###
# InfoPanel — Overview start page.
# One-shot load of /pteronautos/config + /pteronautos/state on mount (no polling).
# Shows kernel/mixer/gyro status and device identity.
###
class InfoPanel extends LitElement
  @properties:
    kernelType:  {state: true}
    profileId:   {state: true}
    gyroEnabled: {state: true}
    linkUp:      {state: true}
    modelName:   {state: true}

  constructor: ->
    super()
    @kernelType  = null
    @profileId   = null
    @gyroEnabled = null
    @linkUp      = null
    @modelName   = ''

  createRenderRoot: -> this

  connectedCallback: ->
    super.connectedCallback()
    @_loadOverview()

  _loadOverview: ->
    try
      # ESP8285 cannot safely retain two ArduinoJson response documents while
      # both TCP responses are in flight. Fetch these sequentially.
      cfgResult = await API.fetchConfig()
      stateResult = await API.fetchState()
      cfg = cfgResult.data
      state = stateResult.data
      if cfg?.ornithopter
        @kernelType = cfg.ornithopter.type or 'servo'
        @profileId = cfg.ornithopter.profile_id ? null
        @modelName = cfg.ornithopter.model_name or ''
      if state
        @gyroEnabled = !!state.zephyrus?.enabled
        @linkUp = !!state.ornithopter?.link_up
    catch e
      # Overview stays static on fetch failure — device info below still renders

  # — i18n shortcut —
  _t: (key, params = {}) -> i18n.t key, params

  # — Derived labels —
  _kernelLabel: ->
    if @kernelType is 'gearbox'
      i18n.t 'elrs.info.kernel_gearbox'
    else
      i18n.t 'elrs.info.kernel_servo'

  _mixerLabel: ->
    id = @profileId
    labels = MIXER_PROFILE_LABELS()
    return '—' if id is null or id < 0 or id >= labels.length
    labels[id]

  _modelLabel: -> if @modelName then @modelName else '—'

  _gyroLabel: ->
    return '—' if @gyroEnabled is null
    if @gyroEnabled then self._t('elrs.info.enabled') else self._t('elrs.info.disabled')

  _linkLabel: ->
    return '—' if @linkUp is null
    if @linkUp then self._t('elrs.info.link_up') else self._t('elrs.info.link_down')

  # — Device info helpers —
  _product: -> elrsState.settings.product_name
  _version: -> elrsState.settings.version
  _firmware: -> elrsState.settings.target
  _radio: -> elrsState.settings['radio-type']
  _band: -> formatBand()
  _uid: -> elrsState.config.uid?.toString()
  _wifi: -> formatWifiRssi()

  render: -> renderFn this

customElements.define 'info-panel', InfoPanel
export default InfoPanel
