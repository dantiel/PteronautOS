import {html, LitElement} from "lit"
import {customElement} from "lit/decorators.js"
import {saveJSONWithReboot} from "../utils/feedback.js"
import {i18n} from "../utils/i18n.js"

@customElement('models-panel')
class ModelsPanel extends LitElement {
    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div class="mui-panel mui--text-title">${i18n.t('elrs.models.title')}</div>
            <div class="mui-panel">
                <p>${i18n.t('elrs.models.backup_text')}</p>
                <div>
                    <a href="/config?export" download="models.json" target="_blank"
                       class="mui-btn mui-btn--primary">${i18n.t('elrs.models.export_btn')}</a>
                </div>
            </div>
            <div class="mui-panel">
                <p>${i18n.t('elrs.models.restore_text')}</p>
                <div>
                    <file-drop label="${i18n.t('elrs.models.import_btn')}" @file-drop=${this.upload}>${i18n.t('elrs.models.drop_text')}</file-drop>
                </div>
            </div>
        `
    }

    upload(e) {
        const files = e.detail.files
        const reader = new FileReader()
        reader.onload = (x) => saveJSONWithReboot(
            i18n.t('elrs.models.upload_title'),
            i18n.t('elrs.models.upload_error'),
            '/import',
            x.target.result,
            () => { return i18n.t('elrs.models.reboot_msg') }
        )
        reader.readAsText(files[0])
    }
}