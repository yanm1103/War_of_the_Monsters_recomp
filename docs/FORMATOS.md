# Formatos de arquivo do jogo (o que o código decompilado revela)

Fonte: `Shell::LoadLevelFiles`, `LoadLevelDB`, `LoadMonstersDB`, `LoadResTexture`, `LoadTexture` (`src/game/Shell.cpp`),
`dbsRelocateFileZero`, `dbsRelocateViaPtrListFile` (`src/common/dbs.cpp`), `dbInitDb`, `dbProcInteractive` (`src/game/db.cpp`).
Tudo abaixo foi lido do código; o que está marcado (?) ainda é suposição.

## Arquivos por fase e por monstro
| Extensão | Conteúdo | Carga |
|---|---|---|
| `lvl\<fase>.NGP` | imagem do nível (hierarquia de nós), comprimida em zip | `zipInflateAll(path, 0xA00000)`; depois `fileOnlyNgpFile` |
| `lvl\<fase>.TEX` | texturas do nível | `getNextNgpLoadAddr` + `fileOnlyTexFile` |
| `lvl\<fase>.RTX` | texturas de recurso (`res`) do nível | `fileOnlyResFile` |
| `lvl\<fase>.PTR` | lista de correções do arquivo zero (ver abaixo) | `dbsRelocateFileZero` |
| `<monstro><costume>.ngp/.tex/.rtx` | um monstro (modelo, textura, recursos); `<monstro>` vem de `MonsterLongNames`, `<costume>` é o número do traje | `fileReadf(path, getNextNgpLoadAddr/Res/Tex())` |
| `mon\<nome>.ptr` | correções de ponteiros do monstro `<nome>` | `dbsRelocateViaPtrListFile` |
| `shell\shell|load|ui|shella|preshell.ptr` (+ `.ngp/.tex/.rtx`) | telas do shell (menus, loading, UI) | idem arquivo zero |
| `host0:monster.ngp/.tex/.rtx` | caminho de desenvolvimento (PC host), usado quando o índice da fase é >= 0x1D |

Nomes dos monstros (`MonsterLongNames`, 8 bytes por entrada, índice = monstro): `null, monkey, robot, lizard, shogun, mantis, spider,
energy, lava, dragon, rock, alien, ogre, assboss, jelly, final, temp16, default`. A seleção de cada vaga é `(monstro << 5) | variante`
em `Shell::m_monsterSel[]` (4 jogadores, depois IAs); o traje fica em `m_costume[]`. No modo 6 (mini-games?) carrega os monstros 1..5 e 7..11.
Quirk do retail: para o 3º e o 4º jogador o caminho do arquivo usa o monstro do jogador 2.

### Nomes de arquivo (`Shell::formatFilename`, `formatFilename1`)
Todos começam pelo prefixo da raiz do disco (`D_006F81E0`, string vazia no retail), então os caminhos ficam `\LVL\...` e `\MON\...`.
| Chamada | Tipo | Caminho |
|---|---|---|
| `formatFilename(dst, nome, ext, tipo)` | `SH_FILE_0` (fase) | `\LVL\<nome>.<ext>` |
| | `SH_FILE_PLAYER` | `\MON\<nome>.<ext>` |
| | `SH_FILE_AI` | `\MON\<nome>0.<ext>` ou `\MON\<nome>1.<ext>` (abaixo) |
| `formatFilename1(dst, nome, traje, ext, tipo)` | `SH_FILE_PLAYER`/`SH_FILE_AI` | `\MON\<nome><traje>.<ext>` (traje em decimal) |
| | `SH_FILE_0` | `\LVL\<nome>.<ext>` |
| `Shell::formatFilename(dst, dir, nome, ext)` (estática) | arquivos dos point tools no host | `<dir>/<nome>.<ext>` |

Sufixo das IAs em `formatFilename(..., SH_FILE_AI)`: `1` se `m_mode` é 0 ou 1 e `m_levelNum == 3`; senão `0` se `m_mode != 1`; no modo 1, se a
IA da vaga 4 ou 5 usa o mesmo monstro do jogador 1 (`m_monsterSel[0]`), `1` quando o jogador 1 está no traje 0 e **nada é escrito** quando
ele está em outro traje (quirk do retail: o `dst` fica como estava); sem esse conflito, `0`. Ou seja, a IA carrega a outra pele para não
ficar igual ao jogador 1.

O `LoadLevelFiles` procura `<raiz>\LVL\<fase>.NGP;1` e `.TEX;1` no CD (`fileCdSearchFile`) para saber os tamanhos (barra de progresso) e
carrega, nesta ordem: nível (`.ngp`), monstros, texturas de recurso, texturas; depois `dbsRelocateFileZero` e `dbInitDb`.

## Imagem `.NGP`
Pré-ligada para o endereço **0xA00000** (a de nível é descompactada lá). Palavra 0 = N (número de nós-raiz), palavras 1..N = ponteiros
para os nós-raiz da hierarquia (`_hierhead`; opcode nos 6 bits baixos da primeira palavra, id do objeto nos 14 bits altos).

## Correções (`.PTR`)
Três (arquivo zero) ou quatro (monstros) listas, cada uma `contagem` seguida de `contagem` offsets em bytes dentro da imagem (0 = não usado):
- **arquivo zero** (`dbsRelocateFileZero`): as duas primeiras listas são puladas; a terceira lista os doublewords com `TEX0` do GS, e soma
  `vram / 64` ao campo `TBP0` (14 bits, bits 37..50 do doubleword).
- **monstro** (`dbsRelocateViaPtrListFile`, `idx` > 0): (cabeçalho) palavras 1..N += `endereço_real - 0xA00000`; (1) ponteiros (mesma soma);
  (2) ids de textura de 16 bits += maior id do arquivo anterior; (3) palavras do GS: `TBP0` += fim das texturas do arquivo anterior, e as
  marcadas 0x1B/0x2C/0x24 somam ao campo baixo de 14 bits o fim dos recursos do arquivo anterior.

## Ids de objeto do nível (`dbProcInteractive`)
Id = 14 bits altos da primeira palavra do nó. Os nós de opcode 0x1F e 0x27 são sempre destrutíveis.
| Faixa | Significado |
|---|---|
| 1 | raiz do mundo (`viewSetWorldEpNode`) |
| 2 | céu (`viewSetSkyEntry` x4) |
| 18 | textura da fonte |
| 0x20..0x400 | monstros (`TheGame::MonsterParse`) |
| 0xBB8 | grupo de pedestres (`PedGroup`, 0x190 bytes na pilha de memória; byte `(head>>7)&0xFF` = tamanho) |
| 0x7D0..0x897 | ignorados aqui |
| 0xFA0..0x11F7 | armas (`Weapons::AddWeaponEpNode`) |
| 0x11F8..0x12BF | efeitos: 0x11F8/0x1203 `HomingBug`, 0x11FC bola da `OgreMace`, 0x11F9..0x11FA/0x11FD/0x11FE/0x11FF/0x1201 `SpecFxAnim` |
| 0x12C0..0x13EB | pickups (`LevelPickups::createPickup`); o id vira o "hat id" do pickup |
| 0x13EC..0x144F | power-ups |
| 0x1450..0x1B57 | destrutíveis (`Destructibles::AddDestruct`) |
| 0x1B58..0x1C1F | texturas de partícula |
| 0x1C20..0x2133 | elementos do HUD |
| 0x2134..0x2327 | scripts/ações (0x2134/0x2135/0x2136 só em certos modos; 0x2260.. scripts do usuário) |
| 0x2328..0x270F | pontos de fim do shell (`Shell::AddEpNode`) |
| 0x2710..0x3E7F | pontos de fim por fase (threemile, tokyo 0x2743, canyon2, airport, FinalBoss, BigShot) |

Os pickups guardam o id do slot de `Interactives` no `hierhead`: `Interactives::addInteractive` devolve esse índice.

## Malhas (`HierObject::polyPkt`) — decodificado em 2026-10-10
Leitor de referência: `tools/ngp.py` (Python, offline, direto do disco) e `native/wotm_native.hpp` (`decodeObject`, C++, lê a RAM do EE).
Um nó `OBJECT` (`_hierobject`, 0xA0 bytes) aponta com `polyPkt` (+4) para uma sub-cadeia de DMA chamada pelo `pktAddVu1ObjAsm` (tag `CALL`; o pacote termina em `RET`):
- Palavra 0: tag DMA com `QWC` nos 16 bits baixos e `ID=6` (`ret`) em `0x60000000`; o pacote tem `QWC + 1` quadwords. As palavras 2 em diante são um **fluxo VIF** para o microcódigo do VU1.
- **Posições**: `UNPACK V3-32` em `0xB5` (até 190 vértices, floats XYZ). **Normais**: `UNPACK V3-8` em `0x175`, bytes com sinal (/127), 3 por normal sem preenchimento.
- Uma ou mais **seções de strip**, cada uma terminada por `MSCNT`. A seção começa com uma **tag GIF** (`UNPACK V4-32`, 1 elemento, em H = `0x238`/`0x2C8`/`0x358`, alternando por causa do buffer duplo do VU1):
  `NLOOP` (bits 0..14) = número de vértices, `PRIM` (bits 47..57; triângulo-strip com Gouraud, textura e fog) e `REGS` = ST, RGBAQ, XYZF2. Depois, com `STCYCL CL=3 WL=1`, cada vértice ocupa 3 quadwords:
  `S-8 @H+1+3k` **índice** da posição (0..189), `V4-8 @H+2+3k` **RGBA** (`0x80` = 1,0; o alfa também), `V2-16 @H+3+3k` **UV** em ponto fixo (`4096` = 1,0; negativo e fora de [0,1] repete a textura).
- **Quebra de strip (ADC)**: `STMASK 0xBFBFBFBF` seguido de `UNPACK S-8 num=2 @H+3+3k` marca os vértices `k` e `k+1` como "não desenha" (bit ADC do XYZF2): o strip recomeça em `k`. Strips de 8 vértices são comuns (cilindros/tubos). Junções por vértice repetido geram triângulos degenerados (descartar).
- Uma nova `UNPACK V3-32 @0xB5` troca a tabela de posições (objetos grandes têm vários lotes de até 190 vértices).
- `_hierobject.bits.numVerts` (13 bits) é o total de vértices do objeto; `texId[5]`/`numTextures` e `gsTexCtx[i]` (+0x70, 0x30 bytes cada: `TEX0`, `TEX1`, `MIP`) descrevem as texturas. Esfera de colisão/visibilidade em +0x10 (centro) e +0x1C (raio); `sphereRadiusSqrd` em +8.
- Convenções do mundo: vetores-linha (`p' = p * M`), **Z para cima**; matrizes de `ROTATE` em +0x10 (4x4), `TRANSLATE` soma `trans` (+0x10), `SCALE` aplica `scale` (+0x20) depois `trans` (+0x10).
- Nós percorridos pelo port: `GROUP` (filhos em +0x20, contagem u16 em +8), `LOD` (lista de `{fade, switchOutDis, child, pad}` em +0x20; o 0 é o mais detalhado), `SWITCH` (filho `whichChild` em +0xC), `CONTROL` (`child1` em +4), `ACTION_DATA` (filho em +4), `CHAR_INSTANCE` (filhos em +0x2C, contagem em +0x28), `INTERACTIVE` (filho em +0xC), `DESTRUCTIBLE` (estados em +0xC, 12 bytes cada; o 0 é o intacto).

## Texturas e a VRAM do GS (em andamento)
O `TEX0` de cada objeto (`gsTexCtx[0].tex0`) usa o formato do GS (`TBP0` bits 0..13, `TBW` 14..19, `PSM` 20..25, `TW` 26..29, `TH` 30..33, `CBP` 37..50, `CPSM` 51..54, `CSM` 55, `CSA` 56..60). Os pixels chegam à VRAM por uploads GIF `IMAGE` do `.TEX` (e o `.RTX`).
`native/wotm_gs.hpp` decodifica `CT32/CT24/CT16/T8/T4` a partir de um dump da VRAM (4 MB); a paleta de 256 entradas (CSM1) está em memória linear com as entradas 8..15 e 16..23 de cada grupo de 32 trocadas.
O runtime grava `ram_<quadro>.bin` e `vram_<quadro>.bin` com `PS2X_DUMP_RAM="quadro,..."` (`PS2X_NO_GS=1 PS2X_GS_UPLOADS=1` processa só os uploads, sem rasterizar).
