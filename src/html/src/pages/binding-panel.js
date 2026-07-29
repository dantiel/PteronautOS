import {html, LitElement} from "lit"
import {unsafeHTML} from "lit/directives/unsafe-html.js"
import {customElement, query, state} from "lit/decorators.js"
import {elrsState, saveConfig, saveOptions} from "../utils/state.js"
import {calcMD5} from "../utils/md5.js"
import {i18n} from "../utils/i18n.js"

@customElement('binding-panel')
class BindingPanel extends LitElement {
    @query('#phrase') accessor phrase

    @state() accessor uid = []
    @state() accessor bindType = 0
    @state() accessor uidData = {}

    originalUIDType = ''
    originalUID = []

    constructor() {
        super()
        this._submitOptions = this._submitOptions.bind(this)

        this.uid = elrsState.config.uid
        this.bindType = elrsState.config.vbind
        this.originalUID = elrsState.config.uid
        this.originalUIDType = (elrsState.settings && elrsState.settings.uidtype) ? elrsState.settings.uidtype : ''
        this._updateUIDType(this.originalUIDType)
    }

    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.binding.title')}</div>
            <div class="mui-panel">
                <form class="mui-form">
                    <!-- FEATURE:NOT IS_TX -->
                    <div class="mui-select">
                        <select @change="${(e) => {this.bindType = parseInt(e.target.value)}}" .value="${this.bindType}" >
                            <option value="0">${i18n.t('elrs.binding.storage_persistent')}</option>
                            <option value="1">${i18n.t('elrs.binding.storage_volatile')}</option>
                            <option value="2">${i18n.t('elrs.binding.storage_returnable')}</option>
                            <option value="3">${i18n.t('elrs.binding.storage_administered')}</option>
                        </select>
                        <label>${i18n.t('elrs.binding.storage_label')}</label>
                    </div>
                    <!-- /FEATURE:NOT IS_TX -->
                    ${this.bindType !== 1 ? html`
                        <div>${unsafeHTML(i18n.t('elrs.binding.phrase_help'))}<br/><br/>
                            <div class="mui-textfield">
                                <input type="text" id="phrase" placeholder="${i18n.t('elrs.binding.phrase_placeholder')}"
                                       @input="${this._updateBindingPhrase}"/>
                                <label for="phrase">${i18n.t('elrs.binding.phrase_label')}</label>
                            </div>
                        </div>
                        <div class="mui-textfield">
                            ${this.bindType !== 1 ? html`
                                <span class="badge" id="uid-type"
                                      style="background-color: ${this.uidData.bg}; color: ${this.uidData.fg}">${i18n.t(this.uidData.i18nKey)}</span>
                            ` : ''}
                            <input size='40' type='text' class='array' readonly
                                   value="${this.uid}"/>
                            <label>${i18n.t('elrs.binding.uid_label')}</label>
                        </div>
                    ` : ''}
                    <button class="mui-btn mui-btn--primary"
                            ?disabled=${!this.checkChanged()}
                            @click="${this._submitOptions}">${i18n.t('elrs.common.save')}
                    </button>
                </form>
            </div>
        `
    }

    _isValidUidByte(s) {
        let f = parseFloat(s)
        return !isNaN(f) && isFinite(s) && Number.isInteger(f) && f >= 0 && f < 256
    }

    _uidBytesFromText(text) {
        // If text is 4-6 numbers separated with [commas]/[spaces] use as a literal UID
        // This is a strict parser to not just extract numbers from text, but only accept if text is only UID bytes
        if (/^[0-9, ]+$/.test(text)) {
            let asArray = text.split(',').filter(this._isValidUidByte).map(Number)
            if (asArray.length >= 4 && asArray.length <= 6) {
                while (asArray.length < 6)
                    asArray.unshift(0)
                return asArray
            }
        }

        const bindingPhraseFull = `-DMY_BINDING_PHRASE="${text}"`
        const bindingPhraseHashed = calcMD5(bindingPhraseFull)
        return [...bindingPhraseHashed.subarray(0, 6)]
    }

    _updateBindingPhrase(e) {
        let text = e.target.value
        if (text.length === 0) {
            this.uid = this.originalUID
            this._updateUIDType(this.originalUIDType)
        } else {
            this.uid = this._uidBytesFromText(text.trim())
            this._updateUIDType('Modified')
        }
    }

    #UID_CONFIG = {
        // --- Specific Named Types ---
        'Flashed': { bg: '#1976D2', fg: 'white', desc: 'The binding UID was generated from a binding phrase set at flash time', i18nKey: 'elrs.binding.uidtype.flashed' },
        'Overridden': { bg: '#689F38', fg: 'black', desc: 'The binding UID has been generated from a binding phrase previously entered into the "binding phrase" field above', i18nKey: 'elrs.binding.uidtype.overridden' },
        'Modified': { bg: '#7c00d5', fg: 'white', desc: 'The binding UID has been modified, but not yet saved', i18nKey: 'elrs.binding.uidtype.modified' },
        'Volatile': { bg: '#FFA000', fg: 'white', desc: 'The binding UID will be cleared on boot', i18nKey: 'elrs.binding.uidtype.volatile' },
        'Loaned': { bg: '#FFA000', fg: 'white', desc: 'This receiver is on loan and can be returned using Lua or three-plug', i18nKey: 'elrs.binding.uidtype.loaned' },

        // --- Special Case (Fallback 1) ---
        'DEFAULT_NOT_SET': { bg: '#D50000', fg: 'white', uidtype: 'Not set', desc: 'Using autogenerated binding UID', i18nKey: 'elrs.binding.uidtype.not_set' },

        // --- RX Fallbacks (Fallback 2) ---
        'RX_NOT_BOUND': { bg: '#FFA000', fg: 'white', uidtype: 'Not bound', desc: 'This receiver is unbound and will boot to binding mode', i18nKey: 'elrs.binding.uidtype.not_bound' },
        'RX_BOUND': { bg: '#1976D2', fg: 'white', uidtype: 'Bound', desc: 'This receiver is bound and will boot waiting for connection', i18nKey: 'elrs.binding.uidtype.bound' }
    }

    _updateUIDType(uidtype) {
        let config

        if (!uidtype || uidtype.startsWith('Not set')) {
            config = this.#UID_CONFIG.DEFAULT_NOT_SET
        }
        else if (this.#UID_CONFIG[uidtype]) {
            config = this.#UID_CONFIG[uidtype]
        }
        else {
            const configKey = this.uid.toString().endsWith('0,0,0,0') ? 'RX_NOT_BOUND' : 'RX_BOUND'
            config = this.#UID_CONFIG[configKey]
        }

        this.uidData = {
            uidtype: config.uidtype || uidtype,
            bg: config.bg,
            fg: config.fg,
            desc: config.desc,
            i18nKey: config.i18nKey
        }
    }

    _submitOptions(e) {
        e.stopPropagation()
        e.preventDefault()

        // FEATURE:IS_TX
        let tx_changes = {
            customised: true,
            uid: this.uid
        }
        saveOptions(tx_changes, () => {
            this.originalUID = this.uid
            this.originalUIDType = 'Overridden'
            this.phrase.value = ''
            this._updateUIDType(this.originalUIDType)
            return this.requestUpdate()
        })
        // /FEATURE:IS_TX
        // FEATURE:NOT IS_TX
        const rx_changes =  {
            uid: this.uid,
            vbind: this.bindType
        }
        saveConfig(rx_changes, () => {
            if (this.bindType !== 1) {
                this.originalUID = this.uid
                this.originalUIDType = 'Overridden'
                this.phrase.value = ''
                this._updateUIDType(this.originalUIDType)
            }
            return this.requestUpdate()
        })
        // /FEATURE:NOT IS_TX
    }

    checkChanged() {
        return this.bindType !== elrsState.config.vbind || this.uidData.uidtype === 'Modified'
    }

}