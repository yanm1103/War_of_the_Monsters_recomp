# Exército em qualquer fase (etapa 3): como trazer os veículos de VEGAS para a carga de outra fase (2026-11-04)

Continuação de `militares_spawn.md` (2026-10-25), `militares_ia.md` e `militares_caos_modos.md`. Só investigação: nada de código mudou.
Endereços de `native/wotm_syms.h` (`wotm::fn::` / `wotm::sym::`). Medidas por scripts de rascunho (fora do repo) sobre `tools/ngp.py` e
`tools/tex.py`, lendo `disc/LVL/VEGAS.{NGP,PTR,TEX,RTX}`; código lido: `src/game/{Shell,db,Pickup,Interactives,Weapon}.cpp`,
`src/common/{dbs,file,texm,animation,hd,CsPool}.cpp`, `native/runtime/wotm_{overrides.cpp,mods.inc,army.inc}`.

## Resumo

- **Recomendado:** um arquivo extra no formato de monstro, `ARMY` (`.NGP` + `.PTR` + `.TEX` + `.RTX`), com só os 17 veículos de VEGAS
  (~61 KB de nós, 238 ponteiros, 6 texturas, 6 paletas), anexado às listas do `fileStatus` na carga. O `dbInitDb` faz o resto sozinho:
  reloca (ponteiros, texIds, CBP), registra a animação do rotor e desce a árvore até o `createPickup`. O pool da etapa 1 já pega o que
  está na `activeList`.
- **Não recomendado:** carregar o `VEGAS.NGP` inteiro uma segunda vez (7,71 MB inflado). A relocação dos ponteiros é fácil, mas as
  texturas obrigam a carregar também o `VEGAS.TEX`/`.RTX` inteiros, e o `dbInitDb` desceria a fase toda (raiz do mundo, céu, prédios,
  pedestres, scripts). Seria preciso reescrever o cabeçalho e o gerenciador de animação, ou seja, o mesmo trabalho da recomendada, com
  8 MB a mais.

## 1. Carga e relocação da imagem

### Sequência (`Shell::LoadLevelFiles`, `fn::LoadLevelFiles__5Shell` 0x1AB1A8)

| Passo | Função | O que faz |
|---|---|---|
| 1 | `LoadLevelDB__5Shell` (0x1A9AB0) | `zipInflateAll("\LVL\<FASE>.NGP;1", 0xA00000)` (0x22B620) + `fileOnlyNgpFile` (0x1FADB8): `ngpAddr[0] = 0xA00000`, `ngpLoaded = 1` |
| 2 | `LoadMonstersDB__5Shell` (0x1A9BF0) | cada monstro: `fileReadf` (0x1FA300) em `getNextNgpLoadAddr` (0x1FA820) + `fileAddNgpFile` (0x1FAAF8) + `fileAddName` (0x1FAD20). Com `WOTM_HIGHRAM` o gancho `loadMonstersDBHigh` põe o próximo endereço em 0x2000000 |
| 3 | `LoadResTexture__5Shell` (0x1A9230) | `.RTX` da fase em `getNextNgpLoadAddr` (logo depois do último NGP), monstros em `getNextResLoadAddr` (`fileAddResFile` 0x1FAC30); no fim `texmResInit` (0x205358): `texmResRelocate` de cada arquivo e upload para a VRAM residente |
| 4 | `LoadTexture__5Shell` (0x1A9690) | `.TEX` da fase de novo em `getNextNgpLoadAddr` (por cima do `.RTX`, que já subiu para a VRAM), monstros em `getNextTexLoadAddr` (`fileAddTexFile` 0x1FAB98); no fim `texmInit` (0x2052A0): `texmRelocate` de cada arquivo |
| 5 | `dbsRelocateFileZero` (0x1F8748) | lê `lvl/<FASE>.PTR` e usa **só a terceira lista**: soma `vram/64` ao CBP (bits 37..50) dos `TEX0` da fase |
| 6 | `dbInitDb` (0x12E418) | para cada arquivo `k`: `dbsRelocateViaPtrListFile(k)` (0x1F8410; sai logo com `k == 0`), `animationSetGlobal` (0x1F19F0) das raízes op 7, `dbsTraverse` (0x1F79F8) com `dbProcInteractive` (0x12E5F8) nas outras raízes |

- **`fileStatus`** (`sym::fileStatus` 0x445390, 0x184 bytes): `texAddr[14]` +0x00, `texLoaded` +0x38, `ngpAddr[14]` +0x3C, `ngpLoaded`
  +0x74, `resAddr[14]` +0x78, `resLoaded` +0xB0, `names[14][9]` +0xB1, `maxTexId[14]` +0x130, `maxTexAddr[14]` +0x14C, `maxResAddr[14]`
  +0x168. O elemento seguinte ao último é o próximo endereço de carga, então cabem **13 arquivos** (fase + 8 monstros = 9; eliminação com
  10 monstros = 11).
- **Pré-ligada em 0xA00000:** sim, a da fase e a dos monstros. A da fase nunca muda de lugar, por isso o jogo ignora as listas 0 e 1 do
  `.PTR` dela. A do monstro é relocada pelo `dbsRelocateViaPtrListFile`: as palavras 1..N do cabeçalho e a lista 0 recebem
  `endereço - 0xA00000`, a lista 1 (texIds de 16 bits, pulando os 0) recebe `maxTexId[k-1]`, e a lista 2 (`TEX0`) recebe
  `maxTexAddr[k-1]` no CBP e, nos formatos altos (PSM 0x1B/0x2C/0x24), `maxResAddr[k-1]` no TBP0. O `.PTR` vai para um buffer
  temporário na `MemoryStack::global`, com aviso acima de 0x30D40 bytes.
- **O `.PTR` da fase tem o mesmo formato do de monstro** (três listas: ponteiros, texIds, `TEX0`):

| Arquivo | Inflado | `.PTR` | Lista 0 (ponteiros) | Lista 1 (texIds) | Lista 2 (`TEX0`) |
|---|---|---|---|---|---|
| `VEGAS.NGP` (2 490 549 no disco) | 8 081 392 (7,71 MB) | 231 976 bytes | 51 517 | 3 235 | 3 235 |
| `METRO.NGP` | 15 423 744 | 549 052 | 120 520 | 8 368 | 8 368 |
| `ALIEN0.NGP` (monstro, cru) | 1 414 880 | 164 376 | 40 893 | 97 | 97 |

- **A lista 0 de VEGAS é confiável:** todos os valores caem dentro da imagem, e as palavras fora da lista com valor igual a um alvo
  conhecido estão dentro de pacotes VIF (coincidência de dados). Nas faixas do exército há só 2 delas, as duas nos pacotes do tanque.
  Ressalva: o jogo nunca usa essa lista para a fase, então só vale o que foi medido nas faixas do exército.
- **Segunda imagem inteira, viável na mecânica:** o `zipInflateAll` do jogo serve para qualquer endereço (8 MB na RAM alta). A relocação
  tanto pode ser a do jogo (registrar como arquivo `k` e apontar `mon/<nome>.ptr` para `LVL/VEGAS.PTR` pelos mods; o `.PTR` passa de
  0x30D40, e o buffer temporário cresce sobre a área livre da `MemoryStack`) quanto um laço no nativo (cabeçalho + 51 517 somas). O que
  pesa é o resto (seção 3).

## 2. O que um nó de veículo referencia

Cada veículo é um `GROUP` (op 1) com o id do pickup, filho único de um `TRANSLATE` (op 3, 0x20 bytes, em `GROUP + 0x30`) que dá a
posição em Vegas (y ≈ -3533..-3865). O fecho foi calculado seguindo os ponteiros da lista 0 a partir de cada `GROUP`.

| Veículo (id) | Instâncias | Nós (descida do `dbsTraverse`) | Objetos | texIds | CBP (paleta) | Fecho | Comum a todas |
|---|---|---|---|---|---|---|---|
| Tanque (0x1392) | 8 | 21: GROUP 8, TRANSLATE 4, ROTATE 1, LOD 1, SWITCH 1, OBJECT 3, ATTACHPT 2, POLY_COLL 1 | 3 | 79, 80, 87 | 0x10B, 0x10F, 0x11F | 23 520 bytes | 23 296 |
| Caminhão (0x1391) | 4 | 19 | 3 | 79, 87 | 0x10B, 0x11F | 8 712 | 8 488 |
| Helicóptero (0x1394) | 5 | 41: + CHAR_INSTANCE 1, ANIM_XFORM 5, LOD 2, ROTATE 3 | 11 | 77, 78, 88 | 0x109, 0x10A, 0x120 | 24 664 | 24 440 |

- **Malhas e subnós são compartilhados** entre as instâncias do mesmo tipo. Por instância só mudam ~224 bytes: `TRANSLATE`, `GROUP`,
  `SWITCH` e o grupo de localizadores. Tem de ser assim: o `Pickup::init` escreve no próprio `GROUP` (`numKids = 1`, `whichChild` do
  `SWITCH` = 0) e o `initAfterDbLoad` grava o id de `Interactives` nos bits 7..17 do cabeçalho dele. Para um pool maior que 17, basta
  clonar essas partes.
- **Tudo em 4 faixas contíguas** (60 544 bytes no total), nenhum ponteiro sai delas: +0x6E2DF0..+0x6E39D0 (as 12 partes por instância
  de tanque e caminhão), +0x6E3A40..+0x6E3F40 (as 5 do helicóptero, incluído o último `TRANSLATE`), +0x72B4F0..+0x736E80 (malhas do
  helicóptero e do tanque), +0x746460..+0x748670 (caminhão e curvas do rotor). Juntas: 238 ponteiros, 17 texIds, 17 `TEX0`. A faixa
  única +0x6E2DF0..+0x748670 tem 416 KB, 1 999 ponteiros e só 18 que saem dela (de dados que não são do exército).
- **Ponteiros de fora para dentro (não atrapalham a cópia):** os 17 `TRANSLATE`, 4 `GROUP` da fase que usam o mesmo `LOD` (+0x747600) e
  o `ANIM_MGR` da fase (raiz op 7 em +0x7B4E70, 90 instâncias), que aponta para o `CHAR_INSTANCE` do rotor (+0x72C780).
- **Para fora, nada:** nenhuma tabela global da fase, nenhum cabeçalho do db nem string. Os ids que o `dbProcInteractive` vê na descida
  são o do pickup e dois `ATTACHPT` (0x7E4, 0x7E6, na faixa ignorada 0x7D0..0x897). Não há emissor de partícula (op 22), script (op 41)
  nem grade de colisão (op 15). A colisão é o `POLY_COLL` (op 42) dentro do fecho.
- **Texturas** (todas do `VEGAS.TEX`, TBP0 = 0, gerenciadas pelo cache por texId): 77 T4 32x32, 78 T4 64x64, 79 T8 128x128, 80 T8
  128x128, 87 T4 64x64, 88 T8 64x64, ~41 KB com os cabeçalhos. **Paletas** (`VEGAS.RTX`, CT32): tbp 265 (8x2), 266 (8x2), 267 (16x16),
  271 (16x16), 287 (8x2), 288 (16x16), ~3,3 KB. São os CBP 0x109, 0x10A, 0x10B, 0x10F, 0x11F e 0x120 dos `TEX0`.
- **Animação do rotor:** um `CHAR_INSTANCE`, o mesmo para os 5 helicópteros (id1 0, id2 0), personagem com 1 animação (0..29) e
  5 curvas quantizadas. Os 16 bytes de `AnimNormData` antes de cada curva caem dentro da 4ª faixa. O nó de controle já vem `active`,
  em laço. Quem faz o rotor girar é o `ANIM_MGR` da fase (`animMgrs[0]`, `D_00735690`); nenhuma função de veículo chama a animação
  (sem `animationGetHandle`). Sem gerenciador, o rotor fica parado na pose do arquivo, sem quebrar nada.
- **Sons e armas:** o som vem de bancos (seção 8 de `militares_ia.md`). Os projéteis usam os modelos da fase: 4026 (míssil, tipo 26) e
  4027 (canhão, 30) existem nas 19 fases; o 4030 (metralhadora do helicóptero, 32) falta em CANYON1 e TOKYODM, e o `InitWeapon` cai no
  modelo 4000 (existe em todas).

## 3. As duas saídas

| | A: segunda `VEGAS.NGP` inteira | B: arquivo `ARMY` com a subárvore (recomendada) |
|---|---|---|
| RAM alta | 7,71 MB + `VEGAS.TEX` (1,67 MB inflado, 327 texturas) + `.RTX` (380 KB, 286 pacotes) | ~61 KB NGP + ~41 KB TEX + ~3 KB RTX |
| Relocação | 51 517 ponteiros; `.PTR` acima do limite de 0x30D40 do buffer do jogo | 238 ponteiros pelo `dbsRelocateViaPtrListFile` do jogo |
| Texturas | o `.TEX`/`.RTX` inteiros como arquivo extra: +327 texIds (de 1000) e todas as paletas de Vegas na VRAM residente (risco do `TRYING TO LOAD TOO MUCH TEXTURE`, que sai do jogo) | 6 texIds, 6 paletas (~15 blocos) |
| Descida | o `dbInitDb` desceria Vegas inteira (raiz do mundo, céu, 125 prédios, 700 pedestres, pickups, scripts). Exige reescrever o cabeçalho (16 raízes; o exército precisa de 18) ou pular o arquivo e chamar o `createPickup` à mão | raízes = 17 `TRANSLATE` + 1 `ANIM_MGR` novo: o `dbInitDb` desce só isso |
| Rotor | o `ANIM_MGR` de Vegas registrado anima 90 instâncias à toa, ou é preciso montar um gerenciador à parte | gerenciador próprio com 1 instância |
| Carga | +~1 s (inflar 2,5 MB + 1,67 MB) | desprezível |
| Ferramenta | nenhuma, mas o gancho é maior | gerar `ARMY.*` a partir dos arquivos do usuário |

### Como montar o `ARMY` (B)
- **NGP** (cru, pré-ligado em 0xA00000, como o de monstro): cabeçalho `N = 18`, raiz 0 = `ANIM_MGR` novo
  (`{op 7, numCharInstances 1, charInstance[0] = rotor}`, 12 bytes), raízes 1..17 = os `TRANSLATE`. Depois, as 4 faixas copiadas
  (ou a faixa única de 416 KB, mais simples e igualmente segura) com os ponteiros remapeados. Opcional: trocar a posição dos `TRANSLATE`
  por um ponto neutro (z bem abaixo do chão), porque as coordenadas de Vegas ficam fora de outros mapas. O `hdGetSphereGridBox` prende
  na borda da grade e o pool os estaciona no `InitPlayers`, então não é obrigatório.
- **PTR** (3 listas, convenção de monstro): lista 0 = os 238 ponteiros mais o do `ANIM_MGR` (deslocamentos no arquivo, **nunca 0**: a
  lista 0 não pula zero e somaria no N); lista 1 = os 17 texIds, renumerados para 1..6; lista 2 = os 17 `TEX0`, com o CBP renumerado
  para o tbp das paletas no `ARMY.RTX` (0, 4, ...).
- **TEX / RTX:** os 6 pacotes e as 6 paletas copiados de `VEGAS.TEX`/`.RTX` com texId 1..6 e tbp a partir de 0 (o mesmo formato dos de
  `MON/`; `tools/tex.py` já lê e escreve os pacotes). Conferir o cabeçalho do `.RTX` (o `tex.py` mostra "260 faixas" também nos de monstro)
  e usar os tamanhos de bloco que o `texmRelocate` já conhece da fase, para não somar faixas no `texFirstFile` (`binMap[15]`).
- **De onde sai:** (i) protótipo: script em `tools/` que grava `mods/MON/ARMY.{NGP,PTR,TEX,RTX}` a partir do disco do usuário. O
  `fileCdSearchFile` do `wotm_mods.inc` já serve arquivo novo, e `ngp.py`/`tex.py` conferem o resultado. (ii) Release: o nativo extrai
  na carga a partir de `LVL/VEGAS.*` (inflar o zip `IE\3\4`: deflate cru, que o raylib faz com `DecompressData`, ou o `zipInflateAll` do
  jogo num rascunho na RAM alta). As faixas são fixas no disco retail; um hash delas confere. Nesse caso, o `.PTR` pode ser dispensado
  com um gancho na entrada do `dbsRelocateViaPtrListFile` que reloca no nativo quando `names[k] == "ARMY"` e volta (`pc = $ra`).

### Ganchos da carga (B), todos de entrada
| Momento | Gancho | Ação |
|---|---|---|
| Fim dos NGP de monstro | entrada de `LoadResTexture__5Shell` (0x1A9230) | copiar `ARMY.NGP` para `ngpAddr[ngpLoaded]`, `fileAddNgpFile`, `fileAddName("ARMY")`. Tem de ser antes do `.RTX`/`.TEX` da fase, que carregam em `getNextNgpLoadAddr` |
| Fim dos `.RTX` | entrada de `texmResInit` (0x205358), só durante a carga da fase | copiar `ARMY.RTX` para `getNextResLoadAddr`, `fileAddResFile` |
| Fim dos `.TEX` | entrada de `texmInit` (0x2052A0), só durante a carga da fase | copiar `ARMY.TEX` para `getNextTexLoadAddr`, `fileAddTexFile` |

- `texmInit`/`texmResInit` também rodam nas telas do menu: ligar uma marca na entrada do `LoadLevelFiles` (o port já engancha:
  `wotmLoadFreeze`) e desligar no `dbInitDb`.
- **Índice igual nas três listas:** o `ARMY` tem de ser o arquivo `k` em `ngpAddr`, `resAddr` e `texAddr`. Os laços de monstro das três
  cargas pulam as mesmas seleções (`sel >> 5 >= 0x10`), então anexar no fim de cada lista mantém o alinhamento. Recurso futuro que
  acrescente arquivos (item 8, monstros com arquivos próprios) precisa respeitar a mesma ordem.
- Condição: `poolWanted(m_gameMode)` e fase ≠ VEGAS (Metro e Tóquio podem receber só tanque e caminhão, ou tudo; o `kMaxPool` 48 aguenta
  12 + 17).

## 4. Criar o pickup

- **O que o `dbProcInteractive` faz por pickup:** só `LevelPickups::createPickup(h, *pos, m, false, 0)` (0x181750). O resto vem de dentro:
  - `createPickup`: o portão do modo 1 (o gancho da etapa 1 já finge o modo com `+0x120468 == 0`), o construtor na
    `MemoryStack::global` (tanque 0x2B0, caminhão 0x2A0, helicóptero 0x300) e o `init` virtual (vt +0x5C). Tudo isso pega
    `CsPool::csActivate` (0x1F75B8; sem cs, "No more CSs" e nada), monta o cs na posição e matriz da descida e chama
    `hierSetCsEpNode(cs, GROUP)`. Exige `GROUP` (op 1). Depois: `SWITCH.whichChild = 0`, `initLocators(child[1])`, `numKids = 1`,
    `drawMe 1`, `testCollision 0`, e o `init` do veículo (`TankVehicle`, `VehicleNavigator` sobre a `PathNet::fodder`). Por fim, um
    `ListNode` de 0xC na cabeça da `activeList` (`sym::x_12LevelPickups_s_activeList` 0x2AECA0) e, se `+0x120468` já é 1, o
    `initAfterDbLoad` na hora.
  - `TheGame::InitAfterDbLoad` (0x136F58) → `LevelPickups::initAfterDbLoad` (0x181C20) → `initAfterDbLoad` virtual (vt +0x34) de cada
    pickup ativo. `Pickup::initAfterDbLoad`: `Interactives::addInteractive` (0x147198; o índice vira o id2 do `GROUP`, o "hat id") e
    `hdReparentCsGrid(cs, &cs->trans, 0)` (põe o cs na grade). Os dos veículos (tanque 0x1CF960, caminhão 0x14DB10, helicóptero
    0x13E9C8) somam `Vehicle::setCs`/`HeliVehicle::setCs`, a inicialização do navegador e as `flags`.
- **Com o `ARMY` registrado, nada à mão:** o `dbInitDb` desce as 17 raízes e cada `createPickup` acontece na mesma hora que os da fase,
  antes do `TheGame::InitAfterDbLoad`. O `animationSetGlobal` do `ANIM_MGR` cria um `AnimPlayer` (0x18 na `MemoryStack`) e liga a
  animação do rotor. O `animationSetInstance` troca o id2 do `CHAR_INSTANCE` pelo `m_curDupId` (+0x120448), o que não afeta nada, porque
  nenhum veículo procura animação por id. O pool da etapa 1 os acha na `activeList` no `InitPlayers` (0x1A6D60).
- **Sem registrar o arquivo** (só se um dia as texturas vierem de outro jeito): `dbsTraverse(&raiz, dbProcInteractive, &zero)` por
  guest call para cada `TRANSLATE`, na entrada do `TheGame::InitAfterDbLoad`. Depois dela, o `createPickup` já chama o `initAfterDbLoad`,
  mas o gancho do modo 1 hoje só finge com `+0x120468 == 0`. As texturas continuariam precisando do `fileStatus` (`texInfo` por texId,
  paletas pelo `texmResRelocate`), e por isso B registra.

### Limites (o que os 17 a mais consomem)
| Recurso | Tamanho | Uso conhecido | +17 veículos |
|---|---|---|---|
| `Interactives::s_dbInteractive` (0x7D3620) | 1024 (o `addInteractive` não confere o teto; id de 11 bits) | na carga: prédios + pickups + monstros, ≤ ~420 na fase mais cheia (METRO ~206 + ~195 + 8) | +17. O `Debris` da luta também pega vaga e nunca devolve (risco que já existe) |
| `CsPool::m_csPool` (0x7D4E90) | 1024 cs de 0xB0 | pickups, armas (até 100), partes de monstro, botões | +17, o mesmo que Vegas na aventura já usa. Não medido em runtime |
| `MemoryStack::global` (0x54D950; pool 0x800E90, 512 KB) | 0x80000 | o resto da carga (não medido) | ~12,3 KB de objetos + 17 × 0x10 de nós + 0x18 do `AnimPlayer`; o `.PTR` do `ARMY` (~1 KB) é temporário |
| `texInfo` (0x50F100) | 1000 | 196..293 nas cargas medidas (`NATIVE.md`) | +6 |
| `fileStatus` | 13 arquivos | 9 (versus com 8 monstros), 11 (eliminação) | +1 |
| `animMgrs` (`D_00735690`) | 14 | um por arquivo | +1 |
| VRAM residente | até `lastVramTexAddr` | | ~15 blocos de paleta (~4 KB) |

- Para conferir na primeira corrida, acrescentar à linha `[exercito] pool da fase`: `s_numInteractives` (0x6F8DF8), os cs livres (percorrer
  `x_6CsPool_m_inactiveList` 0x43A2D0) e a folga da `MemoryStack` (campos de `MemoryStack::global`: `low` +4, `end` +0xC, `hi1` +0x10).

## 5. Riscos

- **Desenho:** o nativo desenha pela lista de cs ativos (`CsPool::m_activeList`), não pela árvore da fase. Os veículos não estão em
  nenhuma árvore de mundo (raiz própria no `ARMY`). Colisão e linha de visada usam a grade (`hdReparentCsGrid`). LOD, `SWITCH` e
  `CHAR_INSTANCE` estão dentro da subárvore. As sombras projetadas só usam os bloqueadores do `psBlkLst` (monstros).
- **Rotor:** depende do `ANIM_MGR` próprio no `ARMY`. Sem ele, o rotor fica parado (sem quebra). O `animationGetHandle` para no primeiro
  `animMgrs[i]` nulo: o `ARMY` vem por último e tem gerenciador, então não esconde nada dos monstros.
- **Partículas:** não há emissor na subárvore. Fumaça e explosão (`particleCreateFx`, `particleKillFx`) usam as texturas de partícula
  de cada fase.
- **Projétil do helicóptero:** modelo 4030 ausente em CANYON1 e TOKYODM (cai no 4000).
- **Alinhamento das três listas do `fileStatus`** e as marcas de "carga em andamento" no `texmInit`/`texmResInit` (que também rodam no
  menu).
- **Lista de ponteiros da fase:** o jogo não a usa, então qualquer defeito nela nunca apareceu no retail. Nas faixas do exército a
  medição não achou buraco, mas a ferramenta deve conferir que todo ponteiro da cópia cai dentro dela.
- **Posição de Vegas** nos `TRANSLATE` até o pool estacionar (a grade prende na borda). A ferramenta pode neutralizá-la.
- **Rede, som e alvo** (das outras investigações, sem mudança): tanque e caminhão sem nó do tipo 3 fora de Vegas (o `g_netType` cai no
  tipo 0 ou desiste); helicóptero sem rede em 5 fases com `.FOP` e nas 6 sem; motor do tanque toca o som 0 do banco da fase.
- **`WOTM_HIGHRAM=0`:** o `ARMY` vai para a RAM baixa atrás dos monstros (+~105 KB); o Metro com 8 monstros já passa do teto sem ele.

## Como foi medido
Scripts de rascunho (fora do repo): listas do `.PTR` (contagem e conferência de que os valores caem na imagem); fecho por "pedaços"
(cada alvo de ponteiro até o próximo alvo) a partir dos 17 `GROUP` militares de VEGAS, seguindo só os ponteiros da lista 0; faixas
contíguas do fecho e ponteiros que saem delas; descida com a semântica do `dbsTraverse` a partir dos `TRANSLATE` (ops e ids vistos);
`CHAR_INSTANCE`, personagem e curvas do rotor; ids de arma 4000/4026/4027/4030 nas 19 fases; `tools/tex.py list` em
`VEGAS.TEX`/`.RTX` e `ALIEN0.TEX`/`.RTX`. Nada rodado no jogo.
