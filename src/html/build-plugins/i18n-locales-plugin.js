// Locale selection plugin for the ESP8285 WebUI.
//
// The WebUI ships 11 locales; each one adds gzipped JS to the single-bundle
// pteronautos payload. The ESP8285 has ~23KB free heap, so a smaller payload
// is a cheaper first load. Select which locales get baked in with the
// I18N_LOCALES env flag (comma-separated list):
//
//   I18N_LOCALES=en,de npm run build:pteronautos   # English + German
//   I18N_LOCALES=pt                               # Portuguese only
//
// Unset -> all locales are bundled (backward compatible).

import path from 'path'

const ALL_LOCALES = ['en', 'pt', 'de', 'es', 'fr', 'hi', 'ja', 'ko', 'ru', 'zh', 'ar']

export function i18nLocalesPlugin(env) {
  const virtualId = 'virtual:i18n-locales'
  const resolvedId = '\0' + virtualId
  const localesDir = path.resolve(process.cwd(), 'src/locales')

  let selected = ALL_LOCALES
  const flag = (env.I18N_LOCALES || '').trim()
  if (flag) {
    const requested = flag.split(',').map((s) => s.trim()).filter(Boolean)
    const known = [...new Set(requested.filter((code) => ALL_LOCALES.includes(code)))]
    if (known.length > 0) selected = known
  }

  return {
    name: 'i18n-locales',
    resolveId(id) {
      if (id === virtualId) return resolvedId
      return null
    },
    load(id) {
      if (id !== resolvedId) return null
      const imports = selected
        .map((code) => `import ${code} from ${JSON.stringify(path.join(localesDir, code + '.js'))};`)
        .join('\n')
      const body = selected.join(', ')
      return `${imports}\n\nconst locales = [${body}]\n\nexport { locales }\n`
    },
  }
}