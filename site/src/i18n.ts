import { createInstance, type i18n as I18n } from 'i18next'
import { initReactI18next } from 'react-i18next'
import { english } from './locales/en'
import type { Lang } from './routes'

// The copy is written in Spanish and each Spanish string is its own key, so Spanish needs no
// resources: a missing key renders as itself. English comes from locales/en.ts.
//
// One instance per language: the URL decides the language and each route renders inside the
// provider for its own instance, so switching language never mutates shared state mid-render,
// and the prerender can render both languages in one process.
const instances: Partial<Record<Lang, I18n>> = {}

// The prerender listens for English keys that have no translation (see scripts/prerender.mjs).
let missingKeyListener: ((key: string) => void) | null = null
export const onMissingKey = (listener: ((key: string) => void) | null) => { missingKeyListener = listener }

export function getI18n(lang: Lang) {
  const cached = instances[lang]
  if (cached) return cached
  const instance = createInstance()
  void instance.use(initReactI18next).init({
    lng: lang,
    fallbackLng: false,
    resources: { en: { translation: english }, es: { translation: {} } },
    keySeparator: false,
    nsSeparator: false,
    interpolation: { escapeValue: false },
    initAsync: false,
    saveMissing: import.meta.env.SSR && lang === 'en',
    saveMissingTo: 'current',
    missingKeyHandler: (_lngs, _ns, key) => missingKeyListener?.(key),
  })
  instances[lang] = instance
  return instance
}

export const languageStorageKey = 'sagwa-language'
