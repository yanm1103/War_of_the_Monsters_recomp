// A popover menu: a button that opens a panel near it. One at a time (opening one closes the others); a click anywhere else closes
// it; Esc closes it and gives the focus back to the button; the arrows walk its buttons and switches; the button says whether it is
// open (aria-expanded, and the "on" look).
//   popover({ button, menu, onOpen, onClose }) -> { isOpen, set(on), toggle() }
// The caller keeps the menu's contents and its place on the page (style.css); onOpen fills it just before it shows.
const openMenus = new Set();

//   place: true puts the menu (position: fixed, in the body) under the button, kept inside the window
//   closeOnItem: true closes it when one of its buttons is chosen (a switch or a field stays open)
export function popover({ button, menu, onOpen, onClose, place, closeOnItem }) {
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
      if (on && place) {
        document.body.append(menu);
        const r = button.getBoundingClientRect();
        menu.style.left = `${Math.max(8, Math.min(Math.round(r.left), innerWidth - menu.offsetWidth - 8))}px`;
        menu.style.top = `${Math.round(r.bottom + 4)}px`;
      }
      // the focus goes into the menu once it shows (an element of a hidden menu cannot take it): arrows and Esc work from there, and
      // a text field that had the focus no longer keeps the Esc
      if (on) { const first = walkable()[0]; if (first) first.focus({ preventScroll: true }); }
      button.classList.toggle('on', on);
      button.setAttribute('aria-expanded', String(on));
      if (on) openMenus.add(api); else { openMenus.delete(api); if (onClose) onClose(); }
    },
    toggle() { api.set(!api.isOpen); },
  };
  button.addEventListener('click', () => api.toggle());
  if (closeOnItem) menu.addEventListener('click', (e) => { const b = e.target.closest('button'); if (b && !b.disabled) { api.set(false); } });
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

// A menu bar: titles in a row, each opening its menu (a popover placed under it). Click opens; with one open, passing over another
// title switches to it; Left and Right go to the neighbor title (opening its menu when one was open), Down and Up open a menu
// from its title, Home and End go to the first and the last title; Esc, a click elsewhere and the popovers' own arrows are
// menu.js's. A chosen item gives the focus back to its title (so that a dialog it opens returns the focus there).
//   menubar({ items: [{ button, menu, onOpen }] }) -> [popover api, ...]   (onOpen fills the menu just before it shows)
export function menubar({ items }) {
  const n = items.length;
  const titles = items.map((i) => i.button);
  const pops = items.map(({ button, menu, onOpen }) => popover({ button, menu, onOpen, place: true, closeOnItem: true }));
  const openAt = () => pops.findIndex((p) => p.isOpen);
  const walkable = (menu) => [...menu.querySelectorAll('button:not(:disabled)')].filter((e) => e.getClientRects().length > 0);
  // one tab stop for the bar (the last title that had the focus)
  for (const t of titles) t.addEventListener('focus', () => { for (const x of titles) x.tabIndex = x === t ? 0 : -1; });
  const show = (j, items_) => {            // open menu j and put the focus in it (or on its title when it has nothing to choose)
    pops[j].set(true);
    const w = walkable(items[j].menu);
    if (!w.length) titles[j].focus({ preventScroll: true });
    else if (items_ === 'last') w[w.length - 1].focus({ preventScroll: true });
  };
  titles.forEach((t, j) => {
    t.addEventListener('pointerenter', () => { if (openAt() >= 0 && !pops[j].isOpen) show(j); });
  });
  items.forEach(({ menu }, j) => {
    // (closed here, before the item acts: an item that disables itself, like Undo on the last change, would stay open otherwise)
    menu.addEventListener('click', (e) => {
      const b = e.target.closest('button');
      if (b && !b.disabled) { titles[j].focus({ preventScroll: true }); pops[j].set(false); }
    }, true);
  });
  document.addEventListener('keydown', (e) => {
    if (e.defaultPrevented || e.ctrlKey || e.altKey || e.metaKey || e.shiftKey) return;
    const at = document.activeElement, ti = titles.indexOf(at), oi = openAt();
    const inMenu = oi >= 0 && items[oi].menu.contains(at);
    if (ti < 0 && !inMenu) return;
    const cur = ti >= 0 ? ti : oi;
    if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
      e.preventDefault();
      const j = (cur + (e.key === 'ArrowRight' ? 1 : -1) + n) % n;
      if (oi >= 0) show(j); else titles[j].focus({ preventScroll: true });
    } else if (ti >= 0 && (e.key === 'ArrowDown' || e.key === 'ArrowUp')) {
      e.preventDefault();
      if (!pops[ti].isOpen) show(ti, e.key === 'ArrowUp' ? 'last' : 'first');
    } else if (ti >= 0 && (e.key === 'Home' || e.key === 'End')) {
      e.preventDefault();
      const j = e.key === 'Home' ? 0 : n - 1;
      if (oi >= 0) show(j); else titles[j].focus({ preventScroll: true });
    }
  }, true);
  return pops;
}
