# Automatic PCSX2 (testing the half-C++ ELF without the gamepad)

- `play.ps1 <nome-do-elf> [n]`: closes PCSX2, opens `build/pcsx2_test/SCUS_971.97_<nome>.elf`, skips the cutscenes (Enter as soon as `FMV started` shows up in the log),
  goes into 1 player > free-for-all > Togera vs CPU > Midtown Park and watches the log for `Unrecognized op|ReportErrorAsync|Trap exception` for `n` x 2 s.
  Takes a screenshot of the PCSX2 window only (`play_<nome>.png`, next to the script) and prints `OK` or `FAIL: <linha do log>`.
- `emu.ps1 start|snap|key|status|stop`: pieces of the script. `start` only opens the ELF; **the keyboard bindings stay fixed in the user's PCSX2.ini** (they are not swapped/restored). The script uses `ENTER` (Start) and `SPACE` (Cross; `X` is an alias). Do not run PCSX2 by other means while a test is open.
- **Controls (Pad1, always keyboard)**: WASD = left stick; mouse = right stick (and IJKL as an alternative); Q/E = L1/R1; Shift/X = L2/R2; Ctrl/V = L3/R3; Space = X; left click or U = Square; right click or O = Triangle; F = Circle; Enter = Start; Backspace = Select; arrows = D-pad. The PCSX2 TogglePause hotkey is on Pause instead of Space.
- **Opening by hand**: `jogar.bat [nome]` at the repository root (default `halfcpp`; e.g. `jogar.bat bis_old`, `jogar.bat control_matching`).
- The keys go through `PostMessage` straight to the PCSX2 window: they do not need focus and do not steal focus from whoever is using the PC.
  The capture uses `PrintWindow` on the window (never the whole screen).
- After a crash the VM stays paused: the EE RAM can be read from the process (find the retail code in memory with `ReadProcessMemory`
  and compare it with the ELF; this is how the heap overwriting `.nm_extra` was found).
