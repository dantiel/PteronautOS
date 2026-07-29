import {html, LitElement, nothing} from 'lit'
import {unsafeHTML} from 'lit/directives/unsafe-html.js'
import {customElement, state} from 'lit/decorators.js'
import {loadJSON, postWithFeedback, saveJSONWithReboot} from '../utils/feedback.js'
import '../components/filedrag.js'
import HARDWARE_SCHEMA from '../utils/hardware-schema.js'
import {_arrayInput, _floatInput, _intInput, _uintInput} from "../utils/libs.js"
import {i18n} from "../utils/i18n.js"

@customElement('hardware-layout')
export class HardwareLayout extends LitElement {

    @state() accessor customised = false
    @state() accessor loadedHardwareJson = null
    @state() accessor currentHardwareJson = '{}'
    loadPromise = null

    createRenderRoot() {
        this._onFormEdited = this._onFormEdited.bind(this)
        return this
    }

    render() {
        return html`
            <div class="hardware-layout">
                <div class="mui-panel mui--text-title">${i18n.t('elrs.hardware.title')}</div>
                <div class="mui-panel">
                    ${i18n.t('elrs.hardware.upload_hint')}
                    <p>
                    <file-drop id="filedrag" label="${i18n.t('elrs.hardware.upload_btn')}" @file-drop=${this._onFileDrop}>${i18n.t('elrs.hardware.drop_text')}</file-drop>
                </div>
                <div class="mui-panel">
                    <div class="mui-panel warning-bg hardware-customised-warning" ?hidden="${!this.customised}">
                        ${unsafeHTML(i18n.t('elrs.hardware.customised_warning'))}
                    </div>
                    <form id="upload_hardware" class="mui-form"
                          @input=${this._onFormEdited}
                          @change=${this._onFormEdited}>
                        ${this._renderTable()}
                        <br>
                        <input type="button" name="_ignore" value="${i18n.t('elrs.hardware.save_btn')}"
                               class="mui-btn mui-btn--primary" @click=${this._submitConfig}
                               ?disabled=${this._isSaveDisabled()} />
                    </form>
                </div>
            </div>
        `
    }

    _renderTable() {
        return html`
            <table>
                <tbody>
                ${this.constructor.SCHEMA.map(section => html`
                    <tr>
                        <td colspan="4"><b>${section.title}</b></td>
                    </tr>
                    ${section.rows.map(row => html`
                        <tr>
                            <td width="30"></td>
                            <td>${row.label}${this._renderIcon(row.icon)}</td>
                            <td>${this._renderField(row)}</td>
                            <td>${row.desc || ''}</td>
                        </tr>
                    `)}
                `)}
                </tbody>
            </table>
        `
    }

    _renderIcon(icon) {
        if (!icon) return html``
        if (icon === 'input-output') {
            return html`<img class="icon-input"/><img class="icon-output"/>`
        }
        return html`<img class="icon-${icon}"/>`
    }

    _renderField(row) {
        switch (row.type) {
            case 'checkbox':
                return html`<input id="${row.id}" name="${row.id}" type="checkbox"/>`
            case 'select':
                return html`<select id="${row.id}" name="${row.id}">
                    ${row.options?.map(opt => html`
                        <option value="${opt.value}">${opt.label}</option>`)}
                </select>`
            case 'float':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 10} maxlength=${row.size ?? 10} type="text" @keypress="${_floatInput}"/>`
            case 'int':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 3} maxlength=${row.size ?? 3} type="text" @keypress="${_intInput}"/>`
            case 'uint':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 3} maxlength=${row.size ?? 3} type="text" @keypress="${_uintInput}"/>`
            case 'array':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? nothing} maxlength=${row.size ?? nothing} type="text" class="array"  @keypress="${_arrayInput}"/>`
        }
    }

    connectedCallback() {
        super.connectedCallback()
        // Add tooltips to icon classes after first paint
        setTimeout(() => this._initTooltips(), 0)
        this.pageReady()
    }

    pageReady() {
        if (!this.loadPromise) {
            this.loadPromise = this.updateComplete
                .then(() => loadJSON('/hardware.json', i18n.t('elrs.hardware.load_error')))
                .then((data) => {
                    this.customised = !!data.customised
                    this._updateHardwareSettings(data)
                    this.loadedHardwareJson = this.currentHardwareJson
                }, (error) => {
                    this.loadPromise = null
                    throw error
                })
        }
        return this.loadPromise
    }

    _field(id) {
        return document.getElementById(id)
    }

    _initTooltips() {
        const add = (cls, label) => {
            const images = this.querySelectorAll('.' + cls)
            images.forEach(i => i.setAttribute('title', label))
        }
        add('icon-input', i18n.t('elrs.hardware.digital_input'))
        add('icon-output', i18n.t('elrs.hardware.digital_output'))
        add('icon-analog', i18n.t('elrs.hardware.analog_input'))
        add('icon-pwm', i18n.t('elrs.hardware.pwm_output'))
    }

    _onFileDrop(e) {
        const files = e.detail.files
        const form = this._field('upload_hardware')
        if (form) form.reset()
        for (const file of files) {
            const reader = new FileReader()
            reader.onload = (ev) => {
                const data = JSON.parse(ev.target.result)
                this._updateHardwareSettings(data)
            }
            reader.readAsText(file)
        }
    }

    _updateHardwareSettings(data) {
        for (const [key, value] of Object.entries(data)) {
            const el = this._field(key)
            if (el) {
                if (el.type === 'checkbox') {
                    el.checked = !!value
                } else {
                    if (Array.isArray(value)) el.value = value.toString()
                    else el.value = value
                }
            }
        }
        this.currentHardwareJson = this._serializeCurrentConfig()
    }

    _submitConfig() {
        const body = this.currentHardwareJson
        // Use shared helper that prompts for reboot on success
        saveJSONWithReboot(i18n.t('elrs.common.update_succeeded'), i18n.t('elrs.common.update_failed'), '/hardware.json', {...JSON.parse(body), "customised": true}, () => {
            this.loadedHardwareJson = body
        })
        return false
    }

    _onFormEdited() {
        this.currentHardwareJson = this._serializeCurrentConfig()
    }

    _serializeCurrentConfig() {
        const form = this._field('upload_hardware')
        if (!form) return '{}'
        const formData = new FormData(form)
        return JSON.stringify(Object.fromEntries(formData), (k, v) => {
            if (v === '') return undefined
            const el = this._field(k)
            if (el && el.type === 'checkbox') {
                return v === 'on'
            }
            if (el && el.classList.contains('array')) {
                const arr = v.split(',').map((element) => Number(element))
                return arr.length === 0 ? undefined : arr
            }
            return isNaN(v) ? v : +v
        })
    }

    _isSaveDisabled() {
        if (this.loadedHardwareJson === null) return true
        return this.currentHardwareJson === this.loadedHardwareJson
    }

    checkChanged() {
        return !this._isSaveDisabled()
    }
}

HardwareLayout.SCHEMA = HARDWARE_SCHEMA