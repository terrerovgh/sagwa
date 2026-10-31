import { useSyncExternalStore } from 'react'
import { useTranslation } from 'react-i18next'
import { isLang, type Lang } from '../routes'

// The public URL of the site (no trailing slash) and the path Vite serves it under.
export const siteUrl: string = import.meta.env.VITE_SITE_URL
export const basePath = import.meta.env.BASE_URL

// A file in public/, wherever the site is deployed: asset('media/pose-sit.png')
export const asset = (path: string) => `${basePath}${path.replace(/^\//, '')}`

// The language of the page being rendered: each route renders inside its language's i18n provider.
export function useLang(): Lang {
  const { i18n } = useTranslation()
  return isLang(i18n.language) ? i18n.language : 'en'
}

// False while rendering on the server and hydrating, true afterwards: for state the prerendered
// HTML cannot know, such as a ?query.
const noSubscription = () => () => {}
export const useHydrated = () => useSyncExternalStore(noSubscription, () => true, () => false)
