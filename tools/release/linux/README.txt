War of the Monsters — Linux Preview

This is an early Linux preview, not Windows feature parity. The AppImage targets glibc-based x86-64 Linux systems with glibc 2.35 or newer, plus AVX2, BMI2, POPCNT, and LZCNT. Void Linux (glibc, X11) has been tested; musl-based systems have not. It currently uses X11; Wayland/XWayland has not been tested. OpenGL and a working Linux audio service are required. Ubuntu 22.04 is the reproducible build environment for the AppImage, not a runtime requirement.

The AppImage contains the native runtime and its application assets. It does not contain an ISO, a separately extracted game ELF, or disc files. On first launch, choose your own NTSC-U War of the Monsters ISO. The runtime verifies it and extracts the retail ELF into your user data directory; the ISO stays where you keep it. Saves, settings, and logs are stored under $XDG_DATA_HOME/WotM, or ~/.local/share/WotM by default.

The bundled FMV decoder is FFmpeg 7.1 built as LGPL 2.1-or-later shared libraries. The matching source archive and build configuration are distributed beside the AppImage. The GCC C++ runtime is statically linked into the game executable; the AppImage does not bundle `libstdc++` or `libgcc_s`. See `GCC-Runtime-copyright.txt` for the applicable license and exception.

Most desktops provide Zenity or KDialog for the first-run ISO picker. If neither is installed, the runtime uses a graphical picker on X11; without a graphical display, launch from a terminal for text prompts. AppImage mounting may require FUSE 2; where unavailable, run with `APPIMAGE_EXTRACT_AND_RUN=1`.

Bundled game mods are omitted. User mods can be loaded from the WotM user-data mods directory or via WOTM_MODS.

Full campaign progression, physical controller behavior, Windows regression, and non-X11 window managers remain unverified. See the project's Linux port progress report for current test coverage.
