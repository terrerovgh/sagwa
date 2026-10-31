// Builds the brand files in public/ from the mark (public/brand/sagwa-mark.svg) and the wordmark
// (src/data/brand.ts): standalone logos, favicons, app icons, the web manifest and the Open Graph
// images in each language. Run it after changing the logo: npm run brand
import { readFile, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { Resvg } from '@resvg/resvg-js'
import opentype from 'opentype.js'

const SITE = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const PUBLIC = resolve(SITE, 'public')
const FONTS = resolve(SITE, 'node_modules/@fontsource')

const brand = await readFile(resolve(SITE, 'src/data/brand.ts'), 'utf8')
const lockup = JSON.parse(brand.match(/export const lockup = (\{[^}]+\})/)[1].replace(/(\w+):/g, '"$1":'))
const wordmarkPath = brand.match(/export const wordmarkPath = "([^"]+)"/)[1]
const markSvg = await readFile(resolve(PUBLIC, 'brand/sagwa-mark.svg'), 'utf8')
const markPixels = markSvg.match(/<g shape-rendering="crispEdges">[\s\S]*<\/g>/)[0]

const colors = { ink: '#2b1e19', cream: '#fbf7ef', creamDeep: '#f3ebde', muted: '#74635a', eyeDeep: '#2f5f86' }

const mark = (x, y, size) => `<g transform="translate(${x} ${y}) scale(${size / 48})">${markPixels}</g>`
const logoBody = (fill) => `${mark(0, lockup.markY, lockup.markSize)}<path fill="${fill}" transform="translate(${lockup.wordmarkX} ${lockup.wordmarkBaseline})" d="${wordmarkPath}"/>`
const logoSvg = (fill) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${lockup.width} ${lockup.height}" role="img" aria-labelledby="title"><title id="title">Sagwa</title>${logoBody(fill)}</svg>\n`

const png = (svg, width) => new Resvg(svg, { fitTo: { mode: 'width', value: width } }).render().asPng()

// An ICO holding PNG images (supported everywhere since Windows Vista).
function ico(images) {
  const header = Buffer.alloc(6 + images.length * 16)
  header.writeUInt16LE(0, 0)
  header.writeUInt16LE(1, 2)
  header.writeUInt16LE(images.length, 4)
  let offset = header.length
  images.forEach(({ size, data }, index) => {
    const entry = 6 + index * 16
    header.writeUInt8(size >= 256 ? 0 : size, entry)
    header.writeUInt8(size >= 256 ? 0 : size, entry + 1)
    header.writeUInt16LE(1, entry + 4)
    header.writeUInt16LE(32, entry + 6)
    header.writeUInt32LE(data.length, entry + 8)
    header.writeUInt32LE(offset, entry + 12)
    offset += data.length
  })
  return Buffer.concat([header, ...images.map((image) => image.data)])
}

// Text as outlines, so the social images never depend on fonts installed where this runs.
function textPath(fontFile, text, x, y, size, fill, maxWidth) {
  const font = opentype.loadSync(resolve(FONTS, fontFile))
  let fontSize = size
  while (maxWidth && font.getAdvanceWidth(text, fontSize) > maxWidth) fontSize -= 1
  return `<path fill="${fill}" d="${font.getPath(text, x, y, fontSize, { kerning: true }).toPathData(2)}"/>`
}

async function main() {
  await writeFile(resolve(PUBLIC, 'brand/sagwa-logo.svg'), logoSvg(colors.ink))
  await writeFile(resolve(PUBLIC, 'brand/sagwa-logo-on-dark.svg'), logoSvg(colors.cream))

  // The mark on a cream tile for icons, which platforms crop and place on any background.
  const tile = (padding, radius) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48"><rect width="48" height="48" rx="${radius}" fill="${colors.cream}"/>${mark(padding, padding, 48 - padding * 2)}</svg>`
  const bare = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">${markPixels}</svg>`
  await writeFile(resolve(PUBLIC, 'favicon.ico'), ico([16, 32, 48].map((size) => ({ size, data: png(bare, size) }))))
  // iOS rounds the touch icon itself; the manifest icons carry their own corners.
  await writeFile(resolve(PUBLIC, 'brand/apple-touch-icon.png'), png(tile(5, 0), 180))
  await writeFile(resolve(PUBLIC, 'brand/icon-192.png'), png(tile(5, 10), 192))
  await writeFile(resolve(PUBLIC, 'brand/icon-512.png'), png(tile(5, 10), 512))

  const manifest = {
    name: 'Sagwa',
    short_name: 'Sagwa',
    description: 'A Siamese cat living inside an M5Stack CoreS3.',
    start_url: './en/',
    scope: './',
    display: 'browser',
    background_color: colors.cream,
    theme_color: colors.cream,
    icons: [
      { src: 'brand/icon-192.png', sizes: '192x192', type: 'image/png' },
      { src: 'brand/icon-512.png', sizes: '512x512', type: 'image/png' },
      { src: 'brand/sagwa-mark.svg', sizes: 'any', type: 'image/svg+xml' },
    ],
  }
  await writeFile(resolve(PUBLIC, 'site.webmanifest'), `${JSON.stringify(manifest, null, 2)}\n`)

  // Open Graph: 1200 × 630, the logo and tagline beside the rendered CoreS3.
  const still = await readFile(resolve(SITE, '../docs/media/sagwa-cores3.png')).catch(() => null)
  const copy = {
    en: { line1: 'Your desk just', line2: 'got a cat.', sub: 'A Siamese cat living inside an M5Stack CoreS3.' },
    es: { line1: 'Tu escritorio acaba', line2: 'de tener una gata.', sub: 'Una gata siamesa viviendo en un M5Stack CoreS3.' },
  }
  for (const [lang, text] of Object.entries(copy)) {
    const scale = 1.25
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="630" viewBox="0 0 1200 630">
  <rect width="1200" height="630" fill="${colors.cream}"/>
  <circle cx="930" cy="330" r="300" fill="${colors.creamDeep}"/>
  ${still ? `<image x="640" y="40" width="560" height="560" href="data:image/png;base64,${still.toString('base64')}"/>` : ''}
  <g transform="translate(72 70) scale(${scale})">${logoBody(colors.ink)}</g>
  ${textPath('fraunces/files/fraunces-latin-600-normal.woff', text.line1, 72, 330, 64, colors.ink, 560)}
  ${textPath('fraunces/files/fraunces-latin-600-italic.woff', text.line2, 72, 408, 64, colors.eyeDeep, 560)}
  ${textPath('ibm-plex-sans/files/ibm-plex-sans-latin-400-normal.woff', text.sub, 74, 478, 26, colors.muted, 540)}
  ${textPath('ibm-plex-mono/files/ibm-plex-mono-latin-400-normal.woff', 'terrerovgh.github.io/sagwa', 74, 560, 20, colors.muted)}
</svg>`
    await writeFile(resolve(PUBLIC, `brand/og-${lang}.png`), png(svg, 1200))
  }
  console.log('[brand] logos, favicon.ico, icons, site.webmanifest, og-en.png, og-es.png')
}

main().catch((error) => {
  console.error(error)
  process.exit(1)
})
