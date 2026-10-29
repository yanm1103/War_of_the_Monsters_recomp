# War of the Monsters (SCUS-97197): análise inicial

## Disco
- Volume `MONSTERS`, preparado por `INCOG INC` (Incognito Entertainment), publicado pela SCEA.
- `SYSTEM.CNF`: `BOOT2 = cdrom0:\SCUS_971.97;1`, `VER = 1.00`, `VMODE = NTSC`.
- ELF principal: `SCUS_971.97` (7,4 MB), SHA1 `1ed7f8457436a9170d8f85386e267369c7fcdd4f`.
- "ROM" gerada pelo splat (PT_LOAD em binário): SHA1 `250e3544aa99a04a2fd45974ff7ea38c31d0fc31`.
- IRX em `MOD/`: 989SND/989ERR (driver de som 989 Studios), LIBSD, MCMAN, MCSERV, MTAPMAN, PADMAN, SIO2MAN e `IOPRP24.IMG`.
- Formatos de asset: `.NGP/.PTR/.RTX/.TEX` (modelos e texturas de nível/monstro), `.AIC/.AIP` (IA),
  `.CHR`, `.MRS`, `.BNK` (som), `.VPK/.VAG` (áudio ADPCM), `.PSS` (vídeo MPEG2).

## ELF
- **Não está stripped**: `.symtab` com 9.669 símbolos, sendo 5.381 funções com tamanho (cobrem 98,8% do `.text`).
- C++ com mangling do **GCC 2.9x** (`regen__6Pickup`, `__tf...`, `_vt$...`) e exceções (`.gcc_except_table`).
- Toolchain da SCE: **ee-gcc 2.9x + newlib**. Libs da Sony versão **2.4.0** (`PsIIlib* 2400`).
- O prólogo salva registradores com `sq`/`lq` (padrão do ee-gcc).
- `.mdebug` (ECOFF + stabs) cobre libgcc, newlib e libs da SCE, e **um arquivo do jogo, `hieri.cpp`, com tipos completos**
  (layouts de struct/classe, como `HierLight`). Use `tools/mdebug_dump.py`.
- `.stab` contém só as linhas do microcode VU (`C:\CLEAN\MONSTERRT\COMMON\*.vsm/*.dsm`).
- `.vutext` (0x251020, 46 KB) e as seções `.DVP.overlay.*` guardam o microcode VU1/VU0.
- 294 classes do jogo nomeadas (Monster, MonsterSound, Ai, Shell, Pickup, TheGame, ...).
- 45 TUs têm construtores estáticos (`_GLOBAL_$I$...`), o que ajuda a achar os limites de arquivo.

## Layout do .text
| Endereço | Conteúdo |
|---|---|
| 0x100000 – ~0x1E5360 | código do jogo |
| ~0x1E5360 – ~0x1EAA00 | libgcc / runtime C++ (tinfo, exception, frame) |
| ~0x1EAA00 – 0x22D140 | mais código do jogo e engine (hier, pkta, ...) |
| 0x22D140 – 0x251020 | newlib (libm/libc), libs da SCE (libpad, libmc, libmpeg, libipu, libcdvd, libkernl) |

## Pendências
- [x] Identificar o compilador: **SN Systems ProDG ee-gcc 2.95.2 (v2.73a) + assembler SN `ps2eeas`**, `-O2` (`-G8` no jogo e na engine). Veja "Toolchain".
- [x] Dividir o `.text` em TUs. São 336, e o rebuild continua fazendo match. Veja "Divisão em TUs" abaixo.
- [x] Dividir `.data`/`.rodata`/`.sdata`/`.sbss`/`.bss`/`.gcc_except_table` por TU (483 faixas). Veja "Divisão dos dados".
- [x] Ambiente Linux/WSL com binutils MIPS; rebuild do asm puro faz match do SHA1.
  Observações: `-mabi=o64` no GAS (o spimdisasm usa nomes de registradores o32) e seções alinhadas a 0x80 (`align: 0x80` no yaml).
- [x] Exportar tipos de `hieri.cpp` para headers: `tools/stabs2h.py` gera `include/ps2_sdk_types.h` e `include/hieri_types.h`. Veja "Tipos do hieri".

## Divisão em TUs
- O gcc 2.x emite um label local `gcc2_compiled.` no início do `.text` de cada TU, seguido de
  `__gnu_compiled_c` ou `__gnu_compiled_cplusplus`, o que dá limites exatos e a linguagem de cada TU.
  As TUs de biblioteca têm o nome real no `.mdebug`.
- `tools/find_tus.py` gera `config/tus.csv`, e `tools/apply_tus.py` reescreve os subsegments no yaml.
- Distribuição: `game/` 112, `common/` 71 (engine, `C:\CLEAN\MONSTERRT\COMMON`), `lib989snd/` 1,
  `gcc/` 21, `newlib/` 77, `sce/` 53 e `crt0`.
- As TUs de `game/` são linkadas em **ordem alfabética, sem diferenciar maiúsculas** (wildcard do makefile).
  Os nomes reais são desconhecidos: a coluna `source` do csv diz se o nome veio do `mdebug`,
  de escolha `manual` (em `OVERRIDES`, guiada pela ordem alfabética) ou de `guess` (classe ou prefixo dominante).
- Ajustes necessários para o match:
  - `subalign: 4`, porque o GAS alinha o `.text` de cada objeto a 16, mas no original as TUs C ficam em 8
    e os `.S` da newlib em 4.
  - `reloc_addrs.txt` (gerado por `tools/gen_relocs.py`) força imediatos crus em 1.606 pares `%hi/%lo`
    que o spimdisasm resolvia para o meio do `.text`. São offsets de struct grande (`lui 0x12`), como
    `0x120380`, e não ponteiros.
- `tools/symdiff.py` lista os símbolos que mudaram de endereço no ELF gerado, útil quando o checksum falha; `tools/romdiff.py` mostra as palavras que diferem.

## Toolchain
- Teste de referência: `zipCrc32__FUlPCUcl` (`common/zip`) é o `crc32` do zlib e dá match exato
  (fonte em `tools/cctest/crc32_variants/localptr2.cpp`; o original usa um ponteiro local `tab`
  declarado depois do teste `buf == 0`).
- Todas as builds Sony 2.95.x (273a/274/3-107/3-114/3-136) e a SN v2.73a geram o **mesmo** `.s` aqui.
  O que decide é o **assembler**: o `ps2eeas` da SN expande `dli r,0xffffffff` como
  `addiu r,$0,-1; dsrl32 r,r,0`, e o GNU as (antigo ou moderno) usa `lui 0xffff`. Os símbolos
  `sn_reg_frame`/`sn_dereg_frame` (libsn) confirmam o ProDG.
- O `ps2eeas` não entende a sintaxe GNU do splat. Por isso o build compila com `-S`, reescreve as macros
  do jeito da SN com `tools/snfix.py` e monta com `mips-linux-gnu-as`. Isso permite `INCLUDE_ASM`.
  Macros sem expansão conhecida fazem o build falhar, em vez de gerar mismatch silencioso.
- `tools/cc.sh` é o pipeline de compilação, e `tools/setup_toolchain.sh` baixa o compilador para `tools/cc/`.
- `tools/ccmatch.py <src> "<flags>" [compiladores|project]` compila e compara cada função com o original.
  `tools/wsl/objdiff.sh <func> <obj>` mostra o diff lado a lado.

## Divisão dos dados
- `tools/find_data_tus.py` gera `config/data_tus.csv`, e `tools/apply_tus.py` grava os subsegments no yaml.
- Evidências: (1) os statics locais, agrupados por objeto na `.symtab` (exatos, e monotônicos em todas as
  seções); (2) as referências `%hi`/`%lo`/`%gp_rel` do código de cada TU. Uma DP escolhe a atribuição
  monotônica de maior peso.
- Os limites em trechos sem evidência (dados sem referência entre duas TUs) são arbitrários dentro do
  intervalo possível. Não afetam o match, mas podem precisar de ajuste ao descompilar.
- TUs em `config/decomp_tus.txt` usam `.rodata` (com ponto): a rodata usada por uma única função migra
  para o `.s` da função (INCLUDE_ASM), e a rodata compartilhada precisa ser definida no C/C++.

## Piloto de descompilação
| TU | Funções | Match | Tempo | Observação |
|---|---|---|---|---|
| `common/zip` | 14 (7.100 B) | 12 (6.840 B) | ~50 min | fonte de referência conhecida (gzip / Info-ZIP); inclui ~15-20 min de descobertas de toolchain |
| `game/CrushLevel` | 5 (1.136 B) | 4 (+1 a ~95%) | ~20 min | sem fonte de referência; exigiu layouts parciais de `TheGame`/`Monster` |

As 3 funções que não deram match esbarraram em **alocação de registradores/escalonamento**, não em lógica.
Um decomp-permuter deve resolver boa parte desses casos.

### Regras de codegen aprendidas
- `ps2eeas` decide `$gp` vs `lui` numa passada só (veja Toolchain); por isso a **ordem de definição das globais** importa.
- g++ 2.95: globais com inicializador (inclusive dinâmico) são emitidas na definição; as sem inicializador vão
  para o fim do arquivo, em ordem de primeira declaração. `static const` de header só é emitido se usado, no fim.
- Literais de string de até 8 bytes vão para `.sdata` (`-G8`).
- R5900 short-loop: loops com menos de 6 instruções recebem `nop`s (o `snfix` faz isso; delay slots em `reorder` contam).
- O escalonador tende a emitir instruções independentes na **ordem inversa** do fonte (ex.: stores consecutivos).
- Store em campo `char` força recarregar globais (alias com tudo); campos `int` não.
- O `zipCheckHeader` e o `zipGetChar` ficaram `NON_MATCHING` (registradores).
- Corpo que termina em `asm` inline: o gcc deixa o delay slot para o assembler. O `ps2eeas` põe um `nop`; o GNU as puxa
  a última instrução do `asm` para o slot. O `snfix` reproduz o `nop` (`.set noreorder` em volta do `j`).
- Matrizes: `A3_A3_f` no nome mangled é `float[4][4]` (o gcc 2.x grava tamanho-1). Cópias de 64 B com `lq/sq t0..t3` são `asm` inline.
- `ptr = (char *)(idx * 64) + (unsigned)base` produz `addu a1,a1,v0` (resultado no registrador de `idx`); `&base[idx]` não.
- Os `.s` por função do splat divergem dos monolíticos (`ACC` sem `$`, `%lo(sym + (0x44000 & 0xFFFF))` sem o carry do `%hi`);
  o `configure.py` corrige os dois depois do split.

## Fronteira com o hardware (primeira passada)
Visão dinâmica (o que roda por quadro no port e a ordem para tirar o código de emulador): `docs/passos/FRONTEIRA.md`.
`tools/hw_boundary.py` gera `config/hw_boundary.csv` com, por TU, chamadas ao SDK, acessos a registradores
(0x1000xxxx/0x1200xxxx), instruções VU0 (COP2), scratchpad e chamadas ao 989snd.
- **VU0 macro-mode está em quase todo o código de jogo** (2.681 `lqc2`, 1.426 `sqc2`, `vmulax/vmadday/vmaddz`...):
  são funções inline de matemática vetorial (matriz×vetor, soma) com assembly inline. Para o port, devem vir
  de um header de math, então trocá-las por C++/SSE resolve o problema num ponto só. Para o match, esse header
  precisa reproduzir o asm inline original.
- O acesso direto a registradores e DMA se concentra na engine (`common/hier`, `texm`, `vi`, `ps`, `pkt`, `disp`,
  `blit`, `particle`...) e em `game/ui`. É a camada de render que o port vai reescrever.
- Som: `game/Sound`, `game/StreamingSoundManager` e `game/ShellFinished` chamam o 989snd (`snd_*`). Memory card: `game/McFile`.

## Tipos do hieri
`python tools/stabs2h.py disc/SCUS_971.97 include` converte os stabs de `hieri.cpp` (.mdebug) em headers C++:
81 tipos do SDK (registradores GIF/VIF/DMA/GS como bitfields, `ThreadParam`, `sceDmaTag`...) e 89 da engine
(`_hierobject`, `_hiergroup`, `_animCharInstance`, `ParticleType`, enums de FX...).
- Cada struct tem um `typedef char _size_X[sizeof(X) == N ? 1 : -1]`; o header compila limpo com o ee-gcc 2.95
  (`tools/cc.sh`), então todos os tamanhos batem.
- Os stabs não guardam `__attribute__((aligned))`; `infer_alignment` recupera o alinhamento (16) a partir dos offsets.
- Métodos (construtores, `operator=`) são descartados; só ficam dados, enums e typedefs. Tipos opacos (`ActHead`) ficam só com forward declaration.
- `long long` e `long128` têm 128 bits no ee-gcc (`long` tem 64).
- Cobrem só a engine em `common/hier*`; o resto do jogo (`game/`) segue sem tipos.

## Recompilação estática
Feita com o [PS2Recomp](https://github.com/ran-j/PS2Recomp) (commit 2c5fbb9), **fora do repo** (o C++ gerado é código do jogo: ~90 MB, 5.385 arquivos).
- O ELF tem símbolos, então o analisador acha 5.382 funções com nome e tamanho. Recompila em ~1,4 s: 5.042 recompiladas, 340 trocadas por handlers do runtime
  (SDK/libc/libm), **0 falhas de decodificação, 0 instruções não tratadas** (COP2/VU0 macro-mode incluso). 432 funções têm `jr`/`jalr` indireto (`switch`) que o analisador não resolve
  (0 jump tables detectadas); o recompilador as cobre com "fallback entries" (60.604).
- Todo o C++ compila e linka com o runtime. O jogo carrega os 7 IRX no IOP emulado (989snd incluso), passa do init de som e entra no laço de interface (`userintMain`).
  O decodificador de filmes do jogo (`decBs0`/`videoDec*`) espera a IPU, que o runtime não emula; o port troca `playMpegMovie*` e a thread `updateMpegMovieDecoding` pelo
  tocador nativo. O jogo roda no Windows (MSVC) com desenho, som, filmes e teclado nativos (`native/`; estado em `docs/passos/NATIVO.md`, o que falta em `TODO.md`).
- O runtime já traz GS em CPU, interpretadores de VU e VIF1 e um IOP emulado; é a ponte, não o destino (o destino é renderização nativa na fronteira da engine). O VU1 emulado serve de referência para a checagem VU1 x nativo (`docs/passos/VU1CHECK.md`).
- **Bug do PS2Recomp**: `SQRT.S fd, ft` lê a fonte em `ft` (usa |ft|) e `RSQRT.S fd, fs, ft` é `fs / sqrt(|ft|)`; o upstream usa `fs` (PR #277 aberto). Afeta 108 funções (`sqrt.s`) e 76 (`rsqrt.s`).
  **`div.s` e `rsqrt.s` por zero**: o EE satura em ±FLT_MAX, não em ±Inf (com ±Inf, o VU0 faz 0 × Inf = NaN; análise no commit 7c1c24e). O gerador do WSL
  (`~/wotm-recomp`, `fpu_translator.cpp`: `FPU_DIV_S`/`FPU_RSQRT_S`) e o runtime (`ps2_fpu_rsqrt` no patch) saturam; o upstream faz o mesmo (PR #272). Não verificado: `min/max` com NaN.
- **Stub `rand` do PS2Recomp** (o upstream tem o mesmo bug): usa `std::rand() & 0x7FFF` em vez do LCG da newlib (31 bits). O `mathfRandf` usa os 23 bits baixos de `rand()` como mantissa, então com 15 bits só sairia em [lo, lo + 0,004·(hi − lo)). O `LibC.cpp` do patch usa o LCG da newlib.
- **`sqrt.s` no gas**: o gas codifica `sqrt.s` na forma MIPS32 (fonte em `fs`); no EE isso lê `$f0`. O C++ NM usa `eeSqrtf` (`include/vecmath.h`).
- Cobertura do boot até a interface (3 min, filmes pulados): 786 funções distintas (97 matched, 31 equivalent, 601 asm, 57 hw). Lista das 601 em `config/boot_coverage_asm.csv`.

## Convenções de status das funções
Cada função do binário está em um destes estados (`python3 tools/progress.py`, tabela completa em `config/status.csv`):

| Estado | Significado | Como aparece no código |
|---|---|---|
| `matched` | C/C++ que gera exatamente os mesmos bytes | função escrita em `src/`, sem `INCLUDE_ASM` |
| `equivalent` | reescrita que se acredita equivalente, mas não bate byte a byte | dentro de `#ifdef NON_MATCHING`, com `INCLUDE_ASM` no `#else`; o comentário diz a pontuação e a causa |
| `hw` | fala direto com o hardware do PS2 ou com serviços do SDK; será reescrita na camada de plataforma do port | listada em `config/hw_funcs.txt` (por TU ou por símbolo) |
| `asm` | ainda só em assembly | `INCLUDE_ASM` simples |

- O build padrão (`ninja`, `tools/wsl/check.sh`) usa o assembly original das funções `equivalent`, então a ROM sempre confere com o retail. Um build com `-DNON_MATCHING` compilaria todas as reescritas em C++ (base do port).
- As funções `equivalent` são verificadas sem testes unitários (decisão do projeto): `tools/difftest.py` (retail x nossa num emulador MIPS, mesmo estado aleatório), `tools/recomp_oracle` (as com VU0 ou `min/max/madd`), o ELF halfcpp no PCSX2 (`docs/passos/TESTE_PCSX2.md`) e, incompleto, o runtime cobaia (`docs/passos/NATIVO.md`). `tools/verify_tu.py` roda tudo isso por TU. Erros reais já achados por cada um estão em `docs/passos/ARMADILHAS.md` e `TESTE_PCSX2.md`.
- Quem quiser continuar o trabalho byte a byte: `grep -rn NON_MATCHING src/`, o comentário ao lado de cada função diz onde parou. `tools/wsl/variants.py` e `tools/wsl/permute.py` testam muitas variações do fonte em paralelo.
- Código `hw` e funções de biblioteca (`libs`) não precisam bater: não gastar esforço neles.
