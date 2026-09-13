// The two physical controls, modelled here so the no-phone playing experience
// gets designed rather than retrofitted. Everything the guitar can do without
// a phone has to be reachable from exactly these.

export class Knob {
  constructor(el, { value = 1, onChange } = {}) {
    this.el = el;
    this.value = value;
    this.onChange = onChange;
    this.el.innerHTML = `
      <div class="knob-dial"><div class="knob-pointer"></div></div>
      <div class="knob-label"></div>`;
    this.dial = el.querySelector('.knob-dial');
    this.label = el.querySelector('.knob-label');
    this.el.addEventListener('pointerdown', (e) => this.start(e));
    this.el.addEventListener('dblclick', () => this.set(1));
    this.render();
  }

  start(e) {
    e.preventDefault();
    this.el.setPointerCapture(e.pointerId);
    const startY = e.clientY;
    const startV = this.value;
    const range = 180; // px of drag for the full sweep
    const move = (ev) => this.set(startV + (startY - ev.clientY) / range);
    const up = () => {
      this.el.removeEventListener('pointermove', move);
      this.el.removeEventListener('pointerup', up);
      this.el.removeEventListener('pointercancel', up);
    };
    this.el.addEventListener('pointermove', move);
    this.el.addEventListener('pointerup', up);
    this.el.addEventListener('pointercancel', up);
  }

  set(v, quiet = false) {
    const next = Math.min(1, Math.max(0, v));
    if (next === this.value) return;
    this.value = next;
    this.render();
    if (!quiet) this.onChange?.(next);
  }

  setLabel(text) { this.label.textContent = text; }

  render() {
    // Real pots sweep about 270 degrees; matching that keeps the feel honest.
    const deg = -135 + this.value * 270;
    this.dial.style.setProperty('--angle', `${deg}deg`);
    this.dial.dataset.value = Math.round(this.value * 100);
  }
}

export class FiveWay {
  constructor(el, { value = 0, onChange } = {}) {
    this.el = el;
    this.value = value;
    this.onChange = onChange;
    this.el.innerHTML = `<div class="sw-track"></div><div class="sw-positions"></div>`;
    this.positions = el.querySelector('.sw-positions');
    this.buttons = [];
    for (let i = 0; i < 5; i++) {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'sw-pos';
      b.innerHTML = `<span class="sw-num">${i + 1}</span><span class="sw-name"></span>`;
      b.addEventListener('click', () => this.set(i));
      this.positions.appendChild(b);
      this.buttons.push(b);
    }
    this.render();
  }

  set(v, quiet = false) {
    this.value = Math.min(4, Math.max(0, v | 0));
    this.render();
    if (!quiet) this.onChange?.(this.value);
  }

  setNames(names) {
    this.buttons.forEach((b, i) => {
      b.querySelector('.sw-name').textContent = names[i] || '--';
    });
  }

  render() {
    this.buttons.forEach((b, i) => b.classList.toggle('on', i === this.value));
  }
}
