# Game file formats (what the decompiled code reveals)

Source: `Shell::LoadLevelFiles`, `LoadLevelDB`, `LoadMonstersDB`, `LoadResTexture`, `LoadTexture` (`src/game/Shell.cpp`),
`dbsRelocateFileZero`, `dbsRelocateViaPtrListFile` (`src/common/dbs.cpp`), `dbInitDb`, `dbProcInteractive` (`src/game/db.cpp`).
Everything below was read from the code; what is marked (?) is still a guess.

## Files per stage and per monster
| Extension | Contents | Loading |
|---|---|---|
| `lvl\<fase>.NGP` | level image (node hierarchy), zip-compressed | `zipInflateAll(path, 0xA00000)`; then `fileOnlyNgpFile` |
| `lvl\<fase>.TEX` | level textures | `getNextNgpLoadAddr` + `fileOnlyTexFile` |
| `lvl\<fase>.RTX` | level resource (`res`) textures | `fileOnlyResFile` |
| `lvl\<fase>.PTR` | fixup list of file zero (see below) | `dbsRelocateFileZero` |
| `<monstro><costume>.ngp/.tex/.rtx` | a monster (model, texture, resources); `<monstro>` comes from `MonsterLongNames`, `<costume>` is the costume number | `fileReadf(path, getNextNgpLoadAddr/Res/Tex())` |
| `mon\<nome>.ptr` | pointer fixups of monster `<nome>` | `dbsRelocateViaPtrListFile` |
| `shell\shell|load|ui|shella|preshell.ptr` (+ `.ngp/.tex/.rtx`) | shell screens (menus, loading, UI) | same as file zero |
| `host0:monster.ngp/.tex/.rtx` | development path (PC host), used when the stage index is >= 0x1D |

Monster names (`MonsterLongNames`, 8 bytes per entry, index = monster): `null, monkey, robot, lizard, shogun, mantis, spider,
energy, lava, dragon, rock, alien, ogre, assboss, jelly, final, temp16` (17 entries; the `default` right before it in the binary is stage 0). Each slot's selection is `(monstro << 5) | variante`
in `Shell::m_monsterSel[]` (4 players, then AIs); the costume is in `m_costume[]`. In mode 6 (minigames?) it loads monsters 1..5 and 7..11.
Retail quirk: for the 3rd and 4th player the file path uses player 2's monster.

### File names (`Shell::formatFilename`, `formatFilename1`)
All start with the disc root prefix (`D_006F81E0`, an empty string in the retail), so the paths become `\LVL\...` and `\MON\...`.
| Call | Type | Path |
|---|---|---|
| `formatFilename(dst, nome, ext, tipo)` | `SH_FILE_0` (stage) | `\LVL\<nome>.<ext>` |
| | `SH_FILE_PLAYER` | `\MON\<nome>.<ext>` |
| | `SH_FILE_AI` | `\MON\<nome>0.<ext>` or `\MON\<nome>1.<ext>` (below) |
| `formatFilename1(dst, nome, traje, ext, tipo)` | `SH_FILE_PLAYER`/`SH_FILE_AI` | `\MON\<nome><traje>.<ext>` (costume in decimal) |
| | `SH_FILE_0` | `\LVL\<nome>.<ext>` |
| `Shell::formatFilename(dst, dir, nome, ext)` (static) | point tools files on the host | `<dir>/<nome>.<ext>` |

AI suffix in `formatFilename(..., SH_FILE_AI)`: `1` if `m_mode` is 0 or 1 and `m_levelNum == 3`; otherwise `0` if `m_mode != 1`; in mode 1, if the
AI of slot 4 or 5 uses the same monster as player 1 (`m_monsterSel[0]`), `1` when player 1 is in costume 0 and **nothing is written** when
they are in another costume (retail quirk: `dst` stays as it was); without that conflict, `0`. In other words, the AI loads the other skin so it does not
look the same as player 1.

`LoadLevelFiles` looks up `<raiz>\LVL\<fase>.NGP;1` and `.TEX;1` on the CD (`fileCdSearchFile`) to know the sizes (progress bar) and
loads, in this order: level (`.ngp`), monsters, resource textures, textures; then `dbsRelocateFileZero` and `dbInitDb`.

## `.PSS` movies
`disc/MOV/*.PSS` (52 files). MPEG-2 program stream in 2048-byte packs (MPEG-2 pack header `00 00 01 BA`, one `BB` system header; `tools/pss.py` and `ffprobe` show the structure):
- **Video:** stream `E0`, MPEG-2 MP@ML at 29.97 frames/s. Full screen at 640x368 with 16:9 aspect ratio: `INTRO_M`, `INTRO_S`, `INT_MAIN`, `INT_ACT1..3`, `INT_TRAN`, `<monstro>_G` (endings), `<monstro>_P`, `CREDITS`. The others are small and without audio: 256x128 (`SHELDEMO`, the menu movie, 173 s long; monster and stage names like `ALIEN`, `CENTRAL`, `TOKYO`: previews, probably), 254x128 (`3-MILE`, `AIRPORT`, `ISLAND`) and 256x160 (`MANTIS`, `ROBOT`).
- **With audio** (in the first 4 MB): `INTRO_M`, `INT_MAIN`, `CREDITS` and the `_G`/`_P`, except `ALIEN_G` and `DRAGON_G`. `INT_ACT1..3` have none (`playMovie` calls them with `comAudio` = 0; it also mentions an `int_act4.pss` that does not exist on the disc); `INTRO_S` and `INT_TRAN` have none either, and do not appear in `playMovie`.
- **Audio:** private stream `BD`, substream `FF`, 4 header bytes per PES. It starts with `"SShd"`, size (0x18), type (1 = 16-bit little-endian PCM), rate (48000), channels (2), interleave (0x200) and two `-1`; then `"SSbd"`, data size and the data: 512-byte blocks alternating left and right channel (256 samples each). FFmpeg does not recognize this audio (it only sees the video).
- **In the game:** the retail plays through `sceMpegDemuxPssRing` + IPU; the native layer (`native/wotm_fmv.hpp`) reads the file directly. The menu movie becomes the texture of a 3D menu object: `moviesSetScreenObject` swaps the texId for 999 (the last of `texInfo[1000]`, reserved) and TEX0 for CT32 at 0x3BA0, width 320 (`VramAddrForMovies` 0xEE800 words / 64, `VramWidthForMovies` 0x140), with `TW`/`TH` = 8/7.

## Sound: `.BNK`, `.VPK`, `.VAG` and the 989snd API
Read with `tools/snd989.py` (banks, statistics, WAV export) and played by `native/wotm_snd.hpp` (`native/sndplay.cpp` writes WAV without the game).

**Path on the EE.** `game/Sound` (`SoundManager`, monster, vehicle, weapon and stage sounds), `game/StreamingSoundManager` (streamed music and speech) and `common/SoundBank` (`SoundBankManager`: bank loading and unloading) call the `snd_*` of `lib989snd`. Each `snd_*` packs the arguments and sends a numbered command to the 989snd driver on the IOP through `sceSifCallRpc` (`snd_SendIOPCommandAndWait`/`NoWait`, with batching in `snd_SendCurrentBatch`, RPC command 0x4D). Examples: 0x09 `SetMasterVolume`, 0x11/0x12 `PlaySoundVolPanPMPB` (with/without wait), 0x15 `StopSound`, 0x18 `StopAllSounds`, 0x19 `SoundIsStillPlaying`, 0x21 `SetSoundParams`, 0x2A `InitVAGStreamingEx`, 0x2C `PlayVAGStreamByLoc`, 0x2F `StopVAGStream`, 0x38 "stream-safe" CD read, 0x4A `SRAMGetFreeMem`. The `_CB` variants deliver the result by calling `cb(valor, user)` on the EE (the game almost always passes `soundPlayCB`, which writes `*user = valor`); the `_A` ones return nothing. The `LIBSD`, `989SND` and `989ERR` IRX are loaded in `inputInit`. The game uses about 40 `snd_*`; it does not use MIDI (`snd_MMDLoad*` is never called).
- `snd_BankLoadByLoc(lsn, 0)` / `_CB`: bank by disc sector (`\SND\<NOME>;1` via `fileCdSearchFile`); returns an IOP pointer used only as an identifier.
- `snd_PlaySoundVolPanPMPB(banco, som, vol, pan, pitchMod, pitchBend)`: vol 0..0x400 (0x400 = the bank's volume), pan in degrees (0 = front, 90 = right, 270 = left; -1 = the bank's). Returns a handle; `SoundIsStillPlaying(h)` returns `h` or 0.
- `snd_SetSoundParams(h, máscara, vol, pan, pm, pb)`: mask 1 = volume, 4 = pan, 8 = pitch mod, 0x10 = pitch bend (deduced from the game's calls: 5, 0xD, 0x15).
- `snd_SetMasterVolume(grupo, vol)`: groups used 0 (effects), 1 (music), 3, 4, 5, 6, 8. Each sound in the bank has its group; streams get the group in the call.
- `snd_PlayVAGStreamByLoc(lsn, lsn2, off1, off2, vol, pan, grupo, fila, flags)` (+ `cb, user` on the stack in `_CB`): the handle works for `SetSoundParams`, `StopSound` and `SoundIsStillPlaying`. In stages, the ambient music (`*AMB.VPK`) and the battle music (`*BAT.VPK`) start together with volume 0 in group 1 and `StreamingSoundManager` crossfades them through `SetSoundParams`.
- `snd_StreamSafeCd*`: they only divert to the IOP after `snd_InitVAGStreamingEx`; before that they become `sceCdRead`/`sceCdSync`.

**`.BNK` (`disc/SND`, 43 files).** Header: `u32 tipo (3)`, `u32 nChunks (2)`, `{u32 offset, u32 tamanho}` per chunk; chunk 0 = `SBlk` block, chunk 1 = SPU2 ADPCM samples (what the driver copies to SRAM). `SBlk` version 1 (the same format as Jak & Daxter's 989snd, read by OpenGOAL): `"SBlk"`, version, flags (4 in all: no names nor user data), 4-letter id reversed (`LEHS` = SHEL), `s16` number of sounds/grains/VAGs at 0x16, `FirstSound` (0x1C) and `FirstGrain` (0x20). Sound (12 bytes): `s8 vol, s8 grupo, s16 pan, s8 nGrains, s8 limite de instâncias, u16 flags (1 = laço), u32 primeiro grain`. Grain v1 (0x28 bytes): `u32 tipo, s32 atraso` in 240 Hz ticks and 0x20 bytes of parameters. `TONE` (type 1) plays a sample: `s8 prioridade, vol, CenterNote, CenterFine; s16 pan; s8 mapLow, mapHigh, pbLow, pbHigh; u16 ADSR1, ADSR2, flags; u32 offset da amostra`. Negative volume and pan read registers (-1..-4 of the sound, -5 random, -6.. globals). Effects play at note 60; a negative `CenterNote` is a PS2 note (48 kHz base), a positive one a PS1 note (44.1 kHz), and `CenterFine` adds to the fine (`sceSdNote2Pitch`).
- Count over the 43 banks: 851 sounds; grains `TONE` 2593, `RAND_PB` 368, `SET_REG_RAND` 158, `RAND_DELAY` 101, `LFO` 90, `RAND_PLAY` 63, `MARKER` 62, `TEST_REG` 56, `GOTO_MARKER` 43, `LOOP_START`/`END` 39, and a few `ADD_REG`, `GOTO_RANDOM_MARKER`, `PLAY_CYCLE`, `STARTCHILD`, `CONTROL_NULL`, `ON_STOP_MARKER`, `ADD_PB`, `SET_REG`, `KEY_OFF`, `WAIT_ALL_VOICES`, `PB`, `STOP`, `DEC_REG`. No XREF, plugin nor `BRANCH`.
- Names: `<MONSTRO>_S` (monster effects) and `<MONSTRO>_LS` (lower quality version, `loadLoResMonsterBank`); `<FASE>_S`; `GENAMB_S`/`GENAM_LS` (common ambience), `GENMON_S`, `SHELL_S` (menu), `COUNT_S`, `FINAL_S` etc.
- **`SHELLMUS.BNK` (1.7 MB) is not a MIDI sequence:** 1 looped sound, 2 `TONE` grains (pan 270 and 90 = left and right channel) and 2 long samples at 44 058 Hz. It is the menu music, sampled.

**`.VPK` (`disc/VAG`, 51 files).** `" KPV"`, `u32 bytes por canal`, `u32 início dos dados` (0x800), `u32 tamanho do par de blocos` (0xA000), `u32 Hz`, `u32 canais` (2 in all), and `{u16 vol, u16 pan}` per channel (127/270 and 127/90). The data alternates blocks of **0x5000** bytes of each channel (the field is the left+right pair: half-blocks 0 and 1 have ~0.6 correlation with each other and ~0 with the next one, in CITYAMB, TOKYOBAT and CONGAI), in SPU2 ADPCM without end or loop flags. `*AMB`/`*BAT` (stage music, ~2 min at 44.1 kHz); the others (speech, cinematics, intro `*I`, `*D`, `*T`) at 48 kHz.

**`.VAG`** (`DEFEAT`, `VICTORY`, 2 KB each): big-endian `"VAGp"` with a 0x30-byte header (data size at 0x0C, Hz at 0x10, name at 0x20); practically empty.

**Back into the game (`tools/snd989.py`).** `vpk`, `vag` and `bnk-replace` encode WAV (8/16/24/32-bit PCM or float) into SPU2 ADPCM. The encoder simulates the decoder block by block (filter and shift with the least error), so what it writes is exactly what the game reconstructs; ~40 dB SNR on music, ~1 s of processing per second of stereo audio. In `.VPK` the bytes-per-channel field goes in whole blocks of 0x5000 (the native reader reads only `início + bytes por canal × canais`), which leaves up to 0.8 s of silence at the end. `bnk-replace` resamples (linear) to the `TONE` rate, keeps the original sample's loop and fixes the offsets of the other sounds, the chunk size and the `SBlk` size fields (0x28 and 0x2C, which follow the samples' total; exact meaning unknown). Still to be tested in the game with real files.

**SPU2 ADPCM.** 16-byte blocks: `(filtro << 4) | shift`, flags (1 = end of segment, 2 = repeat from the loop start, 4 = loop start) and 14 bytes with 28 4-bit samples; filters `(0,0) (60,0) (115,-52) (98,-55) (122,-60)` / 64.

**`.MRS` (`disc/RST`) is not audio:** 48 KB of floats per stage.

## `.NGP` image
Prelinked to address **0xA00000** (the level one is decompressed there). Word 0 = N (number of root nodes), words 1..N = pointers
to the hierarchy's root nodes (`_hierhead`; opcode in the low 6 bits of the first word, object id in the high 14 bits).

## Fixups (`.PTR`)
Three (file zero) or four (monsters) lists, each a `contagem` followed by `contagem` byte offsets inside the image (0 = unused):
- **file zero** (`dbsRelocateFileZero`): the first two lists are skipped; the third lists the doublewords with the GS `TEX0`, and adds
  `vram / 64` to the `TBP0` field (14 bits, bits 37..50 of the doubleword).
- **monster** (`dbsRelocateViaPtrListFile`, `idx` > 0): (header) words 1..N += `endereço_real - 0xA00000`; (1) pointers (same sum);
  (2) 16-bit texture ids += highest id of the previous file; (3) GS words: `TBP0` += end of the previous file's textures, and the ones
  marked 0x1B/0x2C/0x24 add the end of the previous file's resources to the low 14-bit field.

## Level object ids (`dbProcInteractive`)
Id = high 14 bits of the node's first word. Nodes with opcode 0x1F and 0x27 are always destructible.
| Range | Meaning |
|---|---|
| 1 | world root (`viewSetWorldEpNode`) |
| 2 | sky (`viewSetSkyEntry` x4) |
| 18 | font texture |
| 0x20..0x400 | monsters (`TheGame::MonsterParse`) |
| 0xBB8 | pedestrian group (`PedGroup`, 0x190 bytes on the memory stack; byte `(head>>7)&0xFF` = size) |
| 0x7D0..0x897 | ignored here |
| 0xFA0..0x11F7 | weapons (`Weapons::AddWeaponEpNode`) |
| 0x11F8..0x12BF | effects: 0x11F8/0x1203 `HomingBug`, 0x11FC `OgreMace` ball, 0x11F9..0x11FA/0x11FD/0x11FE/0x11FF/0x1201 `SpecFxAnim` |
| 0x12C0..0x13EB | pickups (`LevelPickups::createPickup`); the id becomes the pickup's "hat id" |
| 0x13EC..0x144F | power-ups |
| 0x1450..0x1B57 | destructibles (`Destructibles::AddDestruct`) |
| 0x1B58..0x1C1F | particle textures |
| 0x1C20..0x2133 | HUD elements |
| 0x2134..0x2327 | scripts/actions (0x2134/0x2135/0x2136 only in certain modes; 0x2260.. user scripts) |
| 0x2328..0x270F | shell end points (`Shell::AddEpNode`) |
| 0x2710..0x3E7F | per-stage end points (threemile, tokyo 0x2743, canyon2, airport, FinalBoss, BigShot) |

Pickups keep the `Interactives` slot id in the `hierhead`: `Interactives::addInteractive` returns that index.

## Meshes (`HierObject::polyPkt`)
Reference readers: `tools/ngp.py` (Python, offline, straight from the disc) and `native/wotm_native.hpp` (`decodeObject`, C++, reads EE RAM).
An `OBJECT` node (`_hierobject`, 0xA0 bytes) points with `polyPkt` (+4) to a DMA sub-chain called by `pktAddVu1ObjAsm` (tag `CALL`; the packet ends in `RET`):
- Word 0: DMA tag with `QWC` in the low 16 bits and `ID=6` (`ret`) at `0x60000000`; the packet has `QWC + 1` quadwords. Words 2 onward are a **VIF stream** for the VU1 microcode.
- **Positions**: `UNPACK V3-32` at `0xB5` (up to 190 vertices, XYZ floats). **Normals**: `UNPACK V3-8` at `0x175`, signed bytes (/127), 3 per normal without padding.
- One or more **strip sections**, each ended by `MSCNT`. The section starts with a **GIF tag** (`UNPACK V4-32`, 1 element, at H = `0x238`/`0x2C8`/`0x358`, alternating because of VU1 double buffering):
  `NLOOP` (bits 0..14) = number of vertices, `PRIM` (bits 47..57; triangle strip with Gouraud, texture and fog) and `REGS` = ST, RGBAQ, XYZF2. Then, with `STCYCL CL=3 WL=1`, each vertex takes 3 quadwords:
  `S-8 @H+1+3k` position **index** (0..189), `V4-8 @H+2+3k` **RGBA** (`0x80` = 1.0; alpha too), `V2-16 @H+3+3k` fixed-point **UV** (`4096` = 1.0; negative and outside [0,1] repeats the texture).
- **Strip break (ADC)**: `STMASK 0xBFBFBFBF` followed by `UNPACK S-8 num=2 @H+3+3k` marks vertices `k` and `k+1` as "do not draw" (XYZF2 ADC bit): the strip restarts at `k`. 8-vertex strips are common (cylinders/tubes). Joins by repeated vertex produce degenerate triangles (discard).
- A new `UNPACK V3-32 @0xB5` replaces the position table (big objects have several batches of up to 190 vertices).
- `_hierobject.bits.numVerts` (13 bits) is the object's total vertex count; `texId[5]`/`numTextures` and `gsTexCtx[i]` (+0x70, 0x30 bytes each: `TEX0`, `TEX1`, `MIP`) describe the textures. Collision/visibility sphere at +0x10 (center) and +0x1C (radius); `sphereRadiusSqrd` at +8.
- World conventions: row vectors (`p' = p * M`), **Z up**; `ROTATE` matrices at +0x10 (4x4), `TRANSLATE` adds `trans` (+0x10), `SCALE` applies `scale` (+0x20) then `trans` (+0x10).
- Nodes traversed by the port: `GROUP` (children at +0x20, u16 count at +8), `LOD` (list of `{fade, switchOutDis, child, pad}` at +0x20; 0 is the coarsest and the last the most detailed; the game picks the highest index whose `switchOutDis`, a squared distance, is greater than the distance to the LOD's center), `SWITCH` (child `whichChild` at +0xC), `CONTROL` (`child1` at +4), `ACTION_DATA` (child at +4), `CHAR_INSTANCE` (children at +0x2C, count at +0x28), `INTERACTIVE` (child at +0xC), `DESTRUCTIBLE` (states at +0xC, 12 bytes each; 0 is the intact one).

## Textures and GS VRAM (in progress)
Each object's `TEX0` (`gsTexCtx[0].tex0`) uses the GS format (`TBP0` bits 0..13, `TBW` 14..19, `PSM` 20..25, `TW` 26..29, `TH` 30..33, `CBP` 37..50, `CPSM` 51..54, `CSM` 55, `CSA` 56..60). The pixels reach VRAM through GIF `IMAGE` uploads from the `.TEX` (and the `.RTX`).
`native/wotm_gs.hpp` decodes `CT32/CT24/CT16/T8/T4` from a VRAM dump (4 MB); the 256-entry palette (CSM1) is in linear memory with entries 8..15 and 16..23 of each group of 32 swapped.
The runtime writes `ram_<quadro>.bin` and `vram_<quadro>.bin` with `PS2X_DUMP_RAM="quadro,..."` (`PS2X_NO_GS=1 PS2X_GS_UPLOADS=1` processes only the uploads, without rasterizing).

**`.TEX` and `.RTX` files (read from `src/common/texm.cpp`).** Tool: `tools/tex.py` (`list`, `export` to PNG with
`manifest.json`, `inject` of PNG/TGA).
- They can come raw or in a single-file zip with the magic `IE\3\4` (`Shell` reads `shell/*.rtx|tex` through `zipInflateAll`).
- `_texfile` header: `u32 numSizes`, `u32 headerQw` (the first packet is at `headerQw * 16`) and `{u32 count, u32 blkSize}` per
  size range (the VRAM "bins", `texmLoadTexture`).
- `TexPkt` packet (16-byte header): `u32 size` (the next one is `(size >> 2) * 16` bytes ahead; 0 = last), `u32`,
  `u16 texId`, `u16 tbp` (64-word VRAM block), and the bits `psm` 0..5, `noUpload` 6, `skip` 7, `logW` 8..11, `logH` 12..15,
  `numMips` 16..18, two 4-bit fields still unnamed (19..22 goes to `texmSetupTexture`) and `bin` 27..31.
- Linear pixels (rows of `w` pixels; T4 with the low nibble first). In `.TEX` they start at `+0x80` (the game builds the
  SDK's `sceGsLoadImage` at `+0x20`). In `.RTX`, the high formats (T8H/T4HL/T4HH: menu screens, fonts, portraits) also at `+0x80`;
  the rest (CT32 16x16 and 8x2 palettes) at `+0x10` (`texmLoadResTexture`).
- Relocation on load: `texmRelocate` adds to the `texId` of each `.TEX` after the first the highest id of the previous one (and remaps `bin`);
  `texmResRelocate` adds to the palettes' `tbp` the VRAM base and the end of the previous `.RTX`, and to the `tbp`/`texId` of the high formats the
  end of the previous ones. The `TEX0` of the `.NGP` objects ties each texture (`texId`) to the palette (`CBP` = `tbp` of the `.RTX` entry).
  Since the numbers in the `.NGP` may already be summed, `tex.py` finds both offsets by the largest number of objects that
  match format and size; it needs checking against real files.

## Particles and effects (`common/particle`, `common/partdraw`)
Readers: `native/wotm_fx.hpp` (C++, draws) and `tools/fx.py` (Python: active systems from a RAM dump and `--presets` with the ELF catalog).
The layout of `_ParticleType` (12000 bytes), `EmitterType`, `FieldInfoType`, `ParticleInfoType` and `VisualInfoType` is in `include/hieri_types.h` (stabs from `.mdebug`).
- **Pool**: `fx[100]` at `0x5BA280` (stride `0x2EE0`). Active list: `fxActiveTop` (`0x6F88EC`) is the first index and `fxList[i]` (`0x6DF200`, 12 bytes) keeps the next one at `+8`; `-1` ends it. `fxInactiveTop` (`0x6F88F0`) is the free list.
- **Presets**: `particleInitFx(tipo, ...)` copies the `tipo` rows of `EmitterInfo` (`0x5B2020`, 80 bytes), `FieldInfo` (`0x5AEFF0`, 80), `ParticleInfo` (`0x5B4F70`, 96) and `VisualInfo` (`0x5ADD00`, 32) into the system; there are 151 presets, initialized data in the ELF. The preset index seems to stay in `emit.fxId` (`+0x2DDC`): `particleMove` compares it with 0x91 and 0x93 for edge effects; not confirmed in a dump yet. The names are in the game's own `FxId` enum (`include/hieri_types.h`): 0 `Dust`, 2 `OpaqueDust`, 5 `DustTrail`, 9..12 explosions (`WhiteExplosionFx`, `SmallExplosionFx`, `ExplosionFx`, `SmokeExplosionFx`), 15..18 steam and smokes, 21..26 sparks, 28..32 flashes (`CounterFlash` .. `BlueWhiteFlash`), 43..48 fire (`Flames`, `AttachedFlames`...), 105 `FootStomp`, 146 `WaterWake`. `vis.texInfoIndex` is a `FxTextureId` (`Smoke1`..`Smoke5`, `TexExplosion`, `Fireball`, `TexFlames`, `Spark`, `DustTexAlpha`, `CompositedDust`...).
- **Per particle** (pointers inside the `pkt` packet itself, built in `*PacketInit`): `pos[i]` (`+0x2EB0`, `FVECTOR`; **`w` is the size**, treated as a radius by `particleFOVcalcMaxRadius`, which adds `maxSize + expansionRate * lifetime`), `vel[i]` (`+0x2EB4`; `vel.w` is the expansion rate added to `pos.w` per field) and `age[i]` (`+0x2EB8`, `int16`, in fields). `numParticles` at `+0x2ED0`, `maxParticles` (256 for sprites) at `+0x2ED4`. The low bits of `pos.w` are preserved in `particleMove` (mask 3, or 0xF with `info.attribs & 0x2000`) and pick one of the 4 or 16 UV corner sets that `spritePacketInit` uploads.
- **Draw type** = `vis.attribs` (`+0x2E90`), by `particlePrepDraw`'s jump table: 1 `line` (uploads `pos` and `vel`), 2 `sweepTrail`, 3 `sprite`, 4 `texSprite`, 5 `stripe`, 6 `strite`, 8 `sprine`, 9 `kite`, 13 `frag`, 15 `dustTrail`, 16 `wake`; 7, 10..12 and 14 build no packet there. Even so, the VU1 x native check (`docs/passos/VU1CHECK.md`) saw the VU1 draw systems of type 7 and type 14 (pedestrians) from the `pkt` of the `fx[i]` itself, so other code builds those packets. Electric arcs (`monArcParticle`), chains and lens flares have their own path.
- **Who draws** (`particleDraw`): `info.attribs & 0x400` (packet built this frame), `numParticles >= 1` (`sprine`: `>= 2`), type other than 11 and the `FOVsphere`/`FOVextent` sphere (`+0x2C80`/`+0x2C90`) inside the view. The order is farthest to nearest by the depth (eye Y axis) of `emit.ParticleToEmitVec` (`+0x2CA0`) plus `vis.priority`; systems with `alphaContext == 0` (opaque) get key 50000 + priority and go first.
- **Color** (`loadColors`, the same as in the sprite microcode): RGBA table at `0x256C10` (`0x80` = 1.0, alpha included), `vis.numColors` colors starting at `vis.colorIndex`. `t = age * info.colorsPerFrame` (4.12); the color is the interpolation between `cor[t >> 12]` and `cor[(t >> 12) + 1]` by `(t & 0xFFF) / 4096`. Zero colors = `0x80808080`; one color = constant. Almost all tables end with alpha dropping, and that is how the particle fades out.
- **Blending**: `vis.alphaContext` is the GS `ALPHA` register written in the packet context: `0x44` = `(Cs - Cd) * As + Cd` (normal), `0x48` = `Cs * As + Cd` (additive: fire, sparks, flashes).
- **Texture**: `TextureInfo[vis.texInfoIndex]` (`0x5B9E80`, 8 bytes: `numTextures`, `texIndex`, `du`, `dv`; filled at run time by `particleParseTexture`). `texId[texIndex]` (`0x6DFAB0`, `u16`) is the index into the global `texInfo` (pixels of the upload packet, as for objects) and `TEX0[texIndex]` (`0x6DF6B0`, `u64`) is the `TEX0` register (psm, CBP). `texSprite` picks a frame per particle: `info.attribs & 1` random, `& 2` `uvI[i] >> 2`, otherwise `(age * info.mapsPerFrame) >> 12`, clamped at the last with `& 4` or modulo `vis.texMask`.
- **VU1 particle microcode**: it is not the object microcode (`0x257C10`); the particles' `displayList` calls its own packet at `0x253060` (954 qwords: 1884 instructions via `MPG` and, at the end, the default context at `0x256B90`, 8 qwords: world→screen matrix, constants and `vf27` = focal lengths 320/160). Each `*PacketBuild` ends with `MSCAL` into its program (`D_58` line, `D_2C8` sprite, `D_800` dustTrail, `D_A88` strite, `D_F18` texSprite, `D_1180` frag, `D_13A0` kite, `D_1708` stripe, `D_19F8` electric arc, `D_1D00` chain, `D_2770` sprine, `D_2B20` lens flare, `D_3318` sweepTrail, `D_3470` wake; addresses in bytes, `MSCAL` uses /8). Read so far: in the **sprite**, the on-screen half-side is `pos.w * foco / profundidade`, limited to `foco / 2` (so `pos.w` is the half-side in world units and does not exceed half the depth); the low bits of `pos.w` index the 4 UV sets of `spritePacketInit` (3 normal, 1 mirrors U, 2 mirrors V, 0 both; with `0x2000`, 16 sets); the color comes from the table by age. In the **line**, the tail is `pos - vel`, projected with the previous frame's matrix (the context carries both: camera motion blur). In the **strite**, head `pos` and tail `pos - vel` projected and stretched 2x to each side (from `pos + 2*vel` to `pos - 3*vel`), width by `pos.w`: one equilateral triangle per particle with ST (0.5; 1.5) at the tip and (1.366; 0) and (−0.366; 0) at the base, which circumscribes the texture square (the disc ends up inscribed); the color's alpha comes out multiplied by K/(len + K), with `len` the on-screen length of the trail and K = 2·focal·`pos.w`/w (a long trail is fainter; `Particles::striteTriangle`). `sprine` is a cubic spline through 4 consecutive particles, subdivided on the VU1 by forward differences (with a random offset seeded by `pos.w`); the port joins the particles with straight segments. The **wake** is one triangle per particle in the plane of rows 0 and 2 of the emitter's `StaticMat` (`fx+0x2D20`; world X/Y with `info.attribs & 0x4000`), with size and alpha by age curves and scale/ST by the low 12 bits of `pos.w` (details in `a3973ff^:docs/passos/VU1CHECK.md`, "Wake como o microprograma 0x3470"). Not read yet: `stripe`, `sweepTrail`, `kite`, `frag`, `dustTrail` (the port approximates them: bands joining consecutive particles, the rest as sprites).

## Animation (curves in the `.NGP` and the runtime mixer)
Source: `src/common/AnimCurve.cpp` and `src/common/animation.cpp` (whole TU in C++; the equivalent functions have not gone through difftest/oracle yet, so what depends only on them is marked (?)).
The reading reproduces 128 of 130 channels of a fight (`tools/animcurve.py`). Building the monster's pose from the disc alone (skeleton, palette, textures): `passos/MODELOS_3D.md`.
Struct and field names come from `include/hieri_types.h` (generated from stabs); here is what the values mean.

**Data (in the `.NGP`):** `CHARACTER_INSTANCE` (`_animCharInstance`) -> `character` (+0x20, `_animCharacter`: `numAnims`, `numChannels`, `animations[]`) -> each `_animation` (`startTime`/`endTime` in animation
frames, `numChannels` curves in `channels[]`). Each curve writes a **channel** (`AnimCurveHeader.dataIdx`): a float in the instance's `animOutput.val` vector, which the `ANIM_TRANSFORM` nodes
(`rXidx`..`tZidx`, `visidx`) read to build the matrices. `AnimChannelIdMap` (sorted by `id`, binary search) maps a channel id to the index. `angularChannelBits` marks the channels that are angles
(normalized by `boundEulerAngle` before blending): with up to 16 channels the bits are in the 2 bytes of the field itself; above that the field is an offset from the `_animCharacter`.

**Curve** (`AnimCurveHeader`, 4 bytes): `curveType` (`kHermite` 0, `kBroken` 1, `kPwl` 2 = linear with equally spaced keys, `kStatic` 3, `kSmooth` 4, `kStepped` 5), `dataType`
(`kFloat` 0, `k16bit` 1, `k8bit` 2), `preInfinity`/`postInfinity` (`kConstant` 0, `kLinear` 1, `kCycle` 2, `kCyclePlusOffset` 3, `kOscillate` 4), `dataIdx`. After the header: `u16 numKeys` and the keys.
- `kStatic`: one float right after the header, no keys.
- Keys per type (`T` = float, u16 or u8; `S` = float or 16-bit code): `kPwl` `{T value}` (the time of key `i` is `baseTime + i*deltaTime`); `kStepped` `{T time, T value}`;
  `kSmooth` `{T time, T value, S tangent}`; `kBroken` `{T time, T value, S tangent[2]}` (out/in tangents of the segment); `kHermite` `{T time, T value, S coef[3]}` (cubic, quadratic and linear coefficients of the segment; only exists with float).
- **Quantized** (`k16bit`/`k8bit`): the 16 bytes **before** the header are an `AnimNormData` `{baseTime, deltaTime, baseVal, deltaVal}`; time = `base + q*delta`, value likewise.
- **Tangent code** (s16): `s >> 14` = 0 or -1 -> `s / 16384`; 1 -> `16384 / (32768 - s)`; -2 -> `16384 / (-32768 - s)` (the last two cover steep slopes).
- Outside the key range: `kConstant` repeats the end value; `kCycle` repeats the curve; `kCyclePlusOffset` repeats adding the difference between the ends on each loop; `kOscillate` goes back and forth;
  `kLinear` **returns 0** in our C++ (the `animCurveEval` instances are equivalent, not identical) (?).
- The key search starts at the index kept in `AnimControlNode::prevKey[canal]` (per-channel cache).

**Time:** the game's time unit is the **field** (`timerGetFieldCount`, 60 per second). An `_animControlNode` (opcode 0x21, one per animation, in the instance's `animCtx[i]`) has
`tempo = startTime + deltaTime * (fieldAtual - startField)` while `active`; when paused, `startField` keeps the fields already elapsed. `deltaTime` is animation frames per field (negative = backward;
`animationSetTotalRunFrames(h, n)` makes it last `n` fields). When it passes the end: with `loop`, it goes back to the start and increments `iterations`; without `loop`, it stops at the end (and pauses if it is the main tree).

**Per-instance mixer** (`activeTree` +0x24 points to a 0x18-byte `AnimPlayer`):
- `mainTree`: the main animation, an `_animControlNode` or an `_animBlendNode` (opcode 0x22) transitioning between the previous one (`blendFrom`) and the new one (`blendTo`). A transition during another
  chains: the old one becomes the new one's `blendFrom` and its time drops to 40%.
- `activeList`: extra blends in a doubly linked list, sorted by ascending `priority`, applied **after** the main one, on top of its result. `mainTreePureOut` keeps the main one's output
  without them. Procedure nodes (opcode 0x2B, `_animprocnode`) call `procCallback(nó, instância)`.
- `blendType` (`AnimBlendTypes`): `TRANSITION` 1 (interpolates from `blendFrom` to `blendTo`; once finished, the node is collected and returned to the pool), `FREEZETRANS` 7 (a transition that puts the target at its
  current percentage and, at the end, restores the saved `deltaTime` and reactivates it) (?), `STATIC` 2 (blends the target over what is already in the channel with weight `weight`), `OVERRIDE` 3 (**writes `destino * peso`** to the channel, without blending) (?),
  `ADDITIVE` 4 (adds `peso * curva`), `PROCEEDURAL` 6 (empty function in the retail), `SUBTRACTIVE` 5 and `NONE` 0 zero the weight.
- Weight over time: `r = (ms desde blendStartField) / blendTime` (`blendTime` in **milliseconds**, field = 16.667 ms), passed through the global curve `s_blendCurve` (0 linear, 1 cosine, 2 `r²`, 3 `r(2-r)`).
- Blends come from a global pool of **128** `_animBlendNode` with a free index stack; the temporary buffers of each blend come from the scratchpad stack (`0x70000000`, 16 KB).
- Animation lookup: `animationGetHandle(id1, id2, animId)` walks the animation managers of each loaded `.NGP` (`ANIMATION_MGR`, `charInstance[]`), finds the instance by the
  `HierHead` ids and the animation whose `head.id1 == animId`. The handle (`ci`, `ctrl`, `proc`, `animIdx`) is what the game code keeps (monsters have a table of them, see `Monster::m_anims` in `include/game/game.h`).

## Character (`RST/<MONSTER>.CHR`)
Each monster's gameplay numbers (hits, ranges, timings), read by `charreset::readKey` (0x126620). Reader: `python tools/chr_dump.py
disc/RST/<MONSTRO>.CHR [--keys] [--csv]` (punch table; `--keys` lists the keys).
- 0xC-byte header, then keys `{u32 id; u32 tamanho com estes 8 bytes; dados}` until an id 0 or the end. `OGRE.CHR` (unused monster) carries another set of keys.
- **0xFCC, punches (`StatePunch`):** 10 combo positions × 6 types, each config with 0xA0 bytes at `chave + 0x10 + pos*0x3C0 + tipo*0xA0`.
  The game copies them into the `StatePunch`'s `PunchConfig` (Monster +0x8470 +0x20 + pos*0x420 + tipo*0xB0; config offsets in parentheses):
  +0x10/+0x14 tested locators (+0x7C/+0x80; 0x802/0x803 hands, 0x804/0x805 feet, < 0x7D0 none), +0x20 damage (+0x90), +0x28 animation
  (+0x00, index into `Monster::m_anims`), +0x2C/+0x30 test window as a fraction of the animation (+0x0C/+0x10), +0x34/+0x38 counterattack window
  (+0x14/+0x18), +0x8C/+0x90 knockback (+0x64/+0x68; ≠ 0 knocks down), +0x94 stun (+0x6C; > 100 stuns), +0x98 hit pause in ms
  (+0x70), +0xA8 fist sphere radius (+0x74; 0 uses the default, key 0xFA1 +0x30, `StatePunch` +0x2980). A slot without animation and without damage is empty.
- **Position and type:** the position (`StatePunch` +0x2988) starts at 0 and goes up with each chained hit (up to 9). The type (+0x298C) comes from the
  `PadFlags::curMappedAction` accepted by `StatePunch::transitionOK`: 5 → 0 triangle, 6 → 1 forward + triangle, 7 → 2 back + triangle,
  1 → 3 forward + square, 2 → 4 back + square, 0 → 5 square (in control scheme 0; the direction is a recent tap on the d-pad).
  The combo's next type is the one pressed inside the input window (+0x28/+0x2C of the config), if the float `+0x94 + próximo*4` of the current config is > 0.
- Key 0xFD8 has the same layout without the leading 0x10. 0xFA4/0xFAC/0xFD7/0xFE3 are the object hits (`StateBatSwipe`): their position in the file is not decoded, but in RAM the `SwipeConfig` (0xC0 each, Monster +0xAE90 +0x20 + pos·0x480 + tipo·0xC0) repeat the start of `PunchConfig` (animation +0x00, window +0x0C/+0x10, knockback +0x64/+0x68, stun +0x6C, pause +0x70, radius +0x74, one locator +0x7C); the damage is a multiplier of the object's damage (`LevelPickups::s_info`): +0x90 with a bat (`Pickup` +0xA0 == 8), +0xB0 for the others (`getDamageMod`). Training mode reads from there (`NATIVO.md`, "Punch table").

## Destructibles (`INTERACTIVE_STATE` + `DESTRUCTIBLE`)
Source: `game/Destructible` (`Destructibles::AddDestruct`, `Destructible::takeHit`, `GenericTakeHit`, `ChangeState`, `SwitchState`; the TU is still in asm, read by hand) and the `hierTraverseAsm` traversal
(handwritten asm, in `asm/common/texm.s`). The node structure holds for the 19 stages of `LVL/`.

**On the disc:** every `DESTRUCTIBLE` (op 0x1F) is the only child of an `INTERACTIVE_STATE` (op 0x27): `_interactivestate.child` (+0xC) points to it (1566 of 1566 in the stages). The `DESTRUCTIBLE` has
`hitPointClass` (+4), `hitPoints` (+6), `hpThreshold` (+8), `numKids` (+0xA) and `numKids` 12-byte states starting at +0xC: `{child, actions, damageThreshold (u16), behaviorType (u16)}`.
State 0 is the intact one. `behaviorType` is 0 in all 3426 states of the stages (unused). The `INTERACTIVE_STATE`'s `whichChild` is 0 on the disc.

**What to draw (what the native layer reads from RAM):** the traversal keeps the `INTERACTIVE_STATE` when passing through it and, at the `DESTRUCTIBLE` right below, descends only into
`child[whichChild].child`, that is, `u32(destr + 0xC + 12 * u32(istate + 4))`. The current state is the **parent's `_interactivestate.whichChild` (+4)**, written by the game at run time.

**Health** (`Destructibles::AddDestruct`, one 0xA0-byte `Destructible` object per destructible, up to 1500): by the node's `hitPointClass`:
| `hitPointClass` | health (`+0x14` and max at `+0x18`) | minimum damage (`+0x1C`) |
|---|---|---|
| 0 | node's `hitPoints` | node's `hpThreshold` |
| 1 | 10 | 0 (and clears bit 3 of the flags at `+4`) |
| 2, 3, 4, 5 | 50, 100, 150, 250 | 5 |
| 6 or more | 3000 | 5 |
In the stages classes 0 (349), 1 (1041), 2 (109), 3 (28) and 4 (39) appear. The `Destructible` also keeps the node (`+0x24`), the `INTERACTIVE_STATE` (`+0x28`), the index into `Interactives` (`+0x2C`,
also written in bits 7..17 of the `INTERACTIVE_STATE`'s first word), `hitPointClass` (`+0x30`), the position (`+0x40`), the matrix (`+0x50`) and the requested state (`+0x90`).

**Switch rule** (`takeHit` -> `GenericTakeHit` -> `ChangeState`):
1. A hit only takes health if the damage is **greater** than the minimum damage (or if `HitEvent::s_takeHitInfo` has `source` 5 and `subtype` 15 or 16). Exceptions by id: 0x1A90..0x1B57 do not lose health; in stage 6 (`TheGame::m_levelId`, Three Mile), ids
   0x2712 and 0x2715..0x2718 (vent and chimneys) only accept damage from a player monster (?); in stage 7 (San Francisco), id 0x2762 only accepts damage of exactly 100.
2. `q = (int)vida * 65535 / (int)vidaMáxima` (health fraction in 1/65535). The new state is the **first** `i` with `damageThreshold[i] < q`, but the loop stops at `numKids - 1` and at
   `estadoAtual + 1`: **a hit advances at most one state**. In the stages the typical thresholds are `62258` (95%), `49151` (75%), `32767` (50%) and `0`; `65534` makes the state change on the first damage.
3. If `i` went past the current one: writes `+0x90 = i`, runs the `actions` of state `i`, knocks off the monsters that were on top (`shedMonsters`), gives 50 tokens ("Destructibles") to the monster that hit and
   schedules `SwitchState` in the `TaskManager` (delay 2), which copies `+0x90` to `whichChild`. In other words, **the drawing changes a few frames after the hit** (?).
4. Going back to a lower state writes `whichChild` directly: that is what `rebuildCanyon2Pillars`/`rebuildCapitolPillars` do with `ChangeState(0, 1, 0)` (they rebuild the pillars).
5. With `ChangeState`'s third argument set, the game also sets to 1 the `whichChild` of a `SWITCH` at the start of the new state (the `child` itself or the first child of a `GROUP`), and
   health goes to `damageThreshold[i] * vidaMáxima / 65535 - 0,001`.

## Memory card save (`BASCUS-97197`, `savegame.dat`)
Folder `BASCUS-97197` on the card (title "War of the Monsters"). The runtime's `sceMc*` stub writes to the host's `mc0/` (port 2 in
`mc1/`). The blocks are built by `game/MonsterMc`; `game/McFile` (`McLoadFile`/`McSaveFile`/`McSetupGameDir`/`McScrambleIcon`) and
`game/McPage` (menus) are platform. `python tools/mcsave.py <pasta>` shows both cards as JSON by name (read only). On PC the
game uses `save.json` (`docs/passos/NATIVO.md`, "Save as JSON").

| File | Bytes | Contents | Built / applied by |
|---|---|---|---|
| `BASCUS-97197` | 0x380 | options, battle tokens and unlocks (`_SettingsData`) | `MonsterMc::getSettingsData` / `setSettingsData` |
| `savegame.dat` | 0x280 | 10 adventure slots (`_SaveGameData`) | `MonsterMc::getSaveData(slot)` / `setSaveData(slot)` |
| `icon.sys`, `ICON.ICO` | 964, 61120 | PS2 icon | `McSetupGameDir`; `McScrambleIcon` zeroes `icon.sys` while the options are being saved and rebuilds it identically afterward |

**No checksum.** Both reads (`McLoadSettings::update`, `McSelectReel::refreshSaveData`) only check the `0x12345678` marker at the end
of the block; a wrong marker becomes a card error. No pointers: everything is a 32-bit little-endian `int`.

**`BASCUS-97197` (options):**

| Off | Origin |
|---|---|
| 0x00 | `TheGame+0x1203CC` (`m_matchMode`; `Shell` lights up `m_difficultySw[valor]`: it is the difficulty) |
| 0x04, 0x08 | `Shell::m_sfxVolume`, `m_musicVolume` (0..0x400) |
| 0x0C | `Shell+0x2A40`: the constructor sets 50 and nobody else reads it (dead option; probably a slider cut from the options menu) |
| 0x10 | `Shell::m_mono` (becomes 0/1 on load) |
| 0x14, 0x18 | `Shell::m_ctlrCfg[2]` |
| 0x1C, 0x20 | `Shell::m_vibration[2]` |
| 0x24 | battle tokens (`Shell+0x2B24`) |
| 0x28..0xE7 | `monsterSelectMode[i].state` of the 48 `SelectSlot` (only the `state`; `taken`, price and names stay in the ELF) |
| 0xE8..0xF3 | `TheGame+0x1204B0..B8`, 3 ints: endurance record per difficulty (saved, the game never shows it) |
| 0xF4..0x34B | **not written**: the buffer is on the stack and this stretch saves garbage |
| 0x34C | marker `0x12345678` |

The 48 flags are 12 groups of 4. Groups 0 to 9 are the monsters in the order of `Shell::RandomlySelectAI`'s `aiMonsters` (Raptros, Togera,
Preytor, Congar, Robo-47, Agamo, Ultra V, Magmo, Kineticlops, Zorgulon), and entry `j` of the group is costume `j`. Groups 10 and 11 are
the `levelUnlocked` stages: entry 40 Volcano, 41 Mini Baytown, 42 Capitol, 43 UFO, 44 Dodge Ball, 45 Big Shot, 46 Crush; 47 is
a 5000-token "Level 4" that no code reads. The names and prices are in the table itself (`SelectSlot::name`/`cost` in the ELF) and
confirm the order. The boot's `McCheckForSweetTooth` looks for `BASCUS-97101/globalFile` (the Twisted Metal: Black save) on the card and,
if found, turns on entry 23 ("Agamo Costume 4", the Sweet Tooth Mecha; `ROCK3`) and plays `ShellSound::playSweetUnlockedSound`.

**`savegame.dat` (adventure):** +0 number of slots used (the highest saved index + 1); slots at `0x10 + i*0x3C`; marker at 0x268; the rest
is zero. Each slot uses only 0x14 of the 0x3C bytes:

| Off in slot | Origin | On load (`setSaveData`) |
|---|---|---|
| +0x00 | `Shell::m_levelNum` (stage where it stopped) | same; the AIs come from `m_levelNumAIs[fase]` and `levelMonsters[fase]` |
| +0x04 | `Shell::m_monsterSel[0]` (`monstro << 5 \| variante`) | same |
| +0x08 | `Shell::m_lives[0]` | same |
| +0x0C | `Shell+0x2B28 + Shell+0x2B2C` (stage points + accumulated) | all into `+0x2B2C` |
| +0x10 | `Shell::m_costume[0]` | same; and `m_numPlayers = 1`, `m_mode = 1` |

Saving a slot (`McSaveAdventure::onEnter`) reads the whole `savegame.dat`, replaces only that slot (`copySaveData`) and writes the 0x280 back
again; with the file missing (`McSaveAdventureBlank`) it starts from a zeroed block with the marker. The 10 slots are the "reels" of the
load screen (`McSelectReel`, `McLoadGameBoxes`); card 2 gives another 10.

**When the game saves** (page sequences built in `MonsterMc::update*`): `updateBootLoad` (boot: `McBootPage` →
`McLoadSettings` → `McCheckForSweetTooth`), `updateSettingsLoad`/`updateSettingsSave` (options menu), `updateUnlockSave` (after
buying on the unlock screen: `McAskToSave` and saves the options, where the unlocks live), `updateEndOfStorySave`, `updateStoryModeSave`/
`updateStoryModeLoad` (adventure) and `updateEndureSave`. Every save goes through `McChooseSlot` (card 1 or 2), `McCheckSpace`,
`McCheckFormat` and `McCheckOverwrite` and saves the options (`McSaveSettings`); the options, unlock, endurance and end-of-story ones also
create a blank `savegame.dat` if it is missing (`McSaveAdventureBlank`), and the adventure one picks the slot (`McSelectSaveSlot`) and saves the
slot (`McSaveAdventure`). In other words: tokens and unlocks go to the card on any save. A purchase on the unlock screen only takes
effect inside `McSaveSettings::update` (`g_monsterToUnlock`/`g_skinToUnlock`: turns on the `state` and deducts the price; the screen sets -1
on both right after). Outside these sequences, tokens only change in `Shell::EvaluateOnePlayerStoryStatus` (end of a story
stage), in endurance and in the constructor.

**`MEMCRD/ICON.ICO`** (the icon, also on the disc): a film reel, 1158 vertices, one shape, a compressed 128x128 texture; the format
is in the file's header (`native/wotm_saveicon.hpp`).
