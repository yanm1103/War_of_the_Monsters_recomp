// A popover menu: a button that opens a panel near it. One at a time (opening one closes the others); a click anywhere else closes
// it; Esc closes it and gives the focus back to the button; the arrows walk its buttons and switches; the button says whether it is
// open (aria-expanded, and the "on" look).
//   popover({ button, menu, onOpen, onClose }) -> { isOpen, set(on), toggle() }
// The caller keeps the menu's contents and its place on the page (style.css); onOpen fills it just before it shows.
const openMenus = new Set();

export function popover({ button, menu, onOpen, onClose }) {
  const walkable = () => [...menu.querySelectorAll('button:not(:disabled), input[type=checkbox]:not(:disabled)')]
    .filter((e) => e.getClientRects().length > 0);
  const api = {
    get isOpen() { return !menu.hidden; },
    set(on) {
      on = !!on;
      if (on === api.isOpen) return;
      if (on) for (const o of [...openMenus]) if (o !== api) o.set(false);
      if (on && onOpen) onOpen();
      menu.hidden = !on;
      button.classList.toggle('on', on);
      button.setAttribute('aria-expanded', String(on));
      if (on) openMenus.add(api); else { openMenus.delete(api); if (onClose) onClose(); }
    },
    toggle() { api.set(!api.isOpen); },
  };
  button.addEventListener('click', () => api.toggle());
  // a click anywhere else
  document.addEventListener('pointerdown', (e) => {
    if (api.isOpen && !menu.contains(e.target) && !button.contains(e.target)) api.set(false);
  }, true);
  // Esc, with the focus in the menu, on its button or nowhere: the menu first, and the selection stays
  document.addEventListener('keydown', (e) => {
    if (!api.isOpen) return;
    const at = document.activeElement;
    // (a text field or picker outside the menu keeps its own Esc; so does an open dialog)
    const own = !menu.contains(at) && at.matches && at.matches('input:not([type=checkbox]):not([type=radio]), textarea, select');
    if (e.key === 'Escape' && !e.defaultPrevented && !own && !document.querySelector('dialog:modal')) {
      e.preventDefault(); e.stopPropagation();
      const inside = menu.contains(at);
      api.set(false);
      if (inside || at === button) button.focus();
      return;
    }
    // the arrows: only from a button or a switch (a select or a number field keeps its own)
    if (menu.contains(at) && at.matches('button, input[type=checkbox]') && (e.key === 'ArrowDown' || e.key === 'ArrowUp' || e.key === 'Home' || e.key === 'End')) {
      const items = walkable(), i = items.indexOf(at);
      if (!items.length) return;
      const j = e.key === 'Home' ? 0 : e.key === 'End' ? items.length - 1 : (i + (e.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
      e.preventDefault();
      items[j].focus();
    }
  }, true);
  return api;
}
