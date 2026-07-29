import {html, LitElement} from "lit"
import {unsafeHTML} from "lit/directives/unsafe-html.js"
import {customElement} from "lit/decorators.js"
import {elrsState, saveConfig} from "../utils/state.js"
import {_} from "../utils/libs.js"
import {postWithFeedback} from "../utils/feedback.js"
import {i18n} from "../utils/i18n.js"

export const PWM_MODE_SERIAL = 10
export const PWM_MODE_SERIAL2RX = 14
export const PWM_MODE_SERIAL2TX = 15

@customElement('connections-panel')
class ConnectionsPanel extends LitElement {
    pinModes = []
    pinRxIndex = undefined
    pinTxIndex = undefined

    createRenderRoot() {
        this._onFormEdited = this._onFormEdited.bind(this)
        return this
    }

    render() {
        return html`
            <style>
                .connections-panel-root { container-type: inline-size; }

                .connections-panel-root .connections-mobile-warning { display: none; }

                /* If the container is too narrow in terms of characters, show the warning and hide the table */
                @container (max-width: 80ch) {
                    .connections-panel-root .connections-mobile-warning { display: block; }
                    .connections-panel-root .connections-panel { display: none !important; }
                }

                /* Fallback for browsers without container queries: use an em-based viewport rule */
                @supports not (container-type: inline-size) {
                    @media (max-width: 48em) {
                        .connections-panel-root .connections-mobile-warning { display: block; }
                        .connections-panel-root .connections-panel { display: none !important; }
                    }
                }
            </style>
            <div class="connections-panel-root">
                <div class="mui-panel mui--text-title">${i18n.t('elrs.connections.title')}</div>
                <div class="mui-panel warning-bg connections-mobile-warning">
                    <div class="mui--text-title connections-mobile-title">${i18n.t('elrs.connections.mobile_warning_title')}</div>
                    <p>${i18n.t('elrs.connections.mobile_warning_text')}</p>
                </div>
                <div class="mui-panel connections-panel">
                    ${i18n.t('elrs.connections.subtitle')}
                    <form @input=${this._onFormEdited} @change=${this._onFormEdited}>
                        <div class="mui-panel pwmpnl">
                            <table class="pwmtbl mui-table">
                                <thead>
                                <tr>
                                    <th class="fixed-column">${i18n.t('elrs.connections.th_output')}</th><th class="mui--text-center fixed-column">${i18n.t('elrs.connections.th_features')}</th><th>${i18n.t('elrs.connections.th_mode')}</th><th>${i18n.t('elrs.connections.th_input')}</th><th class="mui--text-center fixed-column">${i18n.t('elrs.connections.th_invert')}</th><th class="mui--text-center fixed-column">${i18n.t('elrs.connections.th_stretch')}</th><th class="mui--text-center fixed-column pwmitm">${i18n.t('elrs.connections.th_failsafe_mode')}</th><th class="mui--text-center fixed-column pwmitm">${i18n.t('elrs.connections.th_failsafe_pos')}</th>
                                </tr>
                                </thead>
                                <tbody>
                                    ${this._renderConnectionPins()}
                                </tbody>
                            </table>
                        </div>
                        <div>
                            <button class="mui-btn mui-btn--small mui-btn--primary"
                                    ?disabled=${!this.checkChanged()}
                                    @click="${this._savePwmConfig}">${i18n.t('elrs.common.save')}</button>
                            ${elrsState.options.customised ? html`
                                <button class="mui-btn mui-btn--small mui-btn--danger mui--pull-right"
                                        @click="${postWithFeedback(i18n.t('elrs.connections.reset_pwm_title'), i18n.t('elrs.connections.reset_pwm_error'), '/reset?config', null)}"
                                >
                                    ${i18n.t('elrs.common.reset_to_defaults')}
                                </button>
                            ` : ''}
                        </div>
                    </form>
                    <div class="mui-divider"></div>
                    <ul>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_output'))}</li>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_features'))}</li>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_mode'))}
                            <ul>
                                <li>${unsafeHTML(i18n.t('elrs.connections.legend_mode_serial_note'))}</li>
                            </ul>
                        </li>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_input'))}</li>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_invert'))}</li>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_stretch'))}</li>
                        <li>${unsafeHTML(i18n.t('elrs.connections.legend_failsafe'))}
                            <ul>
                                <li>${i18n.t('elrs.connections.legend_fs_set_pos')}
                                    <ul>
                                        <li>${i18n.t('elrs.connections.legend_fs_set_pos_note1')}</li>
                                        <li>${i18n.t('elrs.connections.legend_fs_set_pos_note2')}</li>
                                    </ul>
                                </li>
                                <li>${i18n.t('elrs.connections.legend_fs_no_pulses')}
                                    <ul>
                                        <li>${i18n.t('elrs.connections.legend_fs_no_pulses_note1')}</li>
                                        <li>${i18n.t('elrs.connections.legend_fs_no_pulses_note2')}</li>
                                    </ul>
                                </li>
                                <li>${i18n.t('elrs.connections.legend_fs_last_pos')}</li>
                            </ul>
                        </li>
                    </ul>
                </div>
            </div>
        `
    }

    firstUpdated() {
        elrsState.config.pwm.forEach((item, index) => {
            const modeField = _(`pwm_${index}_mode`)
            this._pinModeChange(modeField, index)
            const failsafeModeField = _(`pwm_${index}_fsmode`)
            this._failsafeModeChange(failsafeModeField, index)
        })
    }

    _enumSelectGenerate(id, val, arOptions, onchange) {
        return html`
            <div class="mui-select compact">
                <select id="${id}" class="pwmitm" @change="${onchange}">
                    ${arOptions.map((item, idx) => {
                        if (item) {
                            return html`<option value="${idx}" ?selected=${idx === val}>${item}</option>`
                        }
                        return null
                    })}
                </select>
            </div>
        `
    }

    _generateFeatureBadges(features) {
        let str = []
        if (!!(features & 1)) str.push(html`<span style="color: #696969; background-color: #a8dcfa" class="badge">TX</span>`)
        else if (!!(features & 2)) str.push(html`<span style="color: #696969; background-color: #d2faa8" class="badge">RX</span>`)
        if ((features & 12) === 12) str.push(html`<span style="color: #696969; background-color: #fab4a8" class="badge">I2C</span>`)
        else if (!!(features & 4)) str.push(html`<span style="color: #696969; background-color: #fab4a8" class="badge">SCL</span>`)
        else if (!!(features & 8)) str.push(html`<span style="color: #696969; background-color: #fab4a8" class="badge">SDA</span>`)

        // Serial2
        if ((features & 96) === 96) str.push(html`<span style="color: #696969; background-color: #36b5ff" class="badge">Serial2</span>`)
        else if (!!(features & 32)) str.push(html`<span style="color: #696969; background-color: #36b5ff" class="badge">RX2</span>`)
        else if (!!(features & 64)) str.push(html`<span style="color: #696969; background-color: #36b5ff" class="badge">TX2</span>`)

        return str
    }

    _renderConnectionPins() {
        this.pinRxIndex = undefined
        this.pinTxIndex = undefined
        this.pinModes = []
        const htmlFields = []
        elrsState.config.pwm.forEach((item, index) => {
            const failsafe = (item.config & 2047) + 476; // 11 bits
            const ch = (item.config >> 11) & 15; // 4 bits
            const inv = (item.config >> 15) & 1
            const mode = (item.config >> 16) & 15; // 4 bits
            const stretch = (item.config >> 20) & 1
            const failsafeMode = (item.config >> 22) & 3; // 2 bits
            const features = item.features
            const modes = [
                i18n.t('elrs.connections.mode_50hz'),
                i18n.t('elrs.connections.mode_60hz'),
                i18n.t('elrs.connections.mode_100hz'),
                i18n.t('elrs.connections.mode_160hz'),
                i18n.t('elrs.connections.mode_333hz'),
                i18n.t('elrs.connections.mode_400hz'),
                i18n.t('elrs.connections.mode_10khz_duty'),
                i18n.t('elrs.connections.mode_onoff'),
            ]
            if (features & 16) {
                modes.push(i18n.t('elrs.connections.mode_dshot'), i18n.t('elrs.connections.mode_dshot3d'))
            } else {
                modes.push(undefined, undefined)
            }
            if (features & 1) {
                this.pinRxIndex = index
                modes.push(i18n.t('elrs.connections.mode_serial_tx'))
            } else if (features & 2) {
                this.pinTxIndex = index
                modes.push(i18n.t('elrs.connections.mode_serial_rx'))
            } else {
                modes.push(undefined)
            }
            modes.push(features & 4 ? i18n.t('elrs.connections.mode_i2c_scl') : undefined)
            modes.push(features & 8 ? i18n.t('elrs.connections.mode_i2c_sda') : undefined)
            modes.push(undefined)  // true PWM (not yet supported)
            modes.push(features & 32 ? i18n.t('elrs.connections.mode_serial2_rx') : undefined)
            modes.push(features & 64 ? i18n.t('elrs.connections.mode_serial2_tx') : undefined)
            const selectedMode = modes[mode] ? mode : 0

            htmlFields.push(html`
                <tr><td class="mui--text-center mui--text-title">${index + 1}</td>
                <td>${this._generateFeatureBadges(features)}</td>
                <td>${this._enumSelectGenerate(`pwm_${index}_mode`, selectedMode, modes, (e) => {this._pinModeChange(e.target, index)})}</td>
                <td>${this._enumSelectGenerate(`pwm_${index}_ch`, ch,
                        ['ch1', 'ch2', 'ch3', 'ch4',
                            'ch5 (AUX1)', 'ch6 (AUX2)', 'ch7 (AUX3)', 'ch8 (AUX4)',
                            'ch9 (AUX5)', 'ch10 (AUX6)', 'ch11 (AUX7)', 'ch12 (AUX8)',
                            'ch13 (AUX9)', 'ch14 (AUX10)', 'ch15 (AUX11)', 'ch16 (AUX12)'])}</td>
                <td><div class="mui-checkbox mui--text-center"><input type="checkbox" id="pwm_${index}_inv" ?checked="${inv}"></div></td>
                <td><div class="mui-checkbox mui--text-center"><input type="checkbox" id="pwm_${index}_stretch" ?checked="${stretch}"></div></td>
                <td>${this._enumSelectGenerate(`pwm_${index}_fsmode`, failsafeMode, [i18n.t('elrs.connections.fs_set_position'), i18n.t('elrs.connections.fs_no_pulses'), i18n.t('elrs.connections.fs_last_position')],
                        (e) => {this._failsafeModeChange(e.target, index)})}</td>
                <td><div class="mui-textfield compact"><input id="pwm_${index}_fs" value="${failsafe}" size="6" class="pwmitm" /></div></td></tr>
            `)
            this.pinModes[index] = selectedMode
        });
        return htmlFields
    }

    _pinModeChange(pinMode, index) {
        const setDisabled = (index, onoff) => {
            _(`pwm_${index}_ch`).disabled = onoff
            _(`pwm_${index}_inv`).disabled = onoff
            _(`pwm_${index}_stretch`).disabled = onoff
            _(`pwm_${index}_fs`).disabled = onoff
            _(`pwm_${index}_fsmode`).disabled = onoff
        }

        // disable extra fields for serial & i2c pins
        setDisabled(index, Number.parseInt(pinMode.value) >= PWM_MODE_SERIAL)

        const updateOthers = (value, enable) => {
            if (value > PWM_MODE_SERIAL) { // disable others
                elrsState.config.pwm.forEach((item, other) => {
                    if (other !== index) {
                        document.querySelectorAll(`#pwm_${other}_mode option`).forEach(opt => {
                            if (opt.value === value) {
                                opt.disabled = enable
                            }
                        })
                    }
                })
            }
        }
        updateOthers(pinMode.value, true) // disable others
        updateOthers(this.pinModes[index], false) // enable others
        this.pinModes[index] = pinMode.value

        // put some constraints on pinRx/Tx mode selects
        if (this.pinRxIndex !== undefined && this.pinTxIndex !== undefined) {
            const pinRxMode = _(`pwm_${this.pinRxIndex}_mode`)
            const pinTxMode = _(`pwm_${this.pinTxIndex}_mode`)
            const pinRxModeValue = Number.parseInt(pinRxMode.value)
            const pinTxModeValue = Number.parseInt(pinTxMode.value)
            if (index === this.pinRxIndex) {
                if (pinRxModeValue === PWM_MODE_SERIAL) { // Serial
                    pinTxMode.value = PWM_MODE_SERIAL
                    setDisabled(this.pinRxIndex, true)
                    setDisabled(this.pinTxIndex, true)
                    pinTxMode.disabled = true
                }
                else if (pinTxModeValue === PWM_MODE_SERIAL) {
                    pinTxMode.value = 0
                    setDisabled(this.pinRxIndex, false)
                    setDisabled(this.pinTxIndex, false)
                    pinTxMode.disabled = false
                }
            }
            if (index === this.pinTxIndex) {
                if (pinTxModeValue === PWM_MODE_SERIAL) { // Serial
                    pinRxMode.value = PWM_MODE_SERIAL
                    setDisabled(this.pinRxIndex, true)
                    setDisabled(this.pinTxIndex, true)
                    pinTxMode.disabled = true
                }
            }
            const pinTx = pinTxMode.value
            if (pinRxModeValue !== PWM_MODE_SERIAL) pinTxMode.value = pinTx
        }

    }

    _failsafeModeChange(failsafeMode, index) {
        const mode = _(`pwm_${index}_mode`).value
        if (mode < PWM_MODE_SERIAL) {
            const failsafeField = _(`pwm_${index}_fs`)
            if (failsafeMode.value === '0') {
                failsafeField.disabled = false
                failsafeField.style.display = 'block'
            } else {
                failsafeField.disabled = true
                failsafeField.style.display = 'none'
            }
        }
    }

    _onFormEdited() {
        this.requestUpdate()
    }

    _getPwmFormData(normalizeFields = false) {
        let ch = 0
        let inField
        const outData = []
        while (inField = _(`pwm_${ch}_ch`)) {
            const inChannel = inField.value
            const mode = _(`pwm_${ch}_mode`).value
            const invert = _(`pwm_${ch}_inv`).checked ? 1 : 0
            const stretch = _(`pwm_${ch}_stretch`).checked ? 1 : 0
            const failsafeField = _(`pwm_${ch}_fs`)
            const failsafeModeField = _(`pwm_${ch}_fsmode`)
            let failsafe = failsafeField.value
            if (failsafe > 2523) failsafe = 2523
            if (failsafe < 476) failsafe = 476
            if (normalizeFields) failsafeField.value = failsafe
            let failsafeMode = failsafeModeField.value

            const raw = (failsafeMode << 22) | (stretch << 20) | (mode << 16) | (invert << 15) | (inChannel << 11) | (failsafe - 476)
            // console.log(`PWM ${ch} mode=${mode} input=${inChannel} fs=${failsafe} fsmode=${failsafeMode} inv=${invert} stretch=${stretch} raw=${raw}`)
            outData.push(raw)
            ++ch
        }
        return outData
    }

    _savePwmConfig(e) {
        e.preventDefault()
        const data = this._getPwmFormData(true)
        saveConfig({'pwm': data}, () => this.requestUpdate())
    }

    checkChanged() {
        const data = this._getPwmFormData()
        for (let i = 0; i < data.length; i++) {
            if (elrsState.config.pwm[i].config !== data[i]) {
                return true
            }
        }
        return false
    }

}