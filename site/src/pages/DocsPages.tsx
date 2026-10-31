import { useState, type ReactNode } from 'react'
import { ArrowRight, Check, ChevronRight, CircuitBoard, ExternalLink, Hand, MessageCircle, Moon, Music2, ShieldCheck, Sparkles, Sun, Wifi } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { useSearchParams } from 'react-router'
import DeviceViewer from '../components/DeviceViewer'
import { CodeBlock, DataTable, DocSection, Note, PageIntro } from '../components/DocsBlocks'
import Localize from '../components/Localize'
import PageLink from '../components/PageLink'
import { hardwareParts, partById } from '../data/hardware'
import { assetSize, flashParts, formatBytes, latest, partitions, repoUrl } from '../data/release'
import { asset, useHydrated, useLang } from '../lib/site'
import type { PageId } from '../routes'

function NextPage({ label, title, to, action }: { label: string; title: string; to: PageId; action: string }) {
  return (
    <Localize>
      <div className="next-page"><div><span>{label}</span><strong>{title}</strong></div><PageLink to={to}>{action} <ArrowRight size={18} /></PageLink></div>
    </Localize>
  )
}

// What each image flash.py writes holds; the offsets come from flash.py itself.
const flashContents: Record<string, string> = {
  'bootloader.bin': 'Bootloader de segunda etapa',
  'partition-table.bin': 'Mapa de particiones de 16 MB',
  'ota_data_initial.bin': 'Selección de ranura OTA',
  'sagwa-cores3.bin': 'Firmware firmado de Sagwa',
  'sagwa_clips.bin': '22 animaciones',
}

const partitionUses: Record<string, string> = {
  nvs: 'Credenciales, vínculos BLE y estadísticas',
  otadata: 'Estado de selección OTA',
  ota_0: 'Aplicación principal',
  ota_1: 'Siguiente actualización',
  sagwa: 'Paquete de 22 animaciones',
}

const clipsOffset = flashParts.find((part) => part.file === 'sagwa_clips.bin')?.offset ?? '0x830000'

export function InstallPage() {
  const { t } = useTranslation()
  const lang = useLang()
  return (
    <Localize>
      <PageIntro eyebrow="Guía de instalación" title="Sagwa despierta en un minuto." description="Flashea una release firmada en tu M5Stack CoreS3. No necesitas compilar el firmware ni instalar ESP-IDF para la instalación rápida.">
        <div className="page-meta"><span>Python 3.8+</span><span>USB‑C de datos</span><span>{flashParts.length} imágenes</span></div>
      </PageIntro>
      <DocSection id="requisitos" title="Antes de conectarla">
        <ul className="check-list">
          <li><Check size={18} /><span>Un M5Stack CoreS3 con su cable USB‑C de datos.</span></li>
          <li><Check size={18} /><span>Python 3.8 o posterior y la herramienta <code>esptool</code>.</span></li>
          <li><Check size={18} /><span>Opcional: una microSD FAT32 para caché y actualizaciones en segundo plano.</span></li>
        </ul>
        <CodeBlock code="pip install esptool" label="Instala la dependencia" />
        <Note title="Un cable de carga no basta">Si no aparece ningún puerto serie, comprueba primero que tu cable USB‑C transmite datos.</Note>
      </DocSection>
      <DocSection id="flasheo" title="Flasheo rápido">
        <p>Clona el repositorio y ejecuta el instalador. Detecta el puerto, descarga la última release, verifica sus cinco binarios y reinicia el dispositivo.</p>
        <CodeBlock code={`git clone ${repoUrl}.git\ncd sagwa\n./flash.sh`} label="macOS / Linux" />
        <CodeBlock code={`git clone ${repoUrl}.git\ncd sagwa\npython3 flash.py`} label="Windows / cualquier sistema" />
        <ol className="step-list">
          <li><span>1</span><div><strong>Detecta tu CoreS3</strong><p>Busca puertos <code>usbmodem</code>, <code>ttyACM</code> o <code>COM</code>.</p></div></li>
          <li><span>2</span><div><strong>Descarga la release</strong><p>Los binarios no se guardan en el repositorio: viajan con cada release.</p></div></li>
          <li><span>3</span><div><strong>Escribe y reinicia</strong><p>Sagwa aparecerá en pantalla unos segundos después del reinicio.</p></div></li>
        </ol>
      </DocSection>
      <DocSection id="particiones" title="Qué se escribe en la flash">
        <DataTable headers={['Offset', 'Archivo', 'Contenido', 'Tamaño']} rows={flashParts.map((part) => {
          const size = assetSize(latest, part.file)
          return [part.offset, part.file, t(flashContents[part.file] ?? part.label), size ? formatBytes(size, lang) : '—']
        })} />
        <p>{t('Tamaños de la release')} <a href={latest.url} target="_blank" rel="noreferrer">{latest.tag}</a>. {t('Para instalar una versión concreta, usa')} <code>python3 flash.py --release {latest.tag}</code>. {t('Con --app-only o --clips-only actualizas una sola imagen.')}</p>
      </DocSection>
      <NextPage label="Siguiente" title="Aprende a vivir con Sagwa" to="interactions" action="Ver interacciones" />
    </Localize>
  )
}

export function InteractionsPage() {
  return (
    <Localize>
      <PageIntro eyebrow="Comportamiento" title="No la uses. Hazle compañía." description="Sagwa combina sensores, hora, atención y cuatro estados de ánimo para decidir qué hacer. Cada interacción modifica su comportamiento, no solo su animación." />
      <DocSection id="acciones" title="Qué entiende Sagwa">
        <DataTable headers={['Tú…', 'Sagwa…']} rows={[
          ['Acercas la mano a la pantalla', 'Se despierta, se pone contenta y ronronea'],
          ['Mueves la mano de golpe', 'Se asusta y salta'],
          ['La levantas o golpeas la mesa', 'Se pone de pie; una sacudida fuerte la sobresalta'],
          ['Inclinas la mesa más de 20°', 'Se mantiene en equilibrio sobre sus patas'],
          ['Enciendes la luz después de la oscuridad', 'Despierta y, si descansó, se estira'],
          ['Apagas la luz', 'Se prepara para dormir poco después'],
          ['La dejas sola un rato', 'Se acicala, duerme, camina, juega o llama si se aburre'],
          ['Le envías un mensaje', 'Lo anuncia con el maullido adecuado a su urgencia'],
          ['Tocas dos veces', 'Muestra hora, fecha, sensores y último tiempo conocido'],
        ]} />
      </DocSection>
      <DocSection id="poses" title="Cinco poses, movimiento continuo">
        <p>Las 22 animaciones empiezan y terminan sobre poses compartidas: <strong>sleep, loaf, sit, stand y stalk</strong>. El grafo de clips evita fundidos costosos y mantiene la continuidad corporal a 12 fps.</p>
        <div className="pose-doc-strip">{['sleep', 'loaf', 'sit', 'stand', 'stalk'].map((pose) => <figure key={pose}><img src={asset(`media/pose-${pose}.png`)} alt={`Sagwa en pose ${pose}`} loading="lazy" width="140" height="140" /><figcaption>{pose}</figcaption></figure>)}</div>
      </DocSection>
      <DocSection id="animo" title="Cuatro estados de ánimo">
        <div className="mood-list">
          <div><Sparkles size={20} /><strong>Energía</strong><p>Baja mientras está despierta y se recupera al dormir.</p></div>
          <div><Moon size={20} /><strong>Somnolencia</strong><p>Depende de la hora local y de la luz de la habitación.</p></div>
          <div><Hand size={20} /><strong>Aburrimiento</strong><p>Crece sin estímulos; jugar, conversar y acariciarla lo reducen.</p></div>
          <div><Sun size={20} /><strong>Satisfacción</strong><p>Sube con tu atención y disminuye gradualmente.</p></div>
        </div>
        <p>El planificador evita repetir las poses visitadas recientemente. Sus periodos de descanso, juego y acicalamiento cambian con estos valores.</p>
      </DocSection>
      <DocSection id="mensajes" title="Habla gato">
        <div className="sound-types">
          <div><MessageCircle size={19} /><strong>Chat</strong><span>Un <em>mrrow</em> suave durante una conversación.</span></div>
          <div><Wifi size={19} /><strong>Aviso</strong><span>Un chirp breve para una notificación normal.</span></div>
          <div><Music2 size={19} /><strong>Urgente</strong><span>Doble maullido insistente, repetido hasta que la atiendas.</span></div>
        </div>
        <p>Las voces proceden de grabaciones reales. Hay varias tomas por categoría y una pequeña variación de tono para evitar que suenen idénticas.</p>
      </DocSection>
      <NextPage label="A continuación" title="Descubre sus sentidos" to="hardware" action="Abrir laboratorio 3D" />
    </Localize>
  )
}

export function HardwarePage() {
  const [params, setParams] = useSearchParams()
  const hydrated = useHydrated()
  const selectedPart = hydrated ? partById(params.get('part') ?? '')?.id ?? '' : ''
  const setSelectedPart = (id: string) => setParams(id ? { part: id } : {}, { replace: true, preventScrollReset: true })
  const [filter, setFilter] = useState('all')
  const selected = partById(selectedPart)
  const filtered = filter === 'all' ? hardwareParts : hardwareParts.filter((part) => part.category === filter)
  return (
    <Localize>
      <PageIntro eyebrow="Hardware interactivo" title="Conoce el hogar de Sagwa, pieza a pieza." description="Explora el M5Stack CoreS3 y relaciona cada elemento físico con una función real del firmware. Selecciona una pieza en el modelo o en la lista.">
        <div className="page-meta"><span>CAD oficial</span><span>{hardwareParts.length} componentes</span><span>Vista despiezada</span></div>
      </PageIntro>
      <div className="hardware-lab">
        <div className="lab-stage">
          <DeviceViewer variant="lab" selectedPart={selectedPart} onSelectPart={setSelectedPart} />
        </div>
        <aside className="parts-panel" aria-label="Lista de componentes">
          <div className="parts-panel-head"><span>Componentes</span><strong>{filtered.length}</strong></div>
          <div className="part-filters" aria-label="Filtrar componentes">
            {[['all', 'Todo'], ['front', 'Frontal'], ['pcb', 'Placa'], ['frame', 'Puertos'], ['base', 'Base']].map(([id, label]) => <button type="button" key={id} className={filter === id ? 'is-active' : ''} onClick={() => setFilter(id)}>{label}</button>)}
          </div>
          <div className="parts-list">
            {filtered.map((part) => <button type="button" key={part.id} className={selectedPart === part.id ? 'is-active' : ''} onClick={() => setSelectedPart(selectedPart === part.id ? '' : part.id)} aria-pressed={selectedPart === part.id}><span><strong>{part.name}</strong><small>{part.chip}</small></span><ChevronRight size={16} /></button>)}
          </div>
        </aside>
      </div>
      <div className={`part-detail ${selected ? 'is-visible' : ''}`} aria-live="polite">
        {selected ? <>
          <div className="part-detail-heading"><div><span>{selected.category === 'pcb' ? 'Placa principal' : selected.category === 'front' ? 'Frontal' : selected.category === 'base' ? 'Base y energía' : 'Carcasa y conexiones'}</span><h2>{selected.name}</h2></div><code>{selected.chip}</code></div>
          <div className="part-detail-grid"><div><h3>Qué es</h3><p>{selected.detail}</p></div><div><h3>Qué hace en Sagwa</h3><p>{selected.sagwa}</p></div></div>
          <ul className="spec-pills">{selected.specs.map((spec) => <li key={spec}>{spec}</li>)}</ul>
        </> : <div className="part-empty"><CircuitBoard size={25} /><div><strong>Elige un componente</strong><p>Podrás enfocarlo, aislarlo y leer su relación con Sagwa.</p></div></div>}
      </div>
      <Note title="Precisión del modelo">La carcasa, los puertos y la base proceden del CAD oficial. Los circuitos internos se representan como un modelo documental simplificado para explicar su función; no es un plano de fabricación ni una reproducción de cada resistencia.</Note>
      <DocSection id="especificaciones" title="Especificaciones esenciales">
        <DataTable headers={['Subsistema', 'Componente', 'Especificación']} rows={[
          ['Procesador', 'ESP32-S3FN8', 'Doble núcleo LX7 @ 240 MHz · 512 KB SRAM'],
          ['Memoria', 'Flash + PSRAM', '16 MB SPI Flash · 8 MB Octal PSRAM'],
          ['Pantalla', 'ILI9342C / E', '2,0″ IPS · 320 × 240 · RGB565'],
          ['Entrada', 'FT6336U', 'Táctil capacitivo · I²C 0x38'],
          ['Sensores', 'LTR-553 + BMI270 + BMM150', 'Luz/proximidad · 6 ejes · magnetómetro'],
          ['Audio', 'AW88298 + altavoz', '16 bits I²S · 1 W'],
          ['Energía', 'AXP2101 + BM8563', 'PMIC · RTC con respaldo'],
          ['Dimensiones', 'CoreS3 / conjunto', '54 × 54 × 15,5 mm / 69 × 54 × 31,5 mm'],
        ]} />
      </DocSection>
      <DocSection id="i2c" title="Mapa I²C interno">
        <p>El bus del sistema funciona a 400 kHz: <code>SDA GPIO12</code> y <code>SCL GPIO11</code>.</p>
        <DataTable headers={['Dirección', 'Dispositivo', 'Función en Sagwa']} rows={[
          ['0x23', 'LTR-553ALS-WA', 'Caricias por proximidad y brillo automático'],
          ['0x69', 'BMI270', 'Despertar, rotación, inclinación y temperatura'],
          ['0x10', 'BMM150', 'Magnetómetro auxiliar'],
          ['0x34', 'AXP2101', 'Energía, batería y retroiluminación'],
          ['0x38', 'FT6336U', 'Entrada táctil'],
          ['0x51', 'BM8563', 'Reloj en tiempo real'],
        ]} />
        <a className="text-link" href="https://docs.m5stack.com/en/core/CoreS3" target="_blank" rel="noreferrer">Consultar la documentación oficial del CoreS3 <ExternalLink size={15} /></a>
      </DocSection>
    </Localize>
  )
}

export function ArchitecturePage() {
  const { t } = useTranslation()
  const lang = useLang()
  return (
    <Localize>
      <PageIntro eyebrow="Arquitectura del firmware" title="Una gata construida como un sistema vivo." description="Sensores, comportamiento, animación y sonido funcionan como subsistemas separados sobre ESP-IDF y FreeRTOS. Muse añade mensajería y voz mediante una conexión cifrada." />
      <DocSection id="flujo" title="Del estímulo a la reacción">
        <div className="architecture-flow" aria-label="Flujo de arquitectura">
          <div><span>Sentidos</span><strong>Luz · proximidad<br />IMU · tacto · RTC</strong></div><ArrowRight aria-hidden="true" />
          <div><span>Decisión</span><strong>Ánimos<br />y planificador</strong></div><ArrowRight aria-hidden="true" />
          <div><span>Expresión</span><strong>Grafo de clips<br />+ motor de audio</strong></div><ArrowRight aria-hidden="true" />
          <div><span>Presencia</span><strong>LCD 320 × 240<br />+ altavoz</strong></div>
        </div>
        <p>Muse Home Link se conecta por BLE o Wi‑Fi con sesiones Noise cifradas para mensajes, respuestas e imágenes. Las funciones cotidianas de comportamiento permanecen en el dispositivo.</p>
      </DocSection>
      <DocSection id="animacion" title="Animación sin fundidos">
        <p>Cada clip es una arista entre dos poses hub. Como comparte el primer y último dibujo con los clips vecinos, Sagwa puede encadenar acciones sin mezclas alfa ni fotogramas completos en RAM.</p>
        <div className="feature-lines">
          <div><strong>SGWC</strong><p>Cabecera de 24 bytes, tabla de 22 clips, tabla de fotogramas y JPEG baseline de 240 × 208.</p></div>
          <div><strong>12 fps</strong><p>El decodificador TJpgDec de la ROM del ESP32-S3 trabaja en el núcleo 0.</p></div>
          <div><strong>&lt; 60 KB</strong><p>Las líneas RGB565 se transmiten a buffers LVGL sin guardar un fotograma completo en DRAM.</p></div>
        </div>
        <CodeBlock code={`SGWC header → clip table[22] → frame table → JPEG data\n\nwidth=240  height=208  fps=12  version=1`} label="Estructura del paquete" />
      </DocSection>
      <DocSection id="flash" title="Mapa de flash de 16 MB">
        <DataTable headers={['Partición', 'Offset', 'Tamaño', 'Uso']} rows={partitions.filter((part) => partitionUses[part.name]).map((part) => [
          part.name, part.offset, formatBytes(part.size, lang), t(partitionUses[part.name]),
        ])} />
      </DocSection>
      <DocSection id="muse" title="Muse Home Link">
        <div className="security-callout"><ShieldCheck size={28} /><div><strong>Sesiones cifradas, control explícito</strong><p>El emparejamiento se confirma físicamente en el gadget. El token SDK se guarda en NVS; revócalo y vuelve a emparejar si cambias uno ya registrado.</p></div></div>
        <p>Las respuestas de Muse llegan como texto y aparecen en pantalla. El audio de Sagwa y su lenguaje corporal se eligen según el tipo de mensaje.</p>
      </DocSection>
      <NextPage label="Profundiza" title="Compila y modifica el firmware" to="development" action="Ir a desarrollo" />
    </Localize>
  )
}

export function UpdatesPage() {
  return (
    <Localize>
      <PageIntro eyebrow="Actualizaciones OTA" title="Crece sin volver al cable." description="Después de la primera instalación, Sagwa comprueba las releases de GitHub, verifica firmware y animaciones, y conserva una ruta de recuperación." />
      <DocSection id="proceso" title="Una actualización con red de seguridad">
        <ol className="ota-timeline">
          <li><span>1</span><div><strong>Publicación</strong><p>GitHub Actions compila, firma y publica binarios y un manifiesto con versión, tamaño y SHA‑256.</p></div></li>
          <li><span>2</span><div><strong>Comprobación</strong><p>Dos minutos después de conectar Wi‑Fi y luego cada 180 minutos, con una pequeña variación.</p></div></li>
          <li><span>3</span><div><strong>Momento tranquilo</strong><p>Espera dos minutos sin interacción y, si usa batería, más del 30 % de carga.</p></div></li>
          <li><span>4</span><div><strong>Aviso</strong><p>Muestra las novedades durante 30 s. Tócala para posponer la actualización 10 minutos.</p></div></li>
          <li><span>5</span><div><strong>Verificación</strong><p>Comprueba SHA‑256 de las animaciones y la firma del firmware antes de reiniciar.</p></div></li>
          <li><span>6</span><div><strong>Confirmación o rollback</strong><p>La nueva imagen debe volver a conectar; si no lo logra en cinco minutos, el bootloader revierte.</p></div></li>
        </ol>
      </DocSection>
      <DocSection id="configuracion" title="Ajustes del actualizador">
        <CodeBlock code={`CONFIG_SAGWA_AUTO_UPDATE=y\nCONFIG_SAGWA_UPDATE_INTERVAL_MIN=180\nCONFIG_SAGWA_UPDATE_REPO=terrerovgh/sagwa\nCONFIG_SAGWA_TZ=CET-1CEST,M3.5.0,M10.5.0/3`} label="ESP32 Device SDK · menuconfig" />
        <p>Para buscar ahora, abre <strong>Settings → Sagwa → Check for updates</strong>. Esta acción omite la espera de momento tranquilo y la cuenta atrás.</p>
        <Note title="Tu propia clave de firma">Si configuras <code>SAGWA_SIGNING_KEY</code>, primero flashea manualmente una build creada con esa clave. El dispositivo solo acepta imágenes firmadas con la clave de su firmware.</Note>
      </DocSection>
      <DocSection id="microsd" title="Actualizar mientras juega">
        <p>Con una microSD FAT32, el paquete de animaciones se descarga y se verifica en <code>/sagwa/update/</code> antes de copiarlo a flash. Sagwa puede seguir reproduciendo su paquete actual durante la descarga.</p>
      </DocSection>
      <NextPage label="¿Algo no va bien?" title="Diagnóstico y recuperación" to="troubleshooting" action="Solucionar problemas" />
    </Localize>
  )
}

export function DevelopmentPage() {
  return (
    <Localize>
      <PageIntro eyebrow="Desarrollo" title="Abre el capó. Hazla tuya." description="Sagwa se basa en el Muse Gadgets SDK y ESP-IDF v6.0.1. Puedes modificar firmware, sensores, interfaz, sonidos o el paquete de animación." />
      <DocSection id="entorno" title="Compilar desde el código">
        <Note tone="warning" title="Versión exacta">El proyecto documenta ESP-IDF v6.0.1. Otras versiones no están soportadas por esta guía.</Note>
        <CodeBlock code={`. $IDF_PATH/export.sh\ncd muse-gadget-sdk/esp32\ntools/muse/board.sh build cores3\ntools/muse/board.sh flash cores3`} label="Build y flash para CoreS3" />
      </DocSection>
      <DocSection id="recursos" title="Regenerar animaciones y sonidos">
        <CodeBlock code={`python3 sagwa/tools/pack_clips.py\npython3 sagwa/tools/make_sfx.py\n\n# Escuchar cada toma generada\npython3 sagwa/tools/make_sfx.py --wav ./wav`} label="Pipeline de recursos" />
        <p>El empaquetador recorta los vídeos, los reduce a 12 fps, ajusta los límites a las poses hub y escribe JPEG baseline dentro de <code>sagwa_clips.bin</code>.</p>
      </DocSection>
      <DocSection id="estructura" title="Dónde tocar cada cosa">
        <CodeBlock code={`sagwa/\n├── flash.py, flash.sh\n├── sagwa/\n│   ├── clips/             # 22 clips fuente\n│   ├── hubs/              # poses compartidas\n│   ├── sounds/            # grabaciones originales\n│   └── tools/             # pack, SFX y release\n├── muse-gadget-sdk/esp32/\n│   ├── main/              # app, Wi-Fi, OTA\n│   └── components/muse/   # Sagwa, sensores y UI\n└── .github/workflows/     # build, firma y release`} label="Repositorio" />
      </DocSection>
      <DocSection id="extender" title="Extender el sistema">
        <div className="feature-lines">
          <div><strong>Nuevos comandos</strong><p>Anúncialos con <code>link.register</code> y gestiónalos en <code>main/app.c</code>.</p></div>
          <div><strong>Nuevas respuestas</strong><p>El clasificador está en <code>sagwa_notify.c</code>; incluye pruebas ejecutables en el host.</p></div>
          <div><strong>Nuevas funciones locales</strong><p>Consulta <code>sensors.read</code> como ejemplo de telemetría JSON‑RPC.</p></div>
        </div>
        <a className="text-link" href={repoUrl} target="_blank" rel="noreferrer">Abrir el repositorio y sus pruebas <ExternalLink size={15} /></a>
      </DocSection>
    </Localize>
  )
}

const issues: { title: string; body: ReactNode }[] = [
  { title: 'No aparece ningún puerto serie', body: <><p>Prueba otro cable USB‑C de datos. En macOS busca <code>/dev/cu.usbmodem*</code>; en Linux, <code>/dev/ttyACM0</code> o <code>/dev/ttyUSB0</code>; en Windows, un puerto <code>COM</code>.</p><CodeBlock code="sudo usermod -aG dialout $USER" label="Linux · permisos; vuelve a iniciar sesión después" /></> },
  { title: 'El flasheo no entra en modo descarga', body: <ol><li>Desconecta el USB.</li><li>Mantén pulsado RESET/POWER en la parte inferior.</li><li>Conecta el USB, espera dos segundos y suelta.</li><li>Ejecuta de nuevo <code>./flash.sh</code>.</li></ol> },
  { title: 'La pantalla queda negra al primer arranque', body: <><p>Comprueba que escribiste las cinco imágenes, especialmente <code>sagwa_clips.bin</code> en <code>{clipsOffset}</code>. Sin ese paquete, el firmware vuelve al modo de poses anterior.</p><CodeBlock code="python3 -m serial.tools.miniterm <PORT> 115200" label="Registro serie" /><p>Un arranque sano muestra <code>Muse Gadget starting</code> e inicializa LTR‑553 y BMI270.</p></> },
  { title: 'La microSD no aparece o no actualiza', body: <p>Formatea la tarjeta como FAT32; exFAT y NTFS no son compatibles con este flujo de ESP‑IDF. En <strong>Settings → Sagwa</strong> puedes ver el espacio libre o el aviso «Not FAT32».</p> },
  { title: 'Una actualización no termina de arrancar', body: <p>La imagen debe confirmar que vuelve a alcanzar GitHub o la conexión de control de Muse. Si no lo hace en cinco minutos, el bootloader restaura la ranura anterior. Revisa Wi‑Fi, el repositorio configurado y la clave de firma antes de reintentar.</p> },
]

export function TroubleshootingPage() {
  const { t } = useTranslation()
  return (
    <Localize>
      <PageIntro eyebrow="Diagnóstico" title="Primero respira. Después revisa el cable." description="Las incidencias más habituales tienen una ruta de comprobación corta. Empieza por la alimentación, el cable y el registro serie antes de reescribir la flash." />
      <DocSection id="faq" title="Problemas frecuentes">
        <div className="faq-list">{issues.map((issue, index) => <details key={issue.title} open={index === 0}><summary>{issue.title}<ChevronRight size={18} /></summary><div className="faq-body">{issue.body}</div></details>)}</div>
      </DocSection>
      <DocSection id="recuperacion" title="Volver a una release conocida">
        <CodeBlock code={`python3 flash.py --release ${latest.tag}\n\n# ${t('Si solo quieres restaurar la aplicación')}\npython3 flash.py --release ${latest.tag} --app-only`} label="Reinstalar una versión" />
        <Note tone="warning" title="Riesgo del firmware personalizado">Flashear firmware de terceros puede invalidar la garantía o dejar la placa inutilizable. Conserva una release conocida y no interrumpas la escritura.</Note>
      </DocSection>
      <NextPage label="¿Todo funcionando?" title="Vuelve a disfrutar de Sagwa" to="interactions" action="Ver interacciones" />
    </Localize>
  )
}
