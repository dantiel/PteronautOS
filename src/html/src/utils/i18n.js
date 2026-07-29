/**
 * ÆtherCodex i18n Engine — Reactive Lit-based translation singleton.
 * 
 * Architecture:
 *   - Singleton pattern: `import {i18n} from '../utils/i18n.js'`
 *   - t(key, ...params) returns translated string
 *   - setLocale(code) switches language, persists to localStorage, dispatches event
 *   - Panels listen to 'locale-changed' on window and call requestUpdate()
 *   - PteroElement base class handles this automatically
 *   - RTL support via document.documentElement.dir
 *
 * Usage from CoffeeScript:
 *   import {i18n} from '../utils/i18n'
 *   // In constructor or connectedCallback:
 *   this._boundOnLocale = () => this.requestUpdate()
 *   window.addEventListener('locale-changed', this._boundOnLocale)
 *   // In render methods: i18n.t('key')
 *
 * Usage from LithAML:
 *   = self._t('ornithopter.panel.title')
 *   (where _t is a PteroElement method that calls i18n.t)
 */

const STORAGE_KEY = 'pteronautos-locale';

class I18nEngine {
  constructor() {
    /** @type {string} */
    this._locale = 'en';
    /** @type {Map<string, string>} */
    this._strings = new Map();
    /** @type {Map<string, object>} */
    this._locales = new Map();
    this._loaded = false;
  }

  /**
   * Register a locale module. Called once per locale at import time.
   * @param {object} mod - {code, name, nativeName, dir, keys}
   */
  register(mod) {
    this._locales.set(mod.code, mod);
  }

  /**
   * Initialize: load saved locale or detect from browser, then activate.
   * Must be called once after all locale modules are imported.
   */
  init() {
    if (this._loaded) return;
    this._loaded = true;

    // Priority: localStorage > browser language > 'en'
    const saved = localStorage.getItem(STORAGE_KEY);
    let code = 'en';

    if (saved && this._locales.has(saved)) {
      code = saved;
    } else {
      const browserLang = (navigator.language || '').split('-')[0];
      if (this._locales.has(browserLang)) {
        code = browserLang;
      }
    }

    this._activate(code, false);
  }

  /**
   * Set active locale.
   * @param {string} code
   */
  setLocale(code) {
    if (!this._locales.has(code)) {
      console.warn(`[i18n] Unknown locale: ${code}, falling back to en`);
      code = 'en';
    }
    this._activate(code, true);
  }

  /**
   * Translate a key. Supports {{param}} interpolation.
   * Falls back to key name if translation not found.
   * @param {string} key
   * @param {object} [params]
   * @returns {string}
   */
  t(key, params = {}) {
    let str = this._strings.get(key);
    if (str === undefined) {
      // Try English fallback
      const enMod = this._locales.get('en');
      if (enMod && enMod.keys[key] !== undefined) {
        str = enMod.keys[key];
      } else {
        return key; // bare key as last resort
      }
    }
    // Interpolation: {{name}} -> params.name
    return str.replace(/\{\{(\w+)\}\}/g, (_, name) => {
      return params[name] !== undefined ? String(params[name]) : `{{${name}}}`;
    });
  }

  /**
   * Get current locale code.
   * @returns {string}
   */
  get locale() {
    return this._locale;
  }

  /**
   * Get list of all registered locales.
   * @returns {Array<{code: string, name: string, nativeName: string, dir: string}>}
   */
  get availableLocales() {
    return Array.from(this._locales.values()).map(m => ({
      code: m.code,
      name: m.name,
      nativeName: m.nativeName,
      dir: m.dir
    }));
  }

  /**
   * Get RTL direction for current locale.
   * @returns {'ltr'|'rtl'}
   */
  get dir() {
    const mod = this._locales.get(this._locale);
    return (mod && mod.dir === 'rtl') ? 'rtl' : 'ltr';
  }

  // ── Internal ────────────────────────────────────────────────────

  _activate(code, dispatch) {
    this._locale = code;
    const mod = this._locales.get(code);
    if (!mod) return;

    // Populate strings map
    this._strings.clear();
    for (const [key, val] of Object.entries(mod.keys)) {
      this._strings.set(key, val);
    }

    // Set document direction for RTL
    document.documentElement.lang = code;
    document.documentElement.dir = mod.dir || 'ltr';

    // Persist
    try {
      localStorage.setItem(STORAGE_KEY, code);
    } catch (e) {
      // localStorage may be unavailable
    }

    // Notify listeners
    if (dispatch) {
      window.dispatchEvent(new CustomEvent('locale-changed', {
        detail: { locale: code, dir: mod.dir || 'ltr' }
      }));
    }
  }
}

// Singleton export
export const i18n = new I18nEngine();

// Convenience: t() on window for quick console debugging
if (typeof window !== 'undefined') {
  window._t = (key, params) => i18n.t(key, params);
}
