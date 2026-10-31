// After `vite build` and `vite build --ssr`: renders every page in every language to static HTML
// with its own <head>, so search engines and link previews see real pages, then writes the
// language picker at "/", 404.html, sitemap.xml, robots.txt and the releases.xml Atom feed.
//
// It also renders the English pages with a listener for missing translations and fails the build
// when Spanish copy has no entry in src/locales/en.ts.
import { readFile, rm, writeFile, mkdir } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const SITE = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const DIST = resolve(SITE, 'dist')
const SSR = resolve(SITE, 'dist-ssr')

const server = await import(pathToFileURL(resolve(SSR, 'entry-server.js')).href)
const { render, pages, langs, pathFor, latest, releases, repoUrl, siteUrl, onMissingKey } = server
const template = await readFile(resolve(DIST, 'index.html'), 'utf8')
const base = new URL(`${siteUrl}/`).pathname

// Spanish-looking text: an accent, ¿¡«», or a common Spanish word. English copy and code pass.
const spanish = /[áéíóúñ¿¡«»]|(^|\s)(de|del|la|las|el|los|que|con|en|para|por|una|sin|sus?|se|al|es|y|tu|tus|ver)(\s|$)/i
const missing = new Map()
// Shown as they are in every language: the language switch names each language in itself.
const untranslated = new Set(['EN', 'ES', 'Español'])

const write = async (path, html) => {
  const file = resolve(DIST, `.${path}`, path.endsWith('/') ? 'index.html' : '')
  await mkdir(dirname(file), { recursive: true })
  await writeFile(file, html)
}

const page = (lang, head, html, prerendered = true) => template
  .replace('<html lang="en">', `<html lang="${lang}">`)
  .replace('<!--app-head-->', head)
  .replace('<div id="root"><!--app-html--></div>', `<div id="root"${prerendered ? ' data-prerendered' : ''}>${html}</div>`)

let count = 0
for (const lang of langs) {
  for (const { id } of pages) {
    if (lang === 'en') onMissingKey((key) => { if (spanish.test(key)) missing.set(key, [...(missing.get(key) ?? []), id]) })
    const { html, head, path } = render(id, lang)
    onMissingKey(null)
    // Text that never went through t() at all (a component Localize could not reach) shows up
    // as Spanish in the English HTML. Code blocks are left out: their comments are checked by key.
    if (lang === 'en') {
      const text = html.replace(/<pre[\s\S]*?<\/pre>/g, '').replace(/<(script|style)[\s\S]*?<\/\1>/g, '')
      for (const match of text.matchAll(/>([^<]+)</g)) {
        const value = match[1].replace(/&[a-z#0-9]+;/g, ' ').trim()
        if (value && !untranslated.has(value) && spanish.test(value)) missing.set(value, [...(missing.get(value) ?? []), id])
      }
      for (const match of text.matchAll(/\s(?:alt|title|aria-label|placeholder)="([^"]+)"/g)) {
        if (!untranslated.has(match[1]) && spanish.test(match[1])) missing.set(match[1], [...(missing.get(match[1]) ?? []), id])
      }
    }
    await write(path, page(lang, head, html))
    count += 1
  }
}

// 404.html: GitHub Pages serves it for any unknown path, so it renders on the client from scratch.
{
  const { head } = render(null, 'en')
  await write('/404.html', page('en', `${head}\n    <meta name="robots" content="noindex" />`, '', false))
}

// "/": send visitors to their language. Crawlers follow the hreflang links and the noscript list.
{
  const target = (lang) => `${base}${lang}/`
  const html = `<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Sagwa</title>
    <meta name="robots" content="noindex, follow" />
    <link rel="canonical" href="${siteUrl}/en/" />
    ${langs.map((lang) => `<link rel="alternate" hreflang="${lang}" href="${siteUrl}/${lang}/" />`).join('\n    ')}
    <link rel="alternate" hreflang="x-default" href="${siteUrl}/en/" />
    <link rel="icon" href="${base}favicon.ico" sizes="32x32" />
    <script>
      var lang = 'en'
      try { lang = localStorage.getItem('sagwa-language') || ((navigator.language || '').toLowerCase().indexOf('es') === 0 ? 'es' : 'en') } catch (e) {}
      if (lang !== 'es') lang = 'en'
      location.replace('${base}' + lang + '/' + location.hash)
    </script>
  </head>
  <body>
    <noscript><a href="${target('en')}">English</a> · <a href="${target('es')}">Español</a></noscript>
  </body>
</html>
`
  await writeFile(resolve(DIST, 'index.html'), html)
}

// sitemap.xml, with each page's language versions.
{
  const lastmod = latest.date.slice(0, 10)
  const urls = pages.flatMap(({ id }) => langs.map((lang) => `  <url>
    <loc>${siteUrl}${pathFor(id, lang)}</loc>
    <lastmod>${lastmod}</lastmod>
${langs.map((alt) => `    <xhtml:link rel="alternate" hreflang="${alt}" href="${siteUrl}${pathFor(id, alt)}" />`).join('\n')}
    <xhtml:link rel="alternate" hreflang="x-default" href="${siteUrl}${pathFor(id, 'en')}" />
  </url>`))
  await writeFile(resolve(DIST, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">
${urls.join('\n')}
</urlset>
`)
  await writeFile(resolve(DIST, 'robots.txt'), `User-agent: *\nAllow: /\n\nSitemap: ${siteUrl}/sitemap.xml\n`)
}

// releases.xml: an Atom feed of the firmware releases.
{
  const escape = (value) => value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  const entries = releases.slice(0, 30).map((release) => `  <entry>
    <title>${escape(release.name)}</title>
    <id>${release.url}</id>
    <link href="${release.url}" />
    <updated>${release.date}</updated>
    <summary>${escape(release.notes || release.name)}</summary>
  </entry>`)
  await writeFile(resolve(DIST, 'releases.xml'), `<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Sagwa releases</title>
  <id>${siteUrl}/releases.xml</id>
  <link rel="self" href="${siteUrl}/releases.xml" />
  <link href="${repoUrl}/releases" />
  <updated>${latest.date}</updated>
${entries.join('\n')}
</feed>
`)
}

await rm(SSR, { recursive: true, force: true })

if (missing.size) {
  console.error(`[prerender] ${missing.size} Spanish strings have no English translation in src/locales/en.ts:`)
  for (const [key, ids] of missing) console.error(`  ${JSON.stringify(key)}  (${[...new Set(ids)].join(', ')})`)
  process.exit(1)
}
console.log(`[prerender] ${count} pages, 404.html, sitemap.xml, robots.txt, releases.xml (${latest.tag})`)
