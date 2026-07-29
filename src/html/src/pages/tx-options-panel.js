import {html, LitElement} from "lit"
import {unsafeHTML} from "lit/directives/unsafe-html.js"
import {customElement, state} from "lit/decorators.js"
import {elrsState, saveOptions} from "../utils/state.js"
import {_renderOptions} from "../utils/libs.js"
import {postWithFeedback} from "../utils/feedback.js"
import {i18n} from "../utils/i18n.js"

@customElement('tx-options-panel')
class TxOptionsPanel extends LitElement {
    @state() accessor domain
    @state() accessor isAirport
    @state() accessor baudRate
    @state() accessor tlmInterval
    @state() accessor fanRuntime

    createRenderRoot() {
        this.domain = elrsState.options.domain
        this.isAirport = elrsState.options['is-airport']
        this.baudRate = elrsState.options['airport-uart-baud']
        this.tlmInterval = elrsState.options['tlm-interval']
        this.fanRuntime = elrsState.options['fan-runtime']
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.tx_options.title')}</div>
            <div class="mui-panel">
                <form class="mui-form">
                    <p>${unsafeHTML(i18n.t('elrs.common.override_note'))}</p>
                    <!-- FEATURE:HAS_SUBGHZ -->
                    <div class="mui-select">
                        <select id="domain" @change="${(e) => this.domain = parseInt(e.target.value)}">
                            ${_renderOptions(['AU915', 'FCC915', 'EU868', 'IN866', 'AU433', 'EU433', 'US433', 'US433-Wide'], this.domain)}
                        </select>
                        <label for="domain">${i18n.t('elrs.tx_options.domain_label')}</label>
                    </div>
                    <!-- /FEATURE:HAS_SUBGHZ -->
                    <div class="mui-textfield">
                        <input id="tlm" size='5' type='number'
                               @input="${(e) => this.tlmInterval = parseInt(e.target.value)}"
                               .value="${this.tlmInterval}">
                        <label for="tlm">${i18n.t('elrs.tx_options.tlm_label')}</label>
                    </div>
                    <div class="mui-textfield">
                        <input id="fan" size='3' type='number'
                               @input="${(e) => this.fanRuntime = parseInt(e.target.value)}"
                               .value="${this.fanRuntime}">
                        <label for="fan">${i18n.t('elrs.tx_options.fan_label')}</label>
                    </div>
                    <div class="mui-checkbox">
                        <input id="airport" type='checkbox'
                               @change="${(e) => this.isAirport = e.target.checked}"
                               ?checked="${this.isAirport}">
                        <label for="airport">${i18n.t('elrs.tx_options.airport_checkbox')}</label>
                    </div>
                    ${this.isAirport ? html`
                        <div class="mui-textfield">
                        <input id="baud" size='7' type='number'
                               @input="${(e) => this.baudRate = parseInt(e.target.value)}"
                               .value="${this.baudRate}">
                        <label for="baud">${i18n.t('elrs.tx_options.baud_label')}</label>
                        </div>
                    ` : ''}

                    <button class="mui-btn mui-btn--primary"
                            ?disabled="${!this.checkChanged()}"
                            @click="${this.save}"
                    >
                        ${i18n.t('elrs.common.save')}
                    </button>
                    ${elrsState.options.customised ? html`
                        <button class="mui-btn mui-btn--small mui-btn--danger mui--pull-right"
                                @click="${postWithFeedback(i18n.t('elrs.tx_options.reset_title'), i18n.t('elrs.tx_options.reset_error'), '/reset?options', null)}"
                        >
                            ${i18n.t('elrs.common.reset_to_defaults')}
                        </button>
                    ` : ''}
                </form>
            </div>
        `
    }

    save(e) {
        e.preventDefault()
        const changes = {
            // FEATURE: HAS_SUBGHZ
            'domain': this.domain,
            // /FEATURE: HAS_SUBGHZ
            'tlm-interval': this.tlmInterval,
            'fan-runtime': this.fanRuntime,
            'is-airport': this.isAirport,
            'airport-uart-baud': this.baudRate
        }
        saveOptions(changes, () => {
            return this.requestUpdate()
        })
    }

    checkChanged() {
        let changed = false
        // FEATURE: HAS_SUBGHZ
        changed |= this.domain !== elrsState.options['domain']
        // /FEATURE: HAS_SUBGHZ
        changed |= this.tlmInterval !== elrsState.options['tlm-interval']
        changed |= this.fanRuntime !== elrsState.options['fan-runtime']
        changed |= this.isAirport !== elrsState.options['is-airport']
        changed |= this.baudRate !== elrsState.options['airport-uart-baud']
        return !!changed
    }
}