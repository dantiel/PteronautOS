/**
 * i18n Loader — Imports all locales, registers with engine, initializes.
 * Import this ONCE in app.js to bootstrap the i18n system.
 */
import {i18n} from './i18n.js';
import en from '../locales/en.js';
import pt from '../locales/pt.js';
import de from '../locales/de.js';
import es from '../locales/es.js';
import fr from '../locales/fr.js';
import hi from '../locales/hi.js';
import ja from '../locales/ja.js';
import ko from '../locales/ko.js';
import ru from '../locales/ru.js';
import zh from '../locales/zh.js';
import ar from '../locales/ar.js';

// Register all locales
const locales = [en, pt, de, es, fr, hi, ja, ko, ru, zh, ar];
for (const mod of locales) {
  i18n.register(mod);
}

// Activate saved or detected locale
i18n.init();

export {i18n};
