// utils.js - простые DOM-утилиты и UI-хелперы
window.$$ = (sel, root = document) => root.querySelector(sel);
window.showMessage = (text, type = 'error') => {
  const el = $$('#msg');
  if (!el) return;
  el.textContent = text;
  el.className = type === 'success' ? 'success' : (type === 'error' ? 'error' : '');
};
window.setBusy = (btn, busy = true) => {
  if (!btn) return;
  btn.disabled = busy;
  btn.setAttribute('aria-busy', busy ? 'true' : 'false');
};
