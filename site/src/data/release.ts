// The releases of terrerovgh/sagwa, generated before every build by scripts/fetch-release-data.mjs.
import data from '../generated/releases.json'
import type { Lang } from '../routes'

export type ReleaseAsset = { name: string; size: number; sha256: string | null; url: string }
export type Release = { tag: string; version: string; name: string; date: string; url: string; notes: string; assets: ReleaseAsset[] }
export type Manifest = { version: string; fw_sha256: string; fw_size: number; clips_sha256: string; clips_size: number; notes: string }
export type FlashPart = { offset: string; file: string; label: string }
export type Partition = { name: string; type: string; subtype: string; offset: string; size: number }

type ReleaseData = {
  repo: string
  generatedAt: string
  latest: Release & { manifest: Manifest | null }
  releases: Release[]
  flashParts: FlashPart[]
  partitions: Partition[]
}

const releaseData = data as ReleaseData

export const repo = releaseData.repo
export const repoUrl = `https://github.com/${repo}`
export const latest = releaseData.latest
export const releases = releaseData.releases
export const flashParts = releaseData.flashParts
export const partitions = releaseData.partitions

export const assetSize = (release: Release, name: string) => release.assets.find((asset) => asset.name === name)?.size

const locales: Record<Lang, string> = { en: 'en-US', es: 'es-ES' }

export const formatDate = (iso: string, lang: Lang) =>
  new Intl.DateTimeFormat(locales[lang], { day: 'numeric', month: 'short', year: 'numeric', timeZone: 'UTC' }).format(new Date(iso))

// Binary units, as ESP-IDF and the partition table count them: 314 → "314 B", 49152 → "48 KB", 8192000 → "7.81 MB"
export const formatBytes = (bytes: number, lang: Lang) => {
  const format = (value: number, digits: number) => new Intl.NumberFormat(locales[lang], { maximumFractionDigits: digits }).format(value)
  if (bytes < 1024) return `${format(bytes, 0)} B`
  if (bytes < 1024 * 1024) return `${format(bytes / 1024, 0)} KB`
  return `${format(bytes / (1024 * 1024), 2)} MB`
}
