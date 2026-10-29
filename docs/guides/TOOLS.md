# Tools, shared headers and differential testing

(Doc index in `CLAUDE.md`.)

## Tools (tools/)
- `wsl/gate.sh` (build + SHA1, exits with an error if it fails), `wsl/scoreall.sh` (per-function score with NON_MATCHING), `wsl/convert_tus.sh`, `wsl/check.sh`,
  `ccmatch.py`, `nm_wrap.py`, `new_tu.py`, `place_data.py`, `progress.py` (writes `config/status.csv`), `callgraph.py`, `m2c.sh`, `wsl/variants.py`, `wsl/permute.py`.
- Per-TU verification in one command: `~/.venvs/wotm/bin/python tools/verify_tu.py game/<TU> [...] [--runs N] [--no-nm] [-v]` runs the score (`ccmatch`), `difftest_all`, `nm_retvals` and, unless `--no-nm`, a `build_nm.sh verify` with only those TUs for `nm_calls` and `nm_audit`; it prints a table per function (score, difftest, calls, layout, return) and exits with an error if some equivalent differs in the difftest, in the calls, or has a consumed `void` return. With the ELF it takes ~4.5 min (the `build_nm`); without, seconds. `sh tools/m2c_batch.sh [csv | tu...]` generates the m2c drafts for the whole queue (`config/boot_coverage_asm.csv` by default) in `build/m2c/<tu>/<função>.c`, in parallel and only what is missing.
- Native layer and runtime: `native_syms.py` (`--gen` writes `native/wotm_syms.h` with `wotm::sym::<dado>` and `wotm::fn::<função>` from `symbol_addrs.txt`; `--audit` lists the hex literals in `native/` that match a symbol, fall inside one, or match none; `--apply` replaces the exact ones with names, only in code, and includes the header where missing), `vu1cmp.ps1` (VU1 x native check and images from snapshots), `corpus.ps1` (regression corpus: `-Capture` records menus and fights in `<runtime>\corpus`, `-Check -Baseline <csv>` says what got worse after a change in `native/`), `build_runtime.ps1` (or `build_runtime.bat` at the root), `lote.ps1`, `wotm_env.py` (checks the `WOTM_*` variable table in `NATIVE.md` against the code), `vu0native.py`, `vu1dis.py`, `fx.py`.
- Monster moves: `python tools/chr_dump.py disc/RST/<MONSTRO>.CHR [--keys] [--csv]` prints the punch table (position in the combo, type, window, damage, radius, stun, knockback, pause, locators) from key 0xFCC; read only. Layout in `FORMATS.md`, "Character".
- Memory card save: `python tools/mcsave.py <pasta com mc0/ e mc1/>` (the runtime's `discroot`) shows both cards as a single JSON, in the format of the `save.json` the runtime writes (`native/runtime/wotm_save.inc`); read only. Layout in `FORMATS.md`, "Memory card save".
- Assets to common formats and back (formats in `docs/FORMATS.md`; they read the user's `disc/`, nothing extracted goes to the repo; tested against the native code and with synthetic files, not yet tested with the disc):
  - `tex.py list|export|inject`: `.TEX`/`.RTX` textures to PNG with `manifest.json` (palette found through the `.NGP` objects) and PNG/TGA back, at the same size (current palette or `--new-palette`).
  - `snd989.py wav|vpk|vag|bnk-replace`: sound to WAV and WAV back to `.VPK`, `.VAG` and sounds inside a `.BNK` (ADPCM encoder that simulates the game's decoder).
  - `gltf.py <arquivo.NGP> <saida.glb>`: static geometry with textures in binary glTF (Blender, three.js, web viewer). Posed monsters not yet: the skeleton and the animation curves still need evaluating (see the docstring).
  - `ngp.decode_mesh`: port of the native `decodeObject` (texture per section, ADC like the GS, sprites), used by `gltf.py`.
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

