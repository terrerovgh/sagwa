import { useEffect, type RefObject } from 'react'

const focusable = 'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])'

// While a dialog is open: Tab cycles inside it, Escape closes it, and focus returns to whatever
// opened it once it closes.
export function useFocusTrap(ref: RefObject<HTMLElement | null>, active: boolean, onClose: () => void) {
  useEffect(() => {
    if (!active) return
    const opener = document.activeElement as HTMLElement | null
    const root = ref.current
    const items = () => [...(root?.querySelectorAll<HTMLElement>(focusable) ?? [])]
    if (root && !root.contains(document.activeElement)) items()[0]?.focus()
    const onKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { event.preventDefault(); onClose(); return }
      if (event.key !== 'Tab') return
      const list = items()
      if (!list.length) return
      const first = list[0]
      const last = list[list.length - 1]
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus() }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus() }
    }
    document.addEventListener('keydown', onKey)
    return () => {
      document.removeEventListener('keydown', onKey)
      opener?.focus?.()
    }
  }, [ref, active, onClose])
}
