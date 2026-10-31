import { useEffect } from 'react'
import type { Head } from '../seo'

function upsert(selector: string, create: () => HTMLElement, apply: (element: HTMLElement) => void) {
  let element = document.head.querySelector<HTMLElement>(selector)
  if (!element) {
    element = create()
    document.head.appendChild(element)
  }
  apply(element)
}

const metaTag = (attribute: 'name' | 'property', key: string, content: string) =>
  upsert(`meta[${attribute}="${key}"]`, () => document.createElement('meta'), (element) => {
    element.setAttribute(attribute, key)
    element.setAttribute('content', content)
  })

// Keeps the <head> the prerender wrote in step with client-side navigation.
export function useDocumentHead(head: Head) {
  useEffect(() => {
    document.documentElement.lang = head.lang
    document.title = head.title
    metaTag('name', 'description', head.description)
    metaTag('property', 'og:title', head.title)
    metaTag('property', 'og:description', head.description)
    metaTag('property', 'og:url', head.canonical)
    metaTag('property', 'og:image', head.image)
    metaTag('property', 'og:locale', head.lang === 'es' ? 'es_ES' : 'en_US')
    upsert('link[rel="canonical"]', () => Object.assign(document.createElement('link'), { rel: 'canonical' }), (element) => element.setAttribute('href', head.canonical))
    document.head.querySelectorAll('link[rel="alternate"][hreflang]').forEach((element) => element.remove())
    head.alternates.forEach((alternate) => {
      const link = Object.assign(document.createElement('link'), { rel: 'alternate', hreflang: alternate.hreflang, href: alternate.href })
      document.head.appendChild(link)
    })
    document.head.querySelectorAll('script[type="application/ld+json"]').forEach((element) => element.remove())
    head.jsonLd.forEach((data) => {
      const script = Object.assign(document.createElement('script'), { type: 'application/ld+json', textContent: JSON.stringify(data) })
      document.head.appendChild(script)
    })
  }, [head])
}
