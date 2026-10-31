import { useCallback, useMemo, useRef, useState } from 'react'
import { ArrowRight, Search, X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { useNavigate } from 'react-router'
import { searchItems, type SearchItem } from '../data/navigation'
import { pageById, pathFor } from '../routes'
import { useLang } from '../lib/site'
import { useFocusTrap } from '../lib/useFocusTrap'
import Localize from './Localize'

const normalize = (value: string) => value.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase()

export default function SearchDialog({ onClose }: { onClose: () => void }) {
  const { t } = useTranslation()
  const lang = useLang()
  const navigate = useNavigate()
  const [query, setQuery] = useState('')
  const dialog = useRef<HTMLElement>(null)
  useFocusTrap(dialog, true, onClose)

  const describe = useCallback((item: SearchItem) => {
    if (!item.part) return t(item.description)
    const [chip, ...summary] = item.description.split(' · ')
    return `${chip} · ${t(summary.join(' · '))}`
  }, [t])

  const results = useMemo(() => {
    const needle = normalize(query.trim())
    if (!needle) return searchItems.slice(0, 7)
    return searchItems.filter((item) => normalize(`${item.title} ${item.description} ${item.keywords} ${t(item.title)} ${describe(item)}`).includes(needle)).slice(0, 10)
  }, [query, t, describe])

  const open = (item: SearchItem) => {
    onClose()
    navigate(`${pathFor(item.page, lang)}${item.part ? `?part=${item.part}` : ''}${item.hash ? `#${item.hash}` : ''}`)
  }

  return (
    <div className="overlay" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) onClose() }}>
      <section ref={dialog} className="search-dialog" role="dialog" aria-modal="true" aria-label={t('Buscar en la documentación')}>
        <div className="search-input-row">
          <Search size={20} aria-hidden="true" />
          <input autoFocus value={query} onChange={(event) => setQuery(event.target.value)} onKeyDown={(event) => { if (event.key === 'Enter' && results[0]) { event.preventDefault(); open(results[0]) } }} placeholder={t('Buscar instalación, sensores, OTA…')} aria-label={t('Buscar')} />
          <button type="button" onClick={onClose} aria-label={t('Cerrar búsqueda')}><X size={19} /></button>
        </div>
        <div className="search-results">
          {results.length ? results.map((item) => (
            <button type="button" key={`${item.title}-${item.part ?? ''}`} onClick={() => open(item)}>
              <span><strong>{t(item.title)}</strong><small>{describe(item)}</small></span>
              <span className="result-page">{t(pageById(item.page).title)}<ArrowRight size={15} /></span>
            </button>
          )) : (
            <div className="search-empty">
              <Search size={24} />
              <strong>{t('Sin resultados para «{{query}}»', { query })}</strong>
              <p>{t('Prueba con «flash», «proximidad», «OTA» o «BMI270».')}</p>
            </div>
          )}
        </div>
        <Localize>
          <div className="search-footer"><span><kbd>↵</kbd> abrir</span><span><kbd>esc</kbd> cerrar</span><span>{searchItems.length} {t('temas indexados')}</span></div>
        </Localize>
      </section>
    </div>
  )
}
