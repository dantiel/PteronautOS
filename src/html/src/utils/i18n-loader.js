/**
 * i18n Loader ??? Imports selected locales, registers with engine, initializes.
 * Import this ONCE in app.js to bootstrap the i18n system.
 *
 * The locale set is resolved at build time by the `i18n-locales-plugin`
 * (virtual:i18n-locales), driven by the I18N_LOCALES env flag. When the flag
 * is unset the virtual module re-exports all 11 locales.
 */
import {i18n} from './i18n.js';
import {locales} from 'virtual:i18n-locales';

// Register all (selected) locales
for (const mod of locales) {
  i18n.register(mod);
}

// Activate saved or detected locale
i18n.init();

export {i18n};