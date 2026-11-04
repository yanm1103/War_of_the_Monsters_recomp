# Linux port progress

## Scope

Native Linux x86-64 development build and launch, keeping the Windows build intact. This log tracks implementation and verification; it does not claim Linux support until gameplay has been tested on Linux.

## Status

| Milestone | Status | Evidence / blocker |
|---|---|---|
| Baseline and requirements | Complete | GCC 14.2, CMake, Ninja, and FFmpeg development libraries are available. The ISO in `Game/` passed the retail ELF SHA1 check; generated files remain ignored/local. |
| Native source portability | Compiles | The Linux runtime built successfully, including the recompiled game and native layer. GCC 14 still warns about existing `std::filesystem::u8path` calls and `-Wfree-nonheap-object` in the ISO9660 parser; the actual NTSC-U image passed standalone SHA1 validation. |
| Linux setup/build/run scripts | Smoke-tested | Setup and build completed; launch works repeatedly and now discovers ISOs in `Game/` as well as `ISO/`. The standalone first-run path has Linux GUI/terminal ISO selection and XDG data-directory fallback. |
| Linux menu boot | Verified | The menu and monster-selection UI rendered with native fonts/prompts and stage resources. |
| Linux gameplay verification | Partially verified | The first-run GUI ISO picker validated the retail image, then the game booted to the menu with bundled assets. Keyboard navigation entered Adventure and loaded a fight; a 3-player AI match completed a round, and a 4-player AI fight ran without a crash. The DragonRise pad was enumerated as player 1; synthetic `uinput` events through a matching DragonRise gamepad navigated the in-game pause menu, but physical button/axis behavior remains unverified. Both boot FMVs rendered, and a PulseAudio monitor capture contained non-silent stereo output. The Options screen wrote `WotM/save.json`, and a subsequent launch with the same XDG data directory loaded the saved state; campaign save/continue and Windows regression remain untested. |
| Packaging | Deferred | First establish a working Linux build and gameplay; distribution format is not selected. |

## Baseline findings

- `third_party/PS2Recomp` is a modified submodule checkout with local edits. Do not reset, overwrite, or regenerate its files as part of this work.
- `third_party/ps2-recomp-Agent-SKILL/` is untracked user data. Preserve it.
- The previous Linux experiment is available in Git history as `7df1f85:docs/investigacoes/2026-10-30/linux.md`; it reached the menu but did not test a fight and used placeholder behavior for some embedded resources.
- Linux prerequisites present: GCC 14.2, CMake, Ninja, pkg-config, Python 3, and FFmpeg development libraries.
- `Game/War of the Monsters (USA).iso` is local input. Its extracted ELF SHA1 is `1ed7f8457436a9170d8f85386e267369c7fcdd4f`.
- Extracted disc, translated game code, PS2Recomp checkout, and runtime build are ignored/local. Do not add them to Git.

## Work log

- 2026-11-04: Initial baseline: game inputs and generated outputs were not yet available, so the runtime could not be executed.
- 2026-11-04: Added Linux portability branches, real asset-file loading for the non-Windows build, GCC AVX2 options and the `tools/setup_runtime.sh`, `tools/build_runtime.sh`, and `tools/run_live.sh` workflow. Setup preserves existing translated output; launch uses the extracted ELF and per-user writable data. Verified shell syntax, standalone GCC syntax, whitespace, and patch application against the pinned base.
- 2026-11-04: Ran setup with the ISO in `Game/`; retail SHA1 matched and PS2Recomp generated local game code. Built both `wotm` and `ps2EntryRunner` successfully with GCC/CMake/Ninja and system FFmpeg.
- 2026-11-04: Fixed the launch script's repeat-run handling (an existing per-user ELF had made `cp -n` abort) and added automatic ISO discovery under `Game/`. Verified menu/selection and a direct AI-vs-AI fight using X11 screenshots; confirmed the fight window was on `eDP-1` (root position x=2880 while eDP-1 spans x=1920..3840). Restored HDMI-1 as the desktop primary after testing. Gameplay remained live without a crash during the smoke test.
- 2026-11-04: Added Linux standalone behavior for read-only installs (XDG/HOME data path), ISO selection via Zenity/KDialog or terminal, and log files for interactive launches. Verified the XDG fallback and terminal-based first-run path with the retail ISO; extracted ELF SHA1 matched.
- 2026-11-04: Verified the Zenity first-run picker with a clean temporary install: it selected the ISO in `Game/`, checked the retail ELF SHA1, extracted the ELF, and booted the game with the bundled font and prompt atlas. Keyboard input navigated the menus into Adventure and loaded the first fight on `eDP-1`; the native audio device opened. This does not yet establish sustained combat, audible output, controller support, save/reload, or Windows parity.
- 2026-11-04: Changed setup extraction to use a fresh temporary directory before accepting the retail ELF. A stubbed extraction test confirmed that an existing `disc/SCUS_971.97` cannot make an ISO with no ELF pass validation.
- 2026-11-04: Verified both boot FMVs on Linux; `intro_m.pss` completed and `int_main.pss` rendered, then skipped on input. The app opened a 48 kHz stereo stream, and a five-second PulseAudio monitor capture had non-zero RMS/peak levels. Saved Options through the game UI, relaunched with the same XDG data directory, and confirmed the save JSON was read. The DragonRise controller was recognized as player 1; a matching synthetic `uinput` pad moved the in-game menu selection, but physical controls were not actuated. Sustained 4-player AI combat ran without a crash. Its measured 4-view redraws varied from about 15 to 21 ms, so 60 Hz is not consistent in that mode; the short profile did not isolate a safe code-level optimization, so no rendering changes were made. Campaign save/continue, full physical controller behavior, and Windows regression remain open.
