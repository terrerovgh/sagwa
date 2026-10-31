import { ArrowRight, FileQuestion } from 'lucide-react'
import { PageIntro } from '../components/DocsBlocks'
import Localize from '../components/Localize'
import PageLink from '../components/PageLink'

export default function NotFound() {
  return (
    <Localize>
      <PageIntro eyebrow="Error 404" title="Esta página se ha escondido." description="Quizá se movió cuando la documentación pasó a tener una dirección por página. Empieza por el inicio o busca lo que necesitas con la tecla /." />
      <div className="not-found-actions">
        <PageLink to="overview" className="button button--primary">Ir al inicio <ArrowRight size={18} /></PageLink>
        <PageLink to="troubleshooting" className="button button--secondary"><FileQuestion size={18} /> Solución de problemas</PageLink>
      </div>
    </Localize>
  )
}
