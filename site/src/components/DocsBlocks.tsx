import { useState, type ReactNode } from 'react'
import { Check, Copy, Info, TriangleAlert } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import Localize from './Localize'

export function PageIntro({ eyebrow, title, description, children }: { eyebrow: string; title: string; description: string; children?: ReactNode }) {
  return (
    <Localize>
      <header className="page-intro">
        <div className="page-eyebrow">{eyebrow}</div>
        <h1>{title}</h1>
        <p>{description}</p>
        {children}
      </header>
    </Localize>
  )
}

export function DocSection({ id, title, children }: { id: string; title: string; children: ReactNode }) {
  return (
    <Localize>
      <section className="doc-section" id={id}>
        <h2>{title}</h2>
        {children}
      </section>
    </Localize>
  )
}

export function CodeBlock({ code, label = 'Terminal' }: { code: string; label?: string }) {
  const [copied, setCopied] = useState(false)
  const { t } = useTranslation()
  const localizedCode = t(code)
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(localizedCode)
      setCopied(true)
      window.setTimeout(() => setCopied(false), 1600)
    } catch {
      setCopied(false)
    }
  }
  return (
    <Localize>
      <div className="code-block">
        <div className="code-label"><span>{label}</span><button type="button" onClick={copy} aria-label="Copiar código">{copied ? <Check size={15} /> : <Copy size={15} />}{copied ? 'Copiado' : 'Copiar'}</button></div>
        <pre><code>{localizedCode}</code></pre>
      </div>
    </Localize>
  )
}

export function Note({ children, tone = 'info', title }: { children: ReactNode; tone?: 'info' | 'warning'; title: string }) {
  return (
    <Localize>
      <aside className={`doc-note doc-note--${tone}`}>
        {tone === 'warning' ? <TriangleAlert size={20} /> : <Info size={20} />}
        <div><strong>{title}</strong><div>{children}</div></div>
      </aside>
    </Localize>
  )
}

export function DataTable({ headers, rows }: { headers: string[]; rows: ReactNode[][] }) {
  return (
    <Localize>
      <div className="table-scroll">
        <table className="data-table">
          <thead><tr>{headers.map((header) => <th key={header}>{header}</th>)}</tr></thead>
          <tbody>{rows.map((row, index) => <tr key={index}>{row.map((cell, cellIndex) => <td key={cellIndex}>{cell}</td>)}</tr>)}</tbody>
        </table>
      </div>
    </Localize>
  )
}
