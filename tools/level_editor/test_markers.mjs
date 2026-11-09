// The marker layers' rules (web/markers.js): names, groups, colors, caps and the net's links, with no page.
// Run: node tools/level_editor/test_markers.mjs
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const M = await import(new URL('file:///' + path.join(here, 'web', 'markers.js').replace(/\\/g, '/')));

let n = 0;
const t = (name, fn) => { fn(); n++; console.log('ok    ' + name); };

t('the four layers and their modes', () => {
  assert.deepEqual(M.KINDS, ['SPT', 'PUP', 'AIP', 'FOP']);
  assert.deepEqual(M.KINDS.map((k) => M.LAYERS[k].mode), ['spt', 'pup', 'aip', 'fop']);
  assert.deepEqual(M.KINDS.map((k) => M.LAYERS[k].key), ['3', '4', '5', '6']);
  assert.equal(M.KIND_OF_MODE.aip, 'AIP');
  assert.equal(M.KIND_OF_MODE.objects, undefined);
  assert.ok(M.isNet('AIP') && M.isNet('FOP') && !M.isNet('SPT') && !M.isNet('PUP'));
});

const spt = (...lists) => lists.map((list, i) => ({ list, pos: [i, 0, 0], heading: 0, snap: true }));

t('start point names count inside the list', () => {
  const p = spt(1, 2, 2, 0, 1, 3, 2);
  assert.deepEqual(p.map((_, i) => M.sptLabel(p, i)), ['Player 1', 'CPU 1', 'CPU 2', 'Story', 'Player 2', 'Respawn 1', 'CPU 3']);
  assert.equal(M.markerName('SPT', p, 2, []), 'CPU 2');
});

t('start point lists against their room', () => {
  const caps = [1, 2, 20, 20], p = spt(1, 1, 1, 0, 0);
  assert.deepEqual(p.map((_, i) => M.sptOver(p, i, caps)), [false, false, true, false, true]);
});

t('power-up groups hold every type once', () => {
  const all = M.PUP_GROUPS.flatMap((g) => g.types).sort((a, b) => a - b);
  assert.deepEqual(all, Array.from({ length: 20 }, (_, i) => i));
  assert.equal(M.pupGroup(0), 'health');
  assert.equal(M.pupGroup(7), 'stamina');
  assert.equal(M.pupGroup(8), 'special');
  assert.equal(M.pupGroup(9), 'cloak');
  assert.equal(M.pupGroup(15), 'story');
});

t('one color for the view, the list and the legend', () => {
  assert.equal(M.markerColor('SPT', { list: 0 }), M.markerColor('SPT', { list: 0 }));
  assert.notEqual(M.markerColor('SPT', { list: 1 }), M.markerColor('SPT', { list: 2 }));
  assert.equal(M.markerColor('PUP', { type: 0 }), M.markerColor('PUP', { type: 6 }));   // both health
  assert.equal(M.markerColor('PUP', { type: 3 }), M.markerColor('PUP', { type: 7 }));   // both stamina
  assert.notEqual(M.markerColor('PUP', { type: 0 }), M.markerColor('PUP', { type: 3 }));
  assert.equal(M.markerColor('AIP', { type: 0 }), M.netColor('AIP', 0));
  assert.notEqual(M.markerColor('FOP', { type: 0 }), M.markerColor('FOP', { type: 1 }));
  assert.equal(M.hexColor(0x4AE08A), '#4ae08a');
  assert.equal(M.hexColor(0x0000FF), '#0000ff');
});

t('power-ups: health green, stamina blue, special weapon red, the cloak a gray cube', () => {
  const col = (type) => M.hexColor(M.markerColor('PUP', { type }));
  for (const t of [0, 1, 2, 6]) assert.equal(col(t), '#4ae08a');
  for (const t of [3, 4, 5, 7]) assert.equal(col(t), '#36c5f0');
  assert.equal(col(8), '#f0605a');
  assert.equal(col(9), '#a8b0bc');
  for (const t of [10, 15, 19]) assert.equal(col(t), '#ffc233');
  for (const t of [0, 3, 8, 10]) assert.equal(M.markerShape('PUP', { type: t }), 'ball');
  assert.equal(M.markerShape('PUP', { type: 9 }), 'cube');
  assert.equal(M.markerShape('SPT', { list: 1 }), 'cone');
  assert.equal(M.markerShape('AIP', { type: 0 }), 'ball');
});

t('the text over a start point is its place in its list; one past the room says so', () => {
  const caps = [1, 2, 20, 20], p = [{ list: 1 }, { list: 2 }, { list: 1 }, { list: 0 }, { list: 1 }, { list: 3 }];
  assert.deepEqual(p.map((_, i) => M.sptViewLabel(p, i, caps)), ['Player 1', 'CPU 1', 'Player 2', 'Story', 'Player 3 · past room', 'Respawn 1']);
});

t('the list sections', () => {
  const lists = ['story', 'players', 'cpu', 'respawn'], caps = [1, 2, 20, 20], p = spt(2, 2, 1);
  const s = M.sections('SPT', p, lists, caps);
  assert.deepEqual(s.map((x) => [x.title, x.n, x.cap]), [['Story', 0, 1], ['Player', 1, 2], ['CPU', 2, 20], ['Respawn', 0, 20]]);
  assert.deepEqual(s[2].rows, [0, 1]);
  const pup = M.sections('PUP', [{ type: 0 }, { type: 9 }, { type: 1 }], [], [100]);
  assert.deepEqual(pup.filter((x) => x.n).map((x) => [x.id, x.rows]), [['health', [0, 2]], ['cloak', [1]]]);
  const net = M.sections('FOP', [{ type: 1 }, { type: 0 }, { type: 1 }], ['Car street', 'Helicopter'], [255, 0, 6]);
  assert.deepEqual(net.map((x) => [x.title, x.rows]), [['Car street', [1]], ['Helicopter', [0, 2]]]);
  assert.deepEqual(M.sections('AIP', [{ type: 3 }, { type: 0 }], [], [255, 450, 6]).map((x) => x.title), ['Type 0', 'Type 3']);
});

const node = (type = 0, ...links) => ({ pos: [0, 0, 0], type, links: links.map(([to, width = 20]) => ({ to, width })) });

t('a two-way link is one path', () => {
  const p = [node(0, [1]), node(0, [0], [2]), node(0)];
  assert.ok(M.netTwoWay(p, 0, 1) && M.netTwoWay(p, 1, 0) && !M.netTwoWay(p, 1, 2));
  assert.equal(M.netPaths(p), 2);
});

t('taking a node out renumbers the links and drops the ones to it', () => {
  const p = [node(0, [1], [2]), node(0, [0]), node(0, [1], [0])];
  M.removeNode(p, 1);
  assert.equal(p.length, 2);
  assert.deepEqual(p[0].links.map((l) => l.to), [1]);        // 2 became 1, the link to the removed node went
  assert.deepEqual(p[1].links.map((l) => l.to), [0]);
  assert.ok(p.every((q) => q.links.every((l) => l.to < p.length)));
});

t('linking both ways, with a limit, leaves nothing half done', () => {
  const p = [node(0), node(0), node(0, [0], [1], [0, 5])];
  assert.ok(M.linkBoth(p, 0, 1, { width: 30 }, 6));
  assert.deepEqual(p[0].links, [{ to: 1, width: 30 }]);
  assert.deepEqual(p[1].links, [{ to: 0, width: 30 }]);
  assert.ok(M.linkBoth(p, 0, 1, { width: 40 }, 6));           // again: the width is replaced, no second link
  assert.equal(p[0].links.length, 1);
  assert.equal(p[0].links[0].width, 40);
  const full = [node(0), node(0, [2]), node(0)];
  assert.equal(M.linkBoth(full, 0, 1, { width: 1 }, 1), false);   // node 1 is at its limit: node 0 got no link either
  assert.equal(full[0].links.length, 0);
  assert.ok(M.linkBoth([node(0), node(0, [0])], 0, 1, { width: 1 }, 1), 'a link that exists needs no room');
});

t('unlinking takes both directions; the links that are not returned are found', () => {
  const p = [node(0, [1], [2]), node(0, [0]), node(0)];
  assert.deepEqual(M.oneWayIn(p, 2).map((x) => x.from), [0]);
  assert.deepEqual(M.oneWayIn(p, 1), []);
  M.unlink(p, 0, 1);
  assert.equal(p[0].links.length, 1);
  assert.equal(p[1].links.length, 0);
});

console.log(`test_markers: ${n} ok`);
