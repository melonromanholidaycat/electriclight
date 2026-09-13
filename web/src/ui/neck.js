// Canvas rendering of the neck. Nut at the top, body at the bottom: a guitar
// neck is the one object that fits a phone in portrait.

import { fretDistance, INLAY_FRETS, DOUBLE_INLAY_FRETS } from '../model/geometry.js';

export class NeckView {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d', { alpha: false });
    this.dpr = 1;
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    const w = Math.max(1, Math.round(rect.width * dpr));
    const h = Math.max(1, Math.round(rect.height * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
    this.dpr = dpr;
    this.w = w;
    this.h = h;
  }

  layout(geometry) {
    const padY = this.h * 0.045;
    const lastMm = fretDistance(geometry.frets, geometry.scaleLength);
    const top = padY;
    const bottom = this.h - padY;
    const usable = bottom - top;
    // Longitudinal spacing is the honest part - it is what effects are judged
    // on. Width is drawn a little wider than scale so both strips stay legible
    // on a phone.
    const maxHalf = Math.min(this.w * 0.34, usable * 0.135);
    const nutHalf = maxHalf * (geometry.nutWidth / geometry.heelWidth);
    return {
      top, bottom, lastMm,
      cx: this.w / 2,
      nutHalf,
      heelHalf: maxHalf,
      y: (mm) => top + (mm / lastMm) * usable,
      half: (mm) => nutHalf + (maxHalf - nutHalf) * (mm / lastMm),
    };
  }

  draw(engine, out8) {
    const ctx = this.ctx;
    const g = engine.geometry;
    const L = this.layout(g);

    ctx.fillStyle = '#08090c';
    ctx.fillRect(0, 0, this.w, this.h);

    // Fretboard
    ctx.beginPath();
    ctx.moveTo(L.cx - L.nutHalf, L.top);
    ctx.lineTo(L.cx + L.nutHalf, L.top);
    ctx.lineTo(L.cx + L.heelHalf, L.bottom);
    ctx.lineTo(L.cx - L.heelHalf, L.bottom);
    ctx.closePath();
    const grad = ctx.createLinearGradient(L.cx - L.heelHalf, 0, L.cx + L.heelHalf, 0);
    grad.addColorStop(0, '#241a13');
    grad.addColorStop(0.5, '#3a2a1e');
    grad.addColorStop(1, '#1d1510');
    ctx.fillStyle = grad;
    ctx.fill();

    // Nut
    ctx.fillStyle = '#d8cfc0';
    ctx.fillRect(L.cx - L.nutHalf, L.top - 3 * this.dpr, L.nutHalf * 2, 3 * this.dpr);

    // Frets and inlays
    ctx.lineWidth = Math.max(1, 1.4 * this.dpr);
    for (let n = 1; n <= g.frets; n++) {
      const mm = fretDistance(n, g.scaleLength);
      const y = L.y(mm);
      const half = L.half(mm);
      ctx.strokeStyle = '#9aa0a6';
      ctx.beginPath();
      ctx.moveTo(L.cx - half, y);
      ctx.lineTo(L.cx + half, y);
      ctx.stroke();
    }
    ctx.fillStyle = 'rgba(226,222,214,0.55)';
    const dotR = Math.max(2, 2.6 * this.dpr);
    for (let n = 1; n <= g.frets; n++) {
      const mid = (fretDistance(n - 1, g.scaleLength) + fretDistance(n, g.scaleLength)) / 2;
      const y = L.y(mid);
      const half = L.half(mid);
      if (INLAY_FRETS.includes(n)) {
        ctx.beginPath(); ctx.arc(L.cx, y, dotR, 0, 7); ctx.fill();
      } else if (DOUBLE_INLAY_FRETS.includes(n)) {
        ctx.beginPath(); ctx.arc(L.cx - half * 0.45, y, dotR, 0, 7); ctx.fill();
        ctx.beginPath(); ctx.arc(L.cx + half * 0.45, y, dotR, 0, 7); ctx.fill();
      }
    }

    // LEDs
    const px = engine.pixels;
    const r = Math.max(2.2, Math.min(this.w * 0.016, 5.5 * this.dpr));
    ctx.globalCompositeOperation = 'lighter';
    for (let i = 0; i < px.length; i++) {
      const p = px[i];
      const y = L.y(p.mm);
      const half = L.half(p.mm);
      const x = p.side === 0 ? L.cx - half - r * 1.3 : L.cx + half + r * 1.3;
      const o = i * 3;
      const cr = out8[o], cg = out8[o + 1], cb = out8[o + 2];
      const lum = (cr + cg + cb) / 765;
      if (lum > 0.002) {
        // Halo without per-frame gradients: a few stacked discs are cheaper and
        // hold 60 fps on a phone.
        ctx.fillStyle = `rgba(${cr},${cg},${cb},0.16)`;
        ctx.beginPath(); ctx.arc(x, y, r * (2.4 + lum * 2.6), 0, 7); ctx.fill();
        ctx.fillStyle = `rgba(${cr},${cg},${cb},0.28)`;
        ctx.beginPath(); ctx.arc(x, y, r * 1.7, 0, 7); ctx.fill();
      }
      ctx.fillStyle = `rgb(${Math.max(cr, 6)},${Math.max(cg, 6)},${Math.max(cb, 6)})`;
      ctx.beginPath(); ctx.arc(x, y, r, 0, 7); ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
  }
}
