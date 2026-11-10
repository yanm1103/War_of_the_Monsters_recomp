# Tools, shared headers and differential testing

(Doc index in `CLAUDE.md`.)

## Tools (tools/)
- `wsl/gate.sh` (build + SHA1, exits with an error if it fails), `wsl/scoreall.sh` (per-function score with NON_MATCHING), `wsl/convert_tus.sh`, `wsl/check.sh`,
  `ccmatch.py`, `nm_wrap.py`, `new_tu.py`, `place_data.py`, `progress.py` (writes `config/status.csv`), `callgraph.py`, `m2c.sh`, `wsl/variants.py`, `wsl/permute.py`.
- Per-TU verification in one command: `~/.venvs/wotm/bin/python tools/verify_tu.py game/<TU> [...] [--runs N] [--no-nm] [-v]` runs the score (`ccmatch`), `difftest_all`, `nm_retvals` and, unless `--no-nm`, a `build_nm.sh verify` with only those TUs for `nm_calls` and `nm_audit`; it prints a table per function (score, difftest, calls, layout, return) and exits with an error if some equivalent differs in the difftest, in the calls, or has a consumed `void` return. With the ELF it takes ~4.5 min (the `build_nm`); without, seconds. `sh tools/m2c_batch.sh [csv | tu...]` generates the m2c drafts for the whole queue (`config/boot_coverage_asm.csv` by default) in `build/m2c/<tu>/<função>.c`, in parallel and only what is missing.
- Native layer and runtime: `native_syms.py` (`--gen` writes `native/wotm_syms.h` with `wotm::sym::<dado>` and `wotm::fn::<função>` from `symbol_addrs.txt`; `--audit` lists the hex literals in `native/` that match a symbol, fall inside one, or match none; `--apply` replaces the exact ones with names, only in code, and includes the header where missing), `vu1cmp.ps1` (VU1 x native check and images from snapshots), `corpus.ps1` (regression corpus: `-Capture` records menus and fights in `<runtime>\corpus`, `-Check -Baseline <csv>` says what got worse after a change in `native/`), `build_runtime.ps1` (or `build_runtime.bat` at the root), `lote.ps1`, `wotm_env.py` (checks the `WOTM_*` variable table in `NATIVE.md` against the code), `vu0native.py`, `vu1dis.py`, `fx.py`.
- Monster moves: `python tools/chr_dump.py disc/RST/<MONSTRO>.CHR [--keys] [--csv]` prints the punch table (position in the combo, type, window, damage, radius, stun, knockback, pause, locators) from key 0xFCC; read only. Layout in `FORMATS.md`, "Character". `python tools/chr_punch.py <file.CHR> <pos> <type> <id>` sets the animation of one slot in place (a custom monster's own `.CHR`, `MODS.md`).
- Memory card save: `python tools/mcsave.py <pasta com mc0/ e mc1/>` (the runtime's `discroot`) shows both cards as a single JSON, in the format of the `save.json` the runtime writes (`native/runtime/wotm_save.inc`); read only. Layout in `FORMATS.md`, "Memory card save".
- Assets to common formats and back (formats in `docs/FORMATS.md`; they read the user's `disc/`, nothing extracted goes to the repo; tested against the native code and with synthetic files, not yet tested with the disc):
  - `tex.py list|export|inject`: `.TEX`/`.RTX` textures to PNG with `manifest.json` (palette found through the `.NGP` objects) and PNG/TGA back, at the same size (current palette or `--new-palette`).
  - `snd989.py wav|vpk|vag|bnk-replace`: sound to WAV and WAV back to `.VPK`, `.VAG` and sounds inside a `.BNK` (ADPCM encoder that simulates the game's decoder).
  - `gltf.py <arquivo.NGP> <saida.glb>`: static geometry with textures in binary glTF (Blender, three.js, web viewer). Posed monsters not yet: the skeleton and the animation curves still need evaluating (see the docstring).
  - `lvl.py summary|objects|collision|points|mrs|peds|patch`: a stage as the level editor sees it: placeable objects (handle, position,
    states), the collision grid and paths (`--check` redoes every box and cell list by the disc's rules), the `PTS/*` points and the
    `.MRS` settings (`FORMATS.md`, "Stage objects", "Stage collision", "Stage points"). `Stage.footprint` gives the image ranges a
    deep copy of an object takes. `objects` ends a pickup's line with `on <handle>` when it rests on that object (`Stage.on_top`:
    inside the XY hull of one of its solid boxes, within 25 of the box's top; rooftop tanks and vents, and sidewalk items on a
    building's base). `patch <STAGE> <json>` applies a level patch (`MODS.md`, "Level patches") to the image the way the game hook
    does (objects, props, pedestrians, in that order), cuts the vehicle and pedestrian paths (`cut_nets`) and checks the collision of
    the result; `--mod <dir>` is the mod whose `props/` the `props` models come from (default: the folder above the JSON's `levels/`).
    The arena starts 0x80-aligned, as the hook's, so `arena bytes` is the same number on both sides. `peds <STAGE>` lists the
    pedestrian groups (handle, position, size) and the segment each one walks (`ped_groups`, `ped_bind`, `ped_report`).
  - `lvlglb.py <STAGE> <out.glb>`: the stage as a binary glTF for the level editor, one top-level node per `lvl.py` object (named by
    its handle, extras with kind, from, heading, states and `on`), its broken states as hidden `stateN` children, the rest under
    `rootN` (extras `ident`: 1 the world). The sky, the subtree of the node with id 2 at any depth, hangs under `sky` (extras
    `sky`) without the transforms above it; each sky mesh's node carries extras `obj` (its OBJECT's address, hex) and, under a `CHAR_INSTANCE`,
    `clouds`. Meshes are shared between instances. Colors are the GS's raw bytes (asset extras
    `gsRaw`): `COLOR_0` is the vertex RGBA / 255 and the textures keep the GS alpha (0x80 = 1.0, up to 0xFF), for a viewer that
    does the GS's modulation itself. Each material carries the GS state inherited from the nodes' ctx (`ngp.gs_apply`): extras
    `gs` (`blend`: ALPHA_1 when the mesh has ABE, else null; `aref`: alpha test ref or -1), `zmsk`, `fog` (the OBJECT's
    `fogBurnThru` below 255), `ot` (`alphaSort`) and, on the OBJECTs with `dirLight`, `dirLight`; meshes with ABE are `BLEND`. The editor itself is `tools/level_editor/`
    (`MODS.md`, "Level editor"; three.js r160 vendored in `web/vendor/`, MIT); its HUD panel is `hudapi.py` + `web/hud.js` and its
    pedestrians `pedsapi.py` + `web/peds.js`. `tools/build_level_editor.ps1` packs it as `WotM_LevelEditor.exe` (PyInstaller in its
    own venv in `<runtime>\level_editor_build`, no numpy; `web/_*` test pages left out); `tools/release.ps1` runs it.
    The editor's page has a smoke test, `node tools/level_editor/test_ui.mjs` (Edge headless, port 47393, Node 22, needs the disc):
    it loads CENTRAL, checks the ids other scripts click, an edit through undo and Save, every dialog opening and closing with Esc,
    and the server refusing a request the page would not send (`test_local_guard.py` covers that without a disc). It runs the
    editor program with its own settings file and a temporary mods folder (`WOTM_LEVEL_EDITOR_CONFIG`, `WOTM_LEVEL_EDITOR_NAMES`),
    so nothing of the user's changes; `--shots <dir>` saves a PNG per dialog. `node tools/level_editor/test_contrast.mjs` checks the contrast of the
    colors in `web/style.css` (text 4.5:1, a field's border 3:1, both themes) and needs no browser.
  - `lvlhud.py <STAGE>|--all`: the fight HUD a stage carries (level editor, HUD panel). The HUD nodes (ids 0x1C20..0x2133) and the
    fight font (id 18) are found by id, and each texture gets a role (bar atlas, special icon, BOSS label, player number, font,
    pickup ring, hint box, cinema bars, buttons), its file (`.TEX` or `.RTX`), its texId and the mod PNG name. `--all` prints the
    texId of each role in every stage and the version: A in 17 stages, B in CANYON1 and TOKYODM (other atlas, icons, font and
    meshes; no BOSS label).
  - `lvlnames.py <STAGE>`: names for the `lvl.py` objects (level editor): a content signature per object (vertices in the handle's
    frame and the decoded pixels of its textures, the same for the same model in any stage) and the rule names (special ids, pickup
    type, hit point class, size); model names by signature in `tools/level_editor/names.json`.
  - `lvlorig.py <scene.glb> <handle> <out.glb>`: one original object (a `prop` or `destr`) of a stage's `lvlglb` scene as a standalone `.glb`
    for `lvlprop.py build --units game`: its top node is the only root with its world matrix cleared, the broken states' meshes are named
    `state<N>_*` (a destructible prop), the materials lose the name `tex<texId>` (the prop gets its own copy of each image), and only
    what the object uses is written. The editor's palette calls it (`MODS.md`, "Palette").
  - `lvlprop.py build <in.glb> <prop dir> --stage <STAGE> [--units blender|game] [--scale S | --height H] [--name NAME] [--strength 1..5]`: a static (or, with `state1_*` meshes, destructible; `state1_debris_*` meshes are the chunks thrown when state 1 starts, `state1_smoke_*` empties its dust emitters)
    prop of one stage from a `.glb`, as a mini image prelinked at 0xA00000 (`<NAME>.NGP`, word 0 = 1, word 1 = the root) with a
    `<NAME>.PTR` in the monster layout (pointers; texIds and OBJECT GS contexts that hold the prop's texture slot until the loader
    writes the stage texId and a live context), a `<NAME>.preview.glb` decoded back from the packets it wrote (`lvlglb`'s
    conventions; materials name `tex<texId>`, images only with `--preview-tex`; an image of the prop's own is embedded as material
    `own<slot>`) and a `prop.json` (stage, slot -> texId or the prop's own image: `<NAME>_T<slot>.png`, packet offset, game size; template,
    bounds, collision). Tree: GROUP (0x0003) -> one OBJECT per texture and per "has COLOR_0" (split at the 13-bit `numVerts`) +
    GROUP 0x0102 -> a
    6-plane `COLL_VOL` box from the bounds (or the `hull_*` planes). The OBJECT header and GIF tags come from the stage's first OBJECT that round-trips in
    `vifpkt` with fog, one texture and the asked ABE (rule `first-object-roundtrip`); strips keep each triangle's winding (a strip
    that would start at an odd place of its section repeats its first vertex). `blender` units: Z up, meters, 18 game units per
    meter by default, COLOR_0 linear to sRGB x 128; `game` units: a piece of `lvlglb`'s export, raw colors. Materials `tex<texId>`
    reuse the stage's textures; other images, `state<N>_*` meshes and skins are refused, A `coll_*` mesh is collision by triangles: its faces (at most 16384 triangles) become a `POLY_COLL` leaf of one object per block of at most 512 triangles and 256 vertices, matId `0x10A` (or the `coll_m<hex>_` name), bbox bits of each triangle's extreme vertices; a block of more than 8 triangles gets the box tree the game's query walks (leaves of up to 8 triangles, origin at the middle of the box, scale 32768 over its longest side; `poly_tree_query` and `verify_poly_leaf` read it back, and `check` runs the second on every leaf);
    one leaf per `coll_*` mesh. A `hull_*` mesh is the prop's collision: the convex hull of its vertices (at most 64 distinct, at most 40
    faces) becomes a `COLL_VOL` of its planes, one leaf per `hull_*` mesh; without one, the 6-plane box of the visual stands in. `--light auto`
    (default) turns on dirLight (0x100, the stage's three parallel lights) on the OBJECTs without COLOR_0 and leaves it off on the
    ones with it (light baked in Blender); `vertex` and `dir` force it off or on. `info <prop
    dir> [--at ADDR]` relocates a prop (default the level arena, 0x3700000) and reads every node, packet and fixup back;
    `load_prop(folder)` returns the `Prop` (`ngp`, `ptr`, `root`, `textures`, `bounds`, `relocate(at)`, `resolve(at, ctx_of)`) the
    level patch uses, and `stage_ctx` the context a texId takes. `modkit.py level prop <mod> <in.glb> --stage <STAGE> [--name N]` builds the prop into `mods/<mod>/props/<NAME>/` (extracted disc folder). Standard library only.
  - `lvlterrain.py <heights.png> <out.glb> --size X Y --height H [--cells N] [--colors colors.png] [--brightness P]`: a PNG of heights as a `.glb` for `lvlprop.py build` (units blender, no scale): a grid of at most 90 cells along the longer side, white = H, as the drawn mesh (vertex colors from a ramp or the colors PNG, shaded by the slope, 0.6 under) and the same triangles as a `coll_terrain` mesh; `terrain_glb(data, size_x, size_y, height, cells, colors, brightness)` returns (glb, info). The editor's New terrain dialog calls it (`MODS.md`).
  - `ngp.decode_mesh`: port of the native `decodeObject` (texture per section, ADC like the GS, sprites), used by `gltf.py`.
- `ste/` reads a *Godzilla: Save the Earth* (PS2) ISO the user owns; nothing it writes goes to the repo. `ste/gen_viewer.py <iso> <viewer.html>` builds a standalone WebGL viewer of its monsters (meshes, skeletons, named animations, textures); `ste/export_dae.py <iso> <Godzilla2k.cmp|godzilla2k.bdp> <dir>` writes a rigged Collada that `retarget.py` takes (`rigmaps/ste_godzilla2000.json` maps its Biped on Togera). The file formats are documented in the docstrings of `ste/ste_model.py`, `ste/skeleton.py` and `ste/anim.py`.
- `.sh` scripts must be created by heredoc in bash; writing with Python on Windows leaves CRLF and the WSL `sh` breaks (`sed -i 's/\r$//'` fixes it).
- The per-function state lives in `config/status.csv` (`python3 tools/progress.py`, ~12 s via WSL, needs `disc/`); the README has no progress block.
- The `/wotm` mod (HUD) loads with `startup_command.bat` (in `.gitignore`), which starts Claude with `--plugin-dir ~/.claude/my-plugins/wotm-hud`.

## Shared headers (include/)
Rule: a class/API used by more than one TU lives in a header; do not partially redeclare it inside the `.cpp`.
- `engine.h`: engine API (math, timers, `_animHandle`/animation*, particleKillFx, hier/hd). `game/game.h` includes it.
- `game/game.h`: `Monster` (0x11190 bytes: `m_cs`, `m_playerNum`, `m_id`, `m_health`, `m_stamina`, `m_target`, `m_camUnify`), `TheGame`
  (`m_huds[4]`, `m_slots[16]` = the `Monster`s at 0xB80, `m_monsters[]` = pointers, `m_gravity`, `m_gameMode`, `m_matchMode`, `m_levelId` (1 central, 2 vegas, 3 canyon2,
  5 airport, 6 threemile, 7 sanfran, 8/15 island, 9 tokyo, 10 ufo, 11 final boss, 26 bigshot, 27 crush), `m_numSlots`, `m_numMonsters`, `m_levelIdx`, `m_playerMask`),
  `Cameras` (`m_cameras`, `m_state`), accessors `gameSlotBase(idx)` (keeps the `idx*0x11190 + 0xB80` order), `gameHud(i)`, `gameWeapons()`.
- `point_tool_kit.h` (base of PathTool/PowerUpTool/StartPointTool/AiPathTool; derived classes call `PointToolKit::init/loadPoints/getPoint`), `task_manager.h`, `bidir_link.h`,
  `cs_pool.h` (all static), `game/{shell,hit_history,pickup,military_pickup,pickup_sound,vehicle_navigator,hud,weapons,power_ups,start_points,stamina_meter,crush_level,levels,streaming_sound}.h`.
- Moving a class to a header can change `sizeof` and shift fields of partial structs that embed it (it happened with `StaminaMeter`): always compare before/after.
- Functions the retail calls with `this` even without using it (e.g.: `StartPoints::getNumPoints`) only match if declared non-static; `isThisTypeFull` is static.
- Not unified yet: `GamePad` (`GamePad.cpp` sees 6 ints, `GamePadClipPlayer.cpp` sees bytes), `GamePadClipPlayer`/`AiPadClips` (the clip type diverges between `AiGrapple` and the others),
  `Ai`, `Destructibles`.

## Decomp state in `TheGame` (source: `config/status.csv`)
- `src/game/TheGame.cpp` converted. Equivalent: `Update`, `InitAfterDbLoad`, `ResetLevel`, `UpdatePadTweaks`, `gameResolveLifeAndDeath`; matched: `Update2`, `SetGravity`, `SetOkToUnify`, `GetNumAIsAlive`. Still in asm: `InitBeforeDbLoad` and `gameCheckForCloseCombat`.
- The decomp only advances on the mod surface: `Shell::Load*`, `MonsterParse`, camera. For what is missing in each TU, `grep -c INCLUDE_ASM` or `status.csv`.

## Differential test (tools/difftest.py, tools/difftest_all.py)
Runs the retail function and ours (compiled with `-DNON_MATCHING`) in a MIPS emulator (unicorn), with the same random state, and compares the return value, calls to other functions
(the callees become `jr ra`; name + relevant arguments, with strings compared by text and function pointers by name), bytes changed in the arena and in the retail data, and the variables
the TU itself defines (aliased to the retail copy). It serves to validate the "equivalent (untuned)" functions without PCSX2.
- One function: `~/.venvs/wotm/bin/python tools/difftest.py game/Monster isFullHealth__7Monster --args this --ret int --runs 60 [--objsize 0x122000]`
  (`--args` accepts `this,p,i,b,f`; `--alt OUTRAFUNC` is the negative control, it must give MISMATCH; `--objsize` keeps random pointers outside the object, use 0x122000 for `TheGame`).
- All equivalents of some TUs: `python3 tools/difftest_all.py game/Monster game/TheGame ... --runs 30` (guesses the signature from the mangled name and the return from the definition).
- Kind of error it catches (examples of equivalents that diverged from the retail: Vehicle::setCs/setPos, StaminaMeter::credit, UpdatePadTweaks, TheGame::Update (2nd TaskManager),
  MonkeyChains::init (FxTextureId is an enum), AnimContact).
- Limits: it does not execute VU0/COP2 nor `min.s`/`madd.s`; functions with virtual methods or valid linked structures are skipped ("skipped"); more than 4 integer args or 2 floats are not supported;
  it is evidence (only the paths the random state reaches), not proof. Run it on every new equivalent function before committing.
- Emulator pitfall: a memory write hook breaks jumps with a store in the delay slot; that is why we compare memory images at the end, not writes.

