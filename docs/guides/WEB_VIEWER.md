# Browser 3D model viewer

(Docs index in `CLAUDE.md`.) The viewer is a separate project: https://yanm1103.github.io/wotm-model-museum/. The feasibility
assessment it came from is in `bbed7d9:docs/passos/VIEWER_WEB.md` (read with `git show`).

## Rules
- **No game file on the server.** A static page that reads the user's `disc/` folder (or the ISO) through the File API; everything is
  processed locally. The hosting (GitHub Pages) contains nothing from the game.

## What it uses from this repo
- **Format** (`docs/FORMATS.md`, `NATIVE.md`): `.NGP` (zip `IE\3\4` in the stage, raw in the monster; image pre-linked at
  `0xA00000`; hierarchy GROUP/LOD/SWITCH/ROTATE/TRANSLATE/SCALE/CHAR_INSTANCE/DESTRUCTIBLE/CONTROL), `polyPkt` mesh (positions,
  normals, strips with ADC, per-vertex color, UV 4.12, multitexture per section, batches of 190 vertices), CT32/24/16, T8, T4,
  T8H/4HL/4HH textures with palettes from the `.RTX` (CSM1 swap), monster skinning (bone code in the low bits of xyz, matrix palette,
  3 weights) and animation curves + mixer.
- **Monsters from the disc** (pose and textures without a RAM dump): `passos/MODELS_3D.md`, `tools/pose.py`, `native/wotm_model.hpp`.
- **Scenery and objects:** `tools/ngp.py` (`ngp.decode_mesh`) and `tools/gltf.py` (binary glTF, `TOOLS.md`).
- `native/viewer.cpp` (raylib) is the local viewer with a free camera; it starts from a **RAM dump** (textures by `texInfo` and
  bone matrices computed by the game).
