// Picking rows in a list as a file manager does: click picks one, Ctrl adds or removes, Shift a range from the last click.
// box: the list element (rows are .row with data-id); order(): the visible ids in order; changed(): redraw.
export function listSelect(box, order, changed) {
  const set = new Set();
  let anchor = null;
  box.addEventListener('mousedown', e => {
    const d = e.target.closest('.row');
    if (!d || e.target.classList.contains('arrow')) return;
    const id = d.dataset.id;
    if (e.button === 2 && set.has(id)) return;   // right click on the selection keeps it (the menu acts on it)
    if (e.shiftKey && anchor !== null) {
      const o = order();
      const a = o.indexOf(anchor), b = o.indexOf(id);
      if (a >= 0 && b >= 0) {
        if (!e.ctrlKey) set.clear();
        for (let i = Math.min(a, b); i <= Math.max(a, b); i++) set.add(o[i]);
      }
    } else if (e.ctrlKey || e.metaKey) {
      if (set.has(id)) set.delete(id); else set.add(id);
      anchor = id;
    } else {
      set.clear();
      set.add(id);
      anchor = id;
    }
    changed();
  });
  return {
    has: id => set.has(id),
    ids: () => order().filter(id => set.has(id)),   // the visible ones, in order
    all: () => [...set],                            // also the ones a closed group or a search hides
    set(ids) { set.clear(); ids.forEach(i => set.add(i)); anchor = ids[0] || null; },
    clear() { set.clear(); },
    keepOnly(valid) { for (const id of [...set]) if (!valid.has(id)) set.delete(id); },
  };
}
