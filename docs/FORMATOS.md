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
- Nós percorridos pelo port: `GROUP` (filhos em +0x20, contagem u16 em +8), `LOD` (lista de `{fade, switchOutDis, child, pad}` em +0x20; o 0 é o mais grosseiro e o último o mais detalhado; o jogo escolhe o maior índice cujo `switchOutDis`, uma distância ao quadrado, é maior que a distância ao centro do LOD), `SWITCH` (filho `whichChild` em +0xC), `CONTROL` (`child1` em +4), `ACTION_DATA` (filho em +4), `CHAR_INSTANCE` (filhos em +0x2C, contagem em +0x28), `INTERACTIVE` (filho em +0xC), `DESTRUCTIBLE` (estados em +0xC, 12 bytes cada; o 0 é o intacto).

## Texturas e a VRAM do GS (em andamento)
O `TEX0` de cada objeto (`gsTexCtx[0].tex0`) usa o formato do GS (`TBP0` bits 0..13, `TBW` 14..19, `PSM` 20..25, `TW` 26..29, `TH` 30..33, `CBP` 37..50, `CPSM` 51..54, `CSM` 55, `CSA` 56..60). Os pixels chegam à VRAM por uploads GIF `IMAGE` do `.TEX` (e o `.RTX`).
`native/wotm_gs.hpp` decodifica `CT32/CT24/CT16/T8/T4` a partir de um dump da VRAM (4 MB); a paleta de 256 entradas (CSM1) está em memória linear com as entradas 8..15 e 16..23 de cada grupo de 32 trocadas.
O runtime grava `ram_<quadro>.bin` e `vram_<quadro>.bin` com `PS2X_DUMP_RAM="quadro,..."` (`PS2X_NO_GS=1 PS2X_GS_UPLOADS=1` processa só os uploads, sem rasterizar).

## Animação (curvas no `.NGP` e o mixer em tempo de execução)
Fonte: `src/common/AnimCurve.cpp` e `src/common/animation.cpp` (TU inteiro em C++; as equivalentes ainda não passaram por difftest/oracle, então o que depende só delas está marcado (?)).
Os nomes de struct e de campo vêm de `include/hieri_types.h` (gerado dos stabs); aqui está o que os valores significam.

**Dados (no `.NGP`):** `CHARACTER_INSTANCE` (`_animCharInstance`) -> `character` (+0x20, `_animCharacter`: `numAnims`, `numChannels`, `animations[]`) -> cada `_animation` (`startTime`/`endTime` em quadros da
animação, `numChannels` curvas em `channels[]`). Cada curva escreve um **canal** (`AnimCurveHeader.dataIdx`): um float no vetor `animOutput.val` da instância, que os nós `ANIM_TRANSFORM`
(`rXidx`..`tZidx`, `visidx`) leem para montar as matrizes. `AnimChannelIdMap` (ordenado por `id`, busca binária) leva um id de canal ao índice. `angularChannelBits` marca os canais que são ângulo
(normalizados por `boundEulerAngle` antes de misturar): com até 16 canais os bits ficam nos 2 bytes do próprio campo; acima disso o campo é um offset a partir do `_animCharacter`.

**Curva** (`AnimCurveHeader`, 4 bytes): `curveType` (`kHermite` 0, `kBroken` 1, `kPwl` 2 = linear com chaves igualmente espaçadas, `kStatic` 3, `kSmooth` 4, `kStepped` 5), `dataType`
(`kFloat` 0, `k16bit` 1, `k8bit` 2), `preInfinity`/`postInfinity` (`kConstant` 0, `kLinear` 1, `kCycle` 2, `kCyclePlusOffset` 3, `kOscillate` 4), `dataIdx`. Depois do cabeçalho: `u16 numKeys` e as chaves.
- `kStatic`: um float logo depois do cabeçalho, sem chaves.
- Chaves por tipo (`T` = float, u16 ou u8; `S` = float ou código de 16 bits): `kPwl` `{T value}` (o tempo da chave `i` é `baseTime + i*deltaTime`); `kStepped` `{T time, T value}`;
  `kSmooth` `{T time, T value, S tangent}`; `kBroken` `{T time, T value, S tangent[2]}` (tangentes de saída/entrada do segmento); `kHermite` `{T time, T value, S coef[3]}` (coeficientes cúbico, quadrático e linear do segmento; só existe com float).
- **Quantizadas** (`k16bit`/`k8bit`): as 16 bytes **antes** do cabeçalho são um `AnimNormData` `{baseTime, deltaTime, baseVal, deltaVal}`; tempo = `base + q*delta`, valor idem.
- **Código de tangente** (s16): `s >> 14` = 0 ou -1 -> `s / 16384`; 1 -> `16384 / (32768 - s)`; -2 -> `16384 / (-32768 - s)` (as duas últimas cobrem inclinações íngremes).
- Fora do intervalo das chaves: `kConstant` repete o valor da ponta; `kCycle` repete a curva; `kCyclePlusOffset` repete somando a diferença entre as pontas a cada volta; `kOscillate` vai e volta;
  `kLinear` **devolve 0** no nosso C++ (as instâncias de `animCurveEval` são equivalentes, não idênticas) (?).
- A busca da chave começa no índice guardado em `AnimControlNode::prevKey[canal]` (cache por canal).

**Tempo:** a unidade de tempo do jogo é o **field** (`timerGetFieldCount`, 60 por segundo). Um `_animControlNode` (opcode 0x21, um por animação, em `animCtx[i]` da instância) tem
`tempo = startTime + deltaTime * (fieldAtual - startField)` enquanto `active`; pausado, `startField` guarda os fields já decorridos. `deltaTime` é quadros de animação por field (negativo = de trás para
frente; `animationSetTotalRunFrames(h, n)` faz durar `n` fields). Ao passar do fim: com `loop`, volta ao começo e incrementa `iterations`; sem `loop`, para no fim (e pausa se for a árvore principal).

**Mixer por instância** (`activeTree` +0x24 aponta um `AnimPlayer` de 0x18 bytes):
- `mainTree`: a animação principal, um `_animControlNode` ou um `_animBlendNode` (opcode 0x22) de transição entre a anterior (`blendFrom`) e a nova (`blendTo`). Uma transição durante outra
  encadeia: a antiga fica como `blendFrom` da nova e o tempo dela cai para 40%.
- `activeList`: blends extras em lista duplamente ligada, ordenada por `priority` crescente, aplicados **depois** da principal, por cima do resultado dela. `mainTreePureOut` guarda a saída da principal
  sem eles. Nós de procedimento (opcode 0x2B, `_animprocnode`) chamam `procCallback(nó, instância)`.
- `blendType` (`AnimBlendTypes`): `TRANSITION` 1 (interpola de `blendFrom` para `blendTo`; terminado, o nó é recolhido e devolvido ao pool), `FREEZETRANS` 7 (transição que põe o destino na
  porcentagem atual dele e, no fim, devolve o `deltaTime` guardado e o reativa) (?), `STATIC` 2 (mistura o destino sobre o que já está no canal com peso `weight`), `OVERRIDE` 3 (**escreve `destino * peso`** no canal, sem misturar) (?),
  `ADDITIVE` 4 (soma `peso * curva`), `PROCEEDURAL` 6 (função vazia no retail), `SUBTRACTIVE` 5 e `NONE` 0 zeram o peso.
- Peso no tempo: `r = (ms desde blendStartField) / blendTime` (`blendTime` em **milissegundos**, field = 16,667 ms), passado pela curva global `s_blendCurve` (0 linear, 1 cosseno, 2 `r²`, 3 `r(2-r)`).
- Os blends vêm de um pool global de **128** `_animBlendNode` com pilha de índices livres; os buffers temporários de cada mistura saem da pilha da scratchpad (`0x70000000`, 16 KB).
- Busca de animação: `animationGetHandle(id1, id2, animId)` percorre os gerenciadores de animação de cada `.NGP` carregado (`ANIMATION_MGR`, `charInstance[]`), acha a instância pelos ids do
  `HierHead` e a animação cujo `head.id1 == animId`. O handle (`ci`, `ctrl`, `proc`, `animIdx`) é o que o código de jogo guarda (os monstros têm uma tabela deles, ver `Monster::m_anims` em `include/game/game.h`).

## Destrutíveis (`INTERACTIVE_STATE` + `DESTRUCTIBLE`)
Fonte: `game/Destructible` (`Destructibles::AddDestruct`, `Destructible::takeHit`, `GenericTakeHit`, `ChangeState`, `SwitchState`; o TU ainda está em asm, lido à mão) e a travessia `hierTraverseAsm`
(asm manuscrito, em `asm/common/texm.s`). A estrutura dos nós foi conferida nas 19 fases de `LVL/` (script fora do repo).

**No disco:** todo `DESTRUCTIBLE` (op 0x1F) é filho único de um `INTERACTIVE_STATE` (op 0x27): `_interactivestate.child` (+0xC) aponta para ele (1566 de 1566 nas fases). O `DESTRUCTIBLE` tem
`hitPointClass` (+4), `hitPoints` (+6), `hpThreshold` (+8), `numKids` (+0xA) e `numKids` estados de 12 bytes a partir de +0xC: `{child, actions, damageThreshold (u16), behaviorType (u16)}`.
O estado 0 é o intacto. `behaviorType` vale 0 em todos os 3426 estados das fases (não usado). `whichChild` do `INTERACTIVE_STATE` vale 0 no disco.

**O que desenhar (o que o nativo lê da RAM):** a travessia guarda o `INTERACTIVE_STATE` ao passar por ele e, no `DESTRUCTIBLE` logo abaixo, desce só para
`child[whichChild].child`, isto é, `u32(destr + 0xC + 12 * u32(istate + 4))`. O estado atual é **`_interactivestate.whichChild` (+4) do pai**, escrito pelo jogo em tempo de execução.

**Vida** (`Destructibles::AddDestruct`, um objeto `Destructible` de 0xA0 bytes por destrutível, até 1500): pelo `hitPointClass` do nó:
| `hitPointClass` | vida (`+0x14` e máxima em `+0x18`) | dano mínimo (`+0x1C`) |
|---|---|---|
| 0 | `hitPoints` do nó | `hpThreshold` do nó |
| 1 | 10 | 0 (e limpa o bit 3 das flags em `+4`) |
| 2, 3, 4, 5 | 50, 100, 150, 250 | 5 |
| 6 ou mais | 3000 | 5 |
Nas fases aparecem as classes 0 (349), 1 (1041), 2 (109), 3 (28) e 4 (39). O `Destructible` guarda ainda o nó (`+0x24`), o `INTERACTIVE_STATE` (`+0x28`), o índice em `Interactives` (`+0x2C`,
também gravado nos bits 7..17 da primeira palavra do `INTERACTIVE_STATE`), `hitPointClass` (`+0x30`), a posição (`+0x40`), a matriz (`+0x50`) e o estado pedido (`+0x90`).

**Regra de troca** (`takeHit` -> `GenericTakeHit` -> `ChangeState`):
1. Um golpe só tira vida se o dano for **maior** que o dano mínimo (ou se o `HitEvent::s_takeHitInfo` tiver `source` 5 e `subtype` 15 ou 16). Exceções por id: 0x1A90..0x1B57 não perdem vida; na fase 6 (`TheGame::m_levelId`, Three Mile), os ids
   0x2712 e 0x2715..0x2718 (respiradouro e chaminés) só aceitam dano de monstro do jogador (?); na fase 7 (San Francisco), o id 0x2762 só aceita dano exatamente 100.
2. `q = (int)vida * 65535 / (int)vidaMáxima` (fração da vida em 1/65535). O novo estado é o **primeiro** `i` com `damageThreshold[i] < q`, mas o laço para em `numKids - 1` e em
   `estadoAtual + 1`: **um golpe avança no máximo um estado**. Nas fases os limiares típicos são `62258` (95%), `49151` (75%), `32767` (50%) e `0`; `65534` faz o estado mudar no primeiro dano.
3. Se `i` passou do atual: grava `+0x90 = i`, executa as `actions` do estado `i`, derruba os monstros que estavam em cima (`shedMonsters`), dá 50 tokens ("Destructibles") ao monstro que bateu e
   agenda `SwitchState` no `TaskManager` (atraso 2), que copia `+0x90` para `whichChild`. Ou seja, **o desenho muda alguns quadros depois do golpe** (?).
4. Voltar para um estado menor grava `whichChild` direto: é o que `rebuildCanyon2Pillars`/`rebuildCapitolPillars` fazem com `ChangeState(0, 1, 0)` (reconstroem os pilares).
5. Com o terceiro argumento de `ChangeState` ligado, o jogo também põe em 1 o `whichChild` de um `SWITCH` no começo do novo estado (o próprio `child` ou o primeiro filho de um `GROUP`), e
   a vida vai para `damageThreshold[i] * vidaMáxima / 65535 - 0,001`.
