import { useState } from 'react'
import { ArrowDown, ArrowRight, Box, Check, Github, Hand, Heart, Lightbulb, MessageCircle, Moon, MousePointer2, Music2, ShieldCheck, Sparkles, Wifi } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import DeviceViewer from '../components/DeviceViewer'
import { CodeBlock } from '../components/DocsBlocks'
import Localize from '../components/Localize'
import PageLink from '../components/PageLink'
import { latest, repoUrl } from '../data/release'
import { asset } from '../lib/site'

const poses = [
  { id: 'sleep', name: 'Dormir', title: 'Cuando la habitación se apaga', text: 'La luz ambiental y la hora local alimentan su somnolencia. Al oscurecer, Sagwa busca una postura cómoda y reduce sus sonidos espontáneos.' },
  { id: 'loaf', name: 'Descansar', title: 'Despierta, sin prisa', text: 'La pose loaf es un punto de descanso y escucha. Desde aquí puede dormir, sentarse contigo o levantarse para jugar.' },
  { id: 'sit', name: 'Sentarse', title: 'Contigo, no en segundo plano', text: 'Sentada te escucha, se acicala, piensa, ronronea y conversa. Muchas de sus reacciones más expresivas parten de esta pose.' },
  { id: 'stand', name: 'De pie', title: 'Lista para lo que pase', text: 'Camina, se estira y guarda el equilibrio cuando el CoreS3 se inclina. Una notificación urgente también la pone en alerta.' },
  { id: 'stalk', name: 'Acechar', title: 'Un instinto muy gatuno', text: 'El aburrimiento y la energía pueden convertirse en pequeñas sesiones de juego, con acecho y sonidos de caza.' },
]

export default function Overview() {
  const { t } = useTranslation()
  const [pose, setPose] = useState('sit')
  const activePose = poses.find((item) => item.id === pose) ?? poses[2]
  return (
    <Localize>
      <main id="contenido">
        <section className="hero" id="inicio">
          <div className="hero-copy">
            <div className="hero-kicker"><span className="status-dot" /> {t('Documentación')} {latest.tag} <span>·</span> M5Stack CoreS3</div>
            <h1>Tu escritorio<br />acaba de tener<br /><em>una gata.</em></h1>
            <p>Sagwa vive dentro de un M5Stack CoreS3. Duerme cuando apagas la luz, ronronea si la acaricias y encuentra un maullido distinto para cada mensaje.</p>
            <div className="hero-actions">
              <PageLink to="install" className="button button--primary">Instalar Sagwa <ArrowRight size={18} /></PageLink>
              <PageLink to="hardware" className="button button--secondary"><Box size={18} /> Explorar el hardware</PageLink>
            </div>
            <div className="hero-trust"><span><Check size={15} /> Código abierto</span><span><Check size={15} /> Sin suscripción</span><span><Check size={15} /> Actualizaciones OTA</span></div>
          </div>
          <div className="hero-model">
            <div className="model-caption"><span>M5Stack CoreS3</span><strong>Modelo interactivo</strong></div>
            <DeviceViewer variant="hero" />
            <div className="model-note"><MousePointer2 size={15} /> Gira, acerca y abre el despiece</div>
          </div>
          <a className="hero-scroll" href="#conoce-sagwa"><ArrowDown size={17} /><span>Conócela</span></a>
        </section>

        <section className="quick-facts" aria-label="Datos principales">
          <div><strong>22</strong><span>clips dirigidos</span></div>
          <div><strong>5</strong><span>poses compartidas</span></div>
          <div><strong>12</strong><span>fotogramas / s</span></div>
          <div><strong>16 MB</strong><span>de flash</span></div>
          <div><strong>8 MB</strong><span>de PSRAM</span></div>
        </section>

        <section className="meet-section" id="conoce-sagwa">
          <div className="section-heading">
            <span className="section-label">Más que una pantalla</span>
            <h2>Pequeña en tamaño.<br />Enorme en personalidad.</h2>
            <p>No reproduce un bucle: escucha su entorno, recuerda tu atención y decide qué hacer a continuación.</p>
          </div>
          <div className="personality-grid">
            <article className="feature feature--large">
              <div className="feature-icon"><Heart size={23} /></div>
              <h3>Se alegra de verte</h3>
              <p>Acerca una mano y el sensor de proximidad se convierte en una caricia. Si estaba dormida, despierta; si estaba esperándote, ronronea.</p>
              <PageLink to="interactions" className="inline-button">Descubrir sus sentidos <ArrowRight size={16} /></PageLink>
            </article>
            <article className="feature">
              <div className="feature-icon"><Moon size={23} /></div>
              <h3>Tiene su propio ritmo</h3>
              <p>Energía, sueño, aburrimiento y satisfacción cambian con la luz, la hora y el tiempo que pasáis juntos.</p>
            </article>
            <article className="feature feature--dark">
              <div className="feature-icon"><Music2 size={23} /></div>
              <h3>Habla gato de verdad</h3>
              <p>Maullidos, trinos, bostezos y ronroneos proceden de grabaciones reales, con variaciones para que nunca suene exactamente igual.</p>
              <div className="sound-wave" aria-hidden="true">{[12, 24, 18, 38, 28, 48, 22, 35, 18, 42, 26, 14].map((height, index) => <i key={index} style={{ height }} />)}</div>
            </article>
            <article className="feature">
              <div className="feature-icon"><Wifi size={23} /></div>
              <h3>Conecta cuando hace falta</h3>
              <p>Muse Home Link añade voz y mensajes por BLE o Wi‑Fi. La hora, las animaciones y sus reflejos siguen estando en el dispositivo.</p>
            </article>
          </div>
        </section>

        <section className="pose-section">
          <div className="pose-copy">
            <span className="section-label">Un cuerpo, cinco puntos de encuentro</span>
            <h2>{activePose.title}</h2>
            <p>{activePose.text}</p>
            <div className="pose-selector" role="tablist" aria-label="Poses de Sagwa">
              {poses.map((item) => <button type="button" role="tab" aria-selected={pose === item.id} className={pose === item.id ? 'is-active' : ''} key={item.id} onClick={() => setPose(item.id)}>{item.name}</button>)}
            </div>
          </div>
          <div className="pose-visual" role="tabpanel">
            <div className="pose-orbit" aria-hidden="true" />
            <img key={activePose.id} src={asset(`media/pose-${activePose.id}.png`)} alt={`Sagwa: ${t(activePose.title)}`} width="460" height="460" loading="lazy" />
            <span className="pose-caption">Pose hub · {activePose.id}</span>
          </div>
        </section>

        <section className="install-preview">
          <div className="install-copy">
            <span className="section-label">Del repositorio a tu mesa</span>
            <h2>Instálala antes de que eche de menos compañía.</h2>
            <p>Solo necesitas un CoreS3, un cable USB‑C de datos y Python. El instalador descarga la release, escribe las cinco imágenes y reinicia la placa.</p>
            <ul><li><Check size={17} />Sin compilación local</li><li><Check size={17} />Binarios firmados</li><li><Check size={17} />Actualizaciones posteriores por Wi‑Fi</li></ul>
            <PageLink to="install" className="button button--light">Abrir la guía completa <ArrowRight size={18} /></PageLink>
          </div>
          <div className="install-code"><CodeBlock label="macOS / Linux" code={`git clone ${repoUrl}.git\ncd sagwa\n./flash.sh`} /><div className="code-foot"><span className="status-dot" /> Sagwa despierta tras el reinicio</div></div>
        </section>

        <section className="hardware-preview">
          <div className="hardware-preview-top">
            <div><span className="section-label">Hardware que puedes tocar</span><h2>Cada pieza tiene<br />algo que contar.</h2></div>
            <p>Explora el CAD del CoreS3, abre su despiece y descubre cómo sus sensores se convierten en los sentidos de Sagwa.</p>
          </div>
          <div className="hardware-map">
            <PageLink to="hardware" query="part=proximity"><Lightbulb size={22} /><span>LTR‑553</span><strong>Luz y proximidad</strong><ArrowRight size={18} /></PageLink>
            <PageLink to="hardware" query="part=touch"><Hand size={22} /><span>FT6336U</span><strong>Tacto capacitivo</strong><ArrowRight size={18} /></PageLink>
            <PageLink to="hardware" query="part=imu"><Sparkles size={22} /><span>BMI270</span><strong>Movimiento de 6 ejes</strong><ArrowRight size={18} /></PageLink>
            <PageLink to="hardware" query="part=amplifier"><MessageCircle size={22} /><span>AW88298</span><strong>Voz de 1 W</strong><ArrowRight size={18} /></PageLink>
          </div>
          <PageLink to="hardware" className="button button--secondary hardware-cta"><Box size={18} /> Entrar al laboratorio 3D</PageLink>
        </section>

        <section className="open-section">
          <div><ShieldCheck size={28} /><span>Apache 2.0</span></div>
          <h2>Hecha para vivir contigo.<br />Abierta para crecer contigo.</h2>
          <p>Consulta el firmware, adapta sus animaciones o crea una función nueva. La documentación está organizada para evolucionar con cada release.</p>
          <div className="open-actions"><a className="button button--primary" href={repoUrl} target="_blank" rel="noreferrer"><Github size={18} /> Ver en GitHub</a><PageLink to="development" className="button button--secondary">Empezar a desarrollar <ArrowRight size={18} /></PageLink></div>
        </section>
      </main>
    </Localize>
  )
}
