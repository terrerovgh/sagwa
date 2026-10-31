// Renders one page to HTML for scripts/prerender.mjs.
import { StrictMode } from 'react'
import { renderToString } from 'react-dom/server'
import { StaticRouter } from 'react-router'
import App from './App'
import { getI18n, onMissingKey } from './i18n'
import { latest, releases, repoUrl } from './data/release'
import { headFor, headHtml } from './seo'
import { langs, pages, pathFor, type Lang, type PageId } from './routes'
import { siteUrl } from './lib/site'

export { langs, pages, pathFor, latest, releases, repoUrl, siteUrl, onMissingKey }

export function render(id: PageId | null, lang: Lang) {
  const path = id ? pathFor(id, lang) : `/${lang}/404/`
  const base = import.meta.env.BASE_URL.replace(/\/$/, '')
  const html = renderToString(
    <StrictMode>
      <StaticRouter location={`${base}${path}`} basename={base}>
        <App />
      </StaticRouter>
    </StrictMode>,
  )
  const page = id ? pages.find((item) => item.id === id) ?? null : null
  const head = headHtml(headFor(page, lang, getI18n(lang).t))
  return { html, head, path }
}
