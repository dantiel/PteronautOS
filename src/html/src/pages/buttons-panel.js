import {html, LitElement} from "lit"
import {customElement} from "lit/decorators.js"
import {elrsState, saveConfig} from "../utils/state.js"
import {_renderOptions} from "../utils/libs.js"
import {post} from "../utils/feedback.js"
import {i18n} from "../utils/i18n.js"

function getActionOptions() {
    return [
        i18n.t('elrs.buttons.action_0'),
        i18n.t('elrs.buttons.action_1'),
        i18n.t('elrs.buttons.action_2'),
        i18n.t('elrs.buttons.action_3'),
        i18n.t('elrs.buttons.action_4'),
        i18n.t('elrs.buttons.action_5'),
        i18n.t('elrs.buttons.action_6'),
        i18n.t('elrs.buttons.action_7'),
    ]
}
function getLongPressOptions() {
    return [
        i18n.t('elrs.buttons.longpress_0'),
        i18n.t('elrs.buttons.longpress_1'),
        i18n.t('elrs.buttons.longpress_2'),
        i18n.t('elrs.buttons.longpress_3'),
        i18n.t('elrs.buttons.longpress_4'),
        i18n.t('elrs.buttons.longpress_5'),
        i18n.t('elrs.buttons.longpress_6'),
        i18n.t('elrs.buttons.longpress_7'),
    ]
}
function getCountOptions() {
    return [
        i18n.t('elrs.buttons.count_0'),
        i18n.t('elrs.buttons.count_1'),
        i18n.t('elrs.buttons.count_2'),
        i18n.t('elrs.buttons.count_3'),
        i18n.t('elrs.buttons.count_4'),
        i18n.t('elrs.buttons.count_5'),
        i18n.t('elrs.buttons.count_6'),
        i18n.t('elrs.buttons.count_7'),
    ]
}

@customElement('buttons-panel')
class ButtonsPanel extends LitElement {

    colorTimer = undefined
    colorUpdated = false
    buttonActions = []
    loadedButtonActionsJson = '[]'
    currentButtonActionsJson = '[]'
    buttonActionsInitialized = false

    createRenderRoot() {
        this._timeoutCurrentColors = this._timeoutCurrentColors.bind(this)
        return this
    }

    render() {
        this._initializeButtonActions()
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.buttons.title')}</div>
            <div class="mui-panel">
                <p>${i18n.t('elrs.buttons.help')}</p>
                <form class="mui-form">
                    ${this.buttonActions.length ? html`
                        <table class="mui-table">
                            <tbody id="button-actions">
                            ${this.buttonActions.map((button, b) =>
                                    button.action.map((v, p) => this._appendButtonActionRow(b, p, v))
                            )}
                            </tbody>
                        </table>
                    ` : ``}
                    ${this._renderColorInput(0, i18n.t('elrs.buttons.color_btn1'))}
                    ${this._renderColorInput(1, i18n.t('elrs.buttons.color_btn2'))}
                    <button class="mui-btn mui-btn--primary" @click="${this._submitButtonActions}"
                            ?disabled=${this._isSaveDisabled()}>${i18n.t('elrs.common.save')}
                    </button>
                </form>
            </div>
        `
    }

    _renderColorInput(index, label) {
        const color = this.buttonActions[index]?.color
        if (color === undefined) return ''
        return html`
            <p>
                <input id="button${index + 1}-color" type="color" @input="${(e) => this._changeCurrentColors(e, index)}"
                       .value="${this._toRGB(color)}"/>
                <label for="button${index + 1}-color">${label}</label>
            </p>
        `
    }

    _appendButtonActionRow(b, p, v) {
        return html`
            <tr>
                <td>
                    ${i18n.t('elrs.buttons.btn_label', {n: b + 1})}
                </td>
                <td>
                    <div class="mui-select">
                        <select @change="${(e) => this._changeAction(b, p, parseInt(e.target.value))}">
                            ${_renderOptions(getActionOptions(), v.action)}
                        </select>
                        <label>${i18n.t('elrs.buttons.action_label')}</label>
                    </div>
                </td>
                <td>
                    <div class="mui-select">
                        <select @change="${(e) => this._changePress(b, p, e.target.value)}"
                                ?disabled=${v.action === 0}
                        >
                            <option value='' disabled hidden ?selected="${v.action === 0}"></option>
                            <option value='false' ?selected="${v['is-long-press'] === false}">${i18n.t('elrs.buttons.press_short')}</option>
                            <option value='true' ?selected="${v['is-long-press'] === true}">${i18n.t('elrs.buttons.press_long')}</option>
                        </select>
                        <label>${i18n.t('elrs.buttons.press_label')}</label>
                    </div>
                </td>
                <td>
                    <div class="mui-select">
                        <select @change="${(e) => this._changeCount(b, p, Number(e.target.value))}"
                                ?disabled=${v.action === 0}
                        >
                            <option value='' disabled hidden ?selected="${v.action === 0}"></option>
                            ${v['is-long-press'] === true
                                    ? _renderOptions(getLongPressOptions(), v.count)
                                    : _renderOptions(getCountOptions(), v.count)}
                        </select>
                        <label>${i18n.t('elrs.buttons.count_label')}</label>
                    </div>
                </td>
            </tr>
        `
    }

    _submitButtonActions(e) {
        e.preventDefault()
        saveConfig({'button-actions': this.buttonActions}, () => {
            this._refreshButtonActionsJson()
            this.loadedButtonActionsJson = this.currentButtonActionsJson
            this.requestUpdate()
        })
    }

    _toRGB(c) {
        let r = c & 0xE0
        r = ((r << 16) + (r << 13) + (r << 10)) & 0xFF0000
        let g = c & 0x1C
        g = ((g << 11) + (g << 8) + (g << 5)) & 0xFF00
        let b = ((c & 0x3) << 1) + ((c & 0x3) >> 1)
        b = (b << 5) + (b << 2) + (b >> 1)
        return '#' + (r + g + b).toString(16).padStart(6, '0')
    }

    _to8bit(v) {
        v = parseInt(v.substring(1), 16)
        return ((v >> 16) & 0xE0) + ((v >> (8 + 3)) & 0x1C) + ((v >> 6) & 0x3)
    }

    _changeCurrentColors(e, index) {
        this.buttonActions[index].color = this._to8bit(e.target.value)
        if (this.colorTimer === undefined) {
            this._sendCurrentColors()
            this.colorTimer = setInterval(this._timeoutCurrentColors, 50)
        } else {
            this.colorUpdated = true
        }
    }

    _sendCurrentColors() {
        let colors = [this.buttonActions[0].color]
        if (this.buttonActions[1] && this.buttonActions[1].color !== undefined) colors.push(this.buttonActions[1].color)
        post('/buttons', colors, {
            onload: () => {
                this._refreshButtonActionsJson()
                this.requestUpdate()
            }
        })
        this.colorUpdated = false
    }

    _timeoutCurrentColors() {
        if (this.colorUpdated) {
            this._sendCurrentColors()
        } else {
            clearInterval(this.colorTimer)
            this.colorTimer = undefined
        }
    }

    _isSaveDisabled() {
        return this.currentButtonActionsJson === this.loadedButtonActionsJson
    }

    checkChanged() {
        return !this._isSaveDisabled()
    }

    _initializeButtonActions() {
        if (this.buttonActionsInitialized || !elrsState.config['button-actions']) {
            return
        }
        this.buttonActions = this._cloneButtonActions(elrsState.config['button-actions'] ?? [])
        this._refreshButtonActionsJson()
        this.loadedButtonActionsJson = this.currentButtonActionsJson
        this.buttonActionsInitialized = true
    }

    _cloneButtonActions(buttonActions) {
        return JSON.parse(JSON.stringify(buttonActions))
    }

    _refreshButtonActionsJson() {
        this.currentButtonActionsJson = JSON.stringify(this.buttonActions)
    }

    _changeAction(b, p, value) {
        const actionConfig = this.buttonActions[b].action[p]
        actionConfig.action = value
        if (value === 0) {
            actionConfig['is-long-press'] = undefined
            actionConfig.count = undefined
        } else {
            if (actionConfig['is-long-press'] !== true && actionConfig['is-long-press'] !== false) {
                actionConfig['is-long-press'] = false; // default to short press
            }
            if (!Number.isInteger(actionConfig.count)) {
                actionConfig.count = 0; // default to first count option
            }
        }
        this._refreshButtonActionsJson()
        this.requestUpdate()
    }

    _changePress(b, p, value) {
        this.buttonActions[b].action[p]['is-long-press'] = (value === 'true')
        this._refreshButtonActionsJson()
        this.requestUpdate()
    }

    _changeCount(b, p, value) {
        this.buttonActions[b].action[p].count = value
        this._refreshButtonActionsJson()
        this.requestUpdate()
    }
}