import type { ReactNode } from 'react'
import { Link } from 'react-router'
import { pathFor, type PageId } from '../routes'
import { useLang } from '../lib/site'
import Localize from './Localize'

// A crawlable link to a page of the site, in the current language.
export default function PageLink({ to, query, hash, className, children, onClick, ...rest }: {
  to: PageId
  query?: string
  hash?: string
  className?: string
  children: ReactNode
  onClick?: () => void
  'aria-current'?: 'page' | undefined
}) {
  const lang = useLang()
  return <Link to={`${pathFor(to, lang)}${query ? `?${query}` : ''}${hash ? `#${hash}` : ''}`} className={className} onClick={onClick} {...rest}><Localize>{children}</Localize></Link>
}
