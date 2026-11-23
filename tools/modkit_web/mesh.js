// The 3D model blocks of a skin's or a custom monster's panel (panel.js): "Check your model before importing it" (Check model...),
// "Edit the base model and import it back" (Export 3D model, Import mesh..., Import animations...) and the "Currently using"
// lines. monsters.js adds the custom's own blocks (rigged model, entirely new character, model files) between them.
import { block, button, el, toast } from './panel.js';

const GLB = [['glTF binary', ['*.glb', '*.GLB']]];
const ANY3D = [['3D model', ['*.glb', '*.gltf', '*.dae', '*.GLB', '*.GLTF', '*.DAE']]];

// the model the game loads and where it came from, one line each (modkit_web_skins.py mesh_status)
export function currentModel(mesh) {
  const box = el('div', 'state now');
  box.appendChild(el('div', 'nowhead', 'Currently using:'));
  const ul = el('ul');
  for (const t of mesh.lines || [mesh.text]) ul.appendChild(el('li', '', t));
  box.appendChild(ul);
  return box;
}

export function meshBlocks(kind, tid, custom = kind === 'monster') {   // custom: a custom monster or a skin of one
  const key = (kind === 'skin' ? 'skins/' : 'monsters/') + tid;
  const run = async (action, body) => { kit.workFocus = [key]; const r = await kit.run(action, body); return r; };
  const imp = what => async () => {
    const start = (await kit.api('do/mesh_glb', { kind, tid })).path;
    const p = await kit.pick('file', 'Edited ' + (what === 'mesh' ? 'mesh' : 'animations') + ' of ' + tid + ' (the .glb edited in the 3D editor)', start, GLB);
    if (!p) return;
    const r = await run('mesh_import', { kind, tid, path: p, what });
    if (r.route === 'rig') {
      if (!custom) {
        await kit.dialog({ title: 'Import mesh', text: r.why + ': a new body.\n\nA skin only reshapes its monster\'s pieces. A new body is a custom monster: New monster..., then Import character with animations... there.', okText: 'OK' });
        return;
      }
      if (await kit.confirm('Import mesh', r.why + ': a new body, which Import mesh does not take.\n\nImport it as an entirely new character, on the monster\'s skeleton? From then on this .glb is ' + tid + '\'s model: edit it and import it again the same way.', 'Import character'))
        run('mesh_import', { kind, tid, path: p, what, as_rig: true });
    }
  };
  const blender = block('Edit the base model and import it back',
    (kind === 'skin' ? 'Reshape the monster\'s pieces or change its animations. ' : 'Reshape the body or change its animations. ') +
    'The [Export 3D model] button writes a .glb with the mesh, the named bones and the animations to the work folder. Edit it ' +
    'in Blender or another editor that opens glTF; then [Import mesh...] brings the mesh back and [Import animations...] the ' +
    'animations.',
    [button('Export 3D model', () => run('mesh_export', { kind, tid })), button('Import mesh...', imp('mesh')),
     button('Import animations...', imp('anims'))]);
  const checkBtn = button('Check model...', () => doCheck());
  const check = block('Check your model before importing it',
    'Use this before replacing the current model. It reads your file and changes nothing: the Messages box says which of the ' +
    'imports below can take it and lists what would go wrong (e.g. missing textures, meshes without weights, a model in other ' +
    'units or away from its bones).' + (custom ? ' Formats: .glb, .gltf or .dae.' : ' A skin takes the .glb of Export 3D model, edited.'),
    [checkBtn]);
  // while it runs: a moving bar under the buttons; at the end, a note next to the button (OK, or how many errors / warnings;
  // the lines are in Messages)
  async function doCheck() {
    const start = (await kit.api('do/mesh_glb', { kind, tid, check: true })).path;
    const p = await kit.pick('file', 'Model to check for ' + tid, start, custom ? ANY3D : GLB);
    if (!p) return;
    kit.keepPreview = true;
    const done = kit.nextDone();   // before the run: a check that fails at once ends before the run's answer
    const r = await kit.run('mesh_check', { kind, tid, path: p });
    if (!r.ok) { kit.keepPreview = false; return; }
    const bar = el('div', 'loadbar');
    bar.appendChild(el('span'));
    check.appendChild(bar);
    checkBtn.disabled = true;
    await done;
    bar.remove();
    checkBtn.disabled = false;
    const res = await kit.api('do/check_result', {});
    const [text, k] = res.error || res.errors === undefined ? ['Could not check: see Messages', 'err']
      : res.errors ? [res.errors + ' error' + (res.errors > 1 ? 's' : '') + (res.warnings ? ', ' + res.warnings + ' warning' + (res.warnings > 1 ? 's' : '') : '') + ': see Messages', 'err']
      : res.warnings ? [res.warnings + ' warning' + (res.warnings > 1 ? 's' : '') + ': see Messages', 'warn'] : ['OK', 'ok'];
    toast(checkBtn, text, k);
  }
  return { blender, check, run };
}
