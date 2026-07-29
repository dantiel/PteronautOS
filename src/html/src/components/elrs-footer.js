import {html, LitElement} from 'lit'
import {customElement} from "lit/decorators.js";
import {i18n} from '../utils/i18n.js';

@customElement('elrs-footer')
export class ElrsFooter extends LitElement {
    createRenderRoot() {
        return this
    }

    connectedCallback() {
        super.connectedCallback();
        this._boundOnLocale = () => this.requestUpdate();
        window.addEventListener('locale-changed', this._boundOnLocale);
    }

    disconnectedCallback() {
        super.disconnectedCallback();
        if (this._boundOnLocale) {
            window.removeEventListener('locale-changed', this._boundOnLocale);
            this._boundOnLocale = null;
        }
    }

    _t(key, params = {}) {
        return i18n.t(key, params);
    }

    render() {
        return html`
            <footer id="footer" class="elrs-header">
                // FEATURE:PTERONAUTOS
                <div class="elrs-footer-links" style="font-size:11px; padding:2px 0;">
                    ${this._t('footer.pteronautos_tagline')}
                </div>
                // /FEATURE:PTERONAUTOS
                // FEATURE:NOT PTERONAUTOS
                <div class="elrs-footer-links">
                    ${this._t('footer.expresslrs')}
                </div>
                // /FEATURE:NOT PTERONAUTOS
            </footer>
        `
    }
}