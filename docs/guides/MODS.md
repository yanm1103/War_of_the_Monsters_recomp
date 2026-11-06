# Mods: how to

A mod is a folder inside `mods/` (next to the ELF, at the root of the extracted disc the game uses: `<runtime>\discroot\mods\`). It
mirrors the disc tree, and whatever is there is used in place of the disc file. Nothing of the game is changed and nothing goes into the repo.

```
discroot/mods/
  my_mod/
    MON/lizard0_t0004.png      Togera texture as PNG (any resolution)
    MON/lizard0_rtx_t0003.png  Togera portrait on the HUD
    VAG/CITYAMB.VPK            replaced music
    SND/LIZARD_S.BNK           replaced sound bank
  _work/                       kit work folders, one per mod (starts with '_': the game does not read it)
    my_mod/                    my_mod's PNGs and WAVs being edited, with its modkit.json
```

Loader rules (`native/wotm_mods.hpp`): a folder starting with `_` or `.` is disabled; when two mods have the same file, the last one
in alphabetical order wins (you can use `10_base`, `20_extra`); `WOTM_MODS=<folder>` changes the folder and `WOTM_MODS=0` turns the folder off (the generated army files stay). The
log (`logs\live.err.log`) lists each file and each texture that came from a mod (`[mods] ...`).

**Project mods** live in `mods/<mod>/` at the repo root and are only original art and configuration (JSON, drawn PNG): Goliath
Prime (`mods/Goliath Prime/`), Cerebulon (`mods/Cerebulon/`) and the "Recompiled" logo of the intro (`mods/recompiled_logo/`).
`tools/sync_mods.ps1` mirrors each folder of `mods/` to `<runtime>\discroot\mods\` (`run_live.ps1` calls it on every boot and
`setup_runtime.ps1` on install; `WOTM_RECOMP_DIR` or `.wotm_recomp_dir` give the runtime). Only the folders that exist in the repo are
mirrored; the ones that exist only in `discroot\mods\` (yours, `_work`) stay as they are. Without a `mods` folder (with exactly that name: on
Windows the path `mods` falls on the disc's `MOD\`, the IRX one) next to the ELF, the game and the launcher read the repo's `mods/`
(`WOTM_REPO`, which `run_live.bat` and the launcher pass). An edited disc texture (PNG out of
`modkit.py`) is game art and does not go into the repo, except as new art on top (the logo's case).

## Interface (`tools/modkit_gui.py`, the launcher's "Mod kit" button)

`pythonw tools/modkit_gui.py`, or the **Mod kit** button of `WotM_Launcher.exe`. It needs Python 3 with Pillow (`pip install pillow`). The window is in English.

In the release zip it is `WotM_ModKit.exe`, next to `wotm.exe` (built by `tools/build_modkit.ps1` with PyInstaller, Python, Tk and
Pillow inside; `tools/release.ps1` calls it). There it finds by itself what `wotm.exe` uses: the ISO in `wotm.ini` (`iso=`) and the mods
folder (`<data>/mods`, otherwise `<game>/mods`, where `<data>` is the game folder or `%LOCALAPPDATA%\WotM`).

**Game disc** takes the `.iso` or the extracted disc folder. With the ISO, each file the kit needs is copied once to
`%LOCALAPPDATA%\WotM\modkit_disc\<key>\` (one folder per image; `tools/iso9660.py` reads it), and the rest works as with the folder.

A mod is a project: at the top, **Mod: [list] [New mod...]** picks the mod (New mod creates the mod folder and its work folder,
`<mods>/_work/<mod>/`; the name only has letters, digits, `_` and `-`, starting with a letter or digit). When you pick the mod, its files
that the work folder does not have (a mod made earlier or by hand) are copied there and show up as edited; Build takes them back.
A file removed from the work folder on purpose does not come back. The picked mod governs Extract, the right-hand list and Build; changing the mod changes the whole
work area. Without a mod, the right-hand list is empty and the buttons tied to it are disabled, with the hint "Pick or create a mod first".

1. **Pick from the disc** (left): monsters by name and costume (Togera › Costume 1, and its sound banks), bosses, stages by their in-game
   name, menu and screens, sound banks, music and speech. Search field; Ctrl/Shift pick several; double click or "Extract selected".
2. **Edit <mod>'s work folder** (right): one group per disc file ("MON/MONKEY0 (Congar, costume 1)"); after extracting, the
   new group opens and the others close. Thumbnails, size and state ("edited" = changed since extraction). A texture the export could not
   color comes out gray with `_sempaleta` in the name; `pack` writes it without the suffix (the game accepts both). Preview
   of the image (with a checkerboard behind the alpha) or the sound's waveform. A sound plays in the window itself on click ("play sounds on click"
   turns it off) or on double click; "Stop" stops it. Double clicking a texture opens it in the editor chosen in **Editors...** (one program per
   type, `.png` and `.wav`; empty = Windows default program; with a `.wav` editor set, double clicking a sound opens it there).
   Right click on the list: Play, Open in ..., **Open with...** (picks the program and asks whether it becomes the default), **Replace with...** (only with one file
   selected: replaces it with the file you pick and writes it with the right name; JPG, BMP, TGA etc. become PNG; warns if the aspect ratio
   changes; sound only accepts WAV), Show in folder, **Revert to original** (only shows up if there are edited files in the selection; on a group, reverts its
   edited files; extracts again from the disc and asks for confirmation first),
   **Save to folder...** (copies the selected files, or the whole group, to a folder) and Remove (the Del key too). Clicking a
   section only selects it: open and play apply only to files picked one by one; on a section, right click saves or removes everything.
3. **Build** (bottom): **Build mod** (`pack` of the picked mod), **Check mods**, **Open mods folder** and **Open work folder**. The log
   sits at the bottom, with a divider you can drag up and down.

The folders (disc and mods), the last mod, the editors, the log height and the last "Save to folder" folder are kept in
`~/.wotm_modkit.json` (in `WotM_ModKit.exe`, `modkit.json` in the game's data folder, next to `wotm.ini`); a saved folder that no
longer exists falls back to the default. Underneath are the same functions of `modkit.py` below.

**Skins from the window.** **New skin...** (in the list header, next to Refresh) opens a dialog: *Monster* (a list with the ten
playable monsters and the known custom monsters; it also accepts a typed id, which counts as a custom monster id: no extracted textures
and `skin.json` without `base`), *Base costume* (0 to 3, with each costume's portrait; a costume not on the disc is disabled; for a
custom the default is the `costume` of `monster.json`), *Name*, *Id* (derived from the name, editable) and "CPUs may pick this skin". It creates the
`skin.json` in `mods/<mod>/skins/<id>/` and extracts the base costume's textures to the work folder. In the list, the skin is its own group
at the top ("Skin: NAME (monster/id, base N)"); when selected, the preview becomes the properties panel (Name, Base, CPUs may pick,
*Mesh* with **Export to Blender**, **Import mesh...** and **Import animations...** (see "Mesh and animations from Blender" below), **Save** rewrites `skin.json`). Right click on the group: **Duplicate skin...** (copies the skin in the mod and in the work folder with the new
id) and **Delete skin**. With a playable monster costume group selected (e.g. "MON/SHOGUN3"), the header's **Turn into skin...** button (also
on right click) creates the skin with that monster and costume and moves the group's PNGs to its folder, so the original costume stays untouched in Build; if the mod already has
those PNGs packed in `MON/`, the window warns and offers to delete them.

**Custom monsters from the window.** **New monster...** (next to New skin) opens a dialog: *Base type* (the ten playable monsters and the three
accepted bosses, with a note of what the boss lacks), *Costume* (with the portraits; for Goliath the default is 1), *Name*, *Id* (derived from the
name; it cannot be a playable monster's name), *Camera* (the type's own or another monster's, with the distance in %; for Goliath and
Jelly it defaults to Togera's at 200% and 300%), *Narrator* (the `narrator/*.mp3` the port finds), "Own look" and "CPUs may pick this monster".
It creates the `monster.json` in `mods/<mod>/monsters/<id>/` and, with "Own look", writes `"files": true` and extracts the costume's textures to
`monsters/<id>/files/` in the work folder: on Build the edited PNGs go to the monster's `files/` folder and apply only to it (the
disc costume stays untouched). In the list, the monster is a group at the top ("Monster: NAME (custom id, base X costume N)"); when selected, the
preview becomes the properties panel: Name, Camera, Narrator, *Rules* (the `kind`), *Special* (the `special`), CPUs, *HUD portrait* (**Set...** copies the image to
`portrait.png` and writes the right `portraitTex` of the costume's `.RTX`; **Remove** goes back to the costume's) and *Look* (how many textures and the own
model; **Import model files...** copies `.NGP` + `.PTR`, and optional `.TEX`/`.RTX`, to `files/`; **Extract stats** extracts the base
type's `.CHR` and `.AIC` files to `files/`, where the edited ones become the monster's own; **Extract sounds** extracts the base type's
sound bank as WAVs to `files/`, and on Build the replaced ones make the monster's own bank; **Export to Blender**, **Import mesh...** and
**Import animations...**, below). **Save** rewrites `monster.json`
keeping the other keys. Right click on the group: **New skin for this monster...**, **Duplicate monster...** and **Delete monster** (its
skins stay and the log warns). On an extracted costume group (playable or boss, e.g. "MON/ASSBOSS1"), right click has **Turn into
custom monster...**, which moves the group's PNGs to the new monster.

**Mesh and animations from Blender.** A skin or a custom monster can have its own mesh and its own versions of the base type's
animations, on the base type's skeleton:
1. **Export to Blender** writes the base costume as `<PREFIX><costume>.glb` (e.g. `LIZARD1.glb`) in the work folder: the skin's
   folder, or `monsters/<id>/files/` for a custom. It holds the meshes (`obj_<address>`), every transform of the skeleton as a bone
   (`bone<N>` for the ones the mesh is weighted to; `xf_`, `st_` and `sc_` for the others: helpers, locators, scale nodes), the
   weights, the textures and every animation as an action `a<id>` (`a090` is the idle; the list is in `tools/pose.py --list`). The
   default pose is the bind pose. It takes about a minute, and never overwrites a `.glb` that is already there.
2. In Blender, set the scene's frame rate to 60 fps (*Output Properties > Frame Rate*) before *File > Import > glTF 2.0*: the
   importer places the keys at time x frame rate, and 60 keeps one Blender frame per game frame. Remodel, reweight and re-UV the
   meshes, and/or edit the actions in pose mode, keeping the mesh and bone names and the armature where it is; then *File > Export >
   glTF 2.0* (`.glb`) with the default options (they keep the vertex colors and write every action).
3. **Import mesh...** takes the meshes of the edited `.glb`; **Import animations...** takes its actions (the same `.glb` or another
   one). Each one rebuilds the `.NGP` + `.PTR` next to it from the base costume plus the last `.glb` given to each of the two
   (recorded in `.blender.json` in that folder), so one does not undo the other and the files do not grow with every import; a
   custom gets `"files": true`. **Build mod** sends them to the mod; `.glb` and `.blend` files stay in the work folder. **Revert**
   on the `.NGP` or `.PTR` goes back to the disc's and forgets both `.glb` files.

Mesh: each primitive keeps the texture its material names (`tex<texId>`, one of the costume's); a new UV layout pairs with an edited
PNG of the same size. Up to 3 bones per vertex (the heaviest kept). The few vertex colors brighter than 1.0 (Magmo costume 2) come
back at 1.0. The import refuses a mesh whose object uses two palette blocks, and one whose skeleton was moved or rescaled.

Animations: only the actions that differ from the game's are rewritten; the log lists each one (`a090: rebuilt, 161 -> 161 frames,
1 transforms moved, 127 curves (1 new)`), and the others stay byte for byte. A transform that moved gets new curves (linear,
16-bit) and the rest of the action keeps the game's. An action may get longer or shorter: the curves that did not move, including
the event channels (hits, sounds), are stretched to the new length. The game rotates each transform by three angles on its own
axes: a transform with fewer rotation channels follows the edit as closely as its axes allow, and the log says how far off
(`fit off by up to ...`). Transforms with no channel in the game cannot move (the log names them). Details: `MODELS_3D.md`
(`tools/ngpmesh.py`, `tools/ngpanim.py`).

New animations: an action named with an id the base type does not have (`a299`; ids go up to 299, and `tools/pose.py --list`
shows the ones taken) is added to the model (`a299: new animation 299, 181 frames, 141 curves`). The easy way is to duplicate an
existing action in Blender, rename it and edit it; a bone the action does not key stays in the default (bind) pose. It loops when
the game's monsters loop that id; `a299_loop` and `a299_once` choose. A new animation has none of the event channels of the
game's (hand and foot contacts: footsteps, dust, splashes). The game plays an animation only where its code or data asks for
that id: for a custom monster, the punch table of its own `.CHR` (`files/`, above) picks the animation of each punch, and
`python tools/chr_punch.py <file.CHR> <pos> <type> <id>` changes it (`pos` and `type` as `tools/chr_dump.py` prints them). The
hit window of the slot is a fraction of the animation, so it follows a longer or shorter one. `WOTM_ANIMLOG=1` logs every
animation a monster of the fight starts (`[anim] 2401 m0 fire_togera tipo 3 anim 299 (transition)`).

**Own skeleton (custom monsters).** **Import rig...** (next to **Import mesh...**) takes a `.glb` with an armature of its own and
makes it the custom's body, on top of the base costume: the base type's code, shadow, reticle, collision grid and sounds stay, and
the skeleton, the mesh, the animations and the locators are the `.glb`'s (`tools/ngprig.py`; rules in `MODELS_3D.md`, "New
skeleton"). Any base but Congar (its code moves 8 of its bones by number). What the `.glb` holds:
- an armature (bone names and hierarchy are free) and the meshes weighted to it; up to 3 bones per vertex (the heaviest kept);
  a mesh parented to a bone without weights follows that bone;
- actions named `a<id>` with the game's ids (`a090` is the idle; `tools/pose.py --list` on the base shows the ids, `docs/ANIM_IDS.md` what each one is). An id of the
  base type that the `.glb` lacks gets the rest pose for the base's length, so every move still has an animation. The footsteps
  and dust come from the height of the hand and foot locators; the sounds and the throw channels come from the base's animation
  with the same id. With the scene at 60 fps before the export one Blender frame is one game frame (the log warns when the keys
  are further apart than 1/60 s);
- locators: empties parented to a bone, named by alias (`hand_l`, `hand_r`, `foot_l`, `foot_r`, `grab`, `grabbed`, `impale`,
  `chest`, `hold`, `mouth`, `head`, `look`, `extra1`, `extra2`) or `loc_<hex id>`. A locator of the base the `.glb` lacks goes
  where the base has it, on the nearest bone, and the log names it;
- a mesh named `coll*` is a collision box (not drawn); without one, a box around the rest mesh is used;
- materials: `tex<texId>` keeps one of the base's textures; a material with an image gets a texture of its own (the PNG lands
  in `files/` as `<file>_t<texId>.png`, editable like the others); one without an image gets a PNG of its color.
A scene made in Blender (Z up, facing -Y, meters) is turned to the game's front and scaled to the base type's height, feet on the
ground; `modkit.py monster rig ... --front +y` (a model facing +Y), `--scale S` or `--height H` change that. A `.glb` from **Export
to Blender** (its "WotM" root) is already in the game's axes and units, so the base type's own skeleton, edited, also goes in this
way. The import writes `.NGP`, `.PTR`, `.TEX`, `.RTX` and the new PNGs to `files/` and records the `.glb` in `.blender.json`;
**Import mesh...** and **Import animations...** then refuse that monster (edit the rig `.glb` and import it again). **Revert** on
the `.NGP` goes back to the base type's skeleton. A part that scales, appears or disappears in some action (a growing claw, a
weapon shown only in one move) has to be a rigid mesh parented to a bone: scaling a bone does not reach the weighted meshes.

**A rigged model from elsewhere.** A model that already has a skeleton (a Collada `.dae` with a skinned mesh) can take the base
type's animations instead of new ones: export the base costume (**Export to Blender**, e.g. `LIZARD0.glb`), write a bone map
(which model joint follows which base bone; `tools/rigmaps/biped_togera.json` covers the 3ds Max Biped, `Bip01_*`, on Togera)
and run `python tools/retarget.py LIZARD0.glb model.dae map.json rig.glb`; then **Import rig...** with `rig.glb`. The map can
start from another one (`{"extends": "biped_togera.json", ...}`) and change only the tail, add a `mouth` locator where the
fire comes out or close a jaw the model's bind pose leaves open (`"turn"`). Each joint keeps the tilt it has in the model's
bind pose, so a foot that is not flat there stays tilted in every move; the Biped map lays the feet flat (`"level"`). The base's layers (looking around, flinches) keep only rotations, as the game adds them on top of the main
animation (`MODELS_3D.md`, "New skeleton"). The game lights monsters brightly: a texture made for another game may need
darkening to keep its look.
The model can also bring moves of its own: `--own clips.json` replaces the base's animation with the same id (a walk, a
punch from the model's own game; `docs/ANIM_IDS.md` says which id is which), each clip giving the world of the model's joints
per frame; keep the base's length for attacks and hit reactions, where the game times the move by the animation. A weapon
that appears only in some moves is a rigid mesh on a joint in the `.dae` (a node with `instance_geometry` under the joint)
and `"props": {"<node>": {"show": "<base node>"}}` in the map: it is seen as much as that node of the base is (its scale),
e.g. a sword on the hand that follows the base's own sword parts.

## The kit on the command line (`tools/modkit.py`)

```
python tools/modkit.py export my_mod MON/LIZARD0          # Togera's .TEX and .RTX as PNG, in my_mod's work folder
python tools/modkit.py export my_mod SND/LIZARD_S.BNK VAG/CITYAMB.VPK
  (edit the files in mods/_work/my_mod/)
python tools/modkit.py pack my_mod [--inject]             # takes what changed to mods/my_mod/
python tools/modkit.py check [my_mod]                     # checks names, sizes and texIds of the mods and the conflicts between them
python tools/modkit.py revert my_mod MON/lizard0_t0004.png   # reverts a work folder file to the disc original
python tools/modkit.py monster new my_mod fire_togera --base togera --costume 2 --name "FIRE TOGERA" [--ai]
    [--camera togera:200 | --camera none] [--narrator togera] [--no-textures]   # custom monster (section "Custom monsters")
python tools/modkit.py monster portrait my_mod fire_togera retrato.png   # HUD portrait (2:1) and the portraitTex
python tools/modkit.py monster model my_mod fire_togera X.NGP X.PTR [X.TEX] [X.RTX]   # own model into files/
python tools/modkit.py monster stats my_mod fire_togera   # the base type's .CHR and .AIC to edit (own fight numbers and AI)
python tools/modkit.py monster sounds my_mod fire_togera  # the base type's sound bank as WAVs (own sounds)
python tools/modkit.py monster mesh export my_mod fire_togera   # the base costume as .glb for Blender (mesh and animations)
python tools/modkit.py monster mesh import my_mod fire_togera [edited.glb]   # the edited .glb's meshes into its .NGP + .PTR
python tools/modkit.py monster anim import my_mod fire_togera [edited.glb]   # the edited .glb's actions into its .NGP + .PTR
python tools/modkit.py monster rig my_mod lizardman rig.glb [--front +y|-y] [--scale S | --height H]   # own skeleton
python tools/modkit.py skin mesh export my_mod green          # the same for a skin (skin mesh|anim import my_mod green [edited.glb])
```

- A mod is a project: the work folder is `<mods>/_work/<mod>/`, with its `modkit.json`, and one mod's `pack` never takes what
  was edited for another. `--work <folder>` changes the work folder.
- It reads the repo's extracted disc (`disc/`, or `--disc`/`WOTM_DISC`) and writes to the runtime's mods (`.wotm_recomp_dir` + `discroot\mods`,
  or `--mods`). Wildcards work (`MON/LIZARD*`); without an extension it takes `.TEX` and `.RTX`.
- `pack` only takes what changed since the export (sha1 in `_work/<mod>/modkit.json`). Exporting again does not overwrite what was already edited.
- `check` ignores `_work`. Each file, PNG, skin or monster line is marked "(overrides X)" or "(overridden by Y)" when another
  enabled mod has the same key, and the output ends with the section "conflicts between enabled mods": one line per repeated key (PNG
  by stem/rtx/texId, file by path, skin by `monster/id`, custom monster by id) with the mods and which one wins (the last in
  alphabetical order). `check <mod>` shows only the conflicts that involve that mod. It also warns about a skin whose `monster` is neither playable
  nor a known custom.

| On the disc | In the work folder | `pack` writes |
|---|---|---|
| `.TEX` | `<file>_tNNNN.png` (one per texture) | the PNG as is |
| `.RTX` | `<file>_rtx_tNNNN.png` (portraits, screens, fonts) | the PNG as is |
| `.VPK` / `.VAG` | `<file>.wav` | `.VPK`/`.VAG` (same rate and volumes as the original) |
| `.BNK` | `<bank>_sNNN.wav` (one per sound with a sample; a sound made of layers, several TONEs each with its own delay, pitch and sample, comes out as the mono mix the game plays) | the bank with all edited sounds replaced (a sample another sound also uses: the edited one gets its own copy and the other keeps the original; a layered sound becomes a single layer with the WAV, full volume envelope, no delay and no LFO; it loops only if the original played until the game stopped it, not when a KEY_OFF in its sequence ended the loop) |
| others | copy | copy |

## Textures as PNG

- The game still loads the disc's `.TEX`/`.RTX`; the native drawing swaps the image at draw time
  (`native/wotm_texmods.hpp`). That is why the PNG can have **any resolution** (HD textures): object UVs are normalized, and
  screen quads (bar portraits, font) scale the region by the ratio between the PNG and the original texture. Keep the
  original's aspect ratio (`check` warns).
- It works in any folder of the mod (the file name is enough) and for every palette that uses the texture.
- Alpha: the PNG uses the same scale as the original texture, as the export writes it. Most of the game's textures go from 0 to 0xFF (twice
  the GS opaque 0x80); in them the PNG alpha is taken raw, 255 = 0xFF, and the log says "alfa cru, 255 = 0xFF". In the ones that stop at 0x80,
  255 = 0x80 (the rest / 2). In practice: 255 is the opaque the game uses, and export and `pack --inject` round-trip without losing anything.
  This matters for textures like the title logo (SHELL t140/t141), which the game draws with the vertex at half opacity, counting
  on 0xFF. A semi-transparent PNG from an old kit version can come out more opaque: export again. Textures without alpha in the game
  (TEX0.TCC = 0) ignore the PNG alpha.
- An image split across several textures (the title logo is two 256 × 256 side by side, upside down): scale the whole image
  once and cut at the exact middle, without overlap; cuts made by hand with extra margin repeat a piece at the seam.
- Particles (fire, sparks, explosions, debris, pedestrians, lens flare): their textures live in the stage's `.TEX` (in Midtown
  Park, `central_t0004` to `t0059` and `t0289`), so the PNG carries the stage's name and applies only to it; to replace them in all stages, one PNG per
  stage. Particle UVs are normalized too: any resolution works. Picture of the fire in green at twice the resolution:
  `docs/screenshots/2026-10-25/2026-10-25_mods_png_particulas.png`.
  - In Midtown Park: explosion = `t0008` to `t0039` (32 frames of 64 × 64); electric arc (Kineticlops's bolt, Robo-47's dome)
    = `t0057` (128 × 64); pedestrians = `t0289` (128 × 128 sheet, ~8 × 16 figures upside down).
  - The on-screen size comes from the game's effect, not from the PNG: a bigger PNG only has more pixels in the same quad. Upscaling (Real-ESRGAN
    and the like) only shows when the particle is bigger on screen than the original texture. On the bolt and the explosion seen in a fight
    it is not noticeable (`docs/screenshots/2026-10-25/2026-10-25_mods_png_raio_sem_e_com_upscale.png`); on pedestrians up close it is,
    but the models invent shape where the original has few pixels
    (`docs/screenshots/2026-10-25/2026-10-25_mods_png_pedestres_upscale.png`,
    `docs/screenshots/2026-10-25/2026-10-25_pedestres_folha_original_e_animesharp.png`). For smaller or sharper particles,
    redraw: a smaller drawing in the center, with more transparent border, shows up smaller.
- It does not apply: to composited dust/smoke (the game builds it in VRAM every frame) and to the emulated GS image
  of `ps2EntryRunner` (the reference of the VU1 check; its window uses the PNG). For that one, `pack --inject` also rewrites the mod's `.TEX`/`.RTX` when the PNG has the
  original size (the colors go to the current palette).
- How the native layer finds the file: the upload packet of `texInfo[texId]` is inside the `.TEX` (or `.RTX`) loaded at
  `fileStatus.texAddr[i]` (`resAddr[i]`); the offset inside it gives the disc texId, and the name is `fileStatus.names[i]`. The
  resident ones (T8H/T4HL/T4HH) come through the DBP, as in `decodeResident`.

## Skins (extra costumes, without replacing)

A skin is a folder in `mods/<mod>/skins/<folder>/` with a `skin.json`:

```json
{ "monster": "ultra-v", "id": "roxo", "name": "PURPLE ULTRA V", "base": 3, "ai": true }
```

- `monster`: name of the playable monster (`congar`, `robo-47`, `togera`, `ultra-v`, `preytor`, `kineticlops`, `magmo`, `raptros`,
  `agamo`, `zorgulon`), the file prefix (`SHOGUN`) or the `id` of a custom monster (`moddedtogera`; see "Custom monsters"). A
  custom's skin only shows up in its own list, never in the base monster's column nor in the AI draw; without `base`, the `costume`
  of `monster.json` applies.
- `id`: lowercase letters, digits and `_` (without it, the folder name). The save stores `"ultra-v/roxo"`.
- `name`: what the select screen shows. `base`: disc costume (0 to 3) that provides whatever the folder does not bring, and the costume's sounds.
  `ai`: whether drawn AIs can use it (default no). `narrator`: `narrator/<nome>.mp3` spoken when the skin is confirmed in a select
  screen (empty, or the file missing: the custom's `narrator`, then the type's). `fireColor`: the colour of the fire (`"#FF7A1E"`;
  empty = the custom's `fireColor`, then the game's); see `fireColor` under "Custom monsters". `shotColor`: the colour of the
  default shot, the same way (see `shotColor` under "Custom monsters").
- A mod skin is always unlocked: it does not go through the unlock screen.
- What goes in the folder, from the simplest to the most complete:
  - **PNGs only**: model and textures of the base costume; the PNGs replace the textures. Name as in the base costume's export
    (`shogun3_t0010.png`, `shogun3_rtx_t0011.png`) or just `t0010.png`/`rtx_t0011.png`. They apply **only to the skin**: the real costume 3
    keeps its texture, even in the same fight.
  - **`.TEX` and/or `.RTX`** (any name, by extension): own textures, with the same texIds as the base (`modkit pack --inject`
    writes them this way).
  - **`.NGP` + `.PTR`** (always both): own model, with the same skeleton and animations as costume 0. A new mesh comes from
    Blender through **Export to Blender** / **Import mesh...**, and edited animations through **Import animations...**
    (`skin mesh export|import`, `skin anim import`; "Mesh and animations from Blender").
- The game sees each skin as one more costume of the monster (`MON/SHOGUN4.*`, `SHOGUN5.*`...; numbers assigned on every boot, in
  mod/id order). Same monster and id in two mods: the last in alphabetical order wins. The log says what got in:
  `[mods] skin ultra-v/roxo ("PURPLE ULTRA V", meu_mod) = traje 4 (SHOGUN4); base 3, 2 png`.
- In the menu (native select of screens 3, 6, 7 (2-player minigames), 18 and 19 and of 3 and 4 players): up/down goes through the disc costumes and then through the monster's
  skins, always unlocked, and more than one player can pick the same one. Through `WOTM_MATCH` too (`WOTM_MATCH=4:roxo,...`) and through the
  launcher (each monster's skin box lists its mod skins). The 3D model of the select screen and of the save list shows the skin's PNGs.
- In the fight the skin uses the sounds of the `base` costume (Preytor base 1 and Agamo base 3 have their own sounds). Log:
  `[mods] skin ultra-v/roxo (traje 4) na luta com os sons do traje 3`.
- **Save:** the adventure stores the skin by name (`"costume": "ultra-v/roxo"`; the disc costumes stay as 0 to 3). Without the mod, the slot
  loads with costume 0 (log `[wotm] save: traje ... não achado (mod ausente?); usando traje 0`, and "skin missing" in the save list);
  saving the same adventure keeps the name, and the skin comes back when the mod comes back.
- **Drawn AIs** (endurance): with `"ai": true`, the skin enters the draw of that monster's AI costume, along with the costume the
  game would pick (log `[mods] IA N sorteada com a skin ...`). The adventure's AIs keep the game's costumes.
- **Kit:** `python tools/modkit.py skin new <mod> <monstro> <id> [--base N] [--name "GREEN TOGERA"] [--ai]` creates the `skin.json` and exports the
  base costume's textures to `mods/_work/<mod>/skins/<id>/`; edit the PNGs and run `modkit.py pack <mod>`: only the edited ones go to the
  skin folder. `<monstro>` can also be a custom monster's id: if the kit finds its `monster.json` (in the repo's `mods/` or in the
  mods folder), it exports the base type's textures in the custom's `costume` (or in `--base`), and the `skin.json` comes out without `base` when
  `--base` is not given. An unknown id creates only the `skin.json`, with a warning.
  In the window, the same with **New skin...**, the skin's properties panel and **Turn into skin...** (section "Interface").
- **Elimination** (`native/runtime/wotm_elimcostume.inc`): the game loads the 10 playable monsters at once and whoever dies picks another
  unused one (dialog 8). Each starting player gets their own instance: the first player on a monster takes the retail load in their
  costume (skin or custom included), and each repeated monster is one more load in the repeater's costume. A monster picked in dialog 8
  comes in at the costume of its instance (costume 0 if nobody started with it). The file table holds 12 monsters, so at most 2 repeats:
  three players can share a monster, four cannot; with 2 repeats the army vehicles stay out of that match. Log:
  `[elim] jogador N: monstro T no traje C (instancia I)`.

Example (`WOTM_MATCH=4:roxo,4:3,1`, AI): the skin with the recolored PNG comes out green and costume 3 next to it stays purple;
a skin with only `skin.json` (`togera/demonio`, base 3) loads costume 3's model. Picture:
`docs/screenshots/2026-10-23/2026-10-23_skin_mod_png_so_na_skin.png`.

## Custom monsters (one more monster on the select screen)

Custom monsters are experimental: the owner's mods for his own game, with no guarantee that everything works (Goliath has no
climbing animations nor AI outside his stage). A custom monster is a folder in `mods/<mod>/monsters/<folder>/` with a `monster.json`. It uses the code
of one of the game's types (`base`) and that type's model and files, or its own files (`files`, below; the Mecha). A free game
type (another prefix in `MON/`) does not exist yet. The first one is Goliath Prime (`mods/Goliath Prime/monsters/goliath/monster.json`, in the repo):

```json
{ "id": "goliath", "name": "GOLIATH PRIME", "base": "assboss", "costume": 1,
  "camera": { "from": "togera", "scale": 200 }, "narrator": "goliathprime",
  "portrait": "goliathprime.png" }
```

- `id`: lowercase letters, digits and `_` (without it, the folder name). It is what `WOTM_MATCH` accepts (`WOTM_MATCH=goliath,3,1`).
- `name`: what the select screen and the HUD show. The HUD has 11 letters: it uses the last word that fits, in lowercase ("goliath").
  The custom's skins show it too (the skin's own `name` is for the select screen). The game keeps one HUD name per type, set from
  the monster the camera follows: with it on a custom, the custom's name; on a monster of the game (any costume or skin of its
  own), the disc name.
- `base`: the game type, by name or prefix: the ten playable monsters (`togera`, `LIZARD`...) or a boss that is a `Monster` (`kCustomBases`,
  `native/wotm_mods.hpp`): `assboss` (13, Goliath), `jelly` (14, Cerebulon's second phase) and `final` (15, Cerebulon, the last
  phase). A custom with a playable base runs that monster's code and AI; only Goliath was tested (to change only the look, use
  a skin).
- `costume`: the disc costume (`ASSBOSS1`; `ASSBOSS0` is a prototype with half of the animations and the small portrait; `JELLY0` and
  `FINAL0` only have costume 0).
- `camera`: another monster's camera (`from`, by its in-game name) with the distances scaled (`scale` in %, 10 to 1000; the port's
  JSON only reads integers). `ASSBOSS.CHR` was never used by the game (in the adventure the camera is the stage's) and leaves the camera on top of the
  monster, and `JELLY.CHR`'s sits between Jelly's legs; Togera's at 2x (Goliath) and 3x (Jelly) behaves like a regular
  monster's. `FINAL.CHR`'s already works. The port reads the camera from the `from`'s `.CHR` (from `mods/` or from the disc) and puts it on the monster on every
  POV change (log `[custom] jelly: camera do RST/LIZARD.CHR, distancias x3.00 (POV 0: ...)`).
- `narrator`: `narrator/<nome>.mp3` spoken on confirmation (empty = the type's). It also serves the custom's skins that have no
  `narrator` of their own.
- `costumeName`: the name of the custom's own costume in the select screen's costume list, next to its skins (`"GODZILLA 90s"`;
  empty = "FROM MOD <mod>").
- `fireColor`: `"#RRGGBB"`, the colour of Togera's green fire on this monster (own costume and skins without their own
  `fireColor`). The particle effects Togera makes (breath core, cloud and sparks; the fireball's muzzle flash, impact and sparks;
  crowd control: presets 8, 36, 51, 80, 81, 95, 132, 148) created by this monster are drawn with the colour in place of the green: the
  saturated part takes the colour and the white core stays white, at the same brightness, in the vertex colours and in a recoloured copy of
  the particle texture (the breath's is green itself). Another Togera in the same fight stays green: each effect is tagged when
  created with the monster that made it (`FireBreath::Activate`/`TestCollis`, the fireball's owner in `Weapon::DetonateWeapon`,
  `StateCrowdControl::transitionInto`). Log: `[custom] fogo: preset 51 na cor #FF7A1E`. Only in the native drawing (not in the emulated
  GS of `ps2EntryRunner`). Two green pieces are not particles and stay in the model files: the fireball's body is the texture
  `lizard0_t0001.png` (a recoloured PNG in the skin or in `files/`), and the crowd control swirl is the model with id 0x63 in
  `LIZARD0.NGP`, with green vertex colours (`0d800d`) that have to be recoloured in the skin's `.NGP`.
- `shotColor`: `"#RRGGBB"`, the colour of the type's default shot (the projectile of the ranged attack, `Monster` +0x6878; Ultra-V's
  red bolt, type 6) fired by this monster (own costume and skins without their own `shotColor`). Three parts take the colour, with
  the same recolouring as `fireColor` (the white stays white): the effects created when the shot explodes (`Weapon::DetonateWeapon`;
  Ultra-V's are `ShogunFlashBang` 112, `RedHalo` 116 and `WhiteFlash` 29), the point light the shot carries (`Weapon::InitWeapon`;
  red on Ultra-V) and the shot's body (the model of the weapon type in `Weapons::m_epNodes`, id 4012 on Ultra-V, vertex colour
  `255,64,64`), through the `colorQuad` of the shot's `_cs`, the per-object vertex colour multiplier the game itself applies (so it
  also shows in the emulated GS of `ps2EntryRunner`). A pickup fired as a weapon, the special and the punch flashes keep the game's
  colours. Log: `[custom] tiro: preset 112 na cor #FFD23A`.
- `logo`: a 2:1 PNG with the name drawn (in the monster's folder or at the mod root; the game's are 256 × 128, 512 × 256 keeps it
  sharp in a large window), shown where the game shows its monsters' logos: under the model in the select screen and above the
  stage picture in the save list. Without it, the name in text (the save list never falls back to the base type's logo).
- `files`: the model's own files, in a subfolder of the monster (`true` = `files/`, or its name) with `<any name>.NGP` and
  `.PTR` (and `.TEX`/`.RTX`, optional): they become a mod costume of its own (like an implicit skin, costume 4 to 99), which becomes the default
  costume; the JSON's `costume` provides what the files do not bring (sounds, whatever is missing). Without the folder, the log warns and the disc costume stays.
  The same folder takes the monster's own fight numbers and AI, in place of the base type's: a `.CHR` (punches, damage, ranges, camera;
  `RST/<PREFIX>.CHR` format) and `.AIC` files (`RST/<PREFIX><difficulty>.AIC` format). An `.AIC` whose name ends in 0, 1 or 2 is that
  difficulty's; one without a digit covers the difficulties without their own (the adventure uses them too, in place of the per-stage
  ones). They apply to the custom's own costume and to its skins, never to the game's monster of the same type, and two customs of the same
  type in one fight each read their own. Log: `[mods] monstro fire_togera: .CHR LIZARD.CHR, .AIC 0 LIZARD0.AIC, ... (RST/C_FIRE_TOGERA)`
  and, in the fight, `[custom] fire_togera: .CHR proprio (C_FIRE_TOGERA no lugar de lizard)`. The punch animations named in the `.CHR`
  are the model's, so the `.CHR` has to match the skeleton and animations of the base type.
  A `.BNK` in the folder is the monster's own sound bank: it takes the place of the base type's bank (`SND/<BANK>.BNK`: `APE_S` for
  Congar, `LIZARD_S` for Togera, `ROKMAN_S` for Agamo, `ASSEMB_S` for Goliath, `FINAL_S` for Jelly and the final form) for everything
  this monster plays: its moves and hits (`MonsterSound`), its projectiles (`WeaponSound`, by the weapon's owner) and Preytor's bugs
  (`SuckerBugSound`). It keeps the base bank's numbering (sound N plays where the game asks for sound N of the type), so it is the base
  bank with some sounds replaced, as `monster sounds` and `pack` make it. A Preytor or Magmo hit sounds from the attacker's bank. Like the
  `.CHR`, it applies to the custom's costume and skins only; a game monster of the same type in the same fight keeps the disc's. The
  same bank serves in 3 and 4 player fights, where the game loads the low-res `_LS` banks (same numbering). Log: `[mods] monstro
  fire_togera: som LIZARD_S.BNK` and `[snd] fire_togera: banco de som proprio LIZARD_S.BNK no handle 0x10001`; with `PS2X_SND_LOG=1`
  each `Play banco` line shows the bank's handle. The narrator's MONSTER CRY option plays the cry from this bank too.
  Togera's sounds, by number: 1 death (the 120 animation), 2 cry (fight entry cinematic), 3 pain, 4 jump, 5 and 6 the fireball (launch,
  explosion), 7 the fire breath (a loop while the special lasts), 10 to 12 the three victories, 13 taunt, 14 and 15 the other two
  deaths, 0 crowd control, 17 entering counter block.
- `kind`: the port's own rules for the monster (`wotm_custom.inc`). Only `"mecha"` for now (below); empty = the type's.
- `special`: another monster's special attack, by its in-game name like `base` (`"special": "congar"`); empty = the base type's. Every
  `Monster` carries the states of all the specials; `Monster::initDynamics` points the monster at its type's one (+0x7980) and the
  port, right after it, points it at the one asked for and runs its `init`, so the buttons, the CPU and the training mode use it. The
  special plays the monster's own animations 160 (windup), 161 (loop) and 162 (winddown): to make them look like the other monster's,
  import new ones with those ids ("New animations", above). Type tests inside the special still see the base type (only a Raptros
  sets the victim on fire). It applies to the custom's own costume and its skins (it needs `files`, like the `.CHR`) and to boss-based
  customs outside the adventure. Accepted: Congar's roar, Togera's and Raptros's fire, Kineticlops's shock, and Jelly's and Cerebulon's
  shock (the port's Jelly rules come with it: Triangle + Circle on a player's controller, aimed at the nearest enemy). The others use a
  model that only their owner's `.NGP` brings and the game registers from it (Robo-47's shock wave effect, Ultra-V's hook ball,
  Preytor's bugs, Magmo's lava rocks, Agamo's head, Zorgulon's disc, Goliath's cannon shells): on another body they do not start, hold
  the monster in the windup or crash the game, so the port keeps the base's special and logs why. Log: `[custom] fire_togera: especial
  do congar`. In the kit window: **Special** in the monster's group.
  **Robo-47's homing missile** works on a custom with its own skeleton (`files` from **Import rig...**): the rig import copies the
  missile (weapon model 4005) and the shock dome (`SpecFxAnim` 0x11FF, with its 3 animations) from the disc's `MON/ROBOT0.NGP` into
  the custom's `.NGP` (`tools/ngpgraft.py`; by hand: `python tools/ngpgraft.py <files folder> robo-47`), under ids the game ignores (model + 0x100, `SpecFxAnim` + 0x80) so it does not clash with
  a Robo-47 in the same fight. The port registers the copied dome for that monster, gives it to `SpecFxAnim::isInstanceAvailable` and
  `getInstance` in place of the game's id, points animation slot 191 (the dome the special plays while aiming; the game fills it with
  the type's id + 5) at it, and swaps the copied missile into `Weapons::m_epNodes[13]` while `Weapon::InitWeapon` builds one of that
  monster's missiles. The special adds three animations on top of the pose (`StateRobotSpecial::init` blends): 166 and 167 (aim,
  scrubbed by a percentage, 50% = straight ahead) and 161 (the shot), so the rig writes them as layers: rotations only, as the
  difference from the action's first frame (`ngpgraft.LAYERS`). 160 (windup) ends in the aim pose, which the game holds until the shot;
  162 is the winddown. The missile leaves from locator `extra1` (0x816). Its sounds are in Robo-47's bank: a `ROBOT_S.BNK` in the
  folder is used for them (in Robo-47's bank slot while this monster's sounds play), otherwise they only sound with a Robo-47 in the
  fight. Logs: `[mods] monstro CJ/monsters/cj: especial do robo-47 com as pecas dos arquivos proprios`, `[custom] SpecFxAnim copiada
  0x127f (do 0x11ff) registrada para o dup 0`, `[custom] cj: animacao 191 do especial na copia 0x127f`, `[custom] cj: projetil 13 do
  especial com o modelo proprio (no 0x10a5)`.
- `specialRoar`: `true` = the special starts with the monster's cry (`MonsterSound::playSignatureSound`, the fight entry cinematic's
  sound, from its own bank when it has one), when it enters the special's state. Togera's fire breath has no cry of its own. Log:
  `[custom] godzilla: grito no especial`.
- `fallBlend`: frames (0 to 120) over which the jump's rising layer (animation 33) fades out after the apex, so the fall (34)
  shows on its own; 0 = a cut. Without it the game keeps the rising layer over the fall, weighted by the vertical speed, until
  near the ground: fine when the two poses are alike (Ultra-V), a blend of two poses when they are not. Log (`WOTM_ANIMLOG`):
  `[custom] cj: queda do pulo, camada da subida 1.00 some em 6 quadros`.
- `landOnTouch`: `true` = the jump's landing layer (animation 35) only comes in when the feet touch the ground. Without it the
  game puts it on near the ground, still in the air, held on its first frame (a pose bracing for the landing). Log:
  `[custom] cj: pouso no toque`.
- `ai`: whether a CPU can use it (default no); without it the select screen shows "PLAYERS ONLY" in the CPU's box. With `"ai": true` the CPU can
  pick it in the select screen's CUSTOM slot; the launcher only offers customs to the player. A custom with a playable base fights as a CPU
  (`WOTM_MATCH=1,moddedtogera,1`, with `"base": "togera"`: walks, attacks, breathes fire and loses the round like a Togera), and Jelly and
  Cerebulon too (`WOTM_MATCH=1,cerebulon,1`: the game's regular AI, the same one that moves Cerebulon in the final stage). Goliath does not: his AI
  (`Ai::updateAssBoss`) needs the points of his stage, and as a CPU (`WOTM_MATCH=3,goliath,1`) he loads but does not show up in the fight.
- **On the select screen** (screens 3, 6, 7, 18 and 19 and the 3 and 4 player one; in elimination only customs with a playable base,
  which take the base monster's place in dialog 8): the CUSTOM slot of the monster bar. X opens the list of customs inside the
  player's box (name, mod, how many skins; in a CPU box, "PLAYERS ONLY" on the ones without `"ai"`): up and down scroll, X picks, triangle
  closes. In the adventure (1 to 4 players) X does not pick a boss-type custom (Goliath, Jelly, Cerebulon and the Mecha, Jelly-based):
  its stages' bosses are of those types (`customAllowed`). Once picked, the box shows the monster like the game's ones: up and down change costume (the `costume` of `monster.json` and the
  custom's skins, in gold), X confirms, triangle goes back to the closed slot. With the list open, left and right do not leave the
  slot. On the game's screens, on confirmation the game's cursor stays on the column the box came from, which stands in for it (that column's spot
  is not taken). On the 3 and 4 player one (`customStep` in `wotm_padsel.inc`) the choice is all ours: the custom's type goes to the box's `m_monsterSel` and the costume to
  its costume (players 3 and 4 through `wotmMultiSetCostume`, CPUs in `m_costume[2 + c]`).
- **Kit:** `modkit.py monster new <mod> <id> --base <tipo>` creates the `monster.json` (without `--costume` and `--camera`, the type's defaults:
  Goliath in costume 1 and with Togera's camera at 200%, Jelly with Togera's at 300%) and, without `--no-textures`, writes `"files": true` and
  exports the costume's textures to `mods/_work/<mod>/monsters/<id>/files/`; `pack` takes the edited ones to the monster's `files/` folder,
  where they become its own costume (PNGs alone are enough: what the folder does not bring comes from `costume`). `monster portrait` copies the
  portrait and finds the `portraitTex` (the last 128 × 64 texture of the costume's `.RTX`); `monster model` puts its own `.NGP` + `.PTR` in
  `files/`; `monster stats` extracts the base type's `.CHR` and `.AIC` files to the work folder's `monsters/<id>/files/` (`pack` takes the
  edited ones; `python tools/chr_dump.py` lists the punch table); `monster sounds` extracts the base type's bank as
  `<bank>_sNNN.wav` to the same folder, and `pack` builds the monster's `.BNK` in `files/` from the disc bank with the replaced WAVs
  (`snd989 bnk-replace`). `check` describes the bank and warns when it has fewer sounds than the base's. `check` warns about a `base` that is not accepted, an invalid id and a missing `files` folder. In the window: **New monster...** (section
  "Interface").
- **Launcher:** customs show up in "Your monster", after Random (only for the player).
- **What the port does for Goliath** (`native/runtime/wotm_custom.inc`): he is visible and controllable with AIs in the fight (the
  `SetAIMonster` turned him off), takes damage from them (`takeHit` only accepted hits from player 1), loads the boss sound bank outside
  the adventure and puts the name on the HUD. Also: X+Square does the boss spin (`StateTopSpin`, instead of the regular monsters'
  `StateCrowdControl`; the full hit counts against the nearest AI), the turret does not follow the enemy all the time (the boss's "look" turns the
  whole torso; with lock-on it aims) and the dart (impale) becomes a regular hit on whoever lacks the stuck (0x8E) or pull-out (0x90) animation (it would
  otherwise freeze; the 0x8F that `StateImpaled::transitionOK` also asks for is unused, the struggle being 0x10F/0x110, and Togera, Zorgulon and
  Cerebulon have no 0x8F). Investigations (closed): `git show 18bb1d2:docs/investigacoes/2026-10-23/goliath_topspin.md`, `goliath_lockon.md`, `goliath_impale.md`.
- **HUD portrait:** the one of `ASSBOSS1.RTX` (texId 6) is a placeholder, with Congar's face. `"portrait": "goliathprime.png"` (in the monster's
  folder or at the mod root; 128 × 64 or bigger with the same aspect ratio, e.g. 512 × 256) replaces the portrait image of the custom's costume
  (`"portraitTex"`: the texId in the `.RTX`; default 6 for `ASSBOSS1`). It also applies to the adventure's boss, which uses the same file.
  The HUD draws the portrait upside down from the image, as the disc's own are stored (Togera's `lizard0_rtx_t0003.png`): save the
  PNG flipped top to bottom, normal on the left half and hurt on the right; the disc's monsters face right.
  Log: `[mods] ASSBOSS1.RTX t6 <- Goliath Prime/goliathprime.png`. `"portrait"` covers only the custom's own costume: a skin of the
  custom takes the portrait of its own `.RTX` (a rig skin: the base type's), so it brings its own as a skin PNG
  (`lizard0_rtx_t0003.png` in the skin folder, same rules).
- Not yet: animations ASSBOSS lacks (climbing, some blocks and counterattacks; `18bb1d2:docs/investigacoes/2026-10-23/goliath_codigo.md`,
  section 3) make the game hold the previous pose; elimination and minigames were not tested with him; the adventure save does not store a
  custom (the adventure does not offer the slot).

### Cerebulon (the `mods/Cerebulon/` mod, in the repo)
The two phases of the final boss that are a `Monster`: `jelly` ("JELLY", base `jelly`, Togera's camera at 3x) and `cerebulon` ("CEREBULON",
base `final`), both with `"ai": true` and the `cerebulon` narrator. The first phase, the Mecha, is a piece of the final stage
(`FinalBoss::updateMecha`), not a `Monster`: it comes in as an assembled monster (mod `mods/Mecha/`, section "Mecha" below).
- **What the port does for them** (`wotm_custom.inc`): visible and controllable with AIs (`SetAIMonster` turned off types 0x1C0 and
  0x1E0 and counted them as AI), also visible as a CPU outside the adventure, the `Final_s.bnk` sound bank outside the final stage and the name on the
  HUD ("JELLY WINS", "CEREBULON CRUSHED").
- **How the game treats them** (from the retail, they apply to the player): Jelly has no rage (`Monster.cpp`, rage), does not grab nor get grabbed
  (`Grapple.cpp`) and Cerebulon does not pick up the special item (`PowerUps.cpp`). The hits, jump and throws come from the animations of
  `JELLY0`/`FINAL0`.
- **Jelly's shock** (`StateUltraTazer`, his special): **Triangle + Circle** (the regular monsters' special combination
  in type A), with the special available. The game's `transitionOK`
  asks for no button (in the final stage the `FinalBoss::updateJelly` script decides), and with Jelly as a player the shock would fire
  by itself; the port only lets it start with both buttons pressed on the player's controller. The shock spends the special, and a player
  Jelly picks up the special item like a regular monster (the retail refuses it to types 0x1C0/0x1E0). CPU Jelly: as in the retail.
  **Target:** `StateUltraTazer::update` aims at `game->m_monsters[0]` (in the final stage, the player), with no distance limit; with
  a player Jelly, during the shock that target is the nearest living enemy (any active monster, player or CPU). The shock only
  starts with an enemy within 1200 (Jelly is huge: in the fight the enemy stays at ~700–1100) and ends if none stays within 1500.
  Log: `[custom] Jelly: alvo do choque tipo 1 a 787`, `[custom] Jelly: choque (Triangulo+Circulo)`, or
  `[custom] Jelly: choque sem inimigo a 1200 (o mais perto a 1250)`.
- The HUD portrait is gray (no `"portrait"`).

Example: select screen 6 (1 player against CPU), CUSTOM → GOLIATH PRIME, the CPU refused in the slot,
fight in Midtown Park with camera, sound and name. Picture: `docs/screenshots/2026-10-23/2026-10-23_selecao_casa_custom_goliath.png`.

### Mecha (the `mods/Mecha/` mod, only the `monster.json` in the repo)
The robot of the final boss's first phase, assembled as a Jelly-based monster (`"base": "jelly"`, `"files": true`, `"kind": "mecha"`,
Togera's camera at 3.5x, `"ai": true`, `cerebulon` narrator). The model files are the game's and do not go into the repo: each person
generates them from their own disc with `python tools/mechamon.py mods/Mecha/monsters/mecha/files` (writes `MECHA.NGP`, `.PTR`, `.TEX`, `.RTX`;
all four are in `.gitignore`).
- **What `mechamon.py` does:** `MON/JELLY0` is the template (shadow, reticle, `COLL_GRID`), and root 0 points to the
  Mecha instance copied from `LVL/CAPITOL.NGP`. The main instance becomes type 0x1C0, the shield 0x1D8 and the beam 0x1D9. The
  laser emitter (0x27AA) becomes locator 0x80B, and claw tips become 0x802/0x803/0x808. The animations are renumbered to
  the ones `Monster` asks for (`ANIM_MAP`: walk, recoil, the special 160/161/162, death 120; the turret's turn and tilt, the boss's 11 and 12,
  become 290 and 291). The textures of `CAPITOL.TEX` go to the end of `JELLY0.TEX`. Details: `docs/investigacoes/2026-10-27/`
  (`mecha_pecas_capitol.md`, `mecha_formato_monstro.md`, `mecha_carga_custom.md`).
- **Shield** (like `FinalBoss::updateMecha`): it starts on, and with it the Mecha takes 1/4 of the damage. After each shot the shield
  drops for 240 frames (full damage, no laser) and comes back on.
- **Laser** (the only special attack; always available with the shield on): **Triangle + Circle**. It uses Jelly's `StateUltraTazer`
  as the animation and timing script (charge, 180 frames of burst, shut down). During the burst the beam is the boss's: from the mouth (0x80B)
  up to 4000 in the aim direction (`hdLosCSTest`). The monster hit takes 0.4 per frame (`takeDamage` credited to the Mecha) and is
  pushed; buildings and objects accumulate in their own `HurtHistory` and take a 5.01 hit every 5 (`dispatchHit`); sparks at the point.
  The turret turns and tilts toward the aim (blends of animations 290/291), and the beam stretches to the point.
- **Aim:** the player aims through the **turret camera**. During the laser their view sits 220 above the tip of the cannon, looking at the
  aim, with a reticle in the center (only without split screen); the beam goes to the center of the screen. Both analog sticks and the mouse turn the
  aim (sensitivity and inverted axis from the CAMERA tab), and the camera moves in and out in 12 frames. The CPU aims at the nearest enemy's chest.
- **Sounds:** the boss's (`FinalBossSound`, `Final_s.bnk`): charge, the laser loop, the impact loop on the monster hit
  (`Monster+0x1A84`, like the boss) and the shield going down and up. The loops are turned off by hand at the end of the burst.
- **Memory:** `PS2_RAM_SIZE-0x1400` to `-0x1000`, at the top of the runtime's 64 MB: 2 Mechas × 0x1C0 (6 `_animHandle`, 2 `AnimBlend`, the
  `HurtHistory` and the `FinalBossSound`) and 0x80 of scratch for the calls. Right above sit the split4 `kQuadDefs`.
- It does not jump (the owner's decision). Test without a controller: `WOTM_MATCH=mecha,1,1 WOTM_MECHATEST=1` (always fires and the aim sweeps by itself);
  `WOTM_AIVSAI=1` for AI vs AI. Log: `[custom] Mecha (dup 0): escudo, laser e torre achados`,
  `[custom] Mecha: escudo baixou (...); rajada: 180 quadros no monstro (-72.0 de vida), 0 em objetos`.

## Stage select pictures
`mods/<mod>/levelpics/<nome>.png` replaces the picture of the stages `SHELL.TEX` does not have (`training`, `lakepowell`, `tokyodeathmatch`) and
the one of the RANDOM entry (`random`; without it, a mosaic of the game's pictures). Aspect ratio 2:1, any resolution. It wins over the repo's
`levelpics/` folder (`NATIVE.md`, "Stage select").

## Under the hood (for those who touch the code)

- **Files** (`native/wotm_mods.hpp`, `native/runtime/wotm_mods.inc`): every file the game reads goes through `fileCdSearchFile`
  (stages, monsters, `SHELL`, banks, music, movies) and then through `sceCdRead` (`fileReadf/a/s`, `zipInflateAll`,
  `snd_StreamSafeCdRead`). The search hook runs the original and, if the file is in `mods/`, returns a fake LSN (a range starting
  at 0x800000, beyond any DVD) and the mod's size; the `sceCdRead` hook serves that range by reading from the PC file. The native sound
  (`readLsnRaw`) reads banks and music through the same range. The native layer that reads the disc by name also looks in `mods/` first:
  `.RTX` palettes (`Scene::loadResources`, which reads the file directly and not from RAM), movies (`wotm_fmv.hpp`), icon, pictures and monster
  of the save list (`wotm::disc::readFile`). With no file in the registry (no mod and no generated file) no hook is installed. A file that does not exist on the disc
  is served too ("new file").
- **Generated files:** the army's vehicles for every stage, `MON/ARMY.NGP/.PTR/.TEX/.RTX`, are built from the disc's `LVL/VEGAS.*` into
  `cache/army/` and enter the registry as the mod "army (gerado)" (`Registry::addGenerated`, before the scan), with or without the
  mods folder (`WOTM_MODS=0` included). A mod with its own
  `MON/ARMY.*` wins (`NATIVE.md`, "Army in versus fights").
- **Log:** `[mods] N arquivo(s) de <mods> em <pasta>` at boot and, on the first search of each file, `[mods] MON/LIZARD0.TEX <-
  teste_togera (110080 bytes; disco 110080)`.
- **Mod files bigger than the disc's:** what limits them is where each loader puts the file, not the disc size (stage and menu files
  are compressed, and the inflated size is what counts).
  - Contiguous regions: the stage `.NGP` and the menu (`SHELL/*` `.NGP`, `.RTX`, `.TEX`, `.PTR`) load one after the other from
    0xA00000 up to 0x1F10000, where live data starts (the copy of `SHELL.RTX`/`LOAD.RTX` that `BootInitUserint` inflates at 0x1B7FFF0
    is never read again); the monsters and the stage's `.RTX`/`.TEX` load above 32 MB. A bigger file only pushes the next ones up. For
    every mod file the log gives the inflated end and the room left (`checkLoad`, `wotm_mods.inc`):
    `[mods] LVL/CENTRAL.NGP (meu_mod): descompactado: 8043456 bytes em 0x0A00000-0x11ABBC0, 13.4 MB ate o teto 0x1F10000`, with
    AVISO past the ceiling.
  - Fixed-size formats: `RST/*` and the stages' `.AIC` are read into `resetcom::m_data[0xC000]`, and `PTS/*` into a 0x4000 buffer that
    goes to `PointToolKit::points[256]` (8-byte header, 64 bytes per point). A bigger mod file is handed to the game cut to that size,
    and a point count above 255 is lowered to 255 (`fixedCap`, `cdRead`). Log: `[mods] PTS/CENTRAL.PUP: AVISO: 600 pontos, o jogo tem
    lugar para 255; usando os primeiros`.
  - No limit: sound banks (native sound reads them on the PC side), music, streamed sounds and movies.
- **Mod `.PTR`** (a skin's or a custom's own model): `dbsRelocateViaPtrListFile` reads the whole file into the game's `MemoryStack`
  (a 512 KB pool) in whole sectors. The retail reserves only 0xD40 bytes before the read (its "Pointer file too big" message compares
  against 0x30D40), so the limit is what the stack has free at that moment. A mod `.PTR` that does not fit is read into the free RAM
  right after the end of the load instead (`ptrRelocate`, `wotm_mods.inc`), and the stack is left as it was. Log:
  `[mods] MON/LIZARD5.PTR (meu_mod): 614400 bytes, maior que a pilha do jogo (357 KB livres): lido em 0x23B0000, acima da carga`
  (or `cabe na pilha do jogo`).
- **PNG** (`native/wotm_texmods.hpp`, `Scene::modTexture`): the game keeps loading the disc's `.TEX`/`.RTX`; the native drawing swaps the
  image in `textureId` (objects) and in `residentTexture` (screen quads: enemy bars, font). The disc texId comes from the
  offset of the `texInfo[id]` packet inside the loaded file (`fileStatus.texAddr`/`resAddr`, name in `names`); the
  resident ones, through the DBP. A PNG bigger than the original gets mipmaps. Particles: `Scene::modParticle` in `particleTexture` and in `fxTexture`. 3D monster of the select screen and of the save list: `model::Model::decodeTex` (`wotm_model.hpp`). Not covered: composited dust and the emulated GS (for
  that one, `modkit.py pack --inject`).

## Caveats

- A mod file bigger than the disc one goes up to the ceiling of its region, and the fixed-size formats are cut (see "Under the hood";
  the log and `check` warn). With PNG none of this applies: the game's texture does not change size.
- Custom monsters use the code of one of the game's types; a new type does not exist yet (`TODO.md`, item 8).

Example (`WOTM_MATCH=3,1`, `3,1,2,5,1` and `1,3,1` with AI): Togera's skin as a 4x PNG (1024 × 1024 instead of 256)
and the 2x portrait on the player's HUD and on the enemy bar; the kit does export, editing, pack (PNG, `.VAG`, one sound of `SHELL_S.BNK`),
re-export without losing edits and check. Pictures: `docs/screenshots/2026-10-22/2026-10-22_mods_png_*`.
