// What each page puts in its <head>. The prerender (scripts/prerender.mjs, through entry-server.tsx)
// writes it into the static HTML and useDocumentHead keeps it current while navigating client-side.
import type { TFunction } from 'i18next'
import { latest, repoUrl } from './data/release'
import { langs, pathFor, type Lang, type PageMeta } from './routes'
import { siteUrl } from './lib/site'

export type Head = {
  lang: Lang
  title: string
  description: string
  canonical: string
  alternates: { hreflang: string; href: string }[]
  image: string
  imageAlt: string
  jsonLd: object[]
}

const absolute = (path: string) => `${siteUrl}${path}`

export function headFor(page: PageMeta | null, lang: Lang, t: TFunction): Head {
  const tagline = t('Una gata siamesa viviendo en un M5Stack CoreS3.')
  const title = !page ? `${t('Página no encontrada')} · Sagwa`
    : page.id === 'overview' ? `Sagwa · ${tagline}` : `${t(page.title)} · Sagwa`
  const description = page ? t(page.description) : tagline
  const id = page?.id ?? 'overview'
  const canonical = absolute(pathFor(id, lang))
  const alternates = [
    ...langs.map((code) => ({ hreflang: code, href: absolute(pathFor(id, code)) })),
    { hreflang: 'x-default', href: absolute(pathFor(id, 'en')) },
  ]
  const image = absolute(`/brand/og-${lang}.png`)

  const jsonLd: object[] = []
  if (page?.id === 'overview') {
    jsonLd.push(
      { '@context': 'https://schema.org', '@type': 'WebSite', name: 'Sagwa', url: absolute(`/${lang}/`), inLanguage: lang, description },
      {
        '@context': 'https://schema.org',
        '@type': 'SoftwareApplication',
        name: 'Sagwa',
        description,
        applicationCategory: 'EntertainmentApplication',
        operatingSystem: 'ESP-IDF (ESP32-S3, M5Stack CoreS3)',
        softwareVersion: latest.version,
        datePublished: latest.date,
        downloadUrl: latest.url,
        codeRepository: repoUrl,
        license: 'https://www.apache.org/licenses/LICENSE-2.0',
        image,
        offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
      },
    )
  } else if (page) {
    jsonLd.push({
      '@context': 'https://schema.org',
      '@type': 'BreadcrumbList',
      itemListElement: [
        { '@type': 'ListItem', position: 1, name: 'Sagwa', item: absolute(`/${lang}/`) },
        { '@type': 'ListItem', position: 2, name: t(page.title), item: canonical },
      ],
    })
  }

  return { lang, title, description, canonical, alternates, image, imageAlt: tagline, jsonLd }
}

const escape = (value: string) => value.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;')

// The same head as HTML, for the prerendered pages.
export function headHtml(head: Head) {
  const meta = (attribute: 'name' | 'property', key: string, content: string) => `<meta ${attribute}="${key}" content="${escape(content)}" />`
  return [
    `<title>${escape(head.title)}</title>`,
    meta('name', 'description', head.description),
    `<link rel="canonical" href="${head.canonical}" />`,
    ...head.alternates.map((alternate) => `<link rel="alternate" hreflang="${alternate.hreflang}" href="${alternate.href}" />`),
    meta('property', 'og:type', 'website'),
    meta('property', 'og:site_name', 'Sagwa'),
    meta('property', 'og:locale', head.lang === 'es' ? 'es_ES' : 'en_US'),
    meta('property', 'og:locale:alternate', head.lang === 'es' ? 'en_US' : 'es_ES'),
    meta('property', 'og:title', head.title),
    meta('property', 'og:description', head.description),
    meta('property', 'og:url', head.canonical),
    meta('property', 'og:image', head.image),
    meta('property', 'og:image:width', '1200'),
    meta('property', 'og:image:height', '630'),
    meta('property', 'og:image:alt', head.imageAlt),
    meta('name', 'twitter:card', 'summary_large_image'),
    ...head.jsonLd.map((data) => `<script type="application/ld+json">${JSON.stringify(data).replace(/</g, '\\u003c')}</script>`),
  ].join('\n    ')
}
