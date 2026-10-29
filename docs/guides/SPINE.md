# Game spine: load chain, menu, game loop, monster states

(Doc index in `CLAUDE.md`.) This file holds **what is known about the game flow**, gathered while decompiling the spine. What is or is not in C++ is in `config/status.csv` (`grep <função>`), not in this file.

## Load chain: from `main` to the monster on the map
Flow of `main` (comment in `src/game/Shell.cpp`): boot -> intro/outro -> menus (`userintMain`) -> session: `InitRTState`, `BootInitGame`, `InitBeforeDbLoad`, `LoadLevelFiles`, `FinishLoadBar`, `FadeScreen`, `InitAfterDbLoad`, `InitPlayers`, `initAfter` of the 4 `Hud`s, per-mode init (BigShot/Crush/DodgeBall), `UpdatePadTweaks`, sounds, `rtMain` in a loop with `EvaluateGameStatus`.
- Loading: `Shell::LoadLevelFiles` -> `LoadLevelDB`, `LoadMonstersDB`, `LoadResTexture`, `LoadTexture` -> `dbsRelocateFileZero` (`.PTR`, TBP0) -> `dbInitDb` -> `dbsRelocateViaPtrListFile` (`mon/<nome>.ptr`) -> `dbsTraverse` (+ `dbsPush`/`dbsPop`) -> `dbProcInteractive` (object id table). Formats and the id table in `docs/FORMATS.md`.
- Monsters: `Shell::InitPlayers` -> `TheGame::MonsterParse/AddMonster/GetMonsterFromName/SetPlayerMonster/SetAIMonster` (create the slot, register in `Interactives`) -> `Monster::initBeforeDbLoad/playerInit/aiInit/initAfterDbLoad`. `Monster::playerInit` jumps (`j`) to `aiInit` with the same registers.
- The `file*` and `zip*` functions are a platform layer (the port reads files directly).
- Validation of this chain: halfcpp in PCSX2 (`PCSX2_TESTING.md`; `idx.py` checks that the ids and `Interactives` indices match) and the game running in the runtime.

## Menu (`game/ui`, `game/screen`)
- The `screen*` screens use `screenGetInput(1)`: it returns `(pad << 16) | ação`; actions 1/2 = up/down, 3/4 = left/right, 5 = back, 6 = confirm. Each screen changes `currentSelection[currScreen]` and swaps the shell's `hierSetSwitch`.
- Game modes stored in `shell->m_mode` (and `shell+0x2B54`, the "default" mode restored by `resetGameMode`): 2 = 1-player challenge against the CPU (what `WOTM_MATCH` fills in), 3 = 2P free-for-all (`PO_MPFreeForAll`), 6 = 2P elimination (`PO_MPElimination`), 7/8/9 = the minigames of stages 25/26/27.
- Stages 10, 11, 14, 15, 25, 26 and 27 only open with the flag from the `monsterSelectMode` table (`levelUnlocked`). The bonus monster widens the selectable range from 1..9 to up to 10 (`findAvailableMonster`).
- `currScreen` 0x14 = `screenWaitForStart` (Press Start): it only accepts `screenGetInput(1)==0x10` (Start) when `betweenScreens==0`. 0x10 only does `changeScreen(0x10->0x14)`, 1 = menu, 0x63 = loading (`Shell::FadeScreen`, `DisplayLoadBackground`); inside the stage `currScreen=1`.
- The port can replace the menus with its own UI; the check inventory lists what they draw (`VU1CHECK.md`).

## Game loop (`game/rt`)
- `rtMain(first)` is the per-frame loop: `startFrame` → input → per view: `CullView`, GS double buffer, `viewUpdate`, HUD, `hier(view,0)`, `TheGame::Update` (view 0 only), `animationRunGlobal`, `Update2` (view 1 or the only one), particles, `TaskManager(debris)`, `hier(view,1)`, DMA; at the end of the frame `updateLevelObjectSoundManager`, `updateSoundManager`, `g_frame++` and the time pacing (`timerWaitForMinUpdateRate`; `dt` details in `NATIVE.md`, "Physics with `dt = 1/updateRate`").
- It returns the code given to `rtReturnToShell` (2 = pause/dialog, 3 = quit, 5 = end session, 0/1/4 = end of stage). `rtPauseRT` (START or controller disconnected → `rtReturnToShell(2, bit do campo GS)`).
- The codes come from `Shell::EvaluateGameStatus` and the per-mode `Evaluate*Status` (story, challenge, FFA with/without AI, endurance, bigshot, crush, dodgeball). Without `m_killTarget` (0), `EvaluateOnePlayerChallengeStatus` only raises the downed monster (`reinitSlot`) and the fight goes on without the end dialog.

## Monster states and AI (for when the decomp gets back to them)
- Big blocks still only in asm: `MonsterStates`, `SpecialStates`, `MovementStates` (TU converted; only getters done), `AiReflex`, `AiSeek`, `Weapon`/projectiles (`DetonateWeapon` 0x1220), `Hud` (update/print), `Cameras::Update`. `Sound`/`StreamingSoundManager`/`MonsterMc`/`McPage`/`McFile` are platform: only the contract matters.
- **Monster states:** base in `include/game/monster_state.h` (`id`@0, `flags`@4, `owner`@0xC, `vptr`@0x10; vtable `{delta, 0, função}`; ctor and vtable stay asm, methods without `virtual`). Each state lives embedded in `Monster` at a fixed offset (Recoil 0x7E30, Block 0x7DA0, Stunned 0x10714, Shocked 0x10BA0, Grappled 0xDDCC, victory 0x10E70, special state 0x7980). Suggested order: `transitionOK/transitionFeasible/getRelevantConfig` (small) -> `update` of each state -> `transitionInto/handleCollis`.
- **AI:** `AiReflex` and `AiSeek` depend on `AiBrain`/`AiNavigator` (already in C++); navigation (A*, `seek/arrive/tag`) is already done. `AiNavigator` layout: `monster` 0x0, `turn` 0xC, `strafe` 0x14, `mode` 0x18, `status` 0x1C, `ObstacleSensor` 0x30 (0x130 bytes), `AiPathFinder` 0x160 (0x28), target 0x188..0x19C, `PathInfo` 0x220/0x240 (0x20 each).
- Codegen tricks seen in these TUs: `(int)(bits >> n) & 1` generates the retail's `dsll/dsra32`; to test a bit of an `unsigned long long` in an `if`, keep `bits & m` in a local `unsigned long long`; `lq/sq` via `asm volatile` with `$2` pinned; `fabsf` (extern "C") generates `abs.s`; `max.s`/`rsqrt.s` only via `asm`; `permute.py` needs N <= the number of possible orders (otherwise it hangs).

## Gameplay observations without a cause yet (halfcpp)
- The AI sometimes repeats the same action endlessly (it may be behavior of the original game).
- Two cases of "teleport" after a heavy hit that throws the monster: the last `HitEvent` was type 3 / subtype 30, handled only with damage (same as the retail); the cause may be the knockback/physics outside `takeHit`. Not confirmed.
