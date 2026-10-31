export type HardwarePart = {
  id: string
  name: string
  chip: string
  category: 'front' | 'pcb' | 'frame' | 'base'
  summary: string
  detail: string
  sagwa: string
  specs: string[]
  meshes: string[]
}

export const hardwareParts: HardwarePart[] = [
  {
    id: 'display', name: 'Pantalla IPS', chip: 'ILI9342C / ILI9342E', category: 'front',
    summary: 'La ventana al mundo de Sagwa.',
    detail: 'Panel IPS de 2,0 pulgadas, 320 × 240 píxeles y color RGB565. Recibe los fotogramas por SPI con DMA.',
    sagwa: 'Muestra las 22 animaciones, las respuestas de Muse, los ajustes y la tarjeta de información sin conexión.',
    specs: ['2,0″ · 320 × 240', 'SPI DMA', 'Retroiluminación regulada por AXP2101'],
    meshes: ['display_panel', 'glass'],
  },
  {
    id: 'touch', name: 'Panel táctil', chip: 'FT6336U', category: 'front',
    summary: 'Caricias, toques y control directo.',
    detail: 'Controlador táctil capacitivo conectado al bus I²C interno en la dirección 0x38.',
    sagwa: 'Acaríciala, pospón una actualización, abre ajustes o haz doble toque para consultar hora, sensores y tiempo.',
    specs: ['I²C 0x38', 'SDA GPIO12 · SCL GPIO11', 'Entrada capacitiva'],
    meshes: ['glass', 'display_panel'],
  },
  {
    id: 'camera', name: 'Cámara', chip: 'GC0308', category: 'front',
    summary: 'Un sensor CMOS en el marco inferior.',
    detail: 'Cámara de 0,3 MP integrada junto al sensor de proximidad en el frontal del CoreS3.',
    sagwa: 'Forma parte de las capacidades multimedia del dispositivo; el comportamiento cotidiano de Sagwa no depende de grabarte.',
    specs: ['0,3 MP', 'SCCB / I²C 0x21', 'Anillo de cobre y lente visible'],
    meshes: ['cam_ring', 'cam_lens', 'cam_pupil'],
  },
  {
    id: 'proximity', name: 'Luz y proximidad', chip: 'LTR-553ALS-WA', category: 'front',
    summary: 'Los ojos que notan tu mano y la habitación.',
    detail: 'Sensor digital de luz ambiental y proximidad. El firmware lo lee a 10 Hz mediante I²C.',
    sagwa: 'Una mano a unos 3–5 cm despierta a Sagwa y provoca ronroneos. La luz regula el brillo y su ciclo de sueño.',
    specs: ['I²C 0x23', 'Proximidad ≥ 180 counts', 'Oscuridad < 5 lux'],
    meshes: ['prox0', 'prox1'],
  },
  {
    id: 'microphones', name: 'Micrófonos duales', chip: 'SPM1423 · ES7210', category: 'front',
    summary: 'Dos pequeñas entradas para tu voz.',
    detail: 'El frontal incorpora dos aberturas de micrófono. El firmware de Sagwa documenta entrada PDM; el CoreS3 integra además el codec ES7210.',
    sagwa: 'Capturan notas de voz para conversar mediante Muse Home Link.',
    specs: ['Matriz dual', 'Audio de voz', 'Conexión cifrada con Muse'],
    meshes: ['mic0', 'mic1', 'mic_ring0', 'mic_ring1'],
  },
  {
    id: 'esp32', name: 'Procesador', chip: 'ESP32-S3FN8', category: 'pcb',
    summary: 'El cerebro que hace todo esto posible.',
    detail: 'SoC Xtensa LX7 de doble núcleo a 240 MHz, con 512 KB de SRAM, Wi‑Fi 2,4 GHz y BLE 5.',
    sagwa: 'Ejecuta comportamiento, animación, audio, sensores, LVGL, red y actualizaciones sin necesidad de una app para las funciones locales.',
    specs: ['2 × Xtensa LX7 @ 240 MHz', '512 KB SRAM', 'Wi‑Fi + BLE 5'],
    meshes: ['pcb_esp32_module', 'pcb_esp32_chip', 'pcb_esp32_label'],
  },
  {
    id: 'memory', name: 'Memoria', chip: '16 MB Flash + 8 MB PSRAM', category: 'pcb',
    summary: 'Espacio para pensar, recordar y moverse.',
    detail: 'La flash aloja el firmware y las particiones de Sagwa; la PSRAM Octal da margen a multimedia e interfaz.',
    sagwa: 'Guarda dos ranuras OTA y el paquete SGWC de animaciones de 7,81 MB. Las estadísticas diarias viven en NVS.',
    specs: ['16 MB SPI Flash', '8 MB Octal PSRAM', 'Doble ranura OTA'],
    meshes: ['pcb_flash', 'pcb_psram', 'pcb_flash_label', 'pcb_psram_label'],
  },
  {
    id: 'imu', name: 'Movimiento', chip: 'BMI270', category: 'pcb',
    summary: 'Seis ejes para sentir cada sacudida.',
    detail: 'IMU con acelerómetro y giroscopio de tres ejes. El firmware lo configura a 100 Hz y 200 Hz respectivamente.',
    sagwa: 'Detecta cuando la levantas, una sacudida fuerte o una inclinación superior a 20° para que Sagwa se ponga de pie y guarde el equilibrio.',
    specs: ['I²C 0x69', '±8 g · ±2000°/s', 'Despertar por movimiento'],
    meshes: ['pcb_bmi270', 'pcb_bmi270_label'],
  },
  {
    id: 'magnetometer', name: 'Magnetómetro', chip: 'BMM150', category: 'pcb',
    summary: 'Orientación geomagnética de tres ejes.',
    detail: 'Sensor auxiliar conectado al bus I²C interno en la dirección 0x10.',
    sagwa: 'Está disponible en el hardware para futuras interacciones; no es uno de los sentidos principales del comportamiento actual.',
    specs: ['3 ejes', 'I²C 0x10', 'Sensor auxiliar'],
    meshes: ['pcb_bmm150', 'pcb_bmm150_label'],
  },
  {
    id: 'pmic', name: 'Gestión de energía', chip: 'AXP2101', category: 'pcb',
    summary: 'Reparte y vigila cada rail de alimentación.',
    detail: 'PMIC programable que gestiona batería, alimentación de periféricos y retroiluminación de la pantalla.',
    sagwa: 'Ajusta el brillo entre el 20 % y el 100 %, informa de la batería y participa en las decisiones de actualización.',
    specs: ['I²C 0x34', 'Control de batería', 'Retroiluminación LCD'],
    meshes: ['pcb_axp2101', 'pcb_axp2101_label'],
  },
  {
    id: 'rtc', name: 'Reloj en tiempo real', chip: 'BM8563', category: 'pcb',
    summary: 'La hora sigue aunque Sagwa se duerma.',
    detail: 'RTC con respaldo de batería, conectado por I²C y sincronizado con SNTP cuando hay conexión.',
    sagwa: 'Conserva fecha y hora tras reiniciar y permite consultarlas sin internet.',
    specs: ['I²C 0x51', 'Respaldo de batería', 'Sincronización SNTP'],
    meshes: ['pcb_bm8563', 'pcb_bm8563_label'],
  },
  {
    id: 'codec', name: 'Codec de audio', chip: 'ES7210', category: 'pcb',
    summary: 'La etapa digital de entrada de audio.',
    detail: 'Codec del CoreS3 para entrada de micrófono dual. Comparte el ecosistema de audio con el amplificador I²S.',
    sagwa: 'Da soporte a la cadena de voz utilizada por la conexión con Muse.',
    specs: ['Entrada dual', 'Bus de audio digital', 'I²C interno'],
    meshes: ['pcb_es7210', 'pcb_es7210_label'],
  },
  {
    id: 'amplifier', name: 'Amplificador', chip: 'AW88298', category: 'pcb',
    summary: 'La voz de Sagwa empieza aquí.',
    detail: 'Amplificador Clase D de 16 bits controlado por I²S. El motor mezcla PCM mono a 16 kHz en bloques de 20 ms.',
    sagwa: 'Reproduce maullidos, trinos, chirridos, bostezos y el ronroneo continuo con variaciones de tono.',
    specs: ['16 bits · I²S', '16 kHz PCM', 'BCLK GPIO34 · DOUT GPIO13'],
    meshes: ['pcb_aw88298', 'pcb_aw88298_label'],
  },
  {
    id: 'speaker', name: 'Altavoz', chip: '1 W', category: 'pcb',
    summary: 'Pequeño altavoz, mucha personalidad.',
    detail: 'Altavoz integrado de 1 W situado dentro de la carcasa y conectado al amplificador AW88298.',
    sagwa: 'Da voz a las grabaciones reales de gato que componen el banco sonoro.',
    specs: ['1 W', 'Salida Clase D', 'Ronroneo mezclado bajo los maullidos'],
    meshes: ['pcb_speaker', 'pcb_speaker_driver'],
  },
  {
    id: 'usbc', name: 'USB‑C', chip: 'USB OTG · Serial/JTAG', category: 'frame',
    summary: 'Energía, datos y primer contacto.',
    detail: 'Puerto lateral nativo USB‑C con OTG y CDC para conectar periféricos y flashear firmware.',
    sagwa: 'El instalador lo utiliza para escribir las cinco imágenes y reiniciar el dispositivo. Necesitas un cable de datos.',
    specs: ['USB OTG / CDC', 'VID 0x303A · PID 0x1001', 'Flasheo a 1,5 Mbps'],
    meshes: ['usbc_shell', 'usbc_back', 'usbc_tongue', 'usbc_pin0', 'usbc_pin1', 'usbc_pin2', 'usbc_pin3', 'usbc_pin4', 'usbc_pin5'],
  },
  {
    id: 'porta', name: 'Puerto A', chip: 'HY2.0-4P · I²C', category: 'frame',
    summary: 'Expansión Grove para futuros sentidos.',
    detail: 'Conector rojo lateral con alimentación y señales I²C. El CoreS3 también expone puertos B y C mediante su base y M‑Bus.',
    sagwa: 'Punto de expansión para nuevos accesorios y sensores en próximas versiones.',
    specs: ['GND · 5 V · G2 · G1', 'HY2.0-4P', 'Ecosistema Grove'],
    meshes: ['portA_top', 'portA_bot', 'portA_l', 'portA_r', 'portA_key'],
  },
  {
    id: 'microsd', name: 'microSD', chip: 'FAT32', category: 'pcb',
    summary: 'Almacenamiento opcional para no interrumpir el juego.',
    detail: 'Ranura microSD que comparte el bus SPI de la pantalla. ESP‑IDF admite aquí tarjetas FAT32.',
    sagwa: 'Guarda caché meteorológica y descarga actualizaciones de animaciones mientras Sagwa sigue moviéndose.',
    specs: ['FAT32', '/sdcard/sagwa/cache/', '/sdcard/sagwa/update/'],
    meshes: ['pcb_microsd'],
  },
  {
    id: 'battery', name: 'Batería', chip: '500 mAh · DinBase', category: 'base',
    summary: 'Autonomía dentro de la base.',
    detail: 'La base DIN del kit integra una batería de 3,7 V y 500 mAh, además de entrada DC de 9–24 V.',
    sagwa: 'Con batería, Sagwa espera a tener más del 30 % de carga antes de iniciar ciertas actualizaciones.',
    specs: ['3,7 V · 500 mAh', 'Entrada DC 9–24 V', 'Gestión mediante AXP2101'],
    meshes: ['pcb_battery', 'pcb_battery_label'],
  },
  {
    id: 'dinbase', name: 'Base DIN', chip: 'Montaje modular', category: 'base',
    summary: 'La estructura que completa el CoreS3.',
    detail: 'Base con clip para carril DIN, fijaciones, tornillería de acero e insertos de latón. El conjunto mide 69 × 54 × 31,5 mm.',
    sagwa: 'Le da estabilidad en el escritorio y opciones de montaje para convertirla en una compañera permanente.',
    specs: ['Clip para carril DIN', '4 tornillos M3', '69 × 54 × 31,5 mm'],
    meshes: ['base', 'clip', 'screw0', 'screw1', 'screw2', 'screw3', 'insert0', 'insert1', 'insert_top0', 'insert_top1', 'centre_boss', 'centre_screw', 'centre_cross'],
  },
  {
    id: 'buttons', name: 'Controles físicos', chip: 'POWER · RESET', category: 'frame',
    summary: 'Controles independientes en los laterales.',
    detail: 'Botones de encendido y reinicio. Una pulsación larga de RESET permite entrar en modo de descarga.',
    sagwa: 'Sirven para energía, interacción y recuperación durante el flasheo si el USB no negocia correctamente.',
    specs: ['POWER', 'RESET / descarga', 'Reinicio automático por USB'],
    meshes: ['sw_knob', 'sw_label_a', 'sw_label_b', 'sw_label_c', 'sw_label_d', 'sw_off', 'sw_on'],
  },
]

export const partById = (id: string) => hardwareParts.find((part) => part.id === id)
