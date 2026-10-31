import { useRef } from 'react'
import { ChevronRight, ExternalLink, Github, Menu, Moon, Search, Sun, X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { Link, useLocation } from 'react-router'
import { navGroups } from '../data/navigation'
import { formatDate, latest, repoUrl } from '../data/release'
import { languageStorageKey } from '../i18n'
import { langs, pageById, pathFor, type PageId } from '../routes'
import { useLang } from '../lib/site'
import { useFocusTrap } from '../lib/useFocusTrap'
import Logo from './Logo'
import PageLink from './PageLink'

const languageNames = { en: 'English', es: 'Español' }

export function LanguageSwitch({ page }: { page: PageId | null }) {
  const lang = useLang()
  const { t } = useTranslation()
  const { search, hash } = useLocation()
  return (
    <nav className="language-switch" aria-label={t('Seleccionar idioma')}>
      {langs.map((code) => (
        <Link key={code} to={`${pathFor(page ?? 'overview', code)}${search}${hash}`} hrefLang={code} lang={code} aria-label={languageNames[code]} aria-current={code === lang ? 'true' : undefined} className={code === lang ? 'is-active' : ''}
          onClick={() => { try { localStorage.setItem(languageStorageKey, code) } catch { /* private mode: the URL still decides */ } }}>
          {code.toUpperCase()}
        </Link>
      ))}
    </nav>
  )
}

// The theme is a data-theme attribute on <html>, set before first paint by index.html's inline script.
// Both icons are rendered and CSS shows the right one, so the prerendered HTML never disagrees with it.
export function ThemeToggle() {
  const { t } = useTranslation()
  const toggle = () => {
    const root = document.documentElement
    const dark = root.dataset.theme ? root.dataset.theme === 'dark' : window.matchMedia('(prefers-color-scheme: dark)').matches
    root.dataset.theme = dark ? 'light' : 'dark'
    try { localStorage.setItem('sagwa-theme', root.dataset.theme) } catch { /* the choice lasts for this page */ }
  }
  return (
    <button type="button" className="theme-toggle" onClick={toggle} aria-label={t('Cambiar entre tema claro y oscuro')} title={t('Cambiar entre tema claro y oscuro')}>
      <Moon size={18} className="theme-icon-dark" aria-hidden="true" />
      <Sun size={18} className="theme-icon-light" aria-hidden="true" />
    </button>
  )
}

export function SiteHeader({ page, onSearch, onMenu }: { page: PageId | null; onSearch: () => void; onMenu: () => void }) {
  const { t } = useTranslation()
  const guide = page === 'install' || page === 'interactions'
  return (
    <header className="site-header">
      <PageLink to="overview" className="brand" aria-current={page === 'overview' ? 'page' : undefined}>
        <Logo className="brand-lockup" label={t('Sagwa, inicio')} /><span className="brand-docs">docs</span>
      </PageLink>
      <nav className="top-nav" aria-label={t('Navegación principal')}>
        <PageLink to="install" className={guide ? 'is-active' : ''}>{t('Guía')}</PageLink>
        <PageLink to="hardware" className={page === 'hardware' ? 'is-active' : ''}>{t('Hardware 3D')}</PageLink>
        <PageLink to="architecture" className={page === 'architecture' ? 'is-active' : ''}>{t('Arquitectura')}</PageLink>
        <PageLink to="changelog" className={page === 'changelog' ? 'is-active' : ''}>{t('Versiones')}</PageLink>
      </nav>
      <div className="header-actions">
        <button type="button" className="search-trigger" onClick={onSearch} aria-label={t('Buscar documentación')}><Search size={17} aria-hidden="true" /><span>{t('Buscar documentación')}</span><kbd>/</kbd></button>
        <PageLink to="changelog" className="version-link">v{latest.version}</PageLink>
        <LanguageSwitch page={page} />
        <ThemeToggle />
        <a className="github-link" href={repoUrl} target="_blank" rel="noreferrer" aria-label={t('Sagwa en GitHub')}><Github size={20} /></a>
        <button type="button" className="mobile-menu-button" onClick={onMenu} aria-label={t('Abrir menú')}><Menu size={22} /></button>
      </div>
    </header>
  )
}

function NavGroups({ page, onNavigate, iconSize }: { page: PageId | null; onNavigate?: () => void; iconSize: number }) {
  const { t } = useTranslation()
  return navGroups.map((group) => (
    <div className="nav-group" key={group.label}>
      <span>{t(group.label)}</span>
      {group.items.map(({ id, icon: Icon }) => (
        <PageLink key={id} to={id} onClick={onNavigate} className={page === id ? 'is-active' : ''} aria-current={page === id ? 'page' : undefined}>
          <Icon size={iconSize} aria-hidden="true" />{t(pageById(id).title)}{(page === id || onNavigate) && <ChevronRight size={14} aria-hidden="true" />}
        </PageLink>
      ))}
    </div>
  ))
}

export function Sidebar({ page }: { page: PageId }) {
  const { t } = useTranslation()
  const lang = useLang()
  return (
    <aside className="docs-sidebar">
      <nav className="sidebar-inner" aria-label={t('Documentación')}>
        <NavGroups page={page} iconSize={16} />
        <a className="sidebar-release" href={latest.url} target="_blank" rel="noreferrer">
          <span><span className="status-dot" />{t('Release actual')}</span>
          <strong>{latest.name}</strong>
          <small>{formatDate(latest.date, lang)} <ExternalLink size={12} aria-hidden="true" /></small>
        </a>
      </nav>
    </aside>
  )
}

export function MobileMenu({ page, onClose, onSearch }: { page: PageId | null; onClose: () => void; onSearch: () => void }) {
  const { t } = useTranslation()
  const panel = useRef<HTMLElement>(null)
  useFocusTrap(panel, true, onClose)
  return (
    <div className="mobile-menu-overlay" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) onClose() }}>
      <aside ref={panel} className="mobile-menu" role="dialog" aria-modal="true" aria-label={t('Menú de navegación')}>
        <div className="mobile-menu-head">
          <Logo className="brand-lockup" />
          <button type="button" onClick={onClose} aria-label={t('Cerrar menú')}><X size={22} /></button>
        </div>
        <button type="button" className="mobile-search" onClick={() => { onClose(); onSearch() }}><Search size={18} aria-hidden="true" />{t('Buscar documentación')}</button>
        <div className="mobile-language"><span>{t('Idioma')}</span><LanguageSwitch page={page} /><ThemeToggle /></div>
        <NavGroups page={page} onNavigate={onClose} iconSize={17} />
        <a href={repoUrl} target="_blank" rel="noreferrer" className="mobile-github"><Github size={18} aria-hidden="true" />{t('Abrir repositorio')}<ExternalLink size={15} aria-hidden="true" /></a>
      </aside>
    </div>
  )
}

export function SiteFooter() {
  const { t } = useTranslation()
  return (
    <footer className="site-footer">
      <div className="footer-brand"><Logo className="brand-lockup" /><p>{t('Una gata siamesa viviendo en un M5Stack CoreS3.')}</p></div>
      <div className="footer-links">
        <div><strong>{t('Proyecto')}</strong><a href={repoUrl} target="_blank" rel="noreferrer">GitHub</a><PageLink to="changelog">{t('Versiones')}</PageLink><a href={`${repoUrl}/blob/main/LICENSE`} target="_blank" rel="noreferrer">Apache 2.0</a></div>
        <div><strong>{t('Documentación')}</strong><PageLink to="install">{t('Instalar')}</PageLink><PageLink to="hardware">{t('Hardware 3D')}</PageLink><PageLink to="troubleshooting">{t('Ayuda')}</PageLink></div>
        <div><strong>{t('Ecosistema')}</strong><a href="https://docs.m5stack.com/en/core/CoreS3" target="_blank" rel="noreferrer">M5Stack CoreS3</a><a href={`${repoUrl}/tree/main/muse-gadget-sdk`} target="_blank" rel="noreferrer">Muse Gadget SDK</a><a href="https://docs.espressif.com/projects/esp-idf/en/v6.0.1/" target="_blank" rel="noreferrer">ESP-IDF 6.0.1</a></div>
      </div>
      <div className="footer-bottom"><span>{t('Documentación de código abierto')} · {latest.tag}</span><span>{t('Hecha para un escritorio que necesitaba una gata.')}</span></div>
    </footer>
  )
}
