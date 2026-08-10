// ─── Impulso Engine bootstrap ───
import { App } from './editor/app.js';

try {
  window.impulso = new App();
} catch (err) {
  document.body.innerHTML = `<div style="padding:40px;color:#ff8a8a;font-family:monospace;">
    <h2>Errore di avvio</h2><pre>${err.stack || err.message}</pre></div>`;
  console.error(err);
}
