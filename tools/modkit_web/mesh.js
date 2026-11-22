// The mesh row of a skin's or a custom monster's panel: Export to Blender (.glb with the mesh and the animations), Check
// model..., Import mesh..., Import animations...; on a custom monster also Import rig..., Import rigged model... and Adjust
// bones... (monsters.js adds those through extra()).
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text) e.textContent = text; return e; };
const button = (text, f) => { const b = el('button', '', text); b.type = 'button'; b.onclick = f; return b; };
const GLB = [['glTF binary', ['*.glb', '*.GLB']]];

export function meshRow(kind, tid, status, extra) {
  const key = (kind === 'skin' ? 'skins/' : 'monsters/') + tid;
  const run = async (action, body) => { kit.workFocus = [key]; const r = await kit.run(action, body); return r; };
  const box = el('div', 'meshrow');
  box.appendChild(el('div', 'dim', status.text));
  const r1 = el('div', 'btns');
  r1.append(button('Export to Blender', () => run('mesh_export', { kind, tid })),
    button('Check model...', async () => {
      const start = (await kit.api('do/mesh_glb', { kind, tid, check: true })).path;
      const p = await kit.pick('file', 'Model to check for ' + tid + ' (.glb)', start, GLB);
      if (p) run('mesh_check', { kind, tid, path: p });
    }), el('span', 'dim', '(.glb)'));
  const r2 = el('div', 'btns');
  const imp = what => async () => {
    const start = (await kit.api('do/mesh_glb', { kind, tid })).path;
    const p = await kit.pick('file', 'Edited ' + (what === 'mesh' ? 'mesh' : 'animations') + ' of ' + tid + ' (.glb exported from Blender)', start, GLB);
    if (!p) return;
    const r = await run('mesh_import', { kind, tid, path: p, what });
    if (r.route === 'rig') {
      if (kind === 'skin') {
        await kit.dialog({ title: 'Import mesh', text: r.why + ': a new body.\n\nA skin only reshapes its monster\'s pieces. A new body is a custom monster: New monster..., then Import rig... there.', okText: 'OK' });
        return;
      }
      if (await kit.confirm('Import mesh', r.why + ': a new body, which Import mesh does not take.\n\nImport it with Import rig, on the monster\'s skeleton? From then on this .glb is ' + tid + '\'s rig: edit it and use Import rig again.', 'Import rig'))
        run('mesh_import', { kind, tid, path: p, what, as_rig: true });
    }
  };
  r2.append(button('Import mesh...', imp('mesh')), button('Import animations...', imp('anims')));
  box.append(r1, r2);
  if (extra) extra(box, r2, run);
  return box;
}
