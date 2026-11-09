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

**Project mods** live in `mods/<mod>/` at the repo root: Goliath Prime (`mods/Goliath Prime/`), Cerebulon (`mods/Cerebulon/`),
Godzilla (`mods/Godzilla/`), CJ (`mods/CJ/`) and the "Recompiled" logo of the intro (`mods/recompiled_logo/`). A project mod carries everything it needs
to run, including models and sounds from other games and the files the game loads built on top of the disc's (`.NGP`, `.PTR`, `.TEX`,
`.RTX`, `.BNK`, edited disc textures). The sources it was made from (models, rigs, conversion scripts, WAVs) live in
`mods/_<name>_src/` (Godzilla: `mods/_godzilla_src/`, `_godzilla1954_src/`, `_godzilla2000_src/`; CJ: `mods/_cj_src/`). Mecha is the exception:
`tools/mechamon.py` builds its files from the disc, so only its `monster.json` is in the repo.
`tools/sync_mods.ps1` mirrors each folder of `mods/` to `<runtime>\discroot\mods\` (`run_live.ps1` calls it on every boot and
`setup_runtime.ps1` on install; `WOTM_RECOMP_DIR` or `.wotm_recomp_dir` give the runtime). Only the folders that exist in the repo are
mirrored; the ones that exist only in `discroot\mods\` (yours, `_work`) stay as they are. Without a `mods` folder (with exactly that name: on
Windows the path `mods` falls on the disc's `MOD\`, the IRX one) next to the ELF, the game and the launcher read the repo's `mods/`
(`WOTM_REPO`, which `run_live.bat` and the launcher pass). Folders that start with `_` (sources, `_work`) are not mirrored and the
game does not read them.

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
| `.BNK` | `<bank>_sNNN.wav` (one per sound with a sample; a sound made of layers, several TONEs each with its own delay, pitch and sample, comes out as the mono mix the game plays) | the bank with all edited sounds replaced (a sample another sound also uses: the edited one gets its own copy and the other keeps the original; a layered sound becomes a single layer with the WAV, full volume envelope, no delay and no LFO; it loops only if the original played until the game stopped it, not when a KEY_OFF in its sequence ended the loop). A `<bank>_sNNN_aK.wav` made by hand replaces only the K-th distinct sample of that sound (in TONE order, from 0) and keeps its layers: every TONE that played it plays the WAV with its own delay, pitch and volume, looping if that sample looped; a `_sNNN.wav` of the same sound wins over it |
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
  A `GENAMB_S.BNK` in the folder is the custom's copy of the shared bank (`BANK_AMBIENT`, the one every monster plays generic sounds
  from): while this monster's sounds play it takes the game's place, so a sound replaced in it changes for this monster only. Sound 90
  is the dash loop (Ultra-V, Robo-47 and Zorgulon). The kit builds it from `genamb_s_sNNN.wav` files in the custom's `files/` work
  folder, keeping the shared bank's numbering. Log: `[mods] monstro cj: ..., banco comum GENAMB_S.BNK` and `[snd] cj: banco comum
  proprio GENAMB_S.BNK no handle 0x10003`.
  Ultra-V's sounds, by number (the ones a custom on it usually replaces): 1 a loop while in the air (jump and butt slam), 4 punch whoosh (with 13), 11
  footstep (two samples, left and right foot, `_a0` and `_a1`), 14 landing, 15 body impact (hitting a wall or a monster while dashing).
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
  `getInstance` in place of the game's id, and swaps the copied missile into `Weapons::m_epNodes[13]` while `Weapon::InitWeapon`
  builds one of that monster's missiles. The rig import also fills the locked reticle: the reticle (attachment id type + 5) is a
  `SWITCH` on `m_reticleState`, and its child 2 (locked) is a `CHAR_INSTANCE` spun by animation slot 191 (the game fills it with the
  instance's animation 0), which the special speeds up when locked. Robo-47's holds a red sprite there and Ultra-V's nothing, so
  the tool puts a copy of Robo-47's sprite under the custom's (its texture gets a new id in the custom's `.TEX`, logged as
  `textures {18: N}`; a `<prefix>0_tNNNN.png` replaces it). The special adds three animations on top of the pose (`StateRobotSpecial::init` blends): 166 and 167 (aim,
  scrubbed by a percentage, 50% = straight ahead) and 161 (the shot), so the rig writes them as layers: rotations only, as the
  difference from the action's first frame (`ngpgraft.LAYERS`). 160 (windup) ends in the aim pose, which the game holds until the shot;
  162 is the winddown. The missile leaves from locator `extra1` (0x816). Its sounds are in Robo-47's bank: a `ROBOT_S.BNK` in the
  folder is used for them (in Robo-47's bank slot while this monster's sounds play), otherwise they only sound with a Robo-47 in the
  fight. The kit builds it from `robot_s_sNNN.wav` files in the custom's work folder (`files/`), like the monster's own bank. The
  special plays sound 2 (the head opening, when the special starts), 29 (the aim: beeps that speed up, sample 0, then a held tone,
  sample 1, which loops; `robot_s_s029_a0.wav` and `_a1.wav` keep that sequence), 6 (launch), 7 (missile flight, a loop) and
  5 (explosion). Logs: `[mods] monstro CJ/monsters/cj: especial do robo-47 com as pecas dos arquivos proprios`, `[custom] SpecFxAnim copiada
  0x127f (do 0x11ff) registrada para o dup 0`, `[custom] cj: projetil 13 do
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

## Custom stages (a new entry on the stage select, built on a game stage)
`mods/<mod>/stages/<pasta>/stage.json` adds a stage to the stage select, before RANDOM. It is a game stage (the base) under a new name,
with its own level patch:
```json
{ "id": "mycity", "name": "MY CITY", "base": "CENTRAL", "narrator": "mycity", "random": true }
```
- **`id`:** 1 to 8 letters and digits, starting with a letter. It names the stage's files and its patch. It cannot repeat a game stage
  (`CENTRAL`, `DYLAN`...), a game file (`SHELL`, `ARMY`...), a monster file (`LIZARD1`, `ROBOT7`...) or the id of a custom monster.
- **`name`:** what the stage select shows, up to 17 ASCII characters (default: the id in capitals).
- **`base`:** `CENTRAL`, `VEGAS`, `CANYON2`, `METRO`, `UFO`, `CAPITOL`, `CANYON1`, `MINISF` or `TOKYODM` (or the index, `GLOSSARY.md`). The
  other stages run scripts in every mode, and `TRAINING` belongs to training mode.
- **`narrator`:** `narrator/<name>.mp3`, played when the stage is confirmed, looked up like the skins' (without it, silence).
- **`random`:** `false` keeps the stage out of RANDOM.
- **Patch:** `mods/<mod>/levels/<ID>.json` (`levels/MYCITY.json`), in the format of "Level patches" below, with the base's handles. A
  `levels/CENTRAL.json` does not apply to a CENTRAL-based custom stage, and the other way round.
- **What comes from the base:** the geometry, `.TEX`/`.RTX`, the stage code (water, scripted events, sounds, music, load card, pause
  menu); the lock does not come along: a custom stage is always unlocked, even on a base the save has not opened (UFO, Capitol, Mini Baytown). The files are always the disc's: a mod that replaces
  `LVL/CENTRAL.NGP` does not change a CENTRAL-based custom stage.
- **What the folder can bring:** points (`.SPT`, `.PUP`, `.FOP`, `.PED`, `.AIP`, any name; they enter as `PTS/<ID>.*`), the `.MRS` (as
  `RST/<ID>.MRS`) and PNG textures (`t0004.png` or `central_t0004.png`; the texIds are the base's). A PNG of the base from any mod
  (`CENTRAL_t0004.png`) also applies, unless the folder brings its own. `.NGP`, `.PTR`, `.TEX` and `.RTX` are ignored, with a line in the
  log: the `.PTR` relocates the `.NGP` and the `.TEX` the texIds it uses, so the geometry is a single group, outside custom stages.
- **Picture:** `levelpics/<id>.png` (see the next section); without it, the base's picture. The stage select marks the entry with a
  blue "CUSTOM" label.
- **Slots:** up to 6 custom stages, in the stage index slots 18 to 23 (dev team test stages with no files), in mod/id order. A
  METRO-based stage without its own `.AIP` leaves the list when the screen opens with CPUs, as METRO does.
- **Without the menu:** `WOTM_MATCH=3,1,@mycity` (`NATIVE.md`, "Runtime: direct match, AI vs AI and batch").
- The log lists each stage and each refusal (`[mods] fase mycity ("MY CITY", meu_mod) = vaga 18, base CENTRAL (1); ...`) and the
  confirmation (`[fases] custom mycity ...: vaga 18 -> base CENTRAL (1)`). The registry is read at boot: a new stage enters after
  restarting the game.

## Stage select pictures
`mods/<mod>/levelpics/<nome>.png` replaces the picture of the stages `SHELL.TEX` does not have (`training`, `lakepowell`, `tokyodeathmatch`),
the one of the RANDOM entry (`random`; without it, a mosaic of the game's pictures) and that of a custom stage (`<id>`). Aspect ratio 2:1, any
resolution. It wins over the repo's `levelpics/` folder (`NATIVE.md`, "Stage select").

## Level patches (move, turn, delete and copy stage objects)
`mods/<mod>/levels/<FASE>.json` (the stage's file name: `CENTRAL`, `VEGAS`, `TOKYO`...) changes the stage when it loads. Each entry names
an object by its handle, the `TRANSLATE`/`ROTATE` that `python tools/lvl.py objects <FASE>` prints in the `handle` column, and repeats its
position (`from`, the `pos` column), so a patch made for another `.NGP` does nothing:
```json
{
  "objects": [
    {"handle": "0xB18310", "from": [697.7, 972.2, 0.5], "pos": [1098.2, 664.1, 0.5],
     "copies": [{"pos": [845.9, 1003.2, 0.5], "heading": 90.0}]},
    {"handle": "0xB31DC0", "from": [663.0, 742.6, 0.5], "heading": 45.0},
    {"handle": "0xB8A410", "from": [1044.6, 448.2, -0.8], "delete": true}
  ]
}
```
- `pos`: the new world position of the handle (not of the building's center: the handle is often off to a side; `lvl.py patch`
  prints each collision box, which shows where the building ends up). `heading`: the world heading in degrees (the `heading` column;
  turning is about the handle). `delete`: the object leaves the stage (drawing, collision, registration). `copies`: new copies, each with
  `pos` and an optional `heading`; each copy has its own copy of every part that changes when the object breaks (destructibles,
  switches, smoke emitters) and shares the meshes with the original, so it breaks and smokes on its own and gets its own collision.
- `names` (`{"<handle>": "Hotel"}`, at the top beside `objects`) and `name` in a copy: labels for the level editor; the game and
  `lvl.py` ignore them. A patch with names only still carries `"objects": []`.
- Works on destructibles (buildings), pickups (cars, trees, rooftop objects) and props. Copies: buildings (with the destructibles
  inside them), props and pickups (a pickup copy has its own handle, id `GROUP` and `SWITCH`, so it breaks and is picked up on its
  own). A copy has its own animated parts too: a building that collapses with an animation when it breaks (CENTRAL, ISLAND, METRO,
  TOKYO) collapses on its own, and a destructible inside an animated part (a crane, a turning sign) is copied with it. Only a
  destructible inside a skeleton is refused (`lvl.py patch` and the log say which part; no stage has one). A domino of the original
  (breaking it knocks a neighbour down) stays in the copy only when the neighbour is in the copy too; one aimed outside it is dropped,
  so breaking the copy never breaks the original's neighbours. The copy also leaves out the actions that work on a set part of the
  stage rather than on the object: an animation of an animated part outside the copy, a script, and a game event (in CENTRAL,
  breaking some buildings cuts traffic links; in other stages it moves pickups), so those happen only when the original breaks. An
  animated part the game itself drives by its id (a level script, a boss) only moves in the original. Deleting an object cuts the
  dominoes of its neighbours aimed at it.
- Copies cost room: each one takes arena bytes (8 MB for the whole patch) and slots in the game's table of breakable objects (1024).
  An entry whose copies do not fit is skipped before anything is written. `lvl.py patch` and the log print the table's use:
  `interactives: 348 of 1024 before the patch, 355 after (...)`.
- Objects that sit on a building (water tanks and vents on the roof, items on its sidewalk base) are pickups of their own and do not
  follow it: move or delete them in the same patch. `lvl.py objects` marks them with `on <handle>`.
- Traffic and pedestrians follow their own paths (`PTS/<FASE>.FOP` and `.PED`, `FORMATS.md`, "Path nets") and never look at the
  collision, so the patch cuts those paths around each collision box in its new place (moved, turned and copied objects). A vehicle
  link whose lane (`width · 10` to each side of the line) crosses a box is removed both ways, and vehicles turn back at the node before
  it. Pedestrians are cut only by walls (boxes rising 40 units above the sidewalk; curbs and low bases are walked over) and stop 15
  units from them: each segment keeps its free parts of 30 units or more, on both sides of the building. Flying paths (helicopter,
  saucer) stay. A deleted building leaves an empty lot with no path through it.
- `peds` (at the top beside `objects`): the stage's pedestrians.
  ```json
  "peds": {
    "segments": [{"a": [-300, 0, 0], "b": [300, 0, 0], "width": 5}],
    "groups": [{"handle": "0xA00640", "from": [487.0, -50.0, 0.0], "pos": [480, 100, 0], "count": 30},
               {"handle": "0xA00670", "from": [-29.6, -350.0, 0.0], "delete": true},
               {"pos": [470, -200, 0], "count": 40}]
  }
  ```
  `segments` replaces the stage's pedestrian net (`.PED`) with these sidewalks, at most 128 (`a` and `b` are the ends, `width` 1..255,
  the lane is `width · 10` to each side). A group with `handle` and `from` (`python tools/lvl.py peds <FASE>`) is one of the disc's:
  `pos` moves it, `count` (1..255) changes how many walk, `delete` takes it out. A group with no `handle` is new. `"off": true` takes out
  every group of the disc. A stage holds at most 20 groups; one with no pedestrian net is refused. Each group walks the segment of the
  node nearest to it, so moving a group can move it to another sidewalk: `lvl.py patch` prints the segment each group walks
  (`walks 2-3 (725 long)`) and the segments nobody walks; the game logs the same report (`[fase]   pedestres: grupo ... anda em
  2-3`; also without `peds` with `WOTM_LEVELPATCH=2`).
- `grid` (at the top beside `objects`): a bigger collision grid, `{"min": [x, y], "max": [x, y]}`. The disc's grid is a fixed rectangle
  of 200-unit cells (`python tools/lvl.py collision <FASE>` prints it; 7 x 7 to 102 x 40 cells): the game collides, shoots and sees only
  inside it, and a box outside is refused. The new rectangle must hold the disc's one (it can only grow) and have at most 65536 cells
  (cols = floor(width / 200) + 1). The patch builds the grid in the arena (8 bytes a cell: the cell and its list of moving objects,
  `csListArr`, which the game indexes by cell, so the disc's one is too short; 8 MB for the whole patch), lists every path
  again in the cells its box covers and points the image's root to it; it runs before the entries, so props and copies see it. A bad
  rectangle is skipped with a line (`[fase]   grid pulada: ...`) and the stage keeps the disc's grid. Log: `grid: 36 x 36 celulas (eram 18 x 16)...`.
- `walls` (at the top beside `objects`): `"walls": "off"` turns the stage's invisible boundary walls off, so a monster can leave the
  playable area. The walls are the grid's `POLY_COLL` paths flat in x or y (under 10 units thick), at least 600 tall and 1500 long: CENTRAL
  has four (x −1430 and 1355, y −1349 and 1250, 1400 tall, just past the outer buildings), VEGAS four and CAPITOL two (x −1199 and 1200); the other stages
  have none (what stops a monster there is the end of the floor and the buildings). Each wall's leaf is left with no objects, so the
  game's collision query finds nothing; the paths stay where they are. It does not move the floor: the disc's floor ends at the old
  rectangle, where a monster falls and the game puts it back in the stage. To walk further, add a floor as a `coll_*` prop (a flat
  quad of two triangles over the new `grid`) and give the patch the bigger `grid`; props placed out there collide, break and take
  attacks as inside. The scenery the stage draws beyond its walls (VEGAS has some) has no collision: a monster walks through it. Cars and pedestrians keep to their own paths whatever is outside. The editor sets it in the Stage size dialog (a checkbox, only for a stage that has walls). Log: `walls: 4
  paredes de borda desligadas (x=-1430, ...)`; Check counts it and refuses a stage with none.
- `clear` (at the top beside `objects`): `"clear": "scenery"` takes the stage's bare scenery out, the base of a stage built from
  scratch. Bare is what has no handle: the drawing and collision of the plain `GROUP` chains with no `TRANSLATE`, `ROTATE`, `SCALE`,
  `SWITCH`, `DESTRUCTIBLE`, `INTERACTIVE`, `CHAR_INSTANCE`, `ANIM_XFORM`, `SKEL_BONE` or `ANIM_SCALE` above (the ground, the streets, the
  invisible walls and the ceiling, scenery blocks, smoke emitters). Each `OBJECT` and `PART_EMITTER` of it leaves its parent (the slot
  points to one `NULL` node; the node is not written, so a copy of another object that shares it keeps it) and each of its `POLY_COLL`
  and `COLL_VOL` paths leaves every cell of the grid. Left alone: what has a handle (delete those with `objects`; `python tools/lvl.py
  objects <FASE>` lists them), the animated and scripted parts (cranes, a boss's set), the sky, the lights, the camera triggers and a
  node with an id. It runs first, so `objects`, `props` and `copies` still add what they bring. After it the stage has no floor: give
  it one as a `coll_*` prop over a bigger `grid` ("Stage size", `Apply and add a floor over it`). A stage with nothing bare skips it
  with a line. Log: `clear: 336 OBJECTs e emissores do cenario nu fora, 7 caminhos de colisao fora`.
- `props` (at the top beside `objects`): new static objects made in Blender, placed in the stage.
  ```json
  "props": [{"id": "tower1", "model": "TOWER", "pos": [845.9, 1003.2, 0.5], "heading": 90, "name": "Water tower"},
            {"id": "ramp1", "model": "RAMP", "pos": [600, 400, 0], "heading": 30, "pitch": 20, "roll": -10}]
  ```
  `model` is a folder `mods/<mod>/props/<MODEL>/` of the same mod as the JSON (any case), written by `python tools/lvlprop.py build
  <in.glb> mods/<mod>/props/<MODEL> --stage <FASE>` (`TOOLS.md`): `<MODEL>.NGP`, `<MODEL>.PTR`, `<MODEL>.preview.glb` (what the game
  draws, for the editor) and `prop.json` (`stage`, `textures` as `{"slot", "texId"}` or, for an image of its own, `{"slot", "png",
  "packet", "game"}`, `bounds`, `light`, counts). A prop belongs to one stage. A material `tex<texId>` in Blender draws with that
  texture of the stage; a material with an image (PNG, or JPEG with Pillow, which the editor's exe carries) draws with that image: the PC draws the PNG
  (`<MODEL>_T<slot>.png` in the folder) at its own size, and the game keeps a copy of at most 128 x 128 (CT32) in the arena, with a
  texId past the loaded textures and the smallest texture bin of the stage it fits in (log `[fase]   props: KRAB KRAB_T0.png:
  textura propria 128x128, tex335, caixa de 65536 bytes` and `[mods] prop tex335 <- <mod>/props/KRAB/KRAB_T0.png`). The UVs are
  clamped to the image (no tiling). A mesh with no vertex colors takes the stage's sun
  (its three parallel lights, as the cars do); a mesh with vertex colors keeps them as its light (bake the light in Blender), and
  `--light vertex|dir` forces one way for the whole prop. `pos` is where the model's origin goes; `heading` (degrees,
  default 0) turns it about Z. `pitch` and `roll` (degrees, default 0) tip it first, about its own origin: pitch about its X axis (its
  +Y end goes up for a positive value), roll about its Y axis (its top, +Z, leans toward +X for a positive value); the heading then
  turns the tipped model about the vertical (the entry's `ROTATE` matrix is Rx(pitch) · Ry(roll) · Rz(heading), `lvl.prop_matrix`).
  The box the game files the entry under is the box of the turned model's corners, so a tilted entry covers more cells. Each entry collides as the box of the model's bounds, or as the shapes the .glb names (a mesh `hull_*` is a convex hull, up to 64
  vertices and 40 faces; a mesh `coll_*` is collision by triangles, up to 16384 of them (the arena decides what fits; blocks of 512 triangles and 256 vertices each get a box tree, as the stages' collision does); one leaf per mesh, not drawn; `hull_m20c_wall` and
  `coll_m10a_floor` set the surface word (hex): kind in the high byte, 2 a wall the monsters climb, 1 a floor; the default is
  the stages' building face, 0x10A) (monsters stand on it
  and bump into it; vehicles and pedestrians are cut around it like around a moved building) and is drawn with no view culling.
  **Destructible props:** meshes named `state1_*` (up to `state3_*`; no prefix is state 0, the intact one; the states must be 0, 1, 2...
  with none missing; an empty node named `state1_none` is a state that draws nothing) make a prop that breaks. Each entry gets its own
  `INTERACTIVE_STATE` and `DESTRUCTIBLE` (one Interactives slot each, of the stage's 1024) over the shared state groups, so breaking one
  does not touch the others. `hull_*`/`coll_*` meshes belong to their state (`state1_coll_stump`: what is left collides; a state with
  none does not collide). Health is the hit point class (`--strength 1..5`: 10, 50 (default), 100, 150 or 250 hit points; class 1 takes
  any damage, the others only over 5), and each hit that takes the health under (N - 1 - i) / N of the full moves one state on, at
  most one per hit. **Debris:** meshes named `state1_debris_<name>` (up to 16 per state) are not part of any state's drawing: each is a
  chunk thrown when the prop enters that state (`ActDebris`, the stages' own action). A chunk flies up and outward from where it sits
  in the prop, spins, bounces and lands on its foot at the prop's base, and is gone after 10 seconds; its origin is the middle of its
  footprint at its lowest point. **Smoke:** an empty named `state1_smoke_<name>` (up to 8 per state) puts a particle emitter (the
  stages' own `PART_EMITTER`) at its position in that state: it starts when the prop enters the state. The default is the stages'
  demolition dust (particle 1, size 30, emitting for 45 fields, a cloud that lives 5 s); the empty's custom properties (Blender:
  Object properties, Custom Properties) override `preset` (0 to 150, the game's particle table), `size`, `velocity`, `spread`,
  `heading`, `pitch`, `rate`, `duration`, `onset`, `stretch`, `lifetime`, `gravity`, `airResistance`, `airResistanceTransition`,
  `initialExpansionRate`, `expansionRate`, `expansionRateTransition` and `alphaFade`. An emitter fires once and remembers it in its
  own node, so each copy of the prop gets emitters and a group for the state of its own (the meshes stay shared; about 0xB0 bytes
  per emitter). Log: `props: DEST is destructible: 2 states, hit point class 2 (50 hit points)`, `props: DEST has smoke: 2 emitters in state 1`. Each model takes its file size
  of the arena once, and each entry 100 to 200 bytes more. Skipped with a line: a model missing or made for another stage, a texture
  the stage's `.TEX` does not have, an image larger than every texture bin of the stage, an entry whose box leaves the collision grid. The props are read from the folder at every load,
  so a prop rebuilt during a fight shows after a reload (F10). Log: `[fase]   props: TOWER x3 (1216 bytes); 1660 bytes da arena`,
  the same numbers as `lvl.py patch`.
  **Sky:** an entry `{"model": "SKYDOME", "sky": true}` (no `pos`) replaces the stage's sky dome with the model instead of placing it.
  The dome is every OBJECT under the sky node (id 2) outside the clouds (the `CHAR_INSTANCE` branch, which stays); the model's objects wear
  the first dome's first word, flags and fields at +0x20..+0x4F (fog burn-through 3500, so the fog never reaches it; the tint) and keep
  their own vertices and textures; the first dome's slot points at the model and the other domes' (TOKYO has two) at an empty `GROUP`.
  The sky is drawn around the camera with the dome's blend state, so build the model around its origin with its faces turned
  inward and a texture (a material `tex<texId>` of the stage's sky, or an image; an untextured sky is not drawn); the stock domes
  have bounding spheres of radius 9000 to 12000, and `--scale`/`--height` size the model. Its `hull_*`, `coll_*` and automatic box are never used. A model with `state<N>_*` meshes is refused; so
  is a stage with no dome (CANYON1, MINISF). Log: `[fase]   sky: modelo SKYDOME (1 objetos) troca 1 domo (estilo do OBJECT 0fc8120)`.
  **Clouds:** `{"model": "SKYCLOUDS", "sky": "clouds"}` replaces the cloud layer the same way: the OBJECTs under the first `CHAR_INSTANCE`
  of a sky node, through its `ANIM_XFORM`, so the model gets whatever the stage's animation does to the layer (build it around its
  origin; the stock layer is a bowl 22000 across and 3500 high, its z from 0 up), and the game turns it: the model drifts around the
  vertical axis as the stock layer does (the editor's view draws it still). Only the first `CHAR_INSTANCE` is the layer; the others a sky
  node may hold (CAPITOL has six more, 40 vertices each, under a `SWITCH`) are separate sky objects and stay. The model wears the stock
  layer's style (its own blending) and the dome stays. A sky model belongs to one stage like any prop (its textures are the stage's):
  for another stage build the same `.glb` again with `--stage <FASE>` into a folder of its own (`CLOUDS_CAPITOL`). 14 stages have a layer (every one but BIGSHOT, CANYON2, TOKYODM, TRAINING and UFO,
  which are refused with a line). One model cannot be both the dome and the clouds (the second entry is skipped). Log: `sky: modelo
  SKYCLOUDS (1 objetos) troca 1 camada de nuvens (estilo do OBJECT 0fc7400)`; `lvl.py` and Check count it as a cloud layer replaced.
- Check before playing: `python tools/lvl.py patch CENTRAL mods/<mod>/levels/CENTRAL.json` applies the patch to the disc's image, prints
  what changed, each redone collision box, and checks the collision of the result (box rule and cell rule, `FORMATS.md`, "Stage
  collision"). An entry that does not match is skipped with a line saying why.
- Log in the game: `[fase] CENTRAL <- <mod> (LEVELS/CENTRAL.JSON): ...` with the same counts as `lvl.py patch`, a line per skipped entry,
  and the collision check before and after; `[fase] CENTRAL: sem patch` when no mod has the stage's file. `WOTM_LEVELPATCH=2` also
  prints each redone box; `=0` turns patches off.
- **The patch is read at every load of the stage**, not only at boot: the mods folder is searched again (a mod or a file created or
  deleted after boot counts; with the file in two mods, the last mod in alphabetical order wins, `[fase] LEVELS/CENTRAL.JSON: B vence
  A`). A file that does not parse is read once more 200 ms later (the editor may be writing it). So editing during a fight and
  reloading the stage applies the new patch, without restarting the game. Three ways to reload it: the pause's **RESTART** (YES) or, in
  training, **RESET POSITIONS**; **F10** in the fight (no pause menu); and the file named by `WOTM_LEVELRELOAD=<arquivo>`, where
  writing `<STAGE> <counter>` (new content each time; `<STAGE> <counter> x y z heading` also puts player 1 at that point, as
  `WOTM_SPAWN`) asks for the reload if that stage is loaded. The reload is the retail RESTART: the
  whole stage loads again (the `LOAD` screen), the fight starts over and the continues are restored. A request waits for a frame where
  the game could pause (in the pause, until the fight resumes; during a movie or a dialog, until it ends; time in the pause does not
  count) and is dropped after 10 s of running game without one, or if a stage load came after it, since that load already read the
  file. Log: `[fase] recarga pedida (F10)` or `(arquivo)`, then the load's `[fase]` lines.

### Level editor (`tools/level_editor/editor.py`)
`python tools/level_editor/editor.py` opens the stage in 3D and writes the patch above. It is a local page (three.js, in
`tools/level_editor/web/`) served by the script at `http://127.0.0.1:47197/` (`--port N` changes it; with the port taken by an
editor already running, that one's page opens again; by something else, a free port), in the default browser. `--window` opens
it in its own window instead (`pywebview` when installed, otherwise Edge/Chrome app mode); `--serve` only serves; `?stage=METRO` in the address opens that stage first. Except with `--serve`, the program quits 10 s
after its page closes (a reload of the page keeps it). The release zip carries it as `WotM_LevelEditor.exe`, next to `wotm.exe`
(`tools/build_level_editor.ps1`), whose mods folder is the one the game reads. The page is in English, with a dark, a light and a
System theme (the one the OS uses), in the settings (the cog at the top right).
- **Top bar:** the stage (switching with unsaved changes asks: Cancel, Discard, or Save and switch), the mod (a list of the mods
  folder's mods and **New mod…**, which asks for a name; its `levels/<STAGE>.json`
  loads with the stage, and the editor remembers it; switching with unsaved changes offers **Discard and load X** or **Move my
  changes to X**, and says when X already has a file for this stage; Save with no mod chosen asks **Save into which mod?**),
  **Undo** (Ctrl+Z) and **Redo** (Ctrl+Y or Ctrl+Shift+Z; their tooltips name the change), **Check** (`lvl.py patch` on the disc's
  stage: a one-line summary, "Check passed: …" or how many problems, and a list of the problems, each naming its object, where a
  click selects it: an entry skipped, a pedestrian group with no sidewalk, a collision box or cell that does not match; the cut
  vehicle and pedestrian paths and the full output go with it), **Save** (Ctrl+S, also from a field; an empty patch removes the
  file; a failing check still saves and an amber note says so; keys, entries and names the editor does not edit, such as entries
  for handles the stage does not have, stay in the file as they are, and a note over the view says so when the file loads) and
  **Test in game** (opens the game in the stage in training mode with Togera: `WOTM_MATCH=3,<stage>`, `WOTM_TRAINING=1`,
  `WOTM_MODS=<mods folder>`, windowed; with unsaved changes it asks **Save before testing?**, since the game loads the file, and
  offers to test without them; with the game already open it asks to reload the stage in it or to restart it). The **Test
  options** button (sliders) next to it chooses who plays: your monster and costume (the ten the game plays, and the custom
  monsters and skins of every mod), 1 to 4 players (players 2 to 4 with their own monster and costume; each needs a controller, and
  with none they stand still), the mode with 2 players or more (free for all, or elimination with 0 to 9 continues; the minigames
  play in their own arenas, so they are not offered), 0 to 7 CPUs with theirs (only the custom monsters and skins marked
  `"ai": true`; players and CPUs together are up to 8), training mode and, in it, what the CPUs do (stand still, the game AI, always
  block, block after the first hit, jump); the choices stay in the browser, and the server turns them into `WOTM_MATCH`
  (`<monster>[:<costume or skin>],<player 2..4>...,<CPU>...,<stage>`), `WOTM_PLAYERS`, `WOTM_MODE` (`6:<continues>`),
  `WOTM_TRAINING` and `WOTM_TRAINING_DUMMY`. **Test from here** (the pin, or in that dialog) starts you where the middle of the view
  meets the stage, facing where the view looks (`WOTM_SPAWN`; a respawn goes there too); "put the others around me" (on by
  default, when there are other players or CPUs) starts them on a ring around you (radius 300 plus 40 per other monster), the
  first one in front of you, all facing you, each dropped to the ground under its spot. With the game already open, it asks to
  reload or restart, and a reload carries the start point (and the ring) in the reload file, while a plain **Reload in game** goes
  back to the stage's own points. Only a restart applies a changed monster, player or CPU. Next to these buttons, the game's state:
  starting, then "Game running" and its stage. **Reload in game** (on while a game opened by Test runs; "Save & reload" with
  unsaved changes) saves and has that game load the stage again, as **F10** does there; with the game on another stage it offers a
  restart on this one. **Stop** (the square, while the game runs) closes the game the editor opened. In a window narrower than
  1440 px, Check, Save, Test and Reload show only their icon. The **?** button (or **?**, or **F1**) lists every shortcut.
- **Custom stages:** the stage list also shows the valid custom stages of every mod (`stages/<folder>/stage.json`, "Custom stages")
  as "MY CITY (custom, on CENTRAL)". One opens with its base's objects, scene and handles and saves its patch as
  `levels/<ID>.json`; Test in game opens it with `WOTM_MATCH=3,@<id>`. The **+** button next to the stage list (**New stage…**)
  asks for an id, a name, a base among the nine the game allows, the narrator and whether RANDOM can pick it, and writes
  `stages/<id>/stage.json`; "Start with the changes I have now" also writes them as the new stage's patch when the open stage has
  that base. The **pencil** button (**Edit stage…**, enabled on a custom stage) rewrites the name, base, narrator and RANDOM of
  its `stage.json` (the id stays; other keys are kept). Changing the base keeps the patch and the points made for the old one, so
  the editor reloads the stage on the new base and asks for a Check. A stage with points of its own
  (`.SPT`, `.PED`...) in its folder shows those in the pedestrian panel and the check (the other kinds from the base). The Fight HUD
  panel of a custom stage saves its pictures in the stage's folder (`<BASE>_tNNNN.png`), so they apply to that stage only. `python
  tools/level_editor/test_custom_stages.py` is the integration test (needs the disc set in the editor).
- **Palette (Ctrl+P):** cards, each with a picture of the model, as many to a row as the window holds (at least 170 px each: eight in
  a 1600 px window); it opens from anywhere, even in a text field, and the arrows move over the cards. Two tabs (Tab switches):
  **Add to the stage** has the mod's props for this stage (a group a click on its title folds, remembered) and every original
  prop and breakable building of the game's stages (one card per model, a page holds what fits the window: the arrows in the group's header, or
  PageUp and PageDown; a menu narrows it to one stage; the search takes name, stage and "breakable"); Enter or a click adds one where
  the view looks. **Find in this stage** has the stage's objects (name or handle; one card per model with how many there are; Enter
  selects and frames the first of them). Each picture is rendered by the
  editor once and kept as a PNG in its cache folder (`origpic192`), so later searches show it at once; the palette's footer counts the
  pictures not rendered yet and **Render all** makes them in one go (a few minutes; the editor's address with `?bake=1` does it on load,
  in a browser window of its own). A card of the open stage's own models adds a copy of that object, as Copy does. Picking one of another
  stage builds it as a prop of the mod for the open stage (`tools/lvlorig.py` takes the object out of
  the stage's scene, `lvlprop.py` builds it with units `game`), named from the stage and the model's signature (`VEG_B533`), with
  `origin` in its `prop.json`, and adds it like any prop; picking the same model again reuses the prop. The prop keeps its triangles,
  vertex colors, textures (each as a CT32 copy of at most 128 a side; the PC draws the full PNG) and, for a building, its broken state
  (hit point class 2; four `state<N>_smoke_*` emitters near the footprint's corners, low and sized to the building, throw the dust of
  each break) and its own collision, state by state, with the original surface words (the climbable walls, the walkable rubble; an
  object with no collision of its own collides as the box of its bounds). A part the stage drew without blending gets the GS's opaque vertex alpha,
  since the prop draws with the template's blending, and a texture whose alpha passes 0x80 keeps the raw alpha in its PNG (the stage's
  scale; the vertex alpha around 0x42 counts on it). The source stage's other blending,
  pickups, vehicles and animated parts do not come along.
- **Fight HUD and sky** (the monitor button): the pictures of the stage's fight HUD and of its sky (the OBJECTs under the nodes with id 2),
  each replaceable by a PNG in the mod (`<stage>_tNNNN.png`, "Textures as PNG"). A sky picture is the stage's own (never "apply to all").
- **Light and fog** (the sun button on the rail): a panel docked at the right (not modal, so the view stays in sight) for the stage's
  `RST/<STAGE>.MRS` (`FORMATS.md`, "`.MRS`"): the fog's color (the background takes it), where it starts and ends, how thick it gets
  (0 to 255) and the draw distance, and the three parallel lights, each a color, a strength (100 % is 255 in the strongest channel; it can pass it)
  and a heading and pitch (where the light comes from: heading 0 is +Y and turns toward +X, pitch 90 is straight overhead). The view follows
  each field as it is typed: the fog and the background show with Game look on (the panel offers to turn it on), the lights on the objects
  the game lights as it draws them (cars, towers, statues; the editor does not draw the monsters, which the lights also light in the game).
  The rest of the stage has its light in its vertex colors, which the lights do not change. **Save to mod** writes `mods/<mod>/RST/<STAGE>.MRS` (a custom stage's folder keeps `<ID>.MRS`): a copy of the file the stage uses
  now with those numbers changed, the same size and every other byte as it was (the time of day, the ambient, the dust, trail and debris
  colors). **Back to the disc's** deletes it. Closing the panel with numbers not saved puts the view back. The game reads the file when the
  stage loads (Reload in game). The fog ending before it starts is flagged: the game then draws no fog. `python tools/level_editor/test_mrs.py`
  is its server-side test.
- **Textures** (the picture button on the rail): every picture of the stage's `.TEX` and `.RTX` as a card with its texId, size and format
  (the buildings', ground's and props' pictures, the particles, the HUD and the sky), each replaceable by a PNG in the mod (`<stage>_tNNNN.png`
  or `<stage>_rtx_tNNNN.png`, "Textures as PNG"; the game draws it on every object that uses the old one, at any resolution). Tabs:
  **In the scene** (what the stage's objects in the view draw with), **HUD and sky**, **Other** (particles and effects), **Replaced** and
  **All**, each with its count; the search takes a number (`12`) or a size (`64x64`). A card has **Replace** (or a PNG dropped on it; another
  proportion than the game's asks first), **Save original as PNG** (to edit it) and, when the mod has one, **Remove**. A picture the editor
  finds no palette for shows its original in grey (a "No palette" tag): the game still draws the replacement in its own colors. The dialog
  writes into the mod at once, outside Undo, in the mod's `LVL/` (a custom stage's own folder for its pictures, as the Fight HUD dialog does);
  with the game open, **Reload in game** sits at the bottom. `python tools/level_editor/test_textures.py` is its server-side test.
- **Stage size** (the expand button): the rectangle of the collision grid (`grid` in the patch), four numbers and "Grow every side by 1000"; the view draws the disc's rectangle in grey and the patch's in blue;
  it can only grow. Check reports a rectangle the game would skip. For a stage with boundary walls (CENTRAL, VEGAS, CAPITOL) the dialog also has
  a checkbox that sets `"walls": "off"`. "Apply and add a floor over it" also builds a prop `FL_<stage>` in the mod (a visible plate 0.6 under
  the stage's floor and a `coll_` slab of two triangles at z 0, both 50 units inside the rectangle) and places it at the rectangle's center as
  the entry `floor`; doing it again after a size change builds it again. The slab is at z 0, where the floor's collision is in CENTRAL, VEGAS and CAPITOL (one flat path, its top at z 0); for the other stages check the height of their floor first.
  The dialog also has "Take out the stage's bare scenery" (`"clear": "scenery"`; the view hides the meshes the scene file tags `extras.bare`) and
  "Delete every object" (one Undo step: every original building, pickup and prop gets `delete`): with the size and the floor, the steps of an empty stage.
- **Start points** (the people button): where the monsters appear, from the stage's `.SPT` (`FORMATS.md`, "Stage points"): four lists (0 story, first
  life of the player; 1 first life of the players, room for 2; 2 first life of a CPU, 20; 3 respawn, 20), each point with x, y, z, a heading and
  "Ground" (the point drops to the floor or roof under it when a monster appears). The dialog lists the points, adds one at the view's aim,
  moves one to the aim ("Aim") and draws a cone for each point in the view (color by list) with a line for the way it faces. Save writes the
  file into the mod (`mods/<mod>/PTS/<STAGE>.SPT` for a game stage; for a custom stage the `.SPT` in its folder, `<ID>.SPT` when it has none):
  a game that loads the mod reads it in place of the disc's file; "Back to the disc's" deletes it. Points past a list's room overwrite the
  list's last one (the dialog warns). The heading is kept to 1.41 degrees and is drawn as 0 = facing +Y, growing clockwise seen from above (90 faces +X, as in the game).
  The box button next to it opens the same dialog for the stage's **power-ups** (`.PUP`): the 20 types (health 25/50/100, stamina 25/50/100,
  health 10, stamina 10, special weapon, cloak, story items 10 to 19, which only appear in story mode), a position (z is the height the
  power-up rests at; the rotation is random in the game) and a Shadow box. The game keeps 100 at a time (the dialog warns past that).
  Same file rules: `mods/<mod>/PTS/<STAGE>.PUP`, or `<ID>.PUP` in a custom stage's folder.
  The polyline button opens the **AI net** (`.AIP`): the paths the computer monsters walk. Each node has a position and a type (0 for nearly
  all; the AI only follows paths of the type it stands on, so a type splits the net into sub-nets), each link a width (the lane is
  width x 10 to each side; the disc's are 12 to 24). The dialog lists the nodes with their links (a number alone is a two-way link, an arrow
  a one-way link), adds a node at the view's aim linked both ways to the selected one (so a path is a few clicks), links and unlinks two
  nodes by number, moves a node to the aim and clears the net. The view draws the links (amber around the selected node). Limits shown in
  the dialog: 255 nodes, 6 links per node and 450 paths (a two-way link is one path; the game ignores the rest). A stage without its own
  net gives the CPUs nowhere to go: CAPITOL and TRAINING have 2 nodes, METRO none (a METRO-based custom stage leaves the CPU lists unless
  it brings its own).
  The road button next to it opens the same dialog for the **vehicle net** (`.FOP`): the roads and flight paths of the cars, helicopters,
  flying saucers and tanks (`FORMATS.md`, "Path nets"). The node type is a list (0 car street, 1 helicopter, 2 flying saucer, 3 tank and
  truck, which only VEGAS has) and each link has a width and a drop (how far below the path a helicopter may fly when it chases a target,
  times 10; the disc's car streets all have 0, its helicopters' links 0 to 26 and its saucers' up to 101; the widths run 5 to 12 for cars,
  7 to 29 for helicopters and 8 to 111 for saucers). A road is two-way when both ends link to each other, and Link
  rewrites the width and the drop on both ends. Limits shown: 255 nodes and 6 links per node. The view draws the net only while the dialog is
  open, or all the time with **Layers > Vehicle net** (yellow cars, green helicopters, pink saucers, orange tanks). File rules as for the
  AI net (`PTS/<STAGE>.FOP`); a net of one node gives the vehicles nowhere to drive, and none at all brings back the game's 2 default nodes.
- **Stage picture:** the picture button (lower right of the view) saves the middle 2:1 band of the view, 512x256, as
  `mods/<mod>/levelpics/<name>.png` for a custom stage (`<id>`) and for Training, Lake Powell and Tokyo DM; the other game stages have
  their picture in `SHELL.TEX` (make a custom stage on one). The stage select picks it up the next time the game starts.
- **Winning mod:** the game loads, for each stage, the `levels/<STAGE>.json` of the last mod in name order that has one. When
  another mod than the chosen one wins for this stage, a note under the top bar names it when the stage loads (and after a save),
  since the game does not use the chosen mod's file; its X hides it until the next stage or mod.
- **Mode rail** (the narrow column on the left): **Objects** (the cube, or **1**) and **Pedestrians** (the walking figure, or
  **2**) switch what the editor works on: the left list, the right panel and what a click in the view picks; the editor remembers
  the mode. **Light and fog** (the sun icon), **Textures** (the picture icon) and **Fight HUD** (the screen icon) open their panels (below).
- **Status bar and Output:** the bar at the bottom shows the last message, whether the stage has unsaved changes (or when it was
  saved), the last check (passed, how many problems, or not checked since the last change), the game (closed, starting, running,
  patch applied or ignored), the **room meter** and how many objects the mod changes. The room meter reads "348/1024 slots · arena 3 KB":
  the breakable slots the stage uses (the game's table holds 1024; a copy of a building, a pickup or a breakable prop takes some, and a
  delete does not give them back, as `lvl.py patch` counts them) and the bytes of the 8 MB arena (copies, props, a bigger grid). It updates
  half a second after each edit (`POST /api/budget`: the patch applied without the collision check, about 0.4 s), turns amber from 80 % of
  either and red at the limit or when a step ran out of arena; its tooltip has the exact numbers. A click on a field opens the **Output** drawer on its tab:
  **Messages** (everything the editor says), **Check** (the last check's summary and problems, where a click selects the object,
  and the full `lvl.py patch` output under Details) and **Game** (the game's `[fase]` lines in words: patch applied, file
  rejected, another mod's file used, reload skipped or dropped; the load's details as the game writes them). The drawer opens by
  itself on an error, and its top edge resizes it. Short notes (saved, deleted with Undo, copied, dropped, the game's answer) show
  for a few seconds over the view's lower right corner, and above an open dialog.
- **View:** two cameras, **V** (or **Fly** / **Orbit** on the view's top right bar) switches; the last one is remembered. Fly (the
  default): **W/S** move where the camera looks, **A/D** sideways, **E** up, **Q** down, **Shift** faster, right drag looks around,
  the wheel moves forward. Orbit: left drag orbits, right drag pans, the wheel zooms. In both, a click selects the object under it
  (buildings, pickups, props; the pointer turns into a hand and a white outline shows what a click picks), **F** (or the crosshair
  button) frames the selection, **Home** (or the frame button) frames the whole stage, **Ctrl+D** copies it, **Del** deletes it,
  **Esc** deselects. W/A/S/D/Q/E go by their place on the keyboard (the same keys on AZERTY); the other shortcuts by their letter.
  The shortcuts work after a click on a checkbox or a button; a text field, the stage picker and an open dialog keep the keys. A
  chip at the view's lower left says what the mouse and the keys do at that moment (it fades after a few seconds and comes back
  when that changes).
- **View bars:** on the top left, the tool: **Move** (**G**) and **Turn** (**R**, about the vertical only) for the gizmo, and
  **Snap** with its steps (10 units and 15 degrees by default; they also set the arrow keys' step). On the top right: **Game
  look** (**L**), **Grid** (a line every 100 units, or every snap step when it is smaller, and a stronger one every 1000, at the
  base of the selection), **Layers** (Pedestrians, Collision and Vehicle net), the cameras, Focus, Frame all and the shortcuts. The gizmo's arrows
  have the colors of the X, Y and Z fields; the selected object's amber outline shows through what stands in front of it, the
  others of a selection (orange) only where they are in view. A deleted object shows as a red dashed box where it was while it is
  selected; with **Changed** on, every deleted one does (a click on the box selects it), and a green box marks each changed object.
- **Right panel:** with nothing selected, what a click does, the stage, how many objects the mod changes and the file **Save**
  writes (`mods/<mod>/levels/<STAGE>.json`). With an object: its kind, name and state (Moved, Turned, Copy), its handle (a click
  copies it) and these sections. Every number field changes when dragged sideways (one step per 3 pixels; Shift: ten; within the
  field's limits); a click without dragging types in it. The object, the pedestrian marker or the People and Width of a group
  follow the drag, which is one undo step; the new prop's Height builds once, on release. **Transform**: **X**, **Y** and **Z**
  (the height) in the gizmo's colors, also dragged by their letter (a snap step, or 1 unit, per step), and the gizmo's blue arrow
  points to -Y.
  **Heading**, with buttons that turn 90° either way. A new prop also has **Tilt**: **P**itch (-180 to 180) and **R**oll (-89 to 89),
  dragged like the other numbers, and a button that stands it up straight again; Turn and the heading field turn it about the vertical
  whatever its tilt, and Duplicate and Arrange keep the tilt. **Actions**: **Drop** (**End**: down or up onto the first surface
  under the object's middle, another object's roof included), **Duplicate**, **Restore original** (back to the disc's place,
  copies removed; off when there is nothing to restore) and **Delete**, which says how many objects go. **Name** (closed at first)
  holds the object's name and the model's; **Details** (closed at first) the size, the model id, the damage states, the collision
  boxes, the place on the disc and what it rests on (a click selects it). A deleted object stays in the list, struck through:
  picking it shows **Restore**, which puts it back (with Move objects on top, the objects that were on it too).
- **Arrange** (in Actions' panel, closed at first; works on the whole selection, and with Move objects on top on what rests on it;
  one undo step each; the button says how many copies it makes, at most 500 at a time): **Row of copies** (the count includes the
  original, the distance between neighbours in X, Y and Z, and a turn each copy adds to the one before), **Circle of copies** (count,
  arc, the point to go around, filled with the selection's middle, or where the view looks for a single object, with buttons for
  both; the copies turn around the circle unless that is off), **Mirror** (across X = or Y = a value, with the originals kept as
  copies or moved; the heading is mirrored but the models are not flipped), **Align** (the boxes' lowest, middle or highest side
  along X, Y or Z; bottoms, middles or tops for Z) and **Spread evenly** (the two outermost stay, the others go between them with
  equal gaps or equal distances between their middles). Align needs two objects and Spread three. The copies are the new
  selection.
- **Several objects:** Shift+click (or Ctrl+click, also in the list) adds or takes out one, Shift+left drag selects the ones whose
  middle falls in the box (only the kinds on in the list), **Ctrl+A** selects everything the list shows. The gizmo and the fields
  act on the last one picked (amber outline and row); the others (orange) follow it by the same move or turn, and Drop, Duplicate,
  Delete and Restore original act on all of them. The panel says how many of each kind are selected. **Arrows** move the selection
  along the world axis nearest to where the camera looks, **PgUp/PgDn** up and down (Shift: ten steps).
- **Move objects on top** (a switch in Actions, on by default, with the count beside it): moving, turning, copying, deleting or
  restoring a building does the same to the pickups that rest on it (`lvl.py objects`, `on <handle>`); Delete counts them
  ("Delete 5 (+3 on top)").
- **Pedestrians mode:** the stage's pedestrian groups as violet cones, a dashed line from each to the sidewalk it walks, and the
  sidewalks (`PTS/<STAGE>.PED`) as cyan lanes as wide as the walking lane, with a white ball at each end; the selection is amber.
  The left panel has two meters (groups out of 20, sidewalks out of 127, amber past 90 % and red at the limit), the people of all
  groups added up, the **Pedestrians in this stage** switch (off takes every group out, the patch's `"off"`; on brings the disc's
  back) and a list of **Groups** (people per group; a red mark when it walks nothing) and **Sidewalks** (length), numbered by their
  place in the list. A click, in the view or the list, picks a cone, a lane or an end (the stage's objects are scenery here); the
  gizmo, the arrows, **End** and the position fields only move them, Turn is off. The right panel says what a group walks
  ("Walks on sidewalk 2 (492 units long)", with **Show** to select that sidewalk) or a sidewalk's length and lane, and has
  **People** (1 to 255) or **Width** (units to each side, steps of 10) as a slider and a number (one drag is one Undo step),
  **Position**, **Drop**, **Duplicate** (groups) and **Delete**. With nothing selected it explains the colors. **Add group** and
  **Add sidewalk**, on the tool bar, put a new one where the view looks (off at the limit, or with no sidewalk to walk, with the
  reason in the tooltip); a new sidewalk is a straight piece to drag by its ends. A red cone has nothing to walk (its nearest node
  has no link, or the stage has no sidewalks: add one). Each group walks the sidewalk with the nearest end, so moving a group can
  move it to another sidewalk. The changes go in the patch's `peds` ("Level patches"); Check lists where each group walks, and a
  group the game would skip is a problem that selects it. In the Objects mode the **Pedestrians** layer (the view's **Layers**
  menu, or **P**) shows the markers faint, not clickable.
- **Collision layer:** the **Collision** switch of the view's **Layers** menu (or **C**) draws what the game collides with, over
  the stage and through the walls, with the edits applied (the server applies the patch to a fresh copy of the stage, as Check
  does, `tools/level_editor/collview.py`, `POST /api/collision`; the layer reads it again a moment after each edit). Triangle
  collision (the buildings' and a prop's `coll_` meshes) is green, convex hulls (a prop's `hull_` meshes) are cyan, and trigger
  volumes (the camera's) and parts that animate are orange boxes, drawn as the world box the grid lists them with. A dimmer shade is
  a broken state of a building (it collides only after the building breaks). What touches the selection keeps its color and the
  rest fades. The menu says how many paths and triangles are drawn. `python tools/level_editor/test_collision.py` is the
  server-side test.
- **Fight HUD dialog:** the fight HUD pictures this stage carries (`tools/lvlhud.py`: bar frame and bars, special icon, BOSS
  label, player numbers, font, pickup ring, hint box, cinema bars, buttons), each row with the disc's picture (**Original**), the
  mod's (**In your mod**, or "Not replaced"), the size, format and file. **Replace…** (or a picture dropped on the row) takes an
  image (a PNG as it is; another format the browser reads becomes a PNG; a shape other than the game's asks first) and writes it
  into the mod as `LVL/<stage>_tNNNN.png` or `LVL/<stage>_rtx_tNNNN.png` ("Textures as PNG"); **Apply to all N stages with this
  picture** writes it for every stage that has the same one (the bar atlas is shared by 17). **Save original as PNG** downloads
  the disc's picture, **Remove** takes the mod's out. The dialog writes into the mod at once, outside Undo, and says so; with no
  mod chosen, Replace is off and a mod picker sits at the top. Canyon 1 and Tokyo DM carry the other HUD layout (B), drawn
  differently: the dialog says so on those stages. The game reads the pictures when it loads the stage; with the game open,
  **Reload in game** sits at the bottom of the dialog.
- **New props** (the patch's `props`, above): **Add prop** on the tool bar lists the props of the chosen mod made for this stage
  (`mods/<mod>/props/*/prop.json`, with size and triangles; one whose files do not read is greyed, with the reason) and puts the
  picked one where the view looks, drawn from its `<MODEL>.preview.glb` with the stage's textures. **Import .glb…** (in that menu;
  a file can also be dropped on the dialog) takes a Blender export (glTF Binary) and runs `lvlprop.py build` in the mod:
  **Name** is the folder (1 to 8 letters, digits or `_`; the file's name by default), **Units** Blender meters or game units,
  **Height** scales the model to that many game units (empty keeps the file's size), **Light** as `--light` (`TOOLS.md`). The
  `.glb` is kept as `props/<NAME>/source.glb`; a name the mod already has asks before replacing it, and the new model then shows
  wherever it is placed. A new prop moves, turns, copies (a copy is one more entry with the same model), drops and deletes like
  any object; it has no disc place, so Restore is off and its badge says **New**. In its panel, **Model name** is the model's
  name in the list (`prop.json` `name`; empty goes back to the folder's name) and **Height** (Details) builds the model again
  from its `.glb` (`source.glb`, or the `source` of `prop.json`) at that many game units, scaled about its origin with the same
  units and light; both change the model, so every entry of it takes them, and neither is undoable. A destructible model also has **Strength** (its hit point class; only `prop.json` changes, so it is not a rebuild). It sits in the list's **New props** section,
  and Check names the one the game would skip. Switching to a mod without its model keeps the entry in the file (the editor says
  how many), where the game skips it. **Terrain from a heightmap…** (the same menu) turns a PNG of heights into a prop with
  hills and ramps (`tools/lvlterrain.py`, which makes the `.glb`; the build is `lvlprop.py`'s): **Name**, **Width** and **Depth**
  (game units; they start as the stage's rectangle when the patch has a `grid`), **Height** (what white is; black is the stage's
  floor level, the picture's top is +Y; a gray PNG is exact, any color counts as its brightness, 8 or 16 bits), **Detail** (cells
  along the longer side, at most 90: 16200 triangles, under the collision's 16384) and **Brightness** (percent, default 80; the
  colors are a ramp from grass through earth to rock by height, shaded by the slope against a fixed sun, baked in the vertex colors
  because the stages' own light is). An optional colors PNG laid over the same rectangle replaces the ramp. The mesh comes twice,
  as the drawn model (0.6 units under the floor line so flat places do not fight with the stage's floor) and as a `coll_terrain`
  mesh with the same triangles, which the monsters walk on. The PNGs and the numbers stay in the prop's folder (`heightmap.png`,
  `colors.png`, `terrain.json`), and a name the mod already has asks before replacing. The new prop is placed at the middle of the
  grid's rectangle, else where the view looks. `python tools/lvlterrain.py <heights.png> <out.glb> --size X Y --height H [--cells N]
  [--colors colors.png] [--brightness P]` writes the `.glb` alone. The same menu ends with **Sky dome** and **Cloud layer**: the stage's own, or one of the mod's
  models for this stage (the `sky` entries of `props`, above; undoable, counted by Check as a replaced dome or cloud layer, and
  refused there for a stage with no such part); with Game look (**L**) the view draws the model in the place of the stage's own
  part (the scene tells them apart: the meshes under a `CHAR_INSTANCE` are the layer), both faces of it. The view draws the layer
  still, with no animation.
- **List:** every object by name, its handle beside it, in sections (**Buildings**, **Pickups**, **Props**, **Terrain** and **New props**, the
  props over 500 units; a click on a header closes or opens it, Terrain starts closed, and the editor remembers them). A copy
  sits under its original, indented. Small marks after the name: a green dot (moved or turned), the copy icon (it has copies),
  the bin (deleted: the row is struck through) and the link (it rests on another object; the tooltip names it). Above: the
  search (name, rule name, handle, kind, model id) and chips with counts that show or hide a kind (they also limit Shift+drag
  and Ctrl+A) and **Changed** (only the edited ones). A search that finds nothing offers **Clear filters**.
- **Names** (`tools/lvlnames.py`): the disc has no object names. An object shows, in order, its own **Name** (right panel; kept in
  the patch's `names`), its **Model** name (every object with the same mesh and textures, in every stage; kept in
  `~/.wotm_level_editor_names.json`, over the repo's `tools/level_editor/names.json`), or a rule's name in grey: a stage's
  special objects (reactor stacks, quake buildings, pillars), the pickup type (with "traffic" or "parked" for the cars that can
  drive), the destructible's hit point class (`Building`, `Small object`, `Tower`), `Prop` or `Terrain piece` (over 500 units).
  The Name section says how many objects of the stage share the model.
- **Settings:** the theme (Dark, Light, System; it previews as it is picked), the game disc (the `.iso` itself, or an extracted
  folder with `LVL/` and `PTS/`), the mods folder and the game. Each path shows **Found** or what is wrong with it (not found, not
  a disc, an image the editor cannot read) as it is typed (`POST /api/paths`); **Browse** next to it opens the system's picker in
  the editor's own window (`--window` with `pywebview`). The **Advanced** section (the ELF, the game's working folder and the
  image the dev runtime boots) shows only in a clone of the repo or when an ELF is set. Closing the dialog reloads the stage only
  when the disc or the mods folder changed, asking first with unsaved changes. With a `.iso`, a stage's files (`LVL/<STAGE>.NGP/.PTR/.TEX/.RTX`, its `PTS/` files and `RST/<STAGE>.MRS`) are copied
  once into the mod kit's cache of that image (`%LOCALAPPDATA%\WotM\modkit_disc\<key>`) and read from there; a `.bin`/`.cue`
  image is not read. In a clone they default to `disc/` (else the first `ISO/*.iso`), `mods/` and the runtime next to the repo
  (`wotm-recomp-win/build_dev/ps2xRuntime/wotm.exe` with the ELF in `discroot` and the first `ISO/*.iso` as `PS2X_CD_IMAGE`, the
  way `run_live` starts it; with the disc set to a `.iso`, that one); otherwise `wotm.exe` next to the editor and the `iso=` of
  its `wotm.ini`. They are kept in `~/.wotm_level_editor.json`.
- **Drawing:** the colors are the GS's: the vertex color and alpha times the texture's (`C = Ct·Cv >> 7`, `As = At·Av >> 7`, so a
  vertex above 0x80 brightens the texture), with no color management, and each mesh takes the blend mode, alpha test and Z write of
  the GS state its nodes set (ALPHA_1 0x44 is the normal blend, 0x48 the additive glows). Blended meshes with `alphaSort` are drawn
  last, far to near; the other blended ones after the opaque ones, in the stage's order. The objects with `dirLight` (cars, towers,
  statues) take the three parallel lights of `RST/<STAGE>.MRS` on their vertex color, so turning one changes its light as in the
  game; the rest of the stage has its light painted in the vertex colors.
- **Game look** (top right of the view, or **L**; remembered, off by default) adds what `RST/<STAGE>.MRS` sets: the fog (the GS's,
  per vertex and hyperbolic in the depth, so it is half there not far past its start distance: 2259 units in CENTRAL, 1393 in
  METRO), the background in the fog's color, and the sky, drawn first around the camera and never picked by a click or Drop. The
  objects the game never fogs (the sky, some glows) stay clear, and so do the selection outlines, the gizmo and the grid. Off, the
  whole stage shows with no fog over the theme's background, which is the way to edit from afar. With a mod that has its own
  `RST/<STAGE>.MRS`, that file gives the fog, background and lights.
- The 3D scene is `python tools/lvlglb.py <STAGE> <out.glb>` (`TOOLS.md`), cached per user (`%LOCALAPPDATA%\WotM\level_editor`); the
  first open of a stage takes a few seconds. What it does not show like the game: particles and sprites, animated parts (rest pose),
  the specular highlight of the lit objects, the far clip, and alpha above 1 in the normal blend (the view keeps the brighter
  source but drops the GS's darkening of what is behind, so it comes out brighter there). Icons: Material Design Icons (`web/vendor/mdi/`, Pictogrammers Free License).

## Under the hood (for those who touch the code)

- **Files** (`native/wotm_mods.hpp`, `native/runtime/wotm_mods.inc`): every file the game reads goes through `fileCdSearchFile`
  (stages, monsters, `SHELL`, banks, music, movies) and then through `sceCdRead` (`fileReadf/a/s`, `zipInflateAll`,
  `snd_StreamSafeCdRead`). The search hook runs the original and, if the file is in `mods/`, returns a fake LSN (a range starting
  at 0x800000, beyond any DVD) and the mod's size; the `sceCdRead` hook serves that range by reading from the PC file. The native sound
  (`readLsnRaw`) reads banks and music through the same range. The native layer that reads the disc by name also looks in `mods/` first:
  `.RTX` palettes (`Scene::loadResources`, which reads the file directly and not from RAM), movies (`wotm_fmv.hpp`), icon, pictures and monster
  of the save list (`wotm::disc::readFile`). With no file in the registry (no mod and no generated file) no hook is installed. A file that does not exist on the disc
  is served too ("new file").
- **Custom stages** (`scanStages` in `native/wotm_mods.hpp`, `native/runtime/wotm_stages.h`): the registry gives each stage a slot 18..23
  and aliases `LVL/<ID>.*`, `PTS/<ID>.*` and `RST/<ID>.MRS` to the base's files, marked as disc only. The search hook swaps the name in
  the game's own text during the call (up to 8 letters: every stage path buffer fits `TRAINING.*`) and restores it, including what a longer
  name covered. At the stage select confirmation (`changeScreen(8, 99)`) and in `WOTM_MATCH`, `activate` sets `Shell::m_levelNum` to the
  base and writes the id into `GameLevelNames[base]`: the game builds the file names from the name table and copies `m_levelNum` into
  `m_levelId`, so the files are the custom stage's and the code is the base's. `restore` puts the table back at the next confirmation
  and at the entry of `userintMain` (every return to the menus); RESTART, F10 and "play again" do not pass there and stay on the stage.
- **Generated files:** the army's vehicles for every stage, `MON/ARMY.NGP/.PTR/.TEX/.RTX`, are built from the disc's `LVL/VEGAS.*` into
  `cache/army/` and enter the registry as the mod "army (gerado)" (`Registry::addGenerated`, before the scan), with or without the
  mods folder (`WOTM_MODS=0` included). A mod with its own
  `MON/ARMY.*` wins (`NATIVE.md`, "Army in versus fights").
- **Level patches** (`native/runtime/wotm_levelpatch.inc`): hook at the entry of `dbInitDb` when `Shell::LoadLevelFiles` calls it (the
  stage image is inflated at 0xA00000 with TEX0 relocated, and nothing has been registered yet); installed whenever `WOTM_LEVELPATCH`
  is not 0, and the JSON is looked up in the mods folder at that moment, not in the boot registry. A handle that turns becomes a new
  `ROTATE` in its parent and in its collision paths; a deleted handle becomes `NULL` (op 9) and its paths leave the grid cells; a copy is
  held with the original by a new `GROUP` (noFOV) in the parent's slot, plus copies of the original's paths. A copy clones the handle's
  spine: every node that changes at run time (`DESTRUCTIBLE`, `INTERACTIVE_STATE`, `SWITCH`, `PART_EMITTER`, and nodes with an id that
  `dbProcInteractive` registers) and every node between them and the handle, once each (the image is a DAG); the rest (meshes,
  collision, GS contexts, animated parts with nothing of that below them) stays shared, and nothing is cloned below a `SKEL_BONE`. A
  `CHAR_INSTANCE` is cloned when an `ACT_ANIM` of a destructible of the copy animates it (`animationGetHandle`'s rule: the first
  instance of the stage's `ANIM_MGR` with that id1/id2 and an animation with that id) or something that changes hangs below it; its
  `ANIM_XFORM`s and `ANIM_SCALE`s go with it (they keep the instance's visible flag, angles and dirty bits; the matrices are in the
  instance's `animOutput.atMat[matrixIdx]`). The clone gets its own animation state, copied from the disc: the output block
  (`val`, `dirty`, `atMat`), the control nodes and each one's key cache (`prevKey`), `animPktPtr` pointed at its own `val` (it is
  `val` on every stage), `activeTree` 0 and an id2 no instance with the same id1 uses, from 0x7FF down (monsters look theirs up with
  a small `dupId`). Each copy entry writes a new `ANIM_MGR` in the arena with the old list plus its clones and puts it in the
  image's root list, so `animationSetGlobal`, which runs after the hook, makes each clone's `AnimPlayer` (0x18 bytes of
  `MemoryStack::global`) and starts its control nodes (`anim instances cloned` in the log). On the clones: the
  `INTERACTIVE_STATE` id1 is cleared, the `DESTRUCTIBLE` id1 too (except 0x1A90..0x1B57, the "never loses health" range that
  `Destructible::takeHit` reads), the emitter's `isActivated` (bit 4 of +0x5C) is cleared, single-piece ids go to 0 and pickup ids
  stay. "Single-piece" is any id outside 0x7D0..0x897 on an op `dbsTraverse` hands to `dbProcInteractive`, which also clears a few
  that `dbProcInteractive` does not act on (the `GROUP` id 0xC of the 3-MILE stacks); `hdTraversePath` gives a hit the last nonzero
  id1 of its path, so the copy of such a piece is hit with another id. The state actions stay shared, except the `ActGroup`/
  `ActTransform` blocks above an action the copy changes, which are cloned (a `TRANSFORM` left with nothing gets an empty `GROUP`:
  `doTransform` does not test for null): an `ActDomino` aimed at a node of the copy is pointed at its clone, one aimed outside is
  dropped; an `ACT_ANIM` whose instance the copy cloned gets the clone's id2 in `ids.anim` (`anim actions redirected`), any other
  one is dropped (it plays an animation outside the copy, or a monster's), and so are `ACT_RUNSCRIPT` (a script node with its own
  place) and
  `ACT_GENERICEVENT` (`TheGame`'s handler: `PathNode::removePath` on fixed `PathNet::fodder` links, pickup heights). An emitter with
  `isAutoStart` (bit 3 of +0x5C) takes a lasting `fx[100]` effect and an `autoEmitters[128]` slot from the load on; the game checks
  both, so many copies of such a building leave later explosions without an effect (`auto-start emitters cloned` in the log). After
  the deletes, an `ActDomino` anywhere in the image whose target `dbsTraverse` no longer reaches gets type 6, which
  `DelayAction::doIt` skips (the target is never registered, its id2 stays 0, and `doDomino` would call `ChangeState` on
  `Interactives` slot 0, NULL). Before writing, the entry's worst case is
  checked: arena bytes, and the `Interactives::s_dbInteractive[1024]` slots (unchecked in the retail), counted as the registrations
  `dbsTraverse` makes at load, plus the debris pieces that carry a pickup, plus 32 spare. `WOTM_LEVELPATCH_SHALLOW=1` (`lvl.py patch
  --shallow`) clones only the handle, its `INTERACTIVE_STATE` and its `DESTRUCTIBLE`, and `WOTM_LEVELPATCH_BREAK` breaks the copies or the
  originals at a set time (`NATIVE.md`, variables). Every path through a changed handle gets its box redone (by the disc's rule, or the old box
  carried by the handle's change for animated chains and camera triggers) and its cell lists redone, and the `GROUP`s above the handle
  get their spheres grown to cover the object (the drawing culls a `GROUP` by its sphere). New nodes go to an 8 MB arena below the hooks'
  last MB (`wotm_mods_hooks::kLevelArena`; loads above 32 MB stop at `kHighCeiling`). Then `peds` (`wotm_levelpeds.inc`): `segments`
  rewrites `PathNet::ped` in RAM, a deleted group loses its id (a plain `TRANSLATE`), a moved or recounted one is rewritten in
  place, and new groups (`TRANSLATE` 0xBB8 with a `NULL`) hang from a new `GROUP` in the world `GROUP`'s first slot, which holds
  the old child too. Then `PathNet::fodder` and `PathNet::ped`, already
  built by `InitBeforeDbLoad` and not yet held by any vehicle or `PedGroup`, are cut in RAM against the XY convex hull of each new
  solid box (`cutNets`): vehicle links leave both nodes, as in `PathNode::removePath`, and pedestrian nodes left with no link leave the
  list with the links renumbered (`PedGroup` reads `paths[0]` of the nearest node without looking at the count). `tools/lvl.py`
  (`LevelPatch`, `cut_nets`) does the same in the same order, on the disc's `PTS` files. A reload starts from a clean image: the
  stage `.NGP` is inflated again, the arena is rebuilt and `InitBeforeDbLoad` reads the `PTS` again before each load.
- **Stage reload** (`native/runtime/wotm_levelreload.inc`): F10 (read by the window, `wotm_live.inc`) or the `WOTM_LEVELRELOAD` file
  sets a request; the hook at the entry of `rtTimeToReturnToShell` (end of each `rtMain` frame) serves it under the conditions with
  which `rtPauseRT` accepts START (`m_inSession` 1, `gRtReturn` free, not paused, no movie, `game+0x120454`/`+0x120458` clear), and
  with `StreamingSoundManager+0x109C` at 0 (at 1 the retail `displayDialog` restores the master volume on its way out, which the hook
  skips), the way START does: `shell+0x2BA4 = 1`, `rtReturnToShell(2, 0)`. The `displayDialog(0)` that `EvaluateGameStatus(2)` calls next does not
  open the pause: the hook writes what the pause's RESTART item and the YES of `areYouSure` write (continues restored, `m_restart` 1,
  `m_inMenus` 1, `m_inSession` 0, pause flag cleared, `snd_StopAllSounds` when `StreamingSoundManager+0x109C` is 0) and returns. `main` then runs
  the session again from `LoadLevelFiles`.
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
