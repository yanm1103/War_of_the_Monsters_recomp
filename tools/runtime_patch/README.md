# Patch do runtime (PS2Recomp)

`wotm-runtime.patch`: nossas mudanças sobre `ran-j/PS2Recomp` (commit base `2c5fbb9`): desenho nativo (`wotm_scene.inc`/`wotm_live.inc`), teclado, perfil do host (`PS2X_HOSTPROF`), lote do IOP (`PS2X_IOP_BATCH`, 60 vsync/s na fase) e medições.

Fica de fora `ps2xRuntime/src/runner/register_functions.cpp` (registro de funções gerado a partir do jogo: código do jogo, não entra no repo). Gere-o com o PS2Recomp a partir do seu ELF.

Aplicar: `git apply --whitespace=nowarn tools/runtime_patch/wotm-runtime.patch` dentro do clone do PS2Recomp.
