<img width="2113" height="744" alt="War of The Monsters recompiled LOGO" src="https://github.com/user-attachments/assets/6160bebd-48f0-47b5-bd28-f74d23c662fd" />

# War of the Monsters Recompiled!

A native Windows port of **War of the Monsters** (PS2, 2003), built from the statically recompiled game code and easy to mod.

[⬇️ Grab latest version here](https://github.com/yanm1103/War_of_the_Monsters_recomp/releases/latest)!

[🧌 Also check out the online model museum](https://yanm1103.github.io/wotm-model-museum/)

[💬 Join the Discord](https://discord.gg/sBw3DjVvPN)

## Play
1. unzip
2. open `wotm.exe` 
3. select ISO file

More details in the README file ([tools/release/README.txt](tools/release/README.txt)).

Found a bug? Open an [issue](https://github.com/yanm1103/War_of_the_Monsters_recomp/issues) and attach `wotm.log` (along your save files).

## Features
**Gameplay**
- Up to 4 players, story mode included, with every ending movie shown at the end
- Up to 8 simultaneous monsters
- Playable bosses: Cerebulon and Goliath Prime
- Custom monsters: Godzilla (90s, 1954 and 2000 skins), CJ and Mecha
- Army (toggleable): military units spawn and try to take you down
- Brand new training mode
- Debug menu (F1) and cheats

**Interface**
- New monster select menu with 3D preview
- Damage indicators
- New HUD for enemies
- "Recompiled options" panel: remappable keyboard, mouse and gamepad controls, plus display settings
- Photo mode (F9 or L3+R3): pictures are saved to `screenshots/`

**Graphics and camera**
- Modern game camera (mouse look)
- Dynamic resolution support
- Higher display rates
- Native GPU rendering instead of emulating the PS2's GS

**Modding**
- Mod kit (`WotM_ModKit.exe`): extracts textures as PNG and sounds as WAV, builds skins and custom monsters
- Level editor (`WotM_LevelEditor.exe`): edit stages in 3D and test them in game
- Online [model museum](https://yanm1103.github.io/wotm-model-museum/) for monsters, which you can export

## Mods
PNG Textures, skins and custom monsters go in the `mods\` folder next to `wotm.exe`. The release ships with Goliath Prime, Cerebulon, Mecha, CJ and Godzilla as playable custom monsters (these are just for fun, don't open issues if they can't jump or can't do any other specific actions!).

To make your own mods, check the reference: [docs/guides/MODS.md](docs/guides/MODS.md).

## How it works
1. **Static recompilation.** [PS2Recomp](https://github.com/ran-j/PS2Recomp) translates the whole game executable (VU0 included) into C++ that runs natively. That C++ is game code, so it **never goes into this repository**: everyone generates it locally from their own disc.
2. **Native layer** ([native/](native/)). Instead of emulating the VU1, GS and IOP, the port cuts at the engine boundary and draws with the PC's GPU; sound, FMVs and input are native too. It is not an emulator.

Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The executable: [docs/ANALYSIS.md](docs/ANALYSIS.md). File formats: [docs/FORMATS.md](docs/FORMATS.md).

## Building the port (Windows)
1. Clone with submodules (`git clone --recurse-submodules`).
2. Put your ISO in `ISO/` and run `powershell -ExecutionPolicy Bypass -File tools\setup_runtime.ps1`. It extracts the disc, sets up PS2Recomp in `runtime/`, generates the recompiled code and runs the first build. Needs git, Python 3, CMake 3.24+ and MSVC 14.40+ (VS 2022 17.10+ or Build Tools).
3. `build_runtime.bat` rebuilds after changes; `run_live.bat -Auto 0` runs the game; `level_editor.bat` opens the level editor in its own window (`docs/guides/MODS.md`, "Level editor").

Runtime details, environment variables and the release package: [docs/guides/RUNTIME.md](docs/guides/RUNTIME.md) and [docs/guides/NATIVE.md](docs/guides/NATIVE.md).

## Building the native Linux port

The initial target is Linux x86-64 with AVX2, BMI2, POPCNT, and LZCNT CPU support. Install a C++ compiler, CMake, Ninja, Git, Python 3, 7-Zip, pkg-config, FFmpeg development libraries, and the raylib/GLFW Linux build dependencies. On Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build git python3 p7zip-full pkg-config \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libasound2-dev
```

Put your own NTSC-U ISO in `ISO/` or `Game/` (or pass its path), then run:

```sh
tools/setup_runtime.sh [path/to/your.iso]
tools/build_runtime.sh
tools/run_live.sh
```

Generated game code and build outputs stay local in ignored directories. The launcher stores saves and settings under `$XDG_DATA_HOME/WotM` (default `~/.local/share/WotM`). On X11, borderless mode requests fullscreen on the selected monitor through EWMH without changing the active display mode. The Linux port targets X11. How the Linux path is kept working and packaged: [Linux port maintenance](docs/LINUX_MAINTENANCE.md).

## Building the decompilation (WSL/Ubuntu)
```sh
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install binutils-mips-linux-gnu ninja-build python3-venv wine wine32:i386
python3 -m venv ~/.venvs/wotm && ~/.venvs/wotm/bin/pip install "splat64[mips]" pyelftools unicorn
sh tools/setup_toolchain.sh        # downloads ee-gcc 2.95.2 (SN ProDG) into tools/cc/
```
1. Put your ISO in `ISO/` and extract it to `disc/` (for example `7z x -odisc ISO/*.iso`).
2. `~/.venvs/wotm/bin/python configure.py --split`, then `ninja` (ends with `build/SCUS_971.97.rom: OK`).

Before committing changes to decompiled code, run `sh tools/wsl/gate.sh` (build + SHA1 check). Tools and known build pitfalls: [docs/guides/TOOLS.md](docs/guides/TOOLS.md),
[docs/guides/PITFALLS.md](docs/guides/PITFALLS.md).

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). The rules in [CLAUDE.md](CLAUDE.md) apply to humans too: no game files or game assembly in the repo, issues or PRs (mods in `mods/` are the exception: [MODS.md](docs/guides/MODS.md)), and the ROM must keep matching the retail SHA1. The reference docs are in English; `CLAUDE.md`, `TODO.md`, commit messages and the dated notes in `docs/investigacoes/` are in Portuguese.

## License
The code in this repository is [GPL-3.0](LICENSE). It only covers what is here (decompilation, tools, native layer) and grants no rights over the game, which still belongs to its owners.
