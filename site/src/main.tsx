import { StrictMode } from 'react'
import { createRoot, hydrateRoot } from 'react-dom/client'
import { BrowserRouter } from 'react-router'
import '@fontsource/fraunces/latin-600.css'
import '@fontsource/fraunces/latin-600-italic.css'
import '@fontsource/fraunces/latin-700.css'
import '@fontsource/ibm-plex-sans/latin-400.css'
import '@fontsource/ibm-plex-sans/latin-500.css'
import '@fontsource/ibm-plex-sans/latin-600.css'
import '@fontsource/ibm-plex-mono/latin-400.css'
import './index.css'
import App from './App.tsx'

const container = document.getElementById('root')!
const app = (
  <StrictMode>
    <BrowserRouter basename={import.meta.env.BASE_URL.replace(/\/$/, '')}>
      <App />
    </BrowserRouter>
  </StrictMode>
)

// Prerendered pages hydrate their HTML; the dev server and 404.html render from scratch.
if (container.hasAttribute('data-prerendered')) hydrateRoot(container, app)
else createRoot(container).render(app)
