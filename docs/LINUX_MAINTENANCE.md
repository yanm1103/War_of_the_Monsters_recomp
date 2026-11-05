# Maintaining the Linux port

The Linux port is a thin platform layer on the Windows runtime, not a separate gameplay fork. Keep Linux-specific changes in the native portability paths and `tools/*.sh`; avoid duplicating game behavior.

## Bring in a Windows update

Review and merge an explicitly selected upstream commit into the Linux branch. Do this in a clean checkout or worktree so local generated game code and ISO inputs stay untouched:

```sh
git remote add upstream https://github.com/yanm1103/War_of_the_Monsters_recomp.git
git fetch upstream
git switch linux-native-port
git merge <reviewed-upstream-commit>
```

If `upstream` already exists, keep its URL and fetch it rather than adding it again. Resolve conflicts by preserving the Linux platform path while taking upstream gameplay fixes. Do not blindly replace the PS2Recomp submodule checkout or generated `recomp/retail/` files; setup/build must use your own ISO and regenerate local inputs as needed.

`tools/setup_runtime.sh` pins the PS2Recomp checkout and applies the patches in `tools/runtime_patch/`. If those patches or the pin change, first inspect and preserve any local changes in the ignored `runtime/PS2Recomp/` checkout. Then set up a fresh disposable checkout at the pinned revision and run `tools/setup_runtime.sh` to verify that the patches apply; do not overwrite the repository's separate `third_party/PS2Recomp` submodule.

After the merge, run the existing Linux path:

```sh
tools/setup_runtime.sh [path/to/your.iso]
tools/build_runtime.sh
tools/run_live.sh
```

For a packaged build, use [`tools/build_appimage.sh`](../tools/build_appimage.sh) in the pinned Ubuntu 22.04 x86-64 environment. Re-run the AppImage payload checks and smoke-test boot, gameplay, audio, save/load, and fullscreen on X11. Keep Windows behavior intact; Linux-only code should not change Windows builds. Record meaningful coverage or blockers in [Linux port progress](LINUX_PORT_PROGRESS.md).

Do not publish an AppImage until its complete corresponding-source and game-derived-code distribution questions are resolved. The AppImage build and a successful local launch are not, by themselves, approval to distribute it.
