// The 3D model blocks of a skin's or a custom monster's panel (panel.js): "Edit in a 3D editor" (Export 3D model, Import mesh...,
// Import animations...) and "Check a model first" (Check model...). monsters.js adds the custom's own blocks (rigged model,
// own skeleton, model files) around them.
import { block, button } from './panel.js';

const GLB = [['glTF binary', ['*.glb', '*.GLB']]];

export function meshBlocks(kind, tid) {
  const key = (kind === 'skin' ? 'skins/' : 'monsters/') + tid;
  const run = async (action, body) => { kit.workFocus = [key]; const r = await kit.run(action, body); return r; };
  const imp = what => async () => {
    const start = (await kit.api('do/mesh_glb', { kind, tid })).path;
    const p = await kit.pick('file', 'Edited ' + (what === 'mesh' ? 'mesh' : 'animations') + ' of ' + tid + ' (the .glb edited in the 3D editor)', start, GLB);
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
  const blender = block('Edit in a 3D editor',
    (kind === 'skin' ? 'Reshape the monster\'s pieces or change its moves. ' : 'Reshape the body or change its moves. ') +
    'Export 3D model writes a .glb (mesh, named bones, animations) to the work folder, for Blender or another editor that ' +
    'opens glTF; after editing it, Import mesh... ' +
    'brings back the shapes and Import animations... the moves.',
    [button('Export 3D model', () => run('mesh_export', { kind, tid })), button('Import mesh...', imp('mesh')),
     button('Import animations...', imp('anims'))]);
  const check = block('Check a model first',
    'Reads a .glb and changes nothing: the log says which import takes it and lists what would go wrong (missing textures, ' +
    'meshes without weights, a model in other units or away from its bones).',
    [button('Check model...', async () => {
      const start = (await kit.api('do/mesh_glb', { kind, tid, check: true })).path;
      const p = await kit.pick('file', 'Model to check for ' + tid + ' (.glb)', start, GLB);
      if (p) run('mesh_check', { kind, tid, path: p });
    })]);
  return { blender, check, run };
}
