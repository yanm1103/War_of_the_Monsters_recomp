# 3D monsters straight from the disc

(Doc index in `CLAUDE.md`.) How to assemble an animated monster from the `MON/` files alone, without the game or a RAM dump. It serves the save list
(monster in idle over the stage picture, `NATIVE.md`, "Save as JSON"), the 3D preview in the select screen (item 12) and the web viewer (`WEB_VIEWER.md`). Base formats (mesh, curves) in `docs/FORMATS.md`.

## Code
| Where | What |
|---|---|
| `tools/animcurve.py` | curve evaluator (port of `src/common/AnimCurve.cpp`) |
| `tools/pose.py` | pose of a monster (`Model(ngp).pose(anim, quadro)`), `--list` shows the animations; writes a posed `.glb` |
| `tools/gltf.py` | exports `.glb` (textures from `tex.py`); with `model=` the skinned meshes come out in the pose |
| `tools/monglb.py` | animated `.glb` for the web viewer: joints = bones with an output (parent = whoever wrote the starting matrix on the stack), inverse bind = `restPoseInv`, rigid parts as nodes; curves sampled at 60/s; GS blends, scrolling texture, fire and lens flare in the extras. `--boss plant|root|mecha` assembles the bosses from the stage pieces (3-MILE, CAPITOL). Matches `pose.py` vertex by vertex |
| `tools/shellpics.py` | wide stage pictures and monster logos from `SHELL.TEX/.RTX` as PNG (the same ones as `wotm_savepics.hpp`) |
| `native/wotm_model.hpp` | the same in C++: `wotm::model::Model` (load, pose, posed triangles, textures) and `Renderer` (raylib, RenderTexture) |
| `native/monview.cpp` | `build\monview.exe disc MONKEY0 [--anim 90] [--yaw 35] [--shot x.png]`: animated window (arrows rotate, PgUp/PgDn switch the animation) or PNG |

In the save list: `wotm_save.inc` posts a `savepics::kMonster` (monster | costume << 8); the `Drawer::prepare` of `wotm_savepics.hpp`
draws the monster into the RenderTexture before the frame (`wotm_live.inc` calls it), and `draw` pastes the texture between the frame and the text.

## Files
- `MON/<PREFIXO><traje>.NGP` (raw, no zip), `.TEX`, `.RTX` (raw for the monsters) and `.PTR`. Prefixes: MONKEY Congar, ROBOT Robo-47,
  LIZARD Togera, SHOGUN Ultra-V, MANTIS Preytor, ENERGY Kineticlops, LAVA Magmo, DRAGON Raptros, ROCK Agamo, ALIEN Zorgulon, ASSBOSS
  Goliath Prime. Costume 0 to 3 = `Shell::m_costume` (`Shell::formatFilename1`).
- **The `.NGP` comes pre-linked for 0xA00000.** Read at that base, the pointers are valid without the `.PTR` (the `.PTR` only shifts to where the game
  loaded it). The native layer puts the file in a buffer at 0xA00000 and uses the usual `Ram` (`decodeObject` works unchanged).
- Root 0 = the monster (GROUP with the monster's id); root 1 = GROUP id 0 (HUD portraits and extras); then COLL_GRID and ANIM_MGR.

## Animation
- Main instance: the `CHAR_INSTANCE` (op 25) reachable from root 0 with the most animations. `character` at +0x20 (`numAnims` u16 @0,
  `numChannels` u16 @2, `animations[]` @0xC); initial channel values at `ci+4`.
- Animation `id` = the one in `animations[k]` with `head >> 18 == id` (the Monster stores the k-th as `m_anims[id]`, ids 0..299,
  `Monster::initAfterDbLoad`). `startTime` @4, `endTime` @8, `numChannels` u16 @0xC, curves @0x18. **Idle = 90 (0x5A)**
  (`StateIdle::chooseBaseIdle`; 0x5F tired, 0x60 with special). Time in frames of 60/s; Congar's idle has 160.
- Each curve writes `val[dataIdx]` (FORMATS.md, "Animation (curves in the `.NGP` and the runtime mixer)"). It matches `animOutput.val` from a fight in 128 of 130 channels (the remaining 2, 0.007, come from a blend layer).

## Traversal (part matrices)
Row vectors (`p' = p·M`), child = `local · pai`. Per node:
- **ANIM_XFORM (17):** visible = `+0xC`, or `val[visidx]` (`+0x10`) if the channel was written; 0 = culls. Rotation = `-val` of the channels
  rX/rY/rZ (`+0x12..0x16`; with no channel, the vector at `+0x20`), matrix `mathfRotMatrixPRH` (= VU0 microprogram 0x4D0). Bits at
  `+0xC2`: bit 0 `L = pre·L` (`+0x40`), bit 1 `L = L·post` (`+0x80`). Then `m[3] += translação` (channels `+0x18..0x1C` or `+0x30`).
  Children at `+0xCC`, count at `+4`.
  **It only rebuilds if some channel is dirty:** `firstIdx` (s16 `+0x1E`) and the `animating` mask (u8 `+0xC1`); it rebuilds if some bit
  `b` of the mask is set and channel `firstIdx+b` was written. Otherwise the matrix is the file's, `atMat[matrixIdx]` (`ci+0xC`,
  index at `+0xC4`). In Robo-47 (`ram_1100`) 35 are rebuilt and 5 come from the file; without the rule the claws
  (indices 27 and 39) come out turned.
- **ANIM_SCALE (38):** reads `val` directly (without checking whether it was written); `pre(+0x40) · escala · post(t + 0x50)`; zero scale culls.
- GROUP (bit 0 of `+0xA`), LOD (the most detailed one `hierLod` would draw up close: the highest index with `switchOutDis` (`+0x24 + 16i`) > 0; Togera's costume 3 has a last level with 0, never drawn, and taking the last one gave the coarse mesh in A-pose), TRANSLATE, ROTATE, SCALE, SWITCH, CONTROL/ACTION_DATA,
  INTERACTIVE/DESTRUCTIBLE as in the native `Scene::walk`.

## Skeleton and palette
- **SKEL_BONE (35):** `numKids` +4, `skelOutputMatIdx` s16 +8, `isRootNode` +0xA, children +0xC, `restPoseInv` +0x10. Pushes the bone
  (`si+1`); if it is a root, it also pushes an identity (`mi+1`).
- The **next transform node** (ANIM_XFORM, TRANSLATE, ROTATE) with `si >= 0` pops: `smat[mi+1] = local · smat[mi]`,
  `paleta[osso.out] = restPoseInv · smat[mi+1]`, `si-1`, `mi+1`. The state is passed by value to each subtree.
- Address of each output: `animMatrixPtr[out]` (`ci+0x14`). The skinned OBJECT finds its block through `animPktPtr` (`ci+0x18`)
  `[OBJECT+0x68]` (DMA ref tag, +4 = block), matrix of slot `s` at `bloco + 0x10 + 0x40·s`.
- **Skinning:** 3 bones per vertex, `w1 = w`, `w2 = frac(w·2048)`, `w3 = 1 - w1 - w2`, slot = `(código & 0x7F) / 4`; then the object
  matrix. The normal rotates only by bone x.
- The assembled palette is equal to the one from a fight dump (`ram_1500`). In other frames the RAM palette is one frame behind
  `atMat` (not a model error).

## Textures
- Slot `s` of the OBJECT: texId at `+0x50 + 2s`, TEX0 at `+0x70 + 0x30s` (how many: `+0x5A`, up to 5); the section's slot comes from the GIF tag
  (`Vtx::slot`).
- **For the monsters the `.NGP` numbers are already the file's own:** texId = the one in the `.TEX` package (or the `.RTX`, e.g.: Congar's texId 2 T8H),
  CBP = `tbp` of the `.RTX` CT32 palette. Zero offset in the 10 monsters and in the costumes (`tex.Pairing` finds 0/0).
- Linear pixels: `.TEX` at `+0x80`; `.RTX` at `+0x80` for the high formats (T8H/T4HL/T4HH) and `+0x10` for the rest. 256-color palette with the CSM1
  swap (bits 3 and 4) only for T8; T4 uses `csa·16 + k` in a 256 palette. GS alpha: `>= 0x80` = 255, otherwise `·2`; TCC 0 = opaque.

## GS state per node
Like the native `Scene::groupState`: the context is at `+4` (GROUP, TRANSLATE, ROTATE, SCALE) or `+8` (ANIM_XFORM, ANIM_SCALE); it applies
to the subtree. A+D packet with `+0x14 == 0x10000000`, `+0x18 == 0xE`, NLOOP at `+0x10`, pairs from `+0x20`: ALPHA_1 (0x42,
low byte), TEST_1 (0x47: ATE bit 0, ATST bits 1-3, AREF bits 4-11), ZBUF_1 (0x4E, ZMSK). Default: `0x44`, no test. The blend only
applies with the mesh's PRIM.ABE. Modes seen: `0x44` common, `0x48` additive (Kineticlops), `0x42` `Cd - Cs·As`, `0x46` `Cd·(1-As)`,
`0x81` `(Cd-Cs)·As`. The `Renderer` reproduces them with premultiplied color (opaque ones first, then the ones with ABE in model order) and
draws the background inside the RenderTexture for the modes that depend on the destination. UV offset: `+0x20/+0x24` of the node, or the
channels `+0x30/+0x32` with bit 0x800 at `+0xC`. In Kineticlops (costumes 0-2) these channels (142/143) come from animation 0x91, which
has only those two curves and loops on top of the main one (the idle writes 0 to them): U goes around once every 80 frames, V twice.
In the fight dump `build/dumps_live/ram_energy_robot.bin`, U and V are 0.637 and 1.275 (ratio 1:2).

## Framing
`Model::computeFraming(id)` samples the whole animation and fixes the box; without it the camera follows the idle ("dancing" zoom).

## Fire and lens flare
The effect textures come from `LVL/CENTRAL.TEX/.RTX` (`FxTextures`): Fireball = texId 5 (palette tbp 13); flare = texIds 54/4/55/56
(palettes 203/9/207/211, texInfo 15/17/18/19). Reverse engineering details in `75f5be1:docs/investigacoes/2026-10-21/fogo_agamo.md` and
`75f5be1:docs/investigacoes/2026-10-21/lens_flare_monstros.md`.
- **Fire (node op 22, HierParticleEmitter, preset +4 = 141 IdolHeadFlame).** Emitter E = position(+0x10)·m. Additive sprite particle,
  life 20 fields, size 7 + 8U, growth 1 − 1.5·min(age/10, 1), wind (0,0,3,2), drag 0.0390625, 2 per field, color from a
  table of 16 (age·3072 in 4.12), UV mirrored by 2 bits. A new system starts with 30 warm-up steps. The framing
  stretches the box 55 units above the emitter. Only 141 is simulated (AttachedFlames and the flying head are not).
- **Lens flare (node op 5, HierLight with isOn and hasLensFlare).** P = pos·m, D = beam·m; brightness I/W², size √I/W; directional:
  k = max(dot,0) + bw − 1, scale k/bw. 18 fixed sprites along C + t(S − C), additive, after Mode3D, no Z test
  (occlusion not reproduced: always visible). Monsters with a light: ROBOT0–2, MONKEY1–3, JELLY0, ASSBOSS1.

## Not reproduced
Fur and tail springs (`BoneSpring`: they stay in the animation pose), procedural look/head, animation blend layers and the game's
lighting (the `Renderer` uses a fixed directional light + ambient over the vertex color).

## For the select preview and the web viewer
- Select: the same `Model` + `Renderer` (one per player); switching costume = reloading the `.NGP` (~1 MB, plus a 10 MB buffer).
- Web: port `tools/pose.py` + `animcurve.py` (small) to JS; the textures and the mesh follow `tools/gltf.py`.
