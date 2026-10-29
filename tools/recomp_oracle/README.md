# Recompilation oracle (retail × our function)

Compares the **retail** function with **our equivalent** by running both, recompiled to C++ with
[PS2Recomp](https://github.com/ran-j/PS2Recomp), from the same initial state. It covers what `tools/difftest.py` (unicorn) does not execute:
VU0/COP2 (`lqc2`, `vsub.xyz`…) and `min.s/max.s/madd.s/mula.s…`. Of the 329 `equivalent`, 62 use one of these.

**None of this goes into the repo except these scripts.** The generated C++ is game code (rule 1 of `CLAUDE.md`): it stays in `~/wotm-recomp/` in WSL.
PS2Recomp is GPL; that is why its patch is not here, only described below.

## Setup (once, in WSL Ubuntu)
1. `git clone https://github.com/ran-j/PS2Recomp ~/wotm-recomp/PS2Recomp`
2. **Fix the FPU translator** (`ps2xRecomp/src/lib/fpu_translator.cpp`): on the R5900 `SQRT.S fd, ft` reads the source in **`ft`** (uses `|ft|`) and
   `RSQRT.S fd, fs, ft` does `fs / sqrt(|ft|)`. Upstream generates `sqrt(fs)` and `1/sqrt(fs)`, which gets 108 functions with `sqrt.s` and 76 with `rsqrt.s` wrong.
   Without this the oracle reports false divergences (or hides real ones).
3. Recompiler + analyzer: `cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF && cmake --build out/build`
4. Runtime (only for `libps2_runtime.a`; needs `libx11-dev libgl-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libasound2-dev` and the FFmpeg libs):
   `cmake -S . -B out/rt -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_DEBUG_UI=OFF -DCMAKE_CXX_FLAGS='-msse4.1 -mavx -w' && cmake --build out/rt --target ps2EntryRunner`
   (GCC needs `-msse4.1 -mavx`; the runtime's CMake only passes them to MSVC)

## Generating the two outputs (in `~/wotm-recomp/work/`)
```
cp <repo>/disc/SCUS_971.97 game.elf
ps2_analyzer game.elf config.toml                       # troque  output = "./output_fix/"  no TOML
ps2_recomp config.toml
sh tools/wsl/build_nm.sh nmall                          # no repo: ELF com todas as equivalentes (build/pcsx2_test/SCUS_971.97_nmall.elf)
cp <repo>/build/pcsx2_test/SCUS_971.97_nmall.elf nm_all.elf
ps2_analyzer nm_all.elf config_nm_all.toml              # output = "./output_nm_all/"
ps2_recomp config_nm_all.toml
```

## Running
`python3 tools/recomp_oracle/oracle_all.py [filtro ...]` (filters = pieces of the mangled name). Result in `~/wotm-recomp/work/oracle_all.csv`.
For each candidate function it generates a harness, with arguments derived from the mangled name (`p` pointer, `i` integer, `f` float in `$f12…`),
200 random states in a 16 KB arena (words = float, pointer into the arena itself, small integer or garbage), the retail ELF image loaded
(globals) and correct `$gp`/`$vf0`. Classes:

| class | meaning |
|---|---|
| `exact` | return value, arena and global memory identical |
| `approx` | equal within a tolerance of 1e-3 (float operation order) |
| `retonly` | memory equal; only `f0/v0` differ (a `void` function or a return the harness cannot type): check the type in the source |
| `diff` | **memory differs: real divergence**, investigate |

Limits: only **leaf** functions (no `jal`; 35 of the 60 candidates have calls), simple signatures (no `Q…`, templates, function pointers, `…`),
and functions whose loop depends on the random data can time out (`Monster::getClosestMonster`). It is evidence, not proof.

## Scan for miscoded `sqrt.s`
`python3 tools/recomp_oracle/scan_sqrt.py <elf>`: counts `sqrt.s` in the R5900 form (`fs=0`) and in the wrong form (`ft=0, fs≠0`). Retail has 127 and none wrong.
See `docs/guides/PITFALLS.md` (the `sqrtf` pitfall; use `eeSqrtf` from `include/vecmath.h` in NM code).
