import { useCallback, useEffect, useMemo, useState } from 'react'
import { I18nextProvider, useTranslation } from 'react-i18next'
import { Route, Routes, useLocation, useNavigate, useParams } from 'react-router'
import { SiteFooter, SiteHeader, MobileMenu, Sidebar } from './components/Chrome'
import SearchDialog from './components/SearchDialog'
import Overview from './pages/Overview'
import NotFound from './pages/NotFound'
import ChangelogPage from './pages/Changelog'
import { ArchitecturePage, DevelopmentPage, HardwarePage, InstallPage, InteractionsPage, TroubleshootingPage, UpdatesPage } from './pages/DocsPages'
import { getI18n, languageStorageKey } from './i18n'
import { defaultLang, isLang, legacyHashes, pageById, pageBySlug, pathFor, type Lang, type PageId, type PageMeta } from './routes'
import { headFor } from './seo'
import { useDocumentHead } from './lib/head'
import './App.css'
import './docs.css'

const content: Record<Exclude<PageId, 'overview'>, () => React.JSX.Element> = {
  install: InstallPage,
  interactions: InteractionsPage,
  hardware: HardwarePage,
  architecture: ArchitecturePage,
  updates: UpdatesPage,
  changelog: ChangelogPage,
  development: DevelopmentPage,
  troubleshooting: TroubleshootingPage,
}

export default function App() {
  return (
    <Routes>
      <Route path="/" element={<RootRedirect />} />
      <Route path="/:lang/*" element={<LangRoute />} />
    </Routes>
  )
}

// "/" only picks a language: the one chosen before, else the browser's. The prerendered "/" does the
// same in an inline script before React loads.
function RootRedirect() {
  const navigate = useNavigate()
  const { hash } = useLocation()
  useEffect(() => {
    let saved: string | null = null
    try { saved = localStorage.getItem(languageStorageKey) } catch { /* no storage: use the browser's language */ }
    const lang: Lang = isLang(saved ?? undefined) ? saved as Lang : navigator.language.toLowerCase().startsWith('es') ? 'es' : defaultLang
    navigate(`/${lang}/${hash}`, { replace: true })
  }, [navigate, hash])
  return null
}

function LangRoute() {
  const params = useParams()
  const lang: Lang = isLang(params.lang) ? params.lang : defaultLang
  const slug = (params['*'] ?? '').replace(/\/+$/, '')
  const page = !isLang(params.lang) ? null : slug === '' ? pageById('overview') : pageBySlug(lang, slug) ?? null
  return (
    <I18nextProvider i18n={getI18n(lang)}>
      <Shell lang={lang} page={page} />
    </I18nextProvider>
  )
}

function Shell({ lang, page }: { lang: Lang; page: PageMeta | null }) {
  const { t } = useTranslation()
  const navigate = useNavigate()
  const { pathname, hash } = useLocation()
  const [searchOpen, setSearchOpen] = useState(false)
  const [menuOpen, setMenuOpen] = useState(false)
  const closeSearch = useCallback(() => setSearchOpen(false), [])
  const closeMenu = useCallback(() => setMenuOpen(false), [])

  useDocumentHead(useMemo(() => headFor(page, lang, t), [page, lang, t]))

  // Links from the old single-page site (#install, #hardware, …) land on their page.
  useEffect(() => {
    const legacy = legacyHashes[hash.slice(1)]
    if (legacy) navigate(pathFor(legacy, lang), { replace: true })
  }, [hash, lang, navigate])

  // A new page starts at its top, or at the section its link names.
  useEffect(() => {
    if (!hash) { window.scrollTo({ top: 0, behavior: 'instant' }); return }
    document.getElementById(decodeURIComponent(hash.slice(1)))?.scrollIntoView()
  }, [pathname, hash])

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement
      if (event.key === '/' && !['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName) && !target.isContentEditable) {
        event.preventDefault()
        setSearchOpen(true)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  const id = page?.id ?? null
  const Page = id && id !== 'overview' ? content[id] : null

  return (
    <div className="app-shell">
      <a className="skip-link" href="#contenido">{t('Saltar al contenido')}</a>
      <SiteHeader page={id} onSearch={() => setSearchOpen(true)} onMenu={() => setMenuOpen(true)} />
      {!page ? <main id="contenido" className="docs-content"><NotFound /></main>
        : id === 'overview' ? <Overview />
          : (
            <div className="docs-layout">
              <Sidebar page={page.id} />
              <main id="contenido" className="docs-content">{Page && <Page />}</main>
            </div>
          )}
      <SiteFooter />
      {searchOpen && <SearchDialog onClose={closeSearch} />}
      {menuOpen && <MobileMenu page={id} onClose={closeMenu} onSearch={() => setSearchOpen(true)} />}
    </div>
  )
}
