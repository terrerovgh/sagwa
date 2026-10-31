import { lockup, wordmarkPath } from '../data/brand'
import { asset } from '../lib/site'

// The pixel-art mark keeps its own colours; the wordmark takes the text colour, so one logo works
// on light, dark and themed backgrounds.
export default function Logo({ className = '', label = 'Sagwa' }: { className?: string; label?: string }) {
  return (
    <svg className={`logo ${className}`} viewBox={`0 0 ${lockup.width} ${lockup.height}`} role="img" aria-label={label}>
      <image href={asset('brand/sagwa-mark.svg')} x="0" y={lockup.markY} width={lockup.markSize} height={lockup.markSize} />
      <path fill="currentColor" transform={`translate(${lockup.wordmarkX} ${lockup.wordmarkBaseline})`} d={wordmarkPath} />
    </svg>
  )
}
