# Port para Linux: tamanho

Pergunta do dono: quanto falta para o port rodar no Linux. Resposta curta: **pouco para compilar e bootar, médio para ficar igual ao
Windows e virar release.** O código do jogo traduzido compila com gcc sem nenhuma mudança; a camada nativa precisou de sete
remendos pequenos; com eles o `wotm` de Linux linka (53 MB) e boota até o menu no WSLg, com filmes, som nativo, telão e mods.

## Como foi medido

Cópia do runtime (`wotm-recomp-win/PS2Recomp` com os patches e o jogo traduzido de `src/runner`, mais `native/`) para o disco do
WSL Ubuntu (`~/wotm-linux-probe`, fora do repo; tem código do jogo), configurada com os mesmos `-D` do `configure_dev.bat`
(`PS2X_FAST_LINK=ON`, FFmpeg ligado), gcc 15.2, Ninja, `cmake --build build --target wotm -- -k 0`. Os remendos foram feitos só na
cópia. Depois, `wotm discroot/SCUS_971.97` com as variáveis do `run_live.ps1` (`PS2X_NO_VU1/VIF1/GS`, `PS2X_SCENE`, `PS2X_FMV`,
`PS2X_CD_IMAGE` = a ISO em `/mnt/c`), 60 s, sem roteiro de pad.

- Configuração: passou de primeira (o CMake do patch já tem os ramos `else()` do MSVC; raylib e FFmpeg do sistema acharam sozinhos).
- Compilação: 252 unidades; o código do jogo (5383 funções em unity) compila limpo com gcc depois da flag do item 1.
- Link: limpo depois do item 6.
- Execução: abertura (`intro_m.pss`, `int_main.pss`), menu com o telão (`sheldemo.pss`), bancos de som, os três monstros custom de
  `mods/`, save em JSON. GL do WSLg é llvmpipe (software) e ainda assim o menu fica em ~13 ms por redesenho. Luta não testada.

## O que precisou mudar para compilar (pequeno)

| # | Onde | O que | Correção definitiva |
|---|---|---|---|
| 1 | CMake (patch) | `ps2_runtime_macros.h` e o nativo usam SSE4.1 (`_mm_extract_epi32/64`, `_mm_blendv_epi8`); o MSVC tem `/arch:AVX2`, o gcc não recebe nada | `-mavx2` (e `-mbmi2 -mlzcnt -mpopcnt` se quiser o mesmo do AVX2) no ramo não-MSVC, junto do `/arch:AVX2` |
| 2 | `wotm_snd.inc:14` | `#include "Kernel/Stubs/CD.h"`: o MSVC procura na pasta de quem incluiu (o `ps2_runtime.cpp` em `src/lib`), o gcc não | `src/lib` nos includes do `ps2_runtime` (patch) ou caminho relativo |
| 3 | `wotm_standalone.cpp` | `_stricmp`, `_fseeki64` (linha 234, dentro de um lambda fora do `#ifdef`) e `MB_OK`/`MB_ICONERROR`/`IDOK` fora do `#ifdef _WIN32` | `strcasecmp`, `fseeko`, constantes próprias do `box()` |
| 4 | `wotm_live.inc:300,471`, `wotm_prompts.inc:15` | declarações à mão do kernel32/user32 (`SetUnhandledExceptionFilter`, `Get/SetWindowLongPtrW`, `SetWindowPos`, `FindResourceA`...) sem `#ifdef` | `#ifdef _WIN32` em volta; no Linux o filtro de queda vira `sigaction` e o `WOTM_CAPTUREFIX` some |
| 5 | `wotm_prompts.inc`, `wotm_padsel.inc:2632`, `wotm_optscreen.inc:219` | atlas dos ícones e fonte Oswald vêm do recurso do exe (`wotm.rc`) | os bytes em array C gerado no build (CMake `file(READ ... HEX)` ou `#embed`), igual nos dois sistemas |
| 6 | `wotm_live.inc:553` | `void wotmHostProfWatchThisThread();` declarado dentro de uma função no `namespace wotm_live`: pelo padrão é `wotm_live::`, o MSVC liga com o global | declarar no escopo global |
| 7 | `wotm_savepics.hpp:356` | `fwd = 0.f` em struct aninhada usada num `constexpr` da mesma classe: o gcc recusa | tirar o inicializador (o agregado já zera) |

De brinde, o gcc acusou `wotm_preview.hpp:259`: `x * 73856093` estoura `int` com sinal (comportamento indefinido; inofensivo no
MSVC). Trocar por `uint32_t(x) * 73856093u`.

No teste, o item 5 foi trocado por stubs que devolvem nulo: o jogo cai no fallback já existente (ícones do jogo, fonte do raylib,
log `[prompts] atlas WOTM_PROMPTS nao esta no executavel`).

## O que falta para ficar igual ao Windows (médio)

- **Primeira execução sem argumento** (`wotm_standalone.cpp`): `pickIso` é `GetOpenFileNameW`; no Linux, `zenity`/`kdialog` se houver,
  senão pedir no terminal ou aceitar a ISO como argumento. Pasta de dados: `%LOCALAPPDATA%\WotM` vira `$XDG_DATA_HOME/WotM`
  (`~/.local/share/WotM`). Caixas de mensagem viram stderr (já é o que o `box()` faz fora do Windows).
- **Pilha da queda com nomes** (`wotmCrashTrace`, dbghelp + PDB): `backtrace()` + símbolos do próprio binário (ou `-g1` e
  `addr2line`). Sem isso um relato de queda no Linux chega sem pilha.
- **Vibração** (`wotm_rumble.inc`): XInput não existe; o GLFW do raylib também não vibra no Linux. Caminho: `FF_RUMBLE` do evdev
  (`/dev/input/event*`) ou SDL. Pode ficar de fora numa primeira versão (o log já diz "XInput indisponivel: sem vibracao").
- **Amostrador do host** (`ps2_hostprof.cpp`) e a checagem VU1 (`wotm_vu1check.inc`): diagnóstico, já têm ramo sem Windows; não
  precisam existir no Linux.
- **Não testado:** luta, aventura, 2 a 4 jogadores, gamepads no Linux (o GLFW usa o mapeamento SDL, deve funcionar), desempenho numa
  GPU de verdade, nomes de arquivo em `mods/` com maiúsculas diferentes (o disco vem da ISO, que já não diferencia).

## Ferramentas e release (médio)

Tudo do runtime é `.bat`/`.ps1` com MSVC: `tools/setup_runtime.ps1` (clona o PS2Recomp, aplica os patches, compila analisador e
recompilador, traduz o ELF), `build_runtime.bat` + `tools/build_runtime.ps1`, `run_live.bat` + `runtime_template/run_live.ps1`,
`tools/release.ps1` (acha DLLs com `dumpbin`). Para Linux:
- `setup_runtime.sh` e `build_runtime.sh` equivalentes (o PS2Recomp de cima já tem CI no Linux; o jogo traduzido é o mesmo, dá para
  reaproveitar o `src/runner` gerado no Windows).
- `run_live.sh` (as variáveis acima). `lote.ps1`/`corpus.ps1` só se o dono quiser testar em lote no Linux.
- Release: tarball (ou AppImage) com o `wotm`, raylib já estático; FFmpeg ou do sistema ou junto (`libav*` com rpath `$ORIGIN`).
  O binário pede AVX2 (Steam Deck e qualquer x86-64 dos últimos ~10 anos têm). Licenças: as mesmas do zip.
- Launcher C# (`tools/launcher`): de desenvolvimento, não precisa ir.

## Ordem sugerida

1. Itens 1 a 7 de verdade (com `#ifdef`, sem mudar nada no Windows) e regenerar `tools/runtime_patch/wotm-runtime.patch`. Conferir que
   o build do Windows continua igual (`build_runtime.bat`, abrir uma luta).
2. `run_live.sh` e jogar luta e aventura no WSLg (ou num Linux de verdade) para achar o que não aparece no menu.
3. Primeira execução, pasta de dados e pilha da queda.
4. `setup_runtime.sh`/`build_runtime.sh` e o pacote de release.
5. Vibração por último, se fizer falta.

Wine/Proton com o `wotm.exe` do zip: não testado. Pode ser o caminho mais barato para quem só quer jogar, mas não fecha o "port
nativo"; vale uma rodada de teste antes de decidir.
