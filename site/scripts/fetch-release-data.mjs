// Writes src/generated/releases.json: every GitHub release of terrerovgh/sagwa (newest first),
// the latest release's manifest.json, the flash layout flash.py writes and the CoreS3 partition
// table. Runs before every dev server and build, so the site always shows the current release.
//
// Without network it keeps the last generated file, or falls back to src/data/releases.fallback.json.
// In CI (CI=true) a failed fetch fails the build instead, so a deploy never ships stale data.
import { copyFile, mkdir, readFile, writeFile } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const SITE = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const ROOT = resolve(SITE, '..')
const REPO = process.env.SAGWA_REPO ?? 'terrerovgh/sagwa'
const OUT = resolve(SITE, 'src/generated/releases.json')
const FALLBACK = resolve(SITE, 'src/data/releases.fallback.json')

const headers = { Accept: 'application/vnd.github+json', 'User-Agent': 'sagwa-site' }
if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`

async function getJson(url) {
  const response = await fetch(url, { headers, signal: AbortSignal.timeout(20000) })
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`)
  return response.json()
}

async function fetchReleases() {
  const releases = []
  for (let page = 1; page <= 10; page += 1) {
    const batch = await getJson(`https://api.github.com/repos/${REPO}/releases?per_page=100&page=${page}`)
    releases.push(...batch)
    if (batch.length < 100) break
  }
  return releases
    .filter((release) => !release.draft && !release.prerelease)
    .sort((a, b) => Date.parse(b.published_at) - Date.parse(a.published_at))
    .map((release) => ({
      tag: release.tag_name,
      version: release.tag_name.replace(/^v/, ''),
      name: release.name || release.tag_name,
      date: release.published_at,
      url: release.html_url,
      notes: (release.body ?? '').trim(),
      assets: release.assets.map((asset) => ({
        name: asset.name,
        size: asset.size,
        sha256: asset.digest?.startsWith('sha256:') ? asset.digest.slice(7) : null,
        url: asset.browser_download_url,
      })),
    }))
}

async function fetchManifest(release) {
  const asset = release.assets.find((item) => item.name === 'manifest.json')
  if (!asset) return null
  const response = await fetch(asset.url, { headers: { 'User-Agent': 'sagwa-site' }, signal: AbortSignal.timeout(20000) })
  return response.ok ? response.json() : null
}

// flash.py: FLASH_PARTS = [(0x000000, "bootloader.bin", "ESP32-S3 2nd Stage Bootloader"), ...]
async function readFlashParts() {
  const source = await readFile(resolve(ROOT, 'flash.py'), 'utf8')
  const block = source.match(/FLASH_PARTS\s*=\s*\[([\s\S]*?)\n\]/)
  if (!block) throw new Error('flash.py: no FLASH_PARTS')
  return [...block[1].matchAll(/\((0x[0-9a-fA-F]+),\s*"([^"]+)",\s*"([^"]*)"\)/g)].map(([, offset, file, label]) => ({
    offset: `0x${offset.slice(2).toUpperCase().padStart(6, '0')}`,
    file,
    label,
  }))
}

const parseSize = (value) => {
  const match = value.trim().match(/^(0x[0-9a-f]+|\d+)([km])?$/i)
  if (!match) return null
  const base = Number(match[1])
  return base * ({ k: 1024, m: 1024 * 1024 }[match[2]?.toLowerCase()] ?? 1)
}

// muse-gadget-sdk/esp32/partitions_muse_cores3.csv: name, type, subtype, offset, size, flags
async function readPartitions() {
  const source = await readFile(resolve(ROOT, 'muse-gadget-sdk/esp32/partitions_muse_cores3.csv'), 'utf8')
  return source.split('\n')
    .map((line) => line.replace(/#.*/, '').trim())
    .filter(Boolean)
    .map((line) => line.split(',').map((cell) => cell.trim()))
    .map(([name, type, subtype, offset, size]) => ({
      name, type, subtype,
      offset: `0x${offset.slice(2).toUpperCase().padStart(6, '0')}`,
      size: parseSize(size),
    }))
}

async function main() {
  await mkdir(dirname(OUT), { recursive: true })
  let releases
  try {
    releases = await fetchReleases()
    if (!releases.length) throw new Error(`${REPO} has no releases`)
  } catch (error) {
    if (process.env.CI) throw error
    const from = existsSync(OUT) ? OUT : FALLBACK
    if (from === FALLBACK) await copyFile(FALLBACK, OUT)
    console.warn(`[release-data] ${error.message}; using ${from === OUT ? 'the last generated data' : 'the fallback'}`)
    return
  }

  const latest = { ...releases[0], manifest: await fetchManifest(releases[0]).catch(() => null) }
  const [flashParts, partitions] = await Promise.all([
    readFlashParts().catch(async (error) => {
      console.warn(`[release-data] ${error.message}; keeping the fallback flash layout`)
      return JSON.parse(await readFile(FALLBACK, 'utf8')).flashParts
    }),
    readPartitions().catch(async (error) => {
      console.warn(`[release-data] ${error.message}; keeping the fallback partition table`)
      return JSON.parse(await readFile(FALLBACK, 'utf8')).partitions
    }),
  ])
  const data = { repo: REPO, generatedAt: new Date().toISOString(), latest, releases, flashParts, partitions }
  await writeFile(OUT, `${JSON.stringify(data, null, 2)}\n`)
  console.log(`[release-data] ${latest.tag} (${latest.date.slice(0, 10)}), ${releases.length} releases`)
}

main().catch((error) => {
  console.error(`[release-data] ${error.message}`)
  process.exit(1)
})
