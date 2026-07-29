import {html, LitElement} from "lit"
import {unsafeHTML} from "lit/directives/unsafe-html.js"
import {customElement, state} from "lit/decorators.js"
import {_renderOptions, _uintInput} from "../utils/libs.js"
import {elrsState, saveOptionsAndConfig} from "../utils/state.js"
import {postWithFeedback} from "../utils/feedback.js"
import {i18n} from "../utils/i18n.js"

@customElement('rx-options-panel')
class RxOptionsPanel extends LitElement {
    @state() accessor domain
    @state() accessor enableModelMatch
    @state() accessor lockOnFirst
    @state() accessor modelId
    @state() accessor forceTlmOff

    createRenderRoot() {
        this.domain = elrsState.options.domain
        this.lockOnFirst = elrsState.options['lock-on-first-connection']
        this.enableModelMatch = elrsState.config.modelid!==undefined && elrsState.config.modelid !== 255
        this.modelId = elrsState.config.modelid===undefined ? 0 : elrsState.config.modelid
        this.forceTlmOff = elrsState.config['force-tlm']
        this.save = this.save.bind(this)
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.rx_options.title')}</div>
            <div class="mui-panel">
                <p>${unsafeHTML(i18n.t('elrs.common.override_note'))}</p>
                <form id='upload_options' method='POST' action="/options">
                    <!-- FEATURE:HAS_SUBGHZ -->
                    <div class="mui-select">
                        <select id="domain" @change="${(e) => this.domain = parseInt(e.target.value)}">
                            ${_renderOptions(['AU915','FCC915','EU868','IN866','AU433','EU433','US433','US433-Wide'], this.domain)}
                        </select>
                        <label for="domain">${i18n.t('elrs.rx_options.domain_label')}</label>
                    </div>
                    <!-- /FEATURE:HAS_SUBGHZ -->
                    <h2>${i18n.t('elrs.rx_options.lock_title')}</h2>
                    ${i18n.t('elrs.rx_options.lock_desc')}
                    <br/>
                    <div class="mui-checkbox">
                        <input id="lock" type='checkbox'
                               ?checked="${this.lockOnFirst}"
                               @change="${(e) => {this.lockOnFirst = e.target.checked}}"/>
                        <label for="lock">${i18n.t('elrs.rx_options.lock_checkbox')}</label>
                    </div>
                    <h2>${i18n.t('elrs.rx_options.model_match_title')}</h2>
                    ${i18n.t('elrs.rx_options.model_match_desc')}
                    <br/>
                    <div class="mui-checkbox">
                        <input id="modelMatch" type='checkbox'
                               ?checked="${this.enableModelMatch}"
                               @change="${(e) => {this.enableModelMatch = e.target.checked}}"/>
                        <label for="modelMatch">${i18n.t('elrs.rx_options.model_match_checkbox')}</label>
                    </div>
                    ${this.enableModelMatch ? html`
                    <div class="mui-textfield">
                        <input id="modelId" min="0" max="63" type='number' required
                               @change="${(e) => this.modelId = parseInt(e.target.value)}"
                               .value="${this.modelId}"
                               @keypress="${_uintInput}"/>
                        <label for="modelId">${i18n.t('elrs.rx_options.receiver_id')}</label>
                    </div>
                    ` : ''}
                    <h2>${i18n.t('elrs.rx_options.force_tlm_title')}</h2>
                    ${i18n.t('elrs.rx_options.force_tlm_desc')}
                    <br/>
                    <div class="mui-checkbox">
                        <input id='force-tlm' name='force-tlm' type='checkbox'
                               ?checked="${this.forceTlmOff}"
                               @change="${(e) => this.forceTlmOff = e.target.checked}"
                        />
                        <label for="force-tlm">${i18n.t('elrs.rx_options.force_tlm_checkbox')}</label>
                    </div>

                    <button class="mui-btn mui-btn--primary"
                            ?disabled="${!this.checkChanged()}"
                            @click="${this.save}"
                    >
                        ${i18n.t('elrs.common.save')}
                    </button>
                    ${elrsState.options.customised ? html`
                        <button class="mui-btn mui-btn--small mui-btn--danger mui--pull-right"
                                @click="${postWithFeedback(i18n.t('elrs.rx_options.reset_title'), i18n.t('elrs.rx_options.reset_error'), '/reset?options', null)}"
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
            options: {
                // FEATURE: HAS_SUBGHZ
                'domain': this.domain,
                // /FEATURE: HAS_SUBGHZ
                'lock-on-first-connection': this.lockOnFirst,
            },
            config: {
                'modelid': this.enableModelMatch ? this.modelId : 255,
                'force-tlm': this.forceTlmOff
            }
        }
        saveOptionsAndConfig(changes, () => {
            this.modelId = changes.config.modelid
            return this.requestUpdate()
        })
    }

    checkChanged() {
        let changed = false
        // FEATURE: HAS_SUBGHZ
        changed |= this.domain !== elrsState.options['domain']
        // /FEATURE: HAS_SUBGHZ
        changed |= this.lockOnFirst !== elrsState.options['lock-on-first-connection']
        changed |= this.enableModelMatch && this.modelId !== elrsState.config['modelid']
        changed |= !this.enableModelMatch && this.modelId !== 255
        changed |= this.forceTlmOff !== elrsState.config['force-tlm']
        return !!changed
    }
}