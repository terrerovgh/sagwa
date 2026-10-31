import { ArrowRight, BookOpen, Box, Code2, FileQuestion, History, MessageCircleMore, RefreshCw, Wrench, type LucideIcon } from 'lucide-react'
import { hardwareParts } from './hardware'
import type { PageId } from '../routes'

export const navGroups: { label: string; items: { id: PageId; icon: LucideIcon }[] }[] = [
  { label: 'Empezar', items: [
    { id: 'overview', icon: BookOpen },
    { id: 'install', icon: ArrowRight },
    { id: 'interactions', icon: MessageCircleMore },
  ] },
  { label: 'Dispositivo', items: [
    { id: 'hardware', icon: Box },
    { id: 'architecture', icon: Code2 },
    { id: 'updates', icon: RefreshCw },
  ] },
  { label: 'Referencia', items: [
    { id: 'changelog', icon: History },
    { id: 'development', icon: Wrench },
    { id: 'troubleshooting', icon: FileQuestion },
  ] },
]

export type SearchItem = { title: string; description: string; page: PageId; keywords: string; part?: string; hash?: string }

// Titles and descriptions are Spanish i18n keys; matching ignores accents and checks both languages.
export const searchItems: SearchItem[] = [
  { title: 'Instalación rápida', description: 'Requisitos, flasheo y cinco imágenes', page: 'install', hash: 'flasheo', keywords: 'flash python esptool usb cable instalar bin bootloader install' },
  { title: 'Particiones de instalación', description: 'Offsets de bootloader, firmware y clips', page: 'install', hash: 'particiones', keywords: '0x830000 0x020000 sagwa_clips particion flash offsets' },
  { title: 'Interacciones y gestos', description: 'Caricias, doble toque, movimiento y luz', page: 'interactions', hash: 'acciones', keywords: 'acariciar tocar mano inclinar despertar dormir pet touch' },
  { title: 'Estados de ánimo', description: 'Energía, sueño, aburrimiento y satisfacción', page: 'interactions', hash: 'animo', keywords: 'mood energia somnolencia comportamiento' },
  { title: 'Mensajes y sonidos', description: 'Chat, avisos y maullidos urgentes', page: 'interactions', hash: 'mensajes', keywords: 'ronroneo meow trino audio notificacion muse sound' },
  { title: 'Laboratorio de hardware 3D', description: 'Modelo interactivo y vista despiezada', page: 'hardware', keywords: 'cores3 cad componentes placa carcasa 3d model' },
  { title: 'Mapa I²C', description: 'Direcciones y funciones de los sensores', page: 'hardware', hash: 'i2c', keywords: 'gpio 12 11 0x23 0x69 0x34 0x51 pinout i2c' },
  { title: 'Arquitectura del firmware', description: 'Sensores, animación, LVGL y Muse', page: 'architecture', keywords: 'freertos esp-idf tjpgdec lvgl noise ble wifi' },
  { title: 'Formato SGWC', description: 'Paquete de animación y grafo de poses', page: 'architecture', hash: 'animacion', keywords: 'jpeg 12 fps clips hub memoria 60 kb' },
  { title: 'Actualizaciones OTA', description: 'Manifiesto, firma, SHA-256 y rollback', page: 'updates', keywords: 'github release wifi actualizacion automatica microsd ota update' },
  { title: 'Historial de versiones', description: 'Notas, binarios y SHA-256 de cada release', page: 'changelog', keywords: 'release version changelog novedades descargas binarios download sha256' },
  { title: 'Compilar desde el código', description: 'ESP-IDF v6.0.1 y board.sh', page: 'development', hash: 'entorno', keywords: 'build compilar cores3 sdk source' },
  { title: 'Regenerar animaciones y sonidos', description: 'pack_clips.py y make_sfx.py', page: 'development', hash: 'recursos', keywords: 'recursos clips sounds herramientas' },
  { title: 'Solución de problemas', description: 'Puerto serie, pantalla negra y recuperación', page: 'troubleshooting', keywords: 'error fallo boot descarga dialout fat32' },
  ...hardwareParts.map((part) => ({ title: part.name, description: `${part.chip} · ${part.summary}`, page: 'hardware' as PageId, keywords: `${part.detail} ${part.sagwa}`, part: part.id })),
]
