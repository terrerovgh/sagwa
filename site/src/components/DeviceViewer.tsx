import { useEffect, useRef, useState } from 'react'
import { Expand, Focus, Hand, Minus, Plus, Rotate3D, RotateCcw } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { partById } from '../data/hardware'
import { asset } from '../lib/site'
import type { ViewerEngine } from './viewerEngine'
import Localize from './Localize'

type DeviceViewerProps = {
  variant?: 'hero' | 'lab'
  selectedPart?: string
  onSelectPart?: (id: string) => void
}

const prefersReducedMotion = () => window.matchMedia('(prefers-reduced-motion: reduce)').matches
const coarsePointer = () => window.matchMedia('(pointer: coarse)').matches

export default function DeviceViewer({ variant = 'lab', selectedPart = '', onSelectPart }: DeviceViewerProps) {
  const { t } = useTranslation()
  const rootRef = useRef<HTMLDivElement>(null)
  const mountRef = useRef<HTMLDivElement>(null)
  const engineRef = useRef<ViewerEngine | null>(null)
  const selectRef = useRef(onSelectPart)
  const [near, setNear] = useState(false)
  const [ready, setReady] = useState(false)
  const [failed, setFailed] = useState(false)
  const [exploded, setExploded] = useState(() => partById(selectedPart)?.category === 'pcb')
  const [rotating, setRotating] = useState(variant === 'hero' && !selectedPart)
  const [isolate, setIsolate] = useState(false)
  // On touch screens the hero viewer starts asleep, so a swipe over it scrolls the page.
  const [interactive, setInteractive] = useState(variant === 'lab')
  const [hover, setHover] = useState<{ id: string; x: number; y: number } | null>(null)

  const labelRef = useRef('')
  useEffect(() => { selectRef.current = onSelectPart }, [onSelectPart])
  useEffect(() => {
    labelRef.current = t('Modelo tridimensional interactivo del M5Stack CoreS3')
    mountRef.current?.querySelector('canvas')?.setAttribute('aria-label', labelRef.current)
  }, [t, ready])

  // three.js and the model load only once the viewer is about to be seen.
  useEffect(() => {
    const root = rootRef.current
    if (!root) return
    const observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) { setNear(true); observer.disconnect() }
    }, { rootMargin: '200px' })
    observer.observe(root)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    if (!near || !mountRef.current) return
    const mount = mountRef.current
    let engine: ViewerEngine | null = null
    let cancelled = false
    const reducedMotion = prefersReducedMotion()
    const awake = variant === 'lab' || !coarsePointer()
    import('./viewerEngine').then(({ createViewer }) => {
      if (cancelled) return
      try {
        engine = createViewer(mount, {
          label: labelRef.current,
          modelUrl: asset('models/cores3.glb'),
          videoUrl: asset('media/sagwa-screen.mp4'),
          autoRotate: variant === 'hero' && !reducedMotion,
          interactive: awake,
          hover: variant === 'lab',
          onReady: () => setReady(true),
          onFail: () => setFailed(true),
          onSelect: (id) => selectRef.current?.(id),
          onHover: setHover,
        })
        engineRef.current = engine
        setInteractive(awake)
        if (reducedMotion) setRotating(false)
      } catch {
        setFailed(true)
      }
    }, () => setFailed(true))
    return () => {
      cancelled = true
      engine?.dispose()
      engineRef.current = null
    }
  }, [near, variant])

  useEffect(() => { engineRef.current?.setExploded(exploded) }, [exploded, ready])
  useEffect(() => { engineRef.current?.setAutoRotate(rotating && !exploded && !selectedPart) }, [rotating, exploded, selectedPart, ready])
  useEffect(() => { engineRef.current?.setInteractive(interactive) }, [interactive, ready])
  // A newly selected part stops the turn; a part inside the board opens the device to show it.
  const [shownPart, setShownPart] = useState(selectedPart)
  if (shownPart !== selectedPart) {
    setShownPart(selectedPart)
    const part = partById(selectedPart)
    if (part) setRotating(false)
    if (part?.category === 'pcb') setExploded(true)
    if (!part) setIsolate(false)
  }
  useEffect(() => { engineRef.current?.setSelection(selectedPart, isolate) }, [selectedPart, isolate, ready])

  const reset = () => {
    setExploded(false)
    setIsolate(false)
    setRotating(variant === 'hero' && !prefersReducedMotion())
    onSelectPart?.('')
    engineRef.current?.reset()
  }

  const hoverPart = hover ? partById(hover.id) : undefined

  return (
    <div ref={rootRef} className={`device-viewer device-viewer--${variant}${interactive ? ' is-interactive' : ''}`}>
      <img className={`device-poster ${ready && !failed ? 'is-hidden' : ''}`} src={asset('media/sagwa-cores3.webp')} alt={t('M5Stack CoreS3 con Sagwa en pantalla')} width="900" height="900" fetchPriority={variant === 'hero' ? 'high' : 'auto'} />
      <div ref={mountRef} className="device-canvas" role="presentation" />
      {hover && hoverPart && (
        <div className="viewer-tooltip" style={{ transform: `translate(${hover.x + 14}px, ${hover.y + 14}px)` }} aria-hidden="true">
          <strong>{t(hoverPart.name)}</strong><span>{hoverPart.chip}</span>
        </div>
      )}
      <Localize>
        {near && !ready && !failed && <div className="viewer-status" role="status"><span className="loading-dot" /> Preparando el CoreS3…</div>}
        {failed && <div className="viewer-status" role="status">Vista estática · la lista de componentes sigue disponible</div>}
        {ready && !interactive && <button type="button" className="viewer-wake" onClick={() => setInteractive(true)}><Hand size={16} /> Tocar para girar</button>}
        <div className="viewer-toolbar" role="toolbar" aria-label="Controles del modelo 3D">
          <button type="button" className={exploded ? 'is-active' : ''} onClick={() => { setExploded((value) => !value); setRotating(false) }} aria-pressed={exploded} title="Alternar vista despiezada" disabled={!ready}>
            <Expand size={17} /><span>Despiece</span>
          </button>
          {variant === 'lab' && (
            <button type="button" className={isolate ? 'is-active' : ''} disabled={!selectedPart || !ready} onClick={() => setIsolate((value) => !value)} aria-pressed={isolate} title="Aislar el componente seleccionado">
              <Focus size={17} /><span>Aislar</span>
            </button>
          )}
          <button type="button" className={rotating ? 'is-active' : ''} onClick={() => setRotating((value) => !value)} aria-pressed={rotating} title="Girar automáticamente" disabled={!ready}>
            <Rotate3D size={17} /><span>Giro</span>
          </button>
          <button type="button" onClick={() => engineRef.current?.zoom(0.84)} title="Acercar" disabled={!ready}><Plus size={17} /><span className="sr-only">Acercar</span></button>
          <button type="button" onClick={() => engineRef.current?.zoom(1.18)} title="Alejar" disabled={!ready}><Minus size={17} /><span className="sr-only">Alejar</span></button>
          <button type="button" onClick={reset} title="Restablecer vista" disabled={!ready}><RotateCcw size={17} /><span>Restablecer</span></button>
        </div>
        <div className="viewer-hint">Arrastra para orbitar · rueda o pellizca para hacer zoom · haz clic en una pieza</div>
      </Localize>
    </div>
  )
}
