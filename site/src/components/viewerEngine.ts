// The Three.js side of DeviceViewer, loaded only when a viewer scrolls into view, so three.js stays
// out of the first page load. It renders on demand: a frame is drawn only while something moves
// (orbit damping, a camera transition, the exploded view settling, auto-rotate, a new video frame).
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js'
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js'
import { MeshoptDecoder } from 'three/addons/libs/meshopt_decoder.module.js'
import { hardwareParts, type HardwarePart } from '../data/hardware'

export type ViewerOptions = {
  label: string
  modelUrl: string
  videoUrl: string
  autoRotate: boolean
  interactive: boolean
  hover: boolean
  onReady: () => void
  onFail: () => void
  onSelect?: (id: string) => void
  onHover?: (hit: { id: string; x: number; y: number } | null) => void
}

export type ViewerEngine = {
  setExploded: (on: boolean) => void
  setAutoRotate: (on: boolean) => void
  setInteractive: (on: boolean) => void
  setSelection: (id: string, isolate: boolean) => void
  zoom: (factor: number) => void
  reset: () => void
  dispose: () => void
}

const cleanName = (name: string) => name.replace(/\.\d+$/, '')
const meshMatches = (name: string, meshes: string[]) => meshes.some((mesh) => name === mesh || name.startsWith(`${mesh}_`))
const partForMesh = (name: string) => hardwareParts.find((part) => meshMatches(name, part.meshes))

const defaultCamera = new THREE.Vector3(6.3, 3.4, 8.6)
const origin = new THREE.Vector3()
// How far each assembly travels along the device's depth in the exploded view.
const explodeOffsets: Record<string, number> = { front_assembly: 3.1, frame_assembly: 1.0, pcb_assembly: -0.9, base_assembly: -3.1 }
// The side each kind of part is best seen from.
const viewDirections: Record<HardwarePart['category'], THREE.Vector3> = {
  front: new THREE.Vector3(0.05, 0.12, 1).normalize(),
  pcb: new THREE.Vector3(0.75, 0.45, 0.5).normalize(),
  frame: new THREE.Vector3(-0.85, 0.2, 0.5).normalize(),
  base: new THREE.Vector3(0.5, 0.3, -0.8).normalize(),
}
const highlight = new THREE.Color('#5aa6d6')
const hoverTint = new THREE.Color('#9cc9e6')

type MaterialState = { material: THREE.Material; opacity: number; transparent: boolean; depthWrite: boolean }

function disposeMaterial(material: THREE.Material) {
  for (const value of Object.values(material)) if (value instanceof THREE.Texture) value.dispose()
  material.dispose()
}

export function createViewer(mount: HTMLElement, options: ViewerOptions): ViewerEngine {
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: 'high-performance' })
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
  renderer.setClearColor(0x000000, 0)
  renderer.outputColorSpace = THREE.SRGBColorSpace
  renderer.toneMapping = THREE.ACESFilmicToneMapping
  renderer.toneMappingExposure = 1.02
  renderer.shadowMap.enabled = true
  renderer.shadowMap.type = THREE.PCFSoftShadowMap
  const canvas = renderer.domElement
  canvas.setAttribute('role', 'img')
  canvas.setAttribute('aria-label', options.label)
  mount.appendChild(canvas)

  const scene = new THREE.Scene()
  const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 100)
  camera.position.copy(defaultCamera)
  const controls = new OrbitControls(camera, canvas)
  controls.enableDamping = true
  controls.dampingFactor = 0.08
  controls.enablePan = false
  controls.minDistance = 4.6
  controls.maxDistance = 15
  controls.minPolarAngle = Math.PI * 0.12
  controls.maxPolarAngle = Math.PI * 0.84
  controls.autoRotateSpeed = 0.6
  controls.autoRotate = options.autoRotate

  const pmrem = new THREE.PMREMGenerator(renderer)
  const room = new RoomEnvironment()
  const environment = pmrem.fromScene(room, 0.04).texture
  scene.environment = environment
  scene.environmentIntensity = 0.9
  room.dispose()
  pmrem.dispose()

  const key = new THREE.DirectionalLight(0xfff1e0, 3)
  key.position.set(-4, 7, 6)
  key.castShadow = true
  key.shadow.mapSize.set(2048, 2048)
  Object.assign(key.shadow.camera, { left: -5, right: 5, top: 5, bottom: -5 })
  key.shadow.normalBias = 0.03
  key.shadow.radius = 6
  scene.add(key)
  scene.add(new THREE.HemisphereLight(0xe8eef2, 0x5a453b, 1.5))
  const rim = new THREE.DirectionalLight(0xc9e2f0, 1.6)
  rim.position.set(5, 1.5, -5)
  scene.add(rim)

  const ground = new THREE.Mesh(new THREE.PlaneGeometry(30, 30), new THREE.ShadowMaterial({ color: 0x2b1e19, opacity: 0.16 }))
  ground.rotation.x = -Math.PI / 2
  ground.position.y = -2.65
  ground.receiveShadow = true
  scene.add(ground)

  const wrap = new THREE.Group()
  scene.add(wrap)

  // ---- frame scheduling ---------------------------------------------------------------------------
  let disposed = false
  let visible = true
  let frame = 0
  let dirty = true
  let exploded = 0
  let lastTime = performance.now()
  const cameraGoal = { position: null as THREE.Vector3 | null, target: null as THREE.Vector3 | null }
  const invalidate = () => {
    dirty = true
    if (!frame && visible && !disposed) frame = requestAnimationFrame(tick)
  }

  const assemblies: Record<string, THREE.Object3D> = {}
  const meshes: THREE.Mesh[] = []
  const materialStates = new Map<THREE.Mesh, MaterialState[]>()
  let modelRoot: THREE.Object3D | null = null
  let selected: HardwarePart | undefined
  let hovered: HardwarePart | undefined
  let isolated = false

  function settleExplode(delta: number) {
    let moving = false
    for (const [name, offset] of Object.entries(explodeOffsets)) {
      const assembly = assemblies[name]
      if (!assembly) continue
      const goal = offset * exploded
      const next = THREE.MathUtils.damp(assembly.position.z, goal, 7, delta)
      assembly.position.z = Math.abs(next - goal) < 0.002 ? goal : next
      if (assembly.position.z !== goal) moving = true
    }
    return moving
  }

  function followCameraGoal(delta: number) {
    if (!cameraGoal.position || !cameraGoal.target) return false
    const t = 1 - Math.exp(-6 * delta)
    camera.position.lerp(cameraGoal.position, t)
    controls.target.lerp(cameraGoal.target, t)
    if (camera.position.distanceTo(cameraGoal.position) < 0.01 && controls.target.distanceTo(cameraGoal.target) < 0.005) {
      camera.position.copy(cameraGoal.position)
      controls.target.copy(cameraGoal.target)
      cameraGoal.position = cameraGoal.target = null
    }
    return true
  }

  function tick(now: number) {
    frame = 0
    if (disposed || !visible || document.hidden) return
    const delta = Math.min((now - lastTime) / 1000, 0.05)
    lastTime = now
    const moving = settleExplode(delta)
    const tweening = followCameraGoal(delta)
    if (tweening && selected) aimAtSelection(false)
    const orbiting = controls.update(delta)
    if (moving || tweening || orbiting || dirty) {
      renderer.render(scene, camera)
      dirty = false
    }
    if (moving || tweening || orbiting || controls.autoRotate) frame = requestAnimationFrame(tick)
  }

  controls.addEventListener('change', invalidate)
  controls.addEventListener('start', () => { cameraGoal.position = cameraGoal.target = null; invalidate() })

  // ---- size ---------------------------------------------------------------------------------------
  const resize = () => {
    const { width, height } = mount.getBoundingClientRect()
    if (!width || !height) return
    renderer.setSize(width, height, false)
    camera.aspect = width / height
    // Keep the whole device in frame on narrow (portrait) viewers.
    const fitAspect = 1.08
    camera.fov = camera.aspect < fitAspect
      ? Math.min(62, THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(17)) * fitAspect / camera.aspect)))
      : 34
    camera.updateProjectionMatrix()
    invalidate()
  }
  const resizeObserver = new ResizeObserver(resize)
  resizeObserver.observe(mount)
  resize()

  const visibility = new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting
    if (visible) { lastTime = performance.now(); invalidate(); void video.play().catch(() => {}) }
    else video.pause()
  })
  visibility.observe(mount)
  const onPageVisibility = () => { if (!document.hidden) { lastTime = performance.now(); invalidate() } }
  document.addEventListener('visibilitychange', onPageVisibility)

  // ---- Sagwa on the screen ------------------------------------------------------------------------
  const video = document.createElement('video')
  video.src = options.videoUrl
  video.muted = true
  video.loop = true
  video.playsInline = true
  video.preload = 'auto'
  const videoTexture = new THREE.VideoTexture(video)
  videoTexture.colorSpace = THREE.SRGBColorSpace
  videoTexture.flipY = false
  // Draw once per decoded video frame (the clips run at 12 fps), not 60 times a second.
  const onVideoFrame = () => {
    if (disposed) return
    invalidate()
    video.requestVideoFrameCallback(onVideoFrame)
  }
  if (typeof (video as Partial<HTMLVideoElement>).requestVideoFrameCallback === 'function') video.requestVideoFrameCallback(onVideoFrame)
  else (video as HTMLVideoElement).addEventListener('timeupdate', invalidate)

  // ---- model --------------------------------------------------------------------------------------
  const loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder)
  loader.load(options.modelUrl, (gltf) => {
    if (disposed) return
    modelRoot = gltf.scene
    const bounds = new THREE.Box3().setFromObject(modelRoot)
    const size = bounds.getSize(new THREE.Vector3())
    const center = bounds.getCenter(new THREE.Vector3())
    const scale = 4.35 / Math.max(size.x, size.y, size.z)
    modelRoot.scale.setScalar(scale)
    modelRoot.position.copy(center).multiplyScalar(-scale)
    wrap.add(modelRoot)
    modelRoot.traverse((object) => {
      const name = cleanName(object.name)
      if (name.endsWith('_assembly')) assemblies[name] = object
      if (!(object instanceof THREE.Mesh)) return
      object.castShadow = name !== 'display_panel'
      object.receiveShadow = true
      object.material = Array.isArray(object.material) ? object.material.map((material) => material.clone()) : object.material.clone()
      if (name === 'display_panel') object.material = new THREE.MeshBasicMaterial({ map: videoTexture, toneMapped: false })
      meshes.push(object)
      const list = Array.isArray(object.material) ? object.material : [object.material]
      materialStates.set(object, list.map((material) => ({ material, opacity: material.opacity, transparent: material.transparent, depthWrite: material.depthWrite })))
    })
    applyLook()
    options.onReady()
    void video.play().catch(() => { /* the GLB's still screen is a fine fallback */ })
    invalidate()
  }, undefined, () => { if (!disposed) options.onFail() })

  canvas.addEventListener('webglcontextlost', (event) => { event.preventDefault(); options.onFail() })

  // ---- highlight, isolation, framing --------------------------------------------------------------
  function applyLook() {
    const internal = selected?.category === 'pcb'
    if (assemblies.front_assembly) assemblies.front_assembly.visible = !internal
    if (assemblies.frame_assembly) assemblies.frame_assembly.visible = !internal
    for (const mesh of meshes) {
      const name = cleanName(mesh.name)
      const isSelected = !!selected && meshMatches(name, selected.meshes)
      const isHovered = !isSelected && !!hovered && meshMatches(name, hovered.meshes)
      mesh.visible = !(isolated && selected) || isSelected
      for (const state of materialStates.get(mesh) ?? []) {
        const { material } = state
        // With a part selected the rest of the device fades back, so the part reads in context.
        // Sagwa's screen stays solid: see-through, it would show the board through her.
        const ghost = !!selected && !isSelected && !isolated && name !== 'display_panel'
        material.transparent = ghost || state.transparent
        material.opacity = ghost ? Math.min(state.opacity, 0.35) : state.opacity
        material.depthWrite = ghost ? false : state.depthWrite
        if (material instanceof THREE.MeshStandardMaterial) {
          material.emissive.copy(isSelected ? highlight : isHovered ? hoverTint : new THREE.Color(0x000000))
          material.emissiveIntensity = isSelected ? 0.45 : isHovered ? 0.25 : 0
        }
        material.needsUpdate = true
      }
    }
    invalidate()
  }

  function aimAtSelection(start: boolean) {
    if (!selected || !modelRoot) return
    const box = new THREE.Box3()
    for (const mesh of meshes) if (meshMatches(cleanName(mesh.name), selected.meshes)) box.expandByObject(mesh)
    if (box.isEmpty()) return
    const sphere = box.getBoundingSphere(new THREE.Sphere())
    const distance = THREE.MathUtils.clamp(sphere.radius * 6, 8.6, 11)
    const target = sphere.center.clone().multiplyScalar(0.5)   // halfway to the device's centre: the orbit feels steadier
    const goal = target.clone().add(viewDirections[selected.category].clone().multiplyScalar(distance))
    if (start || cameraGoal.position) {
      cameraGoal.position = goal
      cameraGoal.target = target
    }
  }

  // ---- pointer ------------------------------------------------------------------------------------
  const raycaster = new THREE.Raycaster()
  const pointer = new THREE.Vector2()
  const pick = (event: PointerEvent) => {
    if (!modelRoot) return undefined
    const rect = canvas.getBoundingClientRect()
    pointer.set(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1)
    raycaster.setFromCamera(pointer, camera)
    const hit = raycaster.intersectObject(modelRoot, true).find((item) => item.object.visible && cleanName(item.object.name) !== 'inside')
    return hit ? partForMesh(cleanName(hit.object.name)) : undefined
  }
  let downX = 0
  let downY = 0
  const onDown = (event: PointerEvent) => { downX = event.clientX; downY = event.clientY }
  const onUp = (event: PointerEvent) => {
    if (!options.onSelect || !controls.enabled || Math.hypot(event.clientX - downX, event.clientY - downY) > 6) return
    const part = pick(event)
    if (part) options.onSelect(part.id)
  }
  let hoverQueued = false
  const onMove = (event: PointerEvent) => {
    if (!options.hover || event.pointerType !== 'mouse' || event.buttons || hoverQueued) return
    hoverQueued = true
    requestAnimationFrame(() => {
      hoverQueued = false
      const part = pick(event)
      if (part !== hovered) { hovered = part; applyLook() }
      canvas.style.cursor = part ? 'pointer' : ''
      const rect = canvas.getBoundingClientRect()
      options.onHover?.(part ? { id: part.id, x: event.clientX - rect.left, y: event.clientY - rect.top } : null)
    })
  }
  const onLeave = () => {
    if (hovered) { hovered = undefined; applyLook() }
    canvas.style.cursor = ''
    options.onHover?.(null)
  }
  canvas.addEventListener('pointerdown', onDown)
  canvas.addEventListener('pointerup', onUp)
  canvas.addEventListener('pointermove', onMove)
  canvas.addEventListener('pointerleave', onLeave)

  const setInteractive = (on: boolean) => {
    controls.enabled = on
    // Until the viewer is switched on, a finger on it scrolls the page instead of turning the device.
    canvas.style.touchAction = on ? 'none' : 'pan-y'
  }
  setInteractive(options.interactive)

  if (import.meta.env.DEV) {
    window.__seek = (time: number) => {
      controls.autoRotate = false
      wrap.rotation.y = (time / 10) * Math.PI * 2
      invalidate()
    }
  }

  return {
    setExploded(on) {
      exploded = on ? 1 : 0
      if (on && !selected) {
        cameraGoal.position = new THREE.Vector3(9.8, 4.8, 6.8)
        cameraGoal.target = origin.clone()
      }
      invalidate()
    },
    setAutoRotate(on) {
      controls.autoRotate = on
      invalidate()
    },
    setInteractive,
    setSelection(id, isolate) {
      const next = hardwareParts.find((part) => part.id === id)
      const changed = next !== selected
      selected = next
      isolated = isolate && !!next
      applyLook()
      if (next && changed) aimAtSelection(true)
    },
    zoom(factor) {
      cameraGoal.position = cameraGoal.target = null
      const offset = camera.position.clone().sub(controls.target)
      const distance = THREE.MathUtils.clamp(offset.length() * factor, controls.minDistance, controls.maxDistance)
      cameraGoal.position = controls.target.clone().add(offset.setLength(distance))
      cameraGoal.target = controls.target.clone()
      invalidate()
    },
    reset() {
      cameraGoal.position = defaultCamera.clone()
      cameraGoal.target = origin.clone()
      invalidate()
    },
    dispose() {
      disposed = true
      if (frame) cancelAnimationFrame(frame)
      resizeObserver.disconnect()
      visibility.disconnect()
      document.removeEventListener('visibilitychange', onPageVisibility)
      canvas.removeEventListener('pointerdown', onDown)
      canvas.removeEventListener('pointerup', onUp)
      canvas.removeEventListener('pointermove', onMove)
      canvas.removeEventListener('pointerleave', onLeave)
      controls.dispose()
      video.pause()
      video.removeAttribute('src')
      video.load()
      videoTexture.dispose()
      environment.dispose()
      scene.traverse((object) => {
        if (!(object instanceof THREE.Mesh)) return
        object.geometry.dispose()
        ;(Array.isArray(object.material) ? object.material : [object.material]).forEach(disposeMaterial)
      })
      renderer.dispose()
      canvas.remove()
      if (import.meta.env.DEV) delete window.__seek
    },
  }
}

declare global {
  interface Window {
    __seek?: (time: number) => void
  }
}
