<img width="2113" height="744" alt="War of The Monsters recompiled LOGO" src="https://github.com/user-attachments/assets/6160bebd-48f0-47b5-bd28-f74d23c662fd" />

# War of the Monsters Recompiled!

A native Windows port of **War of the Monsters** (PS2, 2003), built from the statically recompiled game code with a new PC renderer, sound and movie player, and an **easy-to-mod**
decompilation of the parts modders need.

You need your own copy of the game (NTSC-U, SCUS-97197). No game files are, or ever will be, in this repository.

## Play
Download the latest zip from [Releases](https://github.com/yanm1103/War_of_the_Monsters_recomp/releases), unzip it, open `wotm.exe` and pick your ISO. Requirements, controls and where
saves go are in the `README.txt` inside the zip ([tools/release/README.txt](tools/release/README.txt)). Found a bug? Open an [issue](https://github.com/yanm1103/War_of_the_Monsters_recomp/issues)
and attach `wotm.log`.

8 Monsters brawling: [https://www.youtube.com/watch?v=IvLLuSYORPc](https://www.youtube.com/watch?v=IvLLuSYORPc).

<img width="912" height="822" alt="image" src="https://github.com/user-attachments/assets/77483682-f8e2-470a-95aa-06aabe857b6a" />

<img width="2560" height="1440" alt="image" src="https://github.com/user-attachments/assets/8f837f1f-c5a8-49fe-8ad0-30bfd13bdf84" />

<img width="2560" height="1440" alt="image" src="https://github.com/user-attachments/assets/4583be43-878b-48df-a0e5-30b99702bad9" />

<img width="2560" height="1440" alt="image" src="https://github.com/user-attachments/assets/ddb512fd-ebf0-46d8-90bd-cc40ee9a4334" />

<img width="2560" height="1440" alt="image" src="https://github.com/user-attachments/assets/562f7135-1780-4582-b8d2-fed4b5ca9e85" />

## Mods
Textures as PNG, skins and custom monsters go in the `mods\` folder next to `wotm.exe`. The release ships with Goliath Prime, Cerebulon and Mecha as playable custom monsters. How to make
your own: [docs/passos/MODS.md](docs/passos/MODS.md).

## How it works
1. **Static recompilation.** [PS2Recomp](https://github.com/ran-j/PS2Recomp) translates the whole game executable (VU0 included) into C++ that runs natively. That C++ is game code, so it
   **never goes into this repository**: everyone generates it locally from their own disc.
2. **Native layer** ([native/](native/)). Instead of emulating the VU1, GS and IOP, the port cuts at the engine boundary and draws with the PC's GPU; sound, FMVs and input are native too.
   It is not an emulator. What is still PS2-specific: [docs/passos/FRONTEIRA.md](docs/passos/FRONTEIRA.md).
3. **Readable decompilation** ([src/](src/), [include/](include/)). Game logic, monsters, stages and file formats as readable C++ that replaces the recompiled version through a hook. The
   full ROM build must still reproduce the retail SHA1.

Architecture: [docs/PLANO.md](docs/PLANO.md). The executable: [docs/ANALYSIS.md](docs/ANALYSIS.md). File formats: [docs/FORMATOS.md](docs/FORMATOS.md). What comes next: [TODO.md](TODO.md).

## Building the port (Windows)
1. Clone with submodules (`git clone --recurse-submodules`).
2. Put your ISO in `ISO/` and run `powershell -ExecutionPolicy Bypass -File tools\setup_runtime.ps1`. It extracts the disc, sets up PS2Recomp in `runtime/`, generates the recompiled code
   and runs the first build. Needs git, Python 3, CMake 3.24+ and MSVC 14.40+ (VS 2022 17.10+ or Build Tools).
3. `build_runtime.bat` rebuilds after changes; `run_live.bat -Auto 0` runs the game.

Runtime details, environment variables and the release package: [docs/passos/RUNTIME.md](docs/passos/RUNTIME.md) and [docs/passos/NATIVO.md](docs/passos/NATIVO.md).

## Building the decompilation (WSL/Ubuntu)
```sh
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install binutils-mips-linux-gnu ninja-build python3-venv wine wine32:i386
python3 -m venv ~/.venvs/wotm && ~/.venvs/wotm/bin/pip install "splat64[mips]" pyelftools unicorn
sh tools/setup_toolchain.sh        # downloads ee-gcc 2.95.2 (SN ProDG) into tools/cc/
```
1. Put your ISO in `ISO/` and extract it to `disc/` (for example `7z x -odisc ISO/*.iso`).
2. `~/.venvs/wotm/bin/python configure.py --split`, then `ninja` (ends with `build/SCUS_971.97.rom: OK`).

Before committing changes to decompiled code, run `sh tools/wsl/gate.sh` (build + SHA1 check). Tools and known build pitfalls: [docs/passos/FERRAMENTAS.md](docs/passos/FERRAMENTAS.md),
[docs/passos/ARMADILHAS.md](docs/passos/ARMADILHAS.md).

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). The rules in [CLAUDE.md](CLAUDE.md) apply to humans too: no game files or game assembly in the repo, issues or PRs, and the ROM must keep matching
the retail SHA1. Most project documentation is written in Portuguese.

## License
The code in this repository is [GPL-3.0](LICENSE). It only covers what is here (decompilation, tools, native layer) and grants no rights over the game, which still belongs to its owners.
