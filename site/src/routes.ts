// Every page of the site, its URL in each language and the text its <head> carries.
// Titles and descriptions are Spanish i18n keys, translated through src/locales/en.ts like the rest of the copy.

export const langs = ['en', 'es'] as const
export type Lang = (typeof langs)[number]
export const defaultLang: Lang = 'en'
export const isLang = (value: string | undefined): value is Lang => langs.includes(value as Lang)

export type PageId = 'overview' | 'install' | 'interactions' | 'hardware' | 'architecture' | 'updates' | 'changelog' | 'development' | 'troubleshooting'

export type PageMeta = { id: PageId; slug: Record<Lang, string>; title: string; description: string }

export const pages: PageMeta[] = [
  { id: 'overview', slug: { en: '', es: '' }, title: 'Conoce a Sagwa', description: 'Documentación interactiva de Sagwa, la gata siamesa que vive en un M5Stack CoreS3. Instalación, comportamiento, firmware y hardware 3D.' },
  { id: 'install', slug: { en: 'install', es: 'instalacion' }, title: 'Instalación', description: 'Flashea una release firmada de Sagwa en tu M5Stack CoreS3 en un minuto: requisitos, instalador y offsets de cada imagen.' },
  { id: 'interactions', slug: { en: 'interactions', es: 'interacciones' }, title: 'Interacciones', description: 'Cómo reacciona Sagwa a tu mano, a la luz y al movimiento: poses, estados de ánimo, mensajes y sonidos.' },
  { id: 'hardware', slug: { en: 'hardware', es: 'hardware' }, title: 'Hardware 3D', description: 'Explora en 3D el M5Stack CoreS3 pieza a pieza y descubre qué hace cada sensor y chip en Sagwa.' },
  { id: 'architecture', slug: { en: 'architecture', es: 'arquitectura' }, title: 'Arquitectura', description: 'Arquitectura del firmware de Sagwa: sensores, planificador, grafo de clips SGWC, mapa de flash y Muse Home Link.' },
  { id: 'updates', slug: { en: 'updates', es: 'actualizaciones' }, title: 'Actualizaciones', description: 'Cómo se actualiza Sagwa por Wi‑Fi: manifiesto firmado, SHA‑256, momento tranquilo y rollback automático.' },
  { id: 'changelog', slug: { en: 'changelog', es: 'cambios' }, title: 'Historial de versiones', description: 'Todas las releases de Sagwa con sus notas, binarios, tamaños y sumas SHA‑256.' },
  { id: 'development', slug: { en: 'development', es: 'desarrollo' }, title: 'Desarrollo', description: 'Compila Sagwa con ESP-IDF v6.0.1, regenera sus animaciones y sonidos y extiende el firmware.' },
  { id: 'troubleshooting', slug: { en: 'troubleshooting', es: 'solucion-de-problemas' }, title: 'Solución de problemas', description: 'Soluciones a los problemas más frecuentes al instalar y actualizar Sagwa: puerto serie, modo descarga, pantalla negra y microSD.' },
]

export const pageById = (id: PageId) => pages.find((page) => page.id === id) ?? pages[0]

export const pageBySlug = (lang: Lang, slug: string) => pages.find((page) => page.slug[lang] === slug)

// A page's path inside the site, without the base: /en/, /es/instalacion/
export const pathFor = (id: PageId, lang: Lang) => {
  const slug = pageById(id).slug[lang]
  return slug ? `/${lang}/${slug}/` : `/${lang}/`
}

// The old single-page site used #install, #hardware, …; their links still lead somewhere.
export const legacyHashes: Record<string, PageId> = Object.fromEntries(
  pages.filter((page) => page.id !== 'overview').map((page) => [page.id, page.id]),
) as Record<string, PageId>
