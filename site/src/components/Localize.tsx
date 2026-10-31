import { Children, Fragment, cloneElement, createElement, isValidElement, type ReactNode } from 'react'
import { useTranslation } from 'react-i18next'

const translatableAttributes = ['aria-label', 'title', 'placeholder', 'alt'] as const

export default function Localize({ children }: { children: ReactNode }) {
  const { t } = useTranslation()

  const text = (value: string) => {
    const core = value.trim()
    if (!core) return value
    const leading = value.slice(0, value.indexOf(core))
    const trailing = value.slice(value.indexOf(core) + core.length)
    return `${leading}${t(core)}${trailing}`
  }

  const visit = (node: ReactNode): ReactNode => {
    if (typeof node === 'string') return text(node)
    if (Array.isArray(node)) return Children.toArray(node).map(visit)
    if (!isValidElement(node)) return node
    if (typeof node.type !== 'string' && node.type !== Fragment) return node

    const props = node.props as Record<string, unknown> & { children?: ReactNode }
    const next: Record<string, unknown> = {}
    translatableAttributes.forEach((attribute) => {
      const value = props[attribute]
      if (typeof value === 'string' && value.trim()) next[attribute] = text(value)
    })
    if (props.children === undefined) return cloneElement(node, next)
    // Pass translated children back positionally, as JSX wrote them, so React does not treat them as a keyed list.
    const translated = visit(props.children)
    return Array.isArray(translated) ? cloneElement(node, next, ...translated) : cloneElement(node, next, translated)
  }

  const translated = visit(children)
  return Array.isArray(translated) ? createElement(Fragment, null, ...translated) : <>{translated}</>
}
