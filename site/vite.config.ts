import path from 'path'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// Where the site is published. Vite serves and builds it under that URL's path, so the dev server,
// `vite preview` and GitHub Pages (https://sagwa.dev) all use the same links.
const siteUrl = (process.env.SITE_URL ?? 'https://sagwa.dev').replace(/\/$/, '')

// https://vite.dev/config/
export default defineConfig({
  base: `${new URL(siteUrl).pathname.replace(/\/$/, '')}/`,
  plugins: [react()],
  define: {
    'import.meta.env.VITE_SITE_URL': JSON.stringify(siteUrl),
  },
  server: {
    port: 3000,
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: (id) => (id.includes('node_modules/three') ? 'three' : undefined),
      },
    },
  },
  ssr: {
    noExternal: ['lucide-react'],
  },
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
});
