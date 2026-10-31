import { Download, ExternalLink, Rss } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { DataTable, DocSection, PageIntro } from '../components/DocsBlocks'
import Localize from '../components/Localize'
import { formatBytes, formatDate, latest, releases, repoUrl, type Release } from '../data/release'
import { asset, useLang } from '../lib/site'

function AssetTable({ release }: { release: Release }) {
  const lang = useLang()
  return (
    <DataTable headers={['Archivo', 'Tamaño', 'SHA-256']} rows={release.assets.map((item) => [
      <a className="asset-link" href={item.url} key="file"><Download size={13} aria-hidden="true" />{item.name}</a>,
      formatBytes(item.size, lang),
      item.sha256 ? <code className="hash" title={item.sha256}>{item.sha256}</code> : '—',
    ])} />
  )
}

export default function ChangelogPage() {
  const { t } = useTranslation()
  const lang = useLang()
  const [current, ...older] = releases
  return (
    <Localize>
      <PageIntro eyebrow="Historial de versiones" title="Cada release, con sus huellas." description="Esta página se genera con cada release publicada en GitHub. Las notas proceden del commit que la originó; los binarios incluyen su suma SHA‑256 para que puedas verificarlos.">
        <div className="page-meta"><span>{releases.length} {t('releases')}</span><span>{t('Última')}: {latest.tag}</span><a href={asset('releases.xml')}><Rss size={11} aria-hidden="true" /> Atom</a></div>
      </PageIntro>
      <DocSection id={current.tag} title={current.name}>
        <p className="release-meta"><time dateTime={current.date}>{formatDate(current.date, lang)}</time><span className="release-badge">{t('Actual')}</span><a href={current.url} target="_blank" rel="noreferrer">{t('Ver en GitHub')} <ExternalLink size={13} aria-hidden="true" /></a></p>
        {current.notes && <blockquote className="release-notes" lang="en">{current.notes}</blockquote>}
        <AssetTable release={current} />
      </DocSection>
      {older.length > 0 && (
        <DocSection id="anteriores" title="Versiones anteriores">
          <div className="release-list">
            {older.map((release) => (
              <details key={release.tag} id={release.tag}>
                <summary><strong>{release.name}</strong><time dateTime={release.date}>{formatDate(release.date, lang)}</time></summary>
                {release.notes && <blockquote className="release-notes" lang="en">{release.notes}</blockquote>}
                <AssetTable release={release} />
                <a className="text-link" href={release.url} target="_blank" rel="noreferrer">{t('Ver en GitHub')} <ExternalLink size={14} aria-hidden="true" /></a>
              </details>
            ))}
          </div>
        </DocSection>
      )}
      <a className="text-link" href={`${repoUrl}/releases`} target="_blank" rel="noreferrer">{t('Todas las releases en GitHub')} <ExternalLink size={15} aria-hidden="true" /></a>
    </Localize>
  )
}
