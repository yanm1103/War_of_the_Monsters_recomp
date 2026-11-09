// The Arrange section of the properties panel: a row or a circle of copies, a mirror, align and spread, on the selected objects.
// The arithmetic is in arrange_math.js; app.js makes the copies and the moves (and the undo step). initArrange(root, ctx) fills `root`
// (a <details> with its <summary>) and returns {changed}: app.js calls changed() when the selection or its state changes. ctx:
//   boxes() -> the selected (visible) objects' boxes, [{min: [x, y, z], max: [x, y, z], pos: [x, y, z]}], game axes, in selection order
//   view() -> [x, y], where the view looks      carryCount() -> how many objects rest on the selection and travel with it (Move objects on top)
//   copies(placers, what), moveAll(placer, what), shifts(axis, list, what)   (app.js; they return how many objects they made or moved)
//   scrub(input)   (drag a number field sideways to change it)   msg(text, {level, toast})
import { rowPlacers, circlePlacers, circleAngle, mirrorPlacer, alignShifts, spreadShifts, middleOf } from './arrange_math.js';

export const MAX_MADE = 500;   // objects one Apply may add (the stage holds 1024 interactive objects in all)

const MODES = [
  ['row', 'Row of copies'], ['circle', 'Circle of copies'], ['mirror', 'Mirror'], ['align', 'Align'], ['spread', 'Spread evenly'],
];
const AXES = [['0', 'X'], ['1', 'Y'], ['2', 'Z (height)']];
const SIDES = { 0: ['Lowest X', 'Middle', 'Highest X'], 1: ['Lowest Y', 'Middle', 'Highest Y'], 2: ['Bottoms', 'Middles', 'Tops'] };
const r1 = (x) => Math.round(x * 10) / 10;

export function initArrange(root, ctx) {
  const body = document.createElement('div');
  body.className = 'ar-body';
  const num = (id, v, o = {}) => `<input type="number" id="${id}" value="${v}" step="${o.step || 1}"${o.min != null ? ` min="${o.min}"` : ''}${o.max != null ? ` max="${o.max}"` : ''}>`;
  const axis = (id, l, c) => `<label class="field" title="${l}"><i class="pre ${c}">${l}</i>${num(id, 0, { step: 10 })}</label>`;
  body.innerHTML = `
    <select id="arMode" aria-label="What to do">${MODES.map(([v, t]) => `<option value="${v}">${t}</option>`).join('')}</select>
    <div class="ar-pane" data-mode="row">
      <div class="ar-row"><label for="arRowN">Count</label>${num('arRowN', 3, { min: 2, max: 200 })}<span class="unit" title="Counting the original">in all</span></div>
      <div class="ar-lbl">Distance between neighbours</div>
      <div class="xyz">${axis('arRowX', 'X', 'x')}${axis('arRowY', 'Y', 'y')}${axis('arRowZ', 'Z', 'z')}</div>
      <div class="ar-row"><label for="arRowT">Turn each by</label>${num('arRowT', 0, { step: 5 })}<span class="unit">degrees</span></div>
    </div>
    <div class="ar-pane" data-mode="circle" hidden>
      <div class="ar-row"><label for="arCirN">Count</label>${num('arCirN', 6, { min: 2, max: 200 })}<span class="unit" title="Counting the original">in all</span></div>
      <div class="ar-row"><label for="arCirA">Arc</label>${num('arCirA', 360, { step: 15, min: -360, max: 360 })}<span class="unit" title="360 goes all the way around">degrees</span></div>
      <div class="ar-lbl">Around the point</div>
      <div class="xyz two"><label class="field" title="X"><i class="pre x">X</i>${num('arCirX', 0, { step: 10 })}</label>
        <label class="field" title="Y"><i class="pre y">Y</i>${num('arCirY', 0, { step: 10 })}</label></div>
      <div class="ar-btns"><button type="button" id="arCirSel" title="The middle of the selected objects">Selection's middle</button>
        <button type="button" id="arCirView" title="Where the view looks">Where I look</button></div>
      <label class="switch ar-sw"><input type="checkbox" id="arCirTurn" checked><span class="track"></span><span class="tx">Turn them around the circle</span></label>
      <div class="help" id="arCirInfo"></div>
    </div>
    <div class="ar-pane" data-mode="mirror" hidden>
      <div class="ar-row"><label for="arMirAx">Across</label>
        <select id="arMirAx"><option value="x">X = (left and right swap)</option><option value="y">Y = (front and back swap)</option></select></div>
      <div class="ar-row"><label for="arMirAt">At</label>${num('arMirAt', 0, { step: 10 })}<button type="button" id="arMirMid" title="The middle of the selected objects">Middle</button></div>
      <label class="switch ar-sw"><input type="checkbox" id="arMirKeep" checked><span class="track"></span><span class="tx">Keep the originals (make copies)</span></label>
      <div class="help">Places them at the mirrored spots with the mirrored heading. The models themselves are not flipped.</div>
    </div>
    <div class="ar-pane" data-mode="align" hidden>
      <div class="ar-row"><label for="arAlAx">Along</label><select id="arAlAx">${AXES.map(([v, t]) => `<option value="${v}">${t}</option>`).join('')}</select></div>
      <div class="ar-row"><label for="arAlTo">To the</label><select id="arAlTo"></select></div>
      <div class="help">Moves the objects so that their boxes line up. Needs two or more selected.</div>
    </div>
    <div class="ar-pane" data-mode="spread" hidden>
      <div class="ar-row"><label for="arSpAx">Along</label><select id="arSpAx">${AXES.map(([v, t]) => `<option value="${v}">${t}</option>`).join('')}</select></div>
      <div class="ar-row"><label for="arSpBy">Even</label><select id="arSpBy"><option value="gaps">gaps between them</option><option value="mid">distances between their middles</option></select></div>
      <div class="help">The two outermost stay; the rest go between them. Needs three or more selected.</div>
    </div>
    <button type="button" id="arApply" class="wide primary"></button>
    <div class="help ar-note" id="arNote"></div>`;
  root.append(body);
  const $ = (id) => body.querySelector('#' + id);
  const mode = () => $('arMode').value;
  const val = (id) => +$(id).value;
  for (const i of body.querySelectorAll('input[type=number]')) ctx.scrub(i);

  const touched = new Set();   // fields the person typed in keep their value when a new selection would fill them
  for (const i of body.querySelectorAll('input')) i.addEventListener('input', () => touched.add(i.id));
  for (const i of body.querySelectorAll('input, select')) i.addEventListener('input', refresh);
  $('arMode').addEventListener('change', () => {
    for (const p of body.querySelectorAll('.ar-pane')) p.hidden = p.dataset.mode !== mode();
    refresh();
  });
  const setAlignTo = () => {
    const to = $('arAlTo'), keep = to.value || 'mid';
    to.innerHTML = SIDES[$('arAlAx').value].map((t, k) => `<option value="${['min', 'mid', 'max'][k]}">${t}</option>`).join('');
    to.value = keep;
  };
  $('arAlAx').addEventListener('change', setAlignTo);
  setAlignTo();

  const fill = (id, v) => { if (!touched.has(id)) $(id).value = String(r1(v)); };
  let sig = '';
  function boxes() { return ctx.boxes(); }
  const radiusOf = (bs) => Math.hypot(bs[0].pos[0] - val('arCirX'), bs[0].pos[1] - val('arCirY'));
  function defaults(bs) {
    const mid = middleOf(bs), w = Math.max(...bs.map((b) => b.max[0])) - Math.min(...bs.map((b) => b.min[0]));
    fill('arRowX', w + 20);
    if (bs.length > 1) { fill('arCirX', mid[0]); fill('arCirY', mid[1]); } else { const v = ctx.view(); fill('arCirX', v[0]); fill('arCirY', v[1]); }
    fill('arMirAt', $('arMirAx').value === 'x' ? mid[0] : mid[1]);
  }

  // what Apply would do now: {text, ok, note}
  function plan(bs) {
    const n = bs.length, on = ctx.carryCount(), all = n + on;
    const made = (k) => k * all;
    switch (mode()) {
      case 'row': {
        const c = Math.max(2, Math.min(200, Math.round(val('arRowN')) || 2));
        return { text: `Make ${made(c - 1)} ${made(c - 1) === 1 ? 'copy' : 'copies'}`, made: made(c - 1) };
      }
      case 'circle': {
        const c = Math.max(2, Math.min(200, Math.round(val('arCirN')) || 2));
        return { text: `Make ${made(c - 1)} ${made(c - 1) === 1 ? 'copy' : 'copies'}`, made: made(c - 1) };
      }
      case 'mirror': return $('arMirKeep').checked ? { text: `Make ${all} ${all === 1 ? 'copy' : 'copies'}`, made: all } : { text: 'Mirror them', made: 0 };
      case 'align': return { text: 'Align', made: 0, ok: n >= 2, note: n >= 2 ? '' : 'Select two or more objects first.' };
      default: return { text: 'Spread', made: 0, ok: n >= 3, note: n >= 3 ? '' : 'Select three or more objects first.' };
    }
  }
  function refresh() {
    const bs = boxes();
    $('arApply').disabled = true;
    if (!bs.length) { $('arApply').textContent = 'Select something first'; $('arNote').textContent = ''; return; }
    const p = plan(bs), over = p.made > MAX_MADE;
    $('arApply').textContent = p.text;
    $('arApply').disabled = p.ok === false || over || p.made < 0;
    $('arNote').textContent = over ? `That is ${p.made} objects; one go makes at most ${MAX_MADE}.` : p.note || '';
    if (mode() === 'circle') {
      const rad = radiusOf(bs);
      $('arCirInfo').textContent = rad < 1 ? 'The selection is at the centre, so the copies would pile up there: move the point away from it.'
        : `${bs.length > 1 ? 'The first one is' : 'It is'} ${Math.round(rad)} units from the point; ${r1(circleAngle(Math.max(2, val('arCirN')), val('arCirA')))}° between neighbours.`;
    }
  }
  function changed() {
    if (!root.open) return;   // closed: nothing to show (it fills when it opens)
    const bs = boxes();
    const s = bs.length ? JSON.stringify([bs.length, middleOf(bs).map(r1)]) : '';
    if (s !== sig) { sig = s; if (bs.length) defaults(bs); }
    refresh();
  }

  $('arMirAx').addEventListener('change', () => { touched.delete('arMirAt'); sig = ''; changed(); });
  $('arMirMid').addEventListener('click', () => { const m = middleOf(boxes()); $('arMirAt').value = String(r1($('arMirAx').value === 'x' ? m[0] : m[1])); refresh(); });
  $('arCirSel').addEventListener('click', () => { const m = middleOf(boxes()); $('arCirX').value = String(r1(m[0])); $('arCirY').value = String(r1(m[1])); touched.add('arCirX'); refresh(); });
  $('arCirView').addEventListener('click', () => { const v = ctx.view(); $('arCirX').value = String(r1(v[0])); $('arCirY').value = String(r1(v[1])); touched.add('arCirX'); refresh(); });

  $('arApply').addEventListener('click', () => {
    const bs = boxes();
    if (!bs.length) return;
    const m = mode();
    if (m === 'row') {
      const c = Math.max(2, Math.min(200, Math.round(val('arRowN')) || 2));
      ctx.copies(rowPlacers(c, [val('arRowX'), val('arRowY'), val('arRowZ')], val('arRowT')), 'array of');
    } else if (m === 'circle') {
      const c = Math.max(2, Math.min(200, Math.round(val('arCirN')) || 2));
      if (bs.every((b) => Math.hypot(b.pos[0] - val('arCirX'), b.pos[1] - val('arCirY')) < 1)) { ctx.msg('The selection is at the circle\'s centre: move the point away from it first.', { level: 'warn', toast: true }); return; }
      ctx.copies(circlePlacers(c, val('arCirA') || 360, [val('arCirX'), val('arCirY')], $('arCirTurn').checked), 'circle of');
    } else if (m === 'mirror') {
      const f = mirrorPlacer($('arMirAx').value, val('arMirAt'));
      if ($('arMirKeep').checked) ctx.copies([f], 'mirror'); else ctx.moveAll(f, 'mirror');
    } else if (m === 'align') {
      ctx.shifts(+$('arAlAx').value, alignShifts(bs, +$('arAlAx').value, $('arAlTo').value), 'align');
    } else {
      ctx.shifts(+$('arSpAx').value, spreadShifts(bs, +$('arSpAx').value, $('arSpBy').value === 'gaps'), 'spread');
    }
    changed();
  });
  root.addEventListener('toggle', changed);
  refresh();
  return { changed };
}
