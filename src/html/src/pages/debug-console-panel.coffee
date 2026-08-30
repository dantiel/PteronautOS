import {LitElement} from 'lit'
import renderFn from './debug-console-panel.lithaml'
import {i18n} from '../utils/i18n'

###
# DebugConsolePanel — placeholder diagnostics page.
###
class DebugConsolePanel extends LitElement
  createRenderRoot: -> this

  connectedCallback: ->
    super.connectedCallback()
    @_boundOnLocale = => @requestUpdate()
    window.addEventListener 'locale-changed', @_boundOnLocale

  disconnectedCallback: ->
    super.disconnectedCallback()
    if @_boundOnLocale
      window.removeEventListener 'locale-changed', @_boundOnLocale
      @_boundOnLocale = null

  _t: (key, params = {}) -> i18n.t key, params

  render: -> renderFn this

customElements.define 'debug-console-panel', DebugConsolePanel
export default DebugConsolePanel
