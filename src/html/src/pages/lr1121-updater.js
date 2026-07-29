import {html, LitElement} from 'lit'
import {customElement, query, state} from 'lit/decorators.js'
import '../components/filedrag.js'
import {loadJSON, post, postWithFeedback, showAlert} from "../utils/feedback.js"
import {overlay} from '../utils/overlay.js'
import {i18n} from "../utils/i18n.js"

@customElement('lr1121-updater')
export class LR1121Updater extends LitElement {
    @query('#radio2') accessor radio2

    @state() accessor data = undefined
    @state() accessor status = ''
    @state() accessor progress = 0
    @state() accessor manual = false
    loadPromise = null

    createRenderRoot() {
        this._progressHandler = this._progressHandler.bind(this)
        this._completeHandler = this._completeHandler.bind(this)
        this._errorHandler = this._errorHandler.bind(this)
        this._abortHandler = this._abortHandler.bind(this)
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.lr1121.title')}</div>
            <div class="mui-panel" style="display: ${this.manual ? 'block' : 'none'}; background-color: #FFC107;">
                ${i18n.t('elrs.lr1121.manual_warning')}<br>
                <button class="mui-btn mui-btn--small" @click=${this._reset}>${i18n.t('elrs.lr1121.reset_btn')}</button>
            </div>
            <div class="mui-panel">
                ${this._renderRadios()}
                <label>${i18n.t('elrs.lr1121.upload_label')}</label>
                <br>
                <file-drop label="${i18n.t('elrs.lr1121.upload_btn')}" @file-drop=${this._fileSelected}>${i18n.t('elrs.lr1121.drop_text')}</file-drop>
                <br/>
                <h3>${this.status}</h3>
                <progress .value="${this.progress}" max="100" style="width:100%;"></progress>
            </div>
            <div class="mui-panel">
                ${this._renderInfoTable()}
            </div>
        `
    }

    _renderRadios() {
        if (!this.data?.radio2) return html``
        return html`
            <div class="mui-radio">
                <input id="radio1" type="radio" name="optionsRadio" value="1" checked>
                <label for="radio1">${i18n.t('elrs.lr1121.update_radio1')}</label>
            </div>
            <div class="mui-radio">
                <input id="radio2" type="radio" name="optionsRadio" value="2">
                <label for="radio2">${i18n.t('elrs.lr1121.update_radio2')}</label>
            </div>
        `
    }

    _renderInfoTable() {
        if (!this.data) return html``
        const r1 = this.data.radio1
        const r2 = this.data.radio2
        return html`
            <table class="mui-table mui-table--bordered">
                <thead>
                <tr>
                    <th>${i18n.t('elrs.lr1121.th_parameter')}</th>
                    <th>${i18n.t('elrs.lr1121.th_radio1')}</th>
                    <th>${i18n.t('elrs.lr1121.th_radio2')}</th>
                </tr>
                </thead>
                <tbody>
                <tr>
                    <td>${i18n.t('elrs.lr1121.type')}</td>
                    <td><span>${this._dec2hex(r1?.type, 2)}</span></td>
                    <td><span>${r2 ? this._dec2hex(r2.type, 2) : ''}</span></td>
                </tr>
                <tr>
                    <td>${i18n.t('elrs.lr1121.hardware')}</td>
                    <td><span>${this._dec2hex(r1?.hardware, 2)}</span></td>
                    <td><span>${r2 ? this._dec2hex(r2.hardware, 2) : ''}</span></td>
                </tr>
                <tr>
                    <td>${i18n.t('elrs.lr1121.firmware')}</td>
                    <td><span>${this._dec2hex(r1?.firmware, 4)}</span></td>
                    <td><span>${r2 ? this._dec2hex(r2.firmware, 4) : ''}</span></td>
                </tr>
                </tbody>
            </table>
        `
    }

    connectedCallback() {
        super.connectedCallback()
        this.pageReady()
    }

    pageReady() {
        if (!this.loadPromise) {
            this.loadPromise = loadJSON('/lr1121.json', i18n.t('elrs.lr1121.load_error'))
                .then((data) => {
                    this.data = data
                    this.manual = !!data.manual
                }, (error) => {
                    this.loadPromise = null
                    throw error
                })
        }
        return this.loadPromise
    }

    _dec2hex(i, len) {
        if (i === undefined || i === null) return ''
        return "0x" + (i + 0x10000).toString(16).substr(-len).toUpperCase()
    }

    _reset(e) {
        e.preventDefault()
        e.stopPropagation()
        return postWithFeedback(i18n.t('elrs.lr1121.reset_title'), i18n.t('elrs.lr1121.reset_error'), '/reset?lr1121', null)(e)
    }

    _fileSelected(e) {
        const files = e.detail.files
        if (files && files[0]) this._uploadFile(files[0])
    }

    _uploadFile(file) {
        overlay('on', {keyboard: false, static: true})
        post('/lr1121', file, {
            headers: {'X-Radio': document.querySelector('input[name=optionsRadio]:checked')?.value || '1'},
            onprogress: this._progressHandler,
            onload: this._completeHandler,
            onerror: this._errorHandler,
            onabort: this._abortHandler,
        })
    }

    _progressHandler(event) {
        const percent = Math.round((event.loaded / event.total) * 100)
        this.progress = percent
        this.status = i18n.t('elrs.common.uploaded_pct', {pct: percent})
    }

    _completeHandler(request) {
        this._resetProgress()
        const data = JSON.parse(request.responseText || '{}')
        if (data.status === 'ok') {
            let percent = 0
            const interval = setInterval(() => {
                percent = percent + 2
                this.progress = percent
                this.status = i18n.t('elrs.common.flashed_pct', {pct: percent})
                if (percent >= 100) {
                    clearInterval(interval)
                    this._showAlert('success', i18n.t('elrs.common.update_succeeded'), data.msg)
                }
            }, 100)
        } else {
            return this._showAlert('error', i18n.t('elrs.common.update_failed'), data.msg || '')
        }
    }

    _errorHandler(request) {
        return this._showAlert('error', i18n.t('elrs.common.update_failed'), request.responseText || '')
    }

    _abortHandler(request) {
        return this._showAlert('info', i18n.t('elrs.common.update_aborted'), request.responseText || '')
    }

    _resetProgress() {
        this.status = ''
        this.progress = 0
    }

    _showAlert(type, title, message) {
        this._resetProgress()
        overlay('off')
        return showAlert(type, title, message)
    }
}