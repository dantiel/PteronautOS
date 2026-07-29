import {html, LitElement} from "lit"
import {customElement} from "lit/decorators.js"
import {elrsState, formatBand, formatWifiRssi} from "../utils/state.js"
import {SERIAL_OPTIONS1} from '../utils/globals.js'
import {i18n} from "../utils/i18n.js"

@customElement('info-panel')
class InfoPanel extends LitElement {
    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.info.title')}</div>
            <div class="mui-panel">
                <table class="mui-table mui-table--bordered">
                    <tbody>
                    <tr><td><b>${i18n.t('elrs.info.product')}</b></td><td>${elrsState.settings.product_name}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.lua_name')}</b></td><td>${elrsState.settings.lua_name}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.version')}</b></td><td>${elrsState.settings.version}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.git_hash')}</b></td><td>${elrsState.settings['git-commit']}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.device_type')}</b></td><td>${elrsState.settings['module-type']}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.firmware')}</b></td><td>${elrsState.settings.target}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.radio')}</b></td><td>${elrsState.settings['radio-type']}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.domain')}</b></td><td>${formatBand()}</td></tr>
                    <!-- FEATURE:PTERONAUTOS -->
                    <tr><td><b>Ornithopter Mode</b></td><td style="color:#d4a017;">Active</td></tr>
                    <tr><td><b>Zephyrus Gyro</b></td><td>${elrsState.settings?.zephyrus_attached !== false ? 'Connected (MPU6050)' : 'Not Detected'}</td></tr>
                    <!-- /FEATURE:PTERONAUTOS -->
                    <tr><td><b>${i18n.t('elrs.info.binding_uid')}</b></td><td>${elrsState.config.uid.toString()}</td></tr>
                    <tr><td><b>${i18n.t('elrs.info.wifi_state')}</b></td><td>${formatWifiRssi()}</td></tr>
                    </tbody>
                </table>
            </div>
            ${this._hasCustomSettings() ? html`
                <div class="mui-panel">
                    <div class="mui--text-title">${i18n.t('elrs.info.custom_settings_title')}</div>
                    <br>
                    <table class="mui-table mui-table--bordered">
                        <tbody>
                        ${elrsState.options['is-airport'] ?
                                html`<tr><td><b>${i18n.t('elrs.info.airport_mode')}</b></td><td>${i18n.t('elrs.info.enabled')}</td></tr>`
                                : ''}
                        ${elrsState.options['wifi-on-interval'] !== 60 ?
                                html`<tr><td><b>${i18n.t('elrs.info.wifi_auto_interval')}</b></td><td>${elrsState.options['wifi-on-interval']}</td></tr>`
                                : ''}
                        <!-- FEATURE: NOT IS_TX -->
                        ${elrsState.options['lock-on-first-connection'] !== true ?
                                html`<tr><td><b>${i18n.t('elrs.info.lock_on_first')}</b></td><td>${i18n.t('elrs.info.false')}</td></tr>`
                                : ''}
                        ${elrsState.config.modelid !== 255 ?
                                html`<tr><td><b>${i18n.t('elrs.info.model_match')}</b></td><td>${i18n.t('elrs.info.model_match_enabled', {id: elrsState.config.modelid})}</td></tr>`
                                : ''}
                        ${elrsState.config.vbind !== 0 ?
                                html`<tr><td><b>${i18n.t('elrs.info.binding_storage')}</b></td><td>${elrsState.config.vbind === 1 ? 'Volatile' : elrsState.config.vbind === 2 ? 'Returnable' : 'Administered'}</td></tr>`
                                : ''}
                        ${elrsState.config['force-tlm'] !== false ?
                                html`<tr><td><b>${i18n.t('elrs.info.force_tlm_off')}</b></td><td>${i18n.t('elrs.info.enabled')}</td></tr>`
                                : ''}
                        ${elrsState.config['pwm'] === undefined && elrsState.config['serial-protocol'] !== 0 ?
                                html`<tr><td><b>${i18n.t('elrs.info.serial_protocol')}</b></td><td>${SERIAL_OPTIONS1[elrsState.config['serial-protocol']]}</td></tr>`
                                : ''}
                        ${elrsState.config['pwm'] === undefined && elrsState.options['rcvr-uart-baud'] !== 420000 ?
                                html`<tr><td><b>${i18n.t('elrs.info.baud_rate')}</b></td><td>${elrsState.options['rcvr-uart-baud']}</td></tr>`
                                : ''}
                        <!-- /FEATURE: NOT IS_TX -->
                        <!-- FEATURE: IS_TX -->
                        ${elrsState.options['tlm-interval'] !== 240 ?
                                html`<tr><td><b>${i18n.t('elrs.info.tlm_interval')}</b></td><td>${elrsState.options['tlm-interval']}</td></tr>`
                                : ''}
                        <!-- /FEATURE: IS_TX -->
                        ${elrsState.settings?.custom_hardware ?
                                html`<tr><td><b>${i18n.t('elrs.info.custom_hardware')}</b></td><td>${i18n.t('elrs.info.true')}</td></tr>`
                                : ''}

                        </tbody>
                    </table>
                </div>
                `:
                ''
            }
        `
    }

    _hasCustomSettings() {
        let custom = false
        // customised hardware settings
        custom = elrsState.options['is-airport'] || elrsState.options['wifi-on-interval'] !== 60

        // FEATURE: NOT IS_TX
        custom |= elrsState.config['pwm'] === undefined && elrsState.config['serial-protocol'] !== 0
        custom |= elrsState.config['pwm'] === undefined && elrsState.options['rcvr-uart-baud'] !== 420000
        custom |= elrsState.options['lock-on-first-connection'] !== true ||
            elrsState.config.modelid !== 255 ||
            elrsState.config.vbind !== 0 ||
            elrsState.config['force-tlm'] !== false
        // /FEATURE: NOT IS_TX

        // FEATURE: IS_TX
        custom |= elrsState.options['tlm-interval'] !== 240
        // /FEATURE: IS_TX
        return custom
    }
}