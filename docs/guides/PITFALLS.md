# Pitfalls seen (NM decomp / build / game)

(Doc index in `CLAUDE.md`.)

## Pitfalls seen
- When rewriting the end of a `.cpp` with a script, check that the final `INCLUDE_ASM` lines (static init, `__tf`, ctor, `_GLOBAL_$I$`) are still there.
- `ccmatch.py` without `-DNON_MATCHING` only compiles the `INCLUDE_ASM` (everything "MATCH"); to score the new C++ use `-DNON_MATCHING` (`scoreall.sh` already does).
- Layout of the point tools: points of 0x40 bytes, `numPoints` at 0x4000, own fields from 0x4050; `levelData = gameSlotBase(game->m_levelIdx)`.
- `shell` is gp-relative in some TUs (PowerUpTool, PathTool, AiPathTool) and not in others (StartPointTool): `__asm__("#SNFIX_SMALL shell")` only where the retail uses gp.
  Inside a delay slot (`.set nomacro`) a global access is always gp, with no need for the pragma (tokyo).
- gas inserts 2 extra `nop` in a `.p2align 3` right after a `li.s` sequence (StickShaker::DefaultSetup); the retail does not have them. No cause found: mark as equivalent.
- `_fvector` has alignment 16 in ee-gcc: a class with a `_fvector` member is aligned to 16 and its `sizeof` rounds up to a
  multiple of 16 (e.g.: `IslandLevelSound` 0xF4 becomes 0x100). When embedding these classes in a bigger one, do not put manual padding between
  them; check with `typedef char x[sizeof(T) == n ? 1 : -1]` (game/Sound).
- `(x & 1) == 0` instead of `!(x & 1)` changes `lw`/`xori` to `ld`/`andi` with a 64-bit field (MilitaryPickup::kill).
- A `_fvector` copy via `lq/sq` with `jr` followed by `nop` indicates `asm volatile` with `lq/sq` in the retail (setFormationPos, setTrans); register `$2` pinned with `register int t __asm__("$2")`.
- A function call inside the arguments of the final call (comma operator) changes the prologue scheduling and made `loadPoints` match (PathTool/AiPathTool).
- Intermediate pointers (`VehicleNavigator *n = &nav; n->f14 = ...`) imitate the retail's `addiu v1,v0,0x210`.
- Strings of a function that move from `INCLUDE_ASM` to C change the `.rodata` padding: add `.word 0` in `.rodata` via asm.
- `switch` in C generates a jump table; the retail aligns at 24 words (add `.word 0` x2).
- Classes with a vtable: ctor, `__tf` and `_vt$...` stay as `INCLUDE_ASM`; write the methods without `virtual`.
- Static functions with no arguments sometimes have a `v` at the end of the retail symbol: use `__asm__("nome__Classev")` on the member.
- Matrix mangling: `float (*m)[4]` becomes `PA3_f` in gcc 2.95.
- The tool's heredoc can turn `\n` into a real line break inside Python scripts: for files with regex, use Edit.
- `sh tools/wsl/gate.sh | tail` hides the exit code: to commit only with the ROM OK use `sh tools/wsl/commit_if_ok.sh && git commit ...`.
- A gp-relative global already defined in the `.sdata` of a data asm (e.g.: `WaterLevel`): declare `extern float X; __asm__("#SNFIX_SMALL X");`, never define it again (duplicate symbol at link).
- Storing to a `struct` field (e.g.: `gameHud(i)->f0D0 = 0`) does not invalidate the already loaded `game`; storing via `*(int*)((char*)p + off)` invalidates it and gcc reloads. That is why `Hud` has real fields.
- `EnemyInfo::s_info[i][j']` (include/game/enemy_info.h): pairwise table, `j' = j - 1` when `i < j`. `vecLenSq` (vecmath.h) = the retail's `mula.s/madda.s/madd.s`.
- In `for (j = 0; j < n; j++, m++)` loops keep `n = game->m_numSlots` in a local (otherwise gcc reloads it on every iteration).
- **`ccmatch.py` masks the names of the called symbols** (relocations): a function can give MATCH while calling a callee with the wrong name/mangling (local class `MemoryStackG` instead of `MemoryStack`, `transitionOK(Monster*)` instead of `transitionOK()`). It only counts as "matches" after `gate.sh` (real link) with the function outside `#ifdef NON_MATCHING`.
- **Variable vs object in `__asm__("sym")`**: `shell` and `game` are POINTERS (`Shell *shell`); `extern char x[] __asm__("shell")` gives the address of the variable, not of the object, and `x + 0x2C30` silently corrupts memory (the game froze ~30-40 s later, with `fileStatus`/`cdFileSystemToc` overwritten, `getNgpFilesLoaded()` = 80 and the animation loop reading garbage: `cpuTlbMiss` in `animationManager`/`AnimPlayer::UpdateAnimations` in the log). Only use `char[]` for real objects (`gsPkt`, `_7Cameras$m_cameras`, `_12BigShotLevel$instance`); for pointers use `extern T *x`. `ccmatch.py` and the gate do not catch this (masked relocations); only playing does.
- **An NM build with a link error leaves the OLD `halfcpp.elf`** and `play.ps1` keeps saying "OK". Always read the output of `build_nm.sh` looking for `undefined reference` (use `| tail -3`, do not hide it).
- **Order in the file**: each new function goes where its `INCLUDE_ASM` was (address order). Declarations, macros (`GM`, `SHI`) and local classes that an NM function uses must be ABOVE the first use in that order; functions that match (outside the `#ifdef`) can only use declarations that are also outside the `#ifdef`.
- Calling a retail symbol with a "creative" signature (floats in `$f12`, loose `this`): declare a free function with `__asm__("nome__Mangled")` (see `src/game/rt.cpp`, `SYM(...)` block) instead of guessing the class; for class `static` variables: `extern char x[] __asm__("_12BigShotLevel$instance")`.
- m2c shows extra arguments in calls (`transitionOK(this, monstro)`): the mangling says how many parameters really exist (`transitionOK__12StateVictory` = only `this`). Check the symbol name before writing.
- `extern int x;` of a gp global can come out as `lui/%lo` on the first store (instead of `%gp_rel`); the remedy is the `__asm__("#SNFIX_SMALL x")` described above (not tested in this case: `numModsLeft`, `_7Cameras$m_numCameras` stayed equivalent).
- **`sqrtf` in NM C++ comes out with `sqrt.s` in the wrong encoding.** The R5900 uses `SQRT.S fd, ft` (source in `ft`, `fs=0`; retail `0x46020084`); gas/ee-gcc emits the MIPS32 form with the source in `fs`
  (`0x46006044`), which on the EE reads `$f0`. In `AiPathFinder::computeCostEstimate` this discarded the z term (proven by running retail × our recompiled version, 300/300 diverging). The `difftest` (unicorn)
  does not see this. Use `eeSqrtf` (`include/vecmath.h`, `.word 0x46040104`) instead of `sqrtf` in NM code; to sweep an NM ELF: wrong form = `ft==0 && fs!=0` (the retail has 127 `sqrt.s`, all `ft`).
- Fresh clone: `configure.py --split` can fail to generate the data `.s` files (vtables such as `_vt$12PointToolKit.s`) and `ninja` stops on a missing file (splat 0.41.1 and 0.50.0).
  Workaround: take `src/` out of the repo, run `--split` and put `src/` back. The cause was not investigated.
- Wait loops on a hardware register (`objsInPacket`, `gRtReturn`): declare `volatile`; without it gcc turns it into an infinite loop/hoisting.
- The order of stores in a small struct can invert in gcc 2.95 (`rtReturnToShell`: write `code, active, delay` to get `delay, active, code`).
- Automatic test: `powershell -ExecutionPolicy Bypass -File tools/pcsx2_auto/play.ps1 halfcpp [-pause]` (without `-File`/`Bypass` PowerShell refuses the script). It only exercises 1 player/free-for-all; 2P, elimination, minigame and story screens are not covered.

