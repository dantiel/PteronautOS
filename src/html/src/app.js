import {LitElement, html, svg} from 'lit'
import {customElement, query, state} from "lit/decorators.js"
import {initRipple} from './utils/ripple.js'
import {initMuiSelect} from './utils/select.js'
import {elrsState, formatBand} from './utils/state.js'
import {overlay} from './utils/overlay.js'
import './components/elrs-footer.js'
import {i18n} from './utils/i18n-loader.js'

import './pages/info-panel.js'
import {hideLoadingOverlay, loadJSON, showConfirm, showLoadingOverlay} from "./utils/feedback.js"
import {_} from "./utils/libs.js"

@customElement('elrs-app')
export class App extends LitElement {
    static SETTINGS_LOAD_FAILED_MESSAGE = 'Failed to load settings. Retry or power cycle device.'

    @query("#main") accessor mainEl

    // Runtime references and UI constants
    removeRippleListeners = null
    removeSelectListeners = null
    sidedrawerClosePromise = null
    sideDrawer = null

    menu = svg`<svg width="40" height="40" viewBox="0 0 512 512"><path fill="none" stroke="currentColor" stroke-linecap="round" stroke-miterlimit="10" stroke-width="48" d="M88 152h336M88 256h336M88 360h336"/></svg>`

    // Track the currently rendered route so we can restore it when a page blocks navigation
    @state() accessor currentRoute = null

    // Core component setup and rendering
    constructor() {
        super()
        // Bind methods used as callbacks to preserve `this`
        this.renderRoute = this.renderRoute.bind(this)
        this.showSidedrawer = this.showSidedrawer.bind(this)
        this.hideSidedrawer = this.hideSidedrawer.bind(this)
        this._onLocaleChange = () => this.requestUpdate()
        window.addEventListener('locale-changed', this._onLocaleChange)
    }

    disconnectedCallback() {
        super.disconnectedCallback()
        if (this._onLocaleChange) {
            window.removeEventListener('locale-changed', this._onLocaleChange)
        }
    }

    _t(key, params = {}) {
        return i18n.t(key, params)
    }

    _onLangSelect(e) {
        i18n.setLocale(e.target.value)
    }

    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div id="sidedrawer" class="mui--no-user-select">
                <div id="sidedrawer-brand" class="mui--appbar-line-height pto-brand">
                    <div class="pto-brand-headline">
                        <span class="pto-brand-pteronaut">${this._t('app.brand.pteronaut')}</span><span class="pto-brand-os">${this._t('app.brand.os')}</span>
                    </div>
                    <div class="pto-brand-tagline">${this._t('app.brand.tagline')}</div>
                    <pre class="pto-brand-glyph">
                   ___
                  /    \___
                 /  _     o\___
                /  /  \___  /    \
               |  /   \     \/  _  \
               | /     \    /  / \  |
                \_____/\/  /   \  |
                      /\___/     \ |
                     /
                    </pre>
                </div>
                <div class="mui-divider"></div>
                <ul>
                    <li>
                        <strong>${this._t('app.menu.general')}</strong>
                        <ul>
                            <li><a id="menu-info" href="#info"><span class="mui--align-middle icon--symbols icon--symbols--info"></span>${this._t('app.menu.info')}</a></li>
                            <li><a id="menu-binding" href="#binding"><span class="mui--align-middle icon--symbols icon--symbols--bind"></span>${this._t('app.menu.binding')}</a></li>
                            <li><a id="menu-options" href="#options"><span class="mui--align-middle icon--symbols icon--symbols--options"></span>${this._t('app.menu.options')}</a></li>
                            <!-- FEATURE:IS_TX -->
                            ${elrsState.config['button-actions'] && elrsState.config['button-actions'].length !== 0 ? html`
                                <li><a id="menu-buttons" href="#buttons"><span class="mui--align-middle icon--symbols icon--symbols-buttons"></span>${this._t('app.menu.buttons')}</a></li>
                            ` : ''}
                            <li><a id="menu-models" href="#models"><span class="mui--align-middle icon--symbols icon--symbols--settings"></span>${this._t('app.menu.models')}</a></li>
                            <!-- /FEATURE:IS_TX -->
                            <!-- FEATURE:NOT IS_TX -->
                            ${elrsState.config.pwm !== undefined ? html`
                            <li><a id="menu-connections" href="#connections"><span class="mui--align-middle icon--symbols icon--symbols--connections"></span>${this._t('app.menu.connections')}</a></li>
                            ` : ''}
                            <li><a id="menu-serial" href="#serial"><span class="mui--align-middle icon--symbols icon--symbols--serial"></span>${this._t('app.menu.serial')}</a></li>
                            <!-- /FEATURE:NOT IS_TX -->
                            <li><a id="menu-wifi" href="#wifi"><span class="mui--align-middle icon--symbols icon--symbols--wifi"></span>${this._t('app.menu.wifi')}</a></li>
                            <li><a id="menu-update" href="#update"><span class="mui--align-middle icon--symbols icon--symbols--update"></span>${this._t('app.menu.update')}</a></li>
                            <!-- FEATURE:PTERONAUTOS -->
                            <li><a id="menu-ornithopter" href="#ornithopter">${this._t('app.menu.ornithopter')}</a></li>
                            <!-- /FEATURE:PTERONAUTOS -->
                        </ul>
                    </li>
                    <li>
                        <strong>${this._t('app.menu.advanced')}</strong>
                        <ul>
                            <li><a id="menu-hardware" href="#hardware"><span class="mui--align-middle icon--symbols icon--symbols--hardware"></span>${this._t('app.menu.hardware')}</a></li>
                            <!-- FEATURE:PTERONAUTOS -->
                            <li><a id="menu-zephyrus" href="#zephyrus">${this._t('app.menu.zephyrus')}</a></li>
                            <li><a id="menu-servo" href="#servo">${this._t('app.menu.servo')}</a></li>
                            <li><a id="menu-debug" href="#debug">${this._t('app.menu.debug')}</a></li>
                            <!-- /FEATURE:PTERONAUTOS -->
                            <!-- FEATURE:NOT IS_TX -->
                            ${elrsState.settings?.voltage_source_count > 0 ? html`
                                <li><a id="menu-voltage" href="#voltage"><span class="mui--align-middle icon--symbols icon--symbols--voltage"></span>${this._t('app.menu.voltage')}</a></li>
                            ` : ''}
                            <!-- /FEATURE:NOT IS_TX -->
                            <li><a id="menu-cw" href="#cw"><span class="mui--align-middle icon--symbols icon--symbols--wave"></span>${this._t('app.menu.cw')}</a></li>
                            <!-- FEATURE:HAS_LR1121 -->
                            <li><a id="menu-lr1121" href="#lr1121"><span class="mui--align-middle icon--symbols icon--symbols--lr1121"></span>${this._t('app.menu.lr1121')}</a></li>
                            <!-- /FEATURE:HAS_LR1121 -->
                        </ul>
                    </li>
                </ul>
            </div>
            <header id="header" class="mui-appbar elrs-header">
                <div class="mui--appbar-height elrs-header-bar">
                    <div class="elrs-header-menu">
                        <a class="mui--align-middle sidedrawer-toggle mui--visible-xs-inline-block mui--visible-sm-inline-block js-show-sidedrawer"
                           @click="${this.showSidedrawer}">${this.menu}</a>
                        <a class="mui--align-middle sidedrawer-toggle mui--hidden-xs mui--hidden-sm js-hide-sidedrawer"
                           @click="${this.hideSidedrawer}">${this.menu}</a>
                    </div>
                    <div class="elrs-header-meta">
                        <div id="product_name">${elrsState.settings?.product_name}</div>
                        <div>
                            <b>${this._t('app.header.firmware_rev')} </b>${elrsState.settings?.version} ${formatBand()}
                        </div>
                    </div>
                    <div style="margin-left:auto;padding-right:12px;">
                        <select @change="${this._onLangSelect}" style="background:#1c1c22;color:#ccc;border:1px solid #333;border-radius:3px;padding:4px 8px;font-size:12px;cursor:pointer;">
                            ${i18n.availableLocales.map(loc => html`
                                <option value="${loc.code}" ?selected="${i18n.locale === loc.code}">${loc.nativeName}</option>
                            `)}
                        </select>
                    </div>
                </div>
            </header>
            <div id="content-wrapper">
                <div class="mui--appbar-height"></div>
                <div id="main" class="mui-container-fluid">${this.buildRouteContent(this.currentRoute)}</div>
            </div>
            <elrs-footer></elrs-footer>
        `
    }

    // Lifecycle wiring and teardown
    firstUpdated(_changedProperties) {
        this.removeRippleListeners = initRipple()
        this.removeSelectListeners = initMuiSelect()
        this.sideDrawer = _('sidedrawer')
        window.addEventListener('hashchange', this.renderRoute)

        // Initial load sequence
        this.initializeApp()
    }

    disconnectedCallback() {
        this.removeRippleListeners?.()
        this.removeRippleListeners = null
        this.removeSelectListeners?.()
        this.removeSelectListeners = null
        window.removeEventListener('hashchange', this.renderRoute)
        super.disconnectedCallback()
    }

    // Initial data loading and retry flow
    async initializeApp() {
        const loaded = await this.runWithSettingsRetry('Loading settings...', () => this.loadInitialData())
        if (loaded) {
            this.renderRoute()
        }
    }

    async runWithSettingsRetry(loadingMessage, operation) {
        while (true) {
            await showLoadingOverlay(loadingMessage)
            try {
                return await operation()
            } catch (error) {
                hideLoadingOverlay()
                const result = await showConfirm('Settings Load Failed', App.SETTINGS_LOAD_FAILED_MESSAGE, 'Retry', 'Close')
                if (result !== 'confirm') {
                    return false
                }
            } finally {
                hideLoadingOverlay()
            }
        }
    }

    async loadInitialData() {
        const data = await loadJSON('/config', 'Failed to load config')
        elrsState.settings = data.settings || {}
        elrsState.options = data.options || {}
        elrsState.config = data.config || {}
        document.title = 'PteronautOS ' + (data.settings["module-type"] || 'RX') + ' WebUI'
        this.requestUpdate()
        return true
    }

    // UI utilities and drawer DOM helpers
    scrollMainToTop() {
        const doScroll = (behavior = 'smooth') => {
            try {
                window.scrollTo({top: 0, left: 0, behavior})
            } catch {
                window.scrollTo(0, 0)
            }
        }
        requestAnimationFrame(() => requestAnimationFrame(() => doScroll('smooth')))
    }

    getOverlayElement() {
        return document.getElementById('mui-overlay')
    }

    teardownSidedrawer() {
        if (this.sideDrawer && !this.contains(this.sideDrawer)) {
            this.appendChild(this.sideDrawer)
        }
        this.sideDrawer?.classList.remove('active')
    }

    setActiveMenu(route) {
        if (this.sideDrawer) {
            const links = this.sideDrawer.querySelectorAll('a[href^="#"]')
            links.forEach(a => a.classList.remove('active'))
        }
        const cleanRoute = route.replace(/^\/+/, '')
        const id = 'menu-' +cleanRoute
        const el = id ? (this.querySelector(`#${id}`) || document.getElementById(id)) : null
        if (el) el.classList.add('active')
    }

    // Route content rendering and lazy-loading
    buildRouteContent(route) {
        switch (route) {
            case 'info':
                return html`<info-panel></info-panel>`
            case 'binding':
                return html`<binding-panel></binding-panel>`
            case 'options':
            // FEATURE:IS_TX
                return html`<tx-options-panel></tx-options-panel>`
            // /FEATURE:IS_TX
            // FEATURE:NOT IS_TX
                return html`<rx-options-panel></rx-options-panel>`
            // /FEATURE:NOT IS_TX
            case 'wifi':
                return html`<wifi-panel></wifi-panel>`
            case 'update':
                return html`<update-panel></update-panel>`
            // FEATURE:NOT IS_TX
            case 'connections':
                return elrsState.config.pwm !== undefined ? html`<connections-panel></connections-panel>` : null
            case 'serial':
                return html`<serial-panel></serial-panel>`
            case 'voltage':
                return elrsState.settings?.voltage_source_count > 0 ? html`<voltage-calibration-panel></voltage-calibration-panel>` : null
            // /FEATURE:NOT IS_TX
            // FEATURE:IS_TX
            case 'buttons':
                return html`<buttons-panel></buttons-panel>`
            // /FEATURE:IS_TX
            case 'hardware':
                return html`<hardware-layout></hardware-layout>`
            case 'cw':
                return html`<continuous-wave></continuous-wave>`
            case 'models':
                return html`<models-panel></models-panel>`
            // FEATURE:HAS_LR1121
            case 'lr1121':
                return html`<lr1121-updater></lr1121-updater>`
            // /FEATURE:HAS_LR1121
            // FEATURE:PTERONAUTOS
            case 'ornithopter':
                return html`<ornithopter-panel></ornithopter-panel>`
            case 'zephyrus':
                return html`<zephyrus-panel></zephyrus-panel>`
            case 'servo':
                return html`<servo-panel></servo-panel>`
            case 'debug':
                return html`<debug-console-panel></debug-console-panel>`
            // /FEATURE:PTERONAUTOS
            default:
                return null
        }
    }

    generalGroupLoaded = false
    advancedGroupLoaded = false

    loadGeneralGroup() {
        if (this.generalGroupLoaded) return Promise.resolve()
        return showLoadingOverlay('Loading...')
            .then(() => import('./page-groups/general-group.js'))
            .finally(() => {
                hideLoadingOverlay()
                this.generalGroupLoaded = true
            })
    }

    loadAdvancedGroup() {
        if (this.advancedGroupLoaded) return Promise.resolve()
        return showLoadingOverlay('Loading...')
            .then(() => import('./page-groups/advanced-group.js'))
            .finally(() => {
                hideLoadingOverlay()
                this.advancedGroupLoaded = true
            })
    }

    ensureLoadedForRoute(route) {
        if (['binding', 'options', 'wifi', 'update', 'connections', 'serial', 'buttons', 'models'].includes(route)) {
            return this.loadGeneralGroup()
        }
        if (['ornithopter'].includes(route)) {
            return this.loadGeneralGroup()
        }
        if (['hardware', 'voltage', 'cw', 'lr1121', 'zephyrus', 'servo', 'debug'].includes(route)) {
            return this.loadAdvancedGroup()
        }
        return Promise.resolve()
    }

    // Transition/animation timing helpers
    waitForElementTransition(element, mutate, fallbackMs = 220) {
        return new Promise(resolve => {
            if (!element) {
                resolve()
                return
            }

            let settled = false
            let fallbackTimer = null
            const finish = () => {
                if (settled) return
                settled = true
                element.removeEventListener('transitionend', onEnd)
                if (fallbackTimer !== null) {
                    clearTimeout(fallbackTimer)
                    fallbackTimer = null
                }
                resolve()
            }

            const onEnd = (event) => {
                if (event?.target !== element) return
                finish()
            }

            element.addEventListener('transitionend', onEnd)
            try {
                mutate?.()
            } catch {
                finish()
                return
            }
            fallbackTimer = setTimeout(finish, fallbackMs)
        })
    }

    animateMainIn() {
        return new Promise(resolve => {
            if (!this.mainEl) {
                resolve()
                return
            }

            this.mainEl.classList.add('route-fade-in')
            requestAnimationFrame(() => {
                this.mainEl.classList.remove('route-fade-out')
                requestAnimationFrame(() => {
                    this.mainEl.classList.remove('route-fade-in')
                    resolve()
                })
            })
        })
    }

    // Route navigation orchestration
    renderRoute() {
        const route = (location.hash || '#info').replace('#', '').replace(/^\/+/, '')
        if (this.currentRoute && route === this.currentRoute) {
            this.setActiveMenu(route)
            return Promise.resolve()
        }

        const checkNavGuard = () => {
            const currentEl = this.mainEl?.firstElementChild
            if (!currentEl?.checkChanged?.()) return Promise.resolve(true)
            return showConfirm('Configuration Changed', 'Do you wish to navigate away and discard changes to this page?', 'Discard', 'Cancel')
                .then(r => r === 'confirm')
                .catch(() => true)
        }

        return checkNavGuard().then(canNavigate => {
            if (!canNavigate) {
                const hash = '#' + this.currentRoute
                if (this.currentRoute && this.currentRoute !== route && hash !== location.hash) {
                    location.hash = hash
                }
                this.setActiveMenu(this.currentRoute || route)
                return
            }

            this.setActiveMenu(route)
            return this.closeSidedrawer().then(() => this.ensureLoadedForRoute(route)).then(() => {
                let rendered = false
                return this.runWithSettingsRetry('Loading panel data...', () => {
                    return (rendered || !this.currentRoute
                        ? Promise.resolve()
                        : this.waitForElementTransition(this.mainEl, () => this.mainEl.classList.add('route-fade-out')))
                        .then(() => rendered || (this.currentRoute = route) && this.updateComplete)
                        .then(() => rendered || (rendered = true) && this.animateMainIn())
                        .then(() => this.mainEl?.firstElementChild?.pageReady?.())
                        .then(() => {
                            this.scrollMainToTop()
                            return true
                        })
                })
            })
        })
    }

    // Sidedrawer interactions (mobile and desktop toggle)
    showSidedrawer() {
        if (this.sidedrawerClosePromise) {
            this.sidedrawerClosePromise.then(() => this.showSidedrawer())
            return
        }

        if (this.getOverlayElement()) return

        if (!this.sideDrawer) return

        // Ensure known closed baseline before animating in
        this.sideDrawer.classList.remove('active')

        const options = {
            static: true,
            keyboard: false,
            onclose: () => {
                this.teardownSidedrawer()
            }
        }

        const overlayEl = overlay('on', options)
        overlayEl.appendChild(this.sideDrawer)

        overlayEl.addEventListener('click', (event) => {
            if (event.target === overlayEl) {
                this.closeSidedrawer()
            }
        })

        requestAnimationFrame(() => requestAnimationFrame(() => this.sideDrawer.classList.add('active')))
    }

    closeSidedrawer() {
        if (this.sidedrawerClosePromise) {
            return this.sidedrawerClosePromise
        }

        const overlayEl = this.getOverlayElement()
        document.body.classList.remove('hide-sidedrawer')

        // If no overlay, ensure drawer is reset and return
        if (!overlayEl) {
            this.teardownSidedrawer()
            return Promise.resolve()
        }

        this.sidedrawerClosePromise = this.waitForElementTransition(this.sideDrawer, () => {
            this.sideDrawer?.classList.remove('active')
        }).then(() => {
            overlay('off')
        }).finally(() => {
            this.sidedrawerClosePromise = null
        })

        return this.sidedrawerClosePromise
    }

    hideSidedrawer() {
        document.body.classList.toggle('hide-sidedrawer')
    }
}