/**
 * Genera native/icon.ico, il marchio dell'engine.
 *
 * Fino a ieri questo file era una copia esatta dell'icona di Pulse Hub — stessi
 * 8015 byte — e sulla barra delle applicazioni i due programmi erano
 * indistinguibili. Fanno parte della stessa famiglia, quindi il quadrato scuro
 * arrotondato resta; a cambiare sono il segno e il colore, che sono poi le due
 * cose che si leggono a sedici pixel:
 *
 *   Pulse Hub     tre barre impilate, blu     - un elenco, un gestore
 *   Pulse Engine  un'onda che pulsa, ciano    - il battito da cui prende il nome
 *
 * Nessuna dipendenza: il PNG lo comprime zlib, che sta in Node, e un ICO non e'
 * altro che un indice davanti a una fila di PNG.
 *
 *   node make-icon.mjs
 */

import { deflateSync } from 'node:zlib'
import { writeFileSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = dirname(fileURLToPath(import.meta.url))

// Tutto e' disegnato su una griglia di 32 e poi ridotto: le misure qui sotto
// sono le stesse a ogni dimensione richiesta.
const GRID = 32

const BG_TOP = [0x18, 0x26, 0x30]
const BG_BOTTOM = [0x0b, 0x12, 0x19]

/**
 * L'onda: piatta, scatto in alto, ricaduta sotto la linea, ritorno.
 * E' una spezzata e non una curva perche' a sedici pixel una curva diventa
 * una macchia, mentre gli spigoli restano leggibili.
 */
const WAVE = [
  [4.5, 16], [10.5, 16], [14, 7.5], [18, 24.5], [21.5, 14.5], [24, 16], [27.5, 16]
]
const STROKE = 1.85            // mezzo spessore, sulla griglia di 32
const WAVE_TOP = [0x8a, 0xec, 0xff]
const WAVE_BOTTOM = [0x1f, 0x9d, 0xe0]

const SIZES = [16, 24, 32, 48, 64, 128, 256]

/** Punto dentro un rettangolo dagli angoli arrotondati. */
function insideRounded(px, py, x, y, w, h, r) {
  const cx = Math.min(Math.max(px, x + r), x + w - r)
  const cy = Math.min(Math.max(py, y + r), y + h - r)
  const dx = px - cx
  const dy = py - cy
  return dx * dx + dy * dy <= r * r
}

/**
 * Distanza da un segmento. Serve per disegnare la spezzata: un punto piu'
 * vicino di STROKE a uno qualsiasi dei segmenti sta dentro il tratto. Costa
 * niente e da' in regalo gli angoli e le punte arrotondati, che altrimenti
 * andrebbero costruiti a mano.
 */
function distToSegment(px, py, x1, y1, x2, y2) {
  const vx = x2 - x1, vy = y2 - y1
  const len2 = vx * vx + vy * vy
  let t = len2 > 0 ? ((px - x1) * vx + (py - y1) * vy) / len2 : 0
  t = Math.min(Math.max(t, 0), 1)
  const dx = px - (x1 + vx * t), dy = py - (y1 + vy * t)
  return Math.sqrt(dx * dx + dy * dy)
}

function onWave(px, py) {
  for (let i = 0; i < WAVE.length - 1; i++) {
    const [x1, y1] = WAVE[i]
    const [x2, y2] = WAVE[i + 1]
    if (distToSegment(px, py, x1, y1, x2, y2) <= STROKE) return true
  }
  return false
}

function lerp3(a, b, t) {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]
}

/**
 * Disegna l'icona a una data misura.
 *
 * Si campiona a 4x4 per pixel e si media, che e' il modo piu' corto per avere
 * bordi puliti senza scrivere un rasterizzatore. La media va fatta solo sui
 * campioni coperti: dividere per il totale tirerebbe i pixel di bordo verso il
 * nero e l'icona finirebbe con un alone scuro intorno.
 */
function render(size) {
  const S = 4
  const N = size * S
  const acc = new Float64Array(size * size * 4)

  for (let sy = 0; sy < N; sy++) {
    for (let sx = 0; sx < N; sx++) {
      const dx = ((sx + 0.5) / N) * GRID
      const dy = ((sy + 0.5) / N) * GRID

      let color = null
      if (insideRounded(dx, dy, 1, 1, 30, 30, 7)) {
        color = lerp3(BG_TOP, BG_BOTTOM, (dy - 1) / 30)
      }
      // L'onda viene dopo il fondo, e solo dentro il fondo: un tratto che
      // sborda dal quadrato si vedrebbe come uno sbaffo staccato.
      if (color && onWave(dx, dy)) {
        color = lerp3(WAVE_TOP, WAVE_BOTTOM, Math.min(Math.max((dy - 6) / 20, 0), 1))
      }

      if (!color) continue
      const i = (Math.floor(sy / S) * size + Math.floor(sx / S)) * 4
      acc[i] += color[0]
      acc[i + 1] += color[1]
      acc[i + 2] += color[2]
      acc[i + 3] += 255
    }
  }

  const samples = S * S
  const out = Buffer.alloc(size * size * 4)
  for (let i = 0; i < size * size; i++) {
    const a = acc[i * 4 + 3] / samples
    out[i * 4 + 3] = Math.round(a)
    if (a <= 0) continue
    const covered = acc[i * 4 + 3] / 255
    out[i * 4] = Math.round(acc[i * 4] / covered)
    out[i * 4 + 1] = Math.round(acc[i * 4 + 1] / covered)
    out[i * 4 + 2] = Math.round(acc[i * 4 + 2] / covered)
  }
  return out
}

// --- PNG --------------------------------------------------------------------

const CRC_TABLE = (() => {
  const t = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    t[n] = c >>> 0
  }
  return t
})()

function crc32(buf) {
  let c = 0xffffffff
  for (const byte of buf) c = CRC_TABLE[(c ^ byte) & 0xff] ^ (c >>> 8)
  return (c ^ 0xffffffff) >>> 0
}

function chunk(type, data) {
  const len = Buffer.alloc(4)
  len.writeUInt32BE(data.length)
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data])
  const crc = Buffer.alloc(4)
  crc.writeUInt32BE(crc32(body))
  return Buffer.concat([len, body, crc])
}

function encodePng(rgba, size) {
  const ihdr = Buffer.alloc(13)
  ihdr.writeUInt32BE(size, 0)
  ihdr.writeUInt32BE(size, 4)
  ihdr[8] = 8  // bit per canale
  ihdr[9] = 6  // RGBA
  ihdr[10] = 0
  ihdr[11] = 0
  ihdr[12] = 0

  // Ogni riga e' preceduta dal suo byte di filtro; 0 = nessun filtro.
  const stride = size * 4
  const raw = Buffer.alloc(size * (stride + 1))
  for (let y = 0; y < size; y++) {
    raw[y * (stride + 1)] = 0
    rgba.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride)
  }

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0))
  ])
}

// --- ICO --------------------------------------------------------------------

/**
 * Un ICO e' un indice seguito dalle immagini. Da Vista in poi le voci possono
 * essere PNG interi invece di bitmap grezzi, ed e' quello che si fa qui: evita
 * di dover scrivere anche la maschera AND dei formati vecchi.
 */
function encodeIco(images) {
  const header = Buffer.alloc(6)
  header.writeUInt16LE(0, 0)               // riservato
  header.writeUInt16LE(1, 2)               // 1 = icona
  header.writeUInt16LE(images.length, 4)

  const entries = []
  let offset = 6 + 16 * images.length

  for (const { size, png } of images) {
    const e = Buffer.alloc(16)
    e[0] = size >= 256 ? 0 : size          // 0 significa 256
    e[1] = size >= 256 ? 0 : size
    e[2] = 0                               // colori nella tavolozza: nessuna
    e[3] = 0
    e.writeUInt16LE(1, 4)                  // piani
    e.writeUInt16LE(32, 6)                 // bit per pixel
    e.writeUInt32LE(png.length, 8)
    e.writeUInt32LE(offset, 12)
    entries.push(e)
    offset += png.length
  }

  return Buffer.concat([header, ...entries, ...images.map((i) => i.png)])
}

// --- Esecuzione -------------------------------------------------------------

const images = SIZES.map((size) => ({ size, png: encodePng(render(size), size) }))
const ico = encodeIco(images)

writeFileSync(join(HERE, 'icon.ico'), ico)

// Il PNG grande serve a guardare com'e' venuta senza aprire l'eseguibile.
const png256 = images.find((i) => i.size === 256)
writeFileSync(join(HERE, 'icon.png'), png256.png)

console.log(`icon.ico  ${SIZES.join(', ')} px - ${(ico.length / 1024).toFixed(1)} KB`)
console.log(`icon.png  256 px - ${(png256.png.length / 1024).toFixed(1)} KB`)
console.log('Ricordati di rifare icon.res: rc /nologo /fo build\\icon.res icon.rc')
