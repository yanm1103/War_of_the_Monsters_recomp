# Maintaining the Linux port

The Linux port is a thin platform layer on the same runtime as Windows, not a separate gameplay fork. Linux-specific code lives behind `#ifndef _WIN32` / `WOTM_X11_FULLSCREEN` in `native/` (mostly `native/runtime/wotm_standalone.cpp`), in `native/wotm_assets.hpp` and in `tools/*.sh`; game behavior is never duplicated per platform.

## Rules for changes in `native/`

- Windows-only APIs (`windows.h` declarations, `FindResourceA`, `SetUnhandledExceptionFilter`, `GetWindowLongPtrW`) stay inside `#ifdef _WIN32`, with a Linux branch or a no-op in the `#else`.
- Resources that the Windows build embeds in the executable (`FONT_OSWALD`, `WOTM_PROMPTS`) are read on Linux from `native/assets/` through `wotm::assets::file` (`WOTM_ASSET_DIR` overrides the folder).
- Paths from UTF-8 text (JSON, `wotm.ini`) go through `wotm::mods::pathFromUtf8`, not `std::filesystem::u8path`.
- Runtime patch: the Linux pieces of `tools/runtime_patch/wotm-runtime.patch` are the `src/lib` include, the `WOTM_NATIVE_DIR` define, the X11 block (`CMAKE_SYSTEM_NAME STREQUAL "Linux" AND GLFW_BUILD_X11`) and the non-MSVC branch of `EnableFastReleaseMode`. When the patch is regenerated, they stay.

## Building and checking on Linux

`tools/setup_runtime.sh` pins the PS2Recomp checkout and applies the patches in `tools/runtime_patch/`. If those patches or the pin change, first inspect and preserve any local changes in the ignored `runtime/PS2Recomp/` checkout, then set up a fresh disposable checkout at the pinned revision and run `tools/setup_runtime.sh` to verify that the patches apply; do not overwrite the repository's separate `third_party/PS2Recomp` submodule. Never replace generated `recomp/retail/` files with someone else's: setup and build use your own ISO.

```sh
tools/setup_runtime.sh [path/to/your.iso]
tools/build_runtime.sh
tools/run_live.sh
```

## AppImage

`tools/build_appimage.sh` builds it in the pinned Ubuntu 22.04 x86-64 environment (GLIBC 2.35 ceiling, static C++ runtime with hidden symbols, LGPL-only FFmpeg); `tools/check_appimage.py` validates the payload (`tools/test_check_appimage.py` tests the checker). The package carries no ISO, retail ELF, disc files or mods; licenses and notices are in `tools/release/linux/`.

An AppImage is not published until its corresponding-source and game-derived-code distribution questions are resolved. A successful build and local launch are not, by themselves, approval to distribute it.
