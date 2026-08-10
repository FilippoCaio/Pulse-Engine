// ─── Keyboard state tracker ───
export class Input {
  constructor() {
    this.down = new Set();
    this.pressed = new Set(); // pressed this frame
    window.addEventListener('keydown', (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
      if (!this.down.has(e.code)) this.pressed.add(e.code);
      this.down.add(e.code);
    });
    window.addEventListener('keyup', (e) => this.down.delete(e.code));
    window.addEventListener('blur', () => { this.down.clear(); this.pressed.clear(); });
  }
  isDown(code) { return this.down.has(code); }
  wasPressed(code) { return this.pressed.has(code); }
  endFrame() { this.pressed.clear(); }
}

// human-friendly key list for blueprint dropdowns
export const KEY_OPTIONS = [
  ['Space', 'Spazio'], ['ArrowUp', 'Freccia Su'], ['ArrowDown', 'Freccia Giù'],
  ['ArrowLeft', 'Freccia Sx'], ['ArrowRight', 'Freccia Dx'],
  ['KeyW', 'W'], ['KeyA', 'A'], ['KeyS', 'S'], ['KeyD', 'D'],
  ['KeyQ', 'Q'], ['KeyE', 'E'], ['KeyR', 'R'], ['KeyT', 'T'],
  ['Enter', 'Invio'], ['ShiftLeft', 'Shift'],
];
