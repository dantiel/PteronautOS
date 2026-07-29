import {html, LitElement} from 'lit'
import {customElement} from 'lit/decorators.js'
import {i18n} from '../utils/i18n.js'

@customElement('debug-console-panel')
export class DebugConsolePanel extends LitElement {
  createRenderRoot() { return this }

  connectedCallback() {
    super.connectedCallback()
    this._boundOnLocale = () => this.requestUpdate()
    window.addEventListener('locale-changed', this._boundOnLocale)
  }

  disconnectedCallback() {
    super.disconnectedCallback()
    if (this._boundOnLocale) {
      window.removeEventListener('locale-changed', this._boundOnLocale)
      this._boundOnLocale = null
    }
  }

  _t(key, params = {}) {
    return i18n.t(key, params)
  }

  render() {
    return html`<div class="mui-panel mui--text-title">${this._t('debug.panel.title')}</div>
      <div class="mui-panel"><p>${this._t('debug.coming_soon')}</p></div>`
  }
}