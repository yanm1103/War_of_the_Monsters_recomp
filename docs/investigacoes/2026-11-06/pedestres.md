# Pedestres da fase: como o jogo guarda e usa, e o que o editor precisa para controlá-los

Pedido: controle de pedestres no editor de fase (ver as rotas, mover/acrescentar/apagar pontos e ligações, desligar, mudar a
quantidade). Lido antes: `pts_fase.md` (formato do `.PED`, `PathTool::exportPoints`), `objetos_fase.md` (nó 0xBB8 no `.NGP`),
`editor_pc.md`, `MODS.md` ("Level patches", "Level editor") e `native/runtime/wotm_levelpatch.inc` (`cutNets`).

Fontes novas: `asm/game/PedGroup.s` (TU inteira, nada decompilado), `asm/data/game/PedGroup.sdata.s` (constantes),
`src/common/peds.cpp` (`pedsCreate`, `pedsUpdateFOVbox`, `pedsKill`, `pedsTurnOff`), `asm/nonmatchings/common/peds/stampPacketInit*`,
`src/game/db.cpp` (`dbInitDb`, `dbProcInteractive`), `src/game/PathNet.cpp`, `src/game/PathTool.cpp`, chamadores em `tokyo.s`,
`threemile.s`, `Weapon.s`, `ShockWave.s`, `SpecialStates.s`, `ActionDispatch.s`, `TheGame.s`, e `native/wotm_fx.hpp` (desenho).

Scripts de prova (fora do repo, só leem `disc/`), pasta
`C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\peds\`:
- `peds.py [FASE...] [-v]`: por fase, a rede `.PED` (nós, segmentos, larguras, z, w) e os grupos do `.NGP` (id 0xBB8: posição,
  tamanho) ligados como o construtor do `PedGroup` liga (nó mais perto, link 0); conta segmentos sem grupo e grupos que dividem
  segmento. `-v` lista grupo por grupo.
- `dens.py`: densidade de cada grupo (pedestres por 100 × 100 de faixa) e `.PED` idênticos entre fases.
- `kids.py`: o que há embaixo de cada nó 0xBB8 e as palavras do nó.
- `bind_after_cut.py FASE patch.json`: aplica um patch (`lvl.py` `LevelPatch` + `cut_nets`) e mostra a que segmento cada grupo
  passa a se ligar.

Legenda: **[certo]** lido no código (C++ ou asm) e/ou medido nos arquivos do disco; **[hipótese]** não confirmado.

## 1. Resumo

1. Um pedestre não existe sozinho: o jogo cria **grupos** (`PedGroup`), um por nó `TRANSLATE` de id **0xBB8** do `.NGP`, cada um
   com N pedestres (N = byte `(head >> 7) & 0xFF` da palavra de cabeçalho do nó, 1..255). O `.PED` (`PathNet::ped`) só dá a
   **faixa** onde cada grupo anda. [certo]
2. Cada grupo anda **um segmento só**: o construtor pega o nó de `PathNet::ped` mais perto da posição do grupo (qualquer tipo de
   nó, distância 3D) e segue o **link 0** dele. Os pedestres nascem espalhados ao longo do segmento, dentro de `±largura·10` para o
   lado, e ficam nele a luta inteira (forças de volta para o eixo e para dentro das pontas). Não trocam de segmento. [certo]
3. Segmento sem grupo perto = rua vazia. Grupo sem segmento de verdade = pedestres soltos na rede padrão de 2 nós, a z 250. [certo]
4. Nenhum parâmetro por fase além disso: nada no `.MRS`, nada por `m_levelId`; as constantes (velocidade, fuga, raio do
   esmagamento) são globais. Só Tokyo (onda) e Atomic Island (cratera) desligam todos os pedestres no meio da luta. [certo]
5. Limites: **20 grupos** (vetor `s_allPedGroups[20]` **sem checagem**: o 21º sobrescreve o cabeçalho de `PathNet::ped`), **255
   pedestres por grupo** (byte), memória da `MemoryStack::global` (512 KB, sem checagem), 1 sistema de partícula por grupo do
   `fx[100]` comum. [certo]
6. O patch atual não mexe em pedestres de propósito: só corta `PathNet::ped` em volta das caixas sólidas movidas/copiadas. Os nós
   0xBB8 são raízes da imagem sem pai, então o `apply` os recusa como alça ("tem 0 pais"). [certo]
7. Achado no corte atual: quando um segmento se divide em dois pedaços, **só um pedaço recebe pedestres** (o do nó mais perto do
   grupo); o outro fica vazio, e o grupo inteiro se espreme no pedaço que sobrou. Medido com `mods/LevelTest`. [certo] (seção 6)

## 2. Onde os pedestres nascem: o nó 0xBB8 do `.NGP`

**[certo]** `dbProcInteractive` (`src/game/db.cpp`): id `0xBB8` → tira 0x190 bytes alinhados a 16 da `gMemStackDb` (é o mesmo símbolo
`_11MemoryStack$global`) e chama `PedGroup::PedGroup((head >> 7) & 0xFF, pos)` (0x134C18). `pos` é a posição de mundo que o
`dbsTraverse` acumula (inclui a translação do próprio nó: os grupos ficam no meio do segmento, a meia distância das pontas, e
se ligam a ele; com `pos` sem a translação todos iriam para o nó mais perto da origem).

**[certo, medido com `kids.py`]** Nas 19 fases os 59 nós 0xBB8 têm a mesma forma:
- são **raízes** da imagem (lista de raízes do `.NGP`), sem pai, fora do mundo (GROUP id 1) e fora da grade de colisão;
- `TRANSLATE` (op 3) com **um filho `NULL`** (op 9); nada para desenhar;
- palavra 0: bits 0..5 = op 3, bits 7..14 = número de pedestres, bits 15..17 = 0, bits 18..31 = 0xBB8. Translação em +0x10/+0x14/+0x18;
- como não estão embaixo de GROUP, a máscara de modo do `dbsTraverse` (`g_dbsDisableNodeMask`, só GROUPs) não os tira: existem em
  todos os modos.

Construtor (`__8PedGroupUcR8_fvector`), o que importa para o editor:
- registra o grupo em `s_allPedGroups[s_numPedGroups++]` (0x7CFFB0, **0x50 bytes = 20 ponteiros**, sem checagem; o vetor termina
  exatamente em 0x7D0000 = `PathNet::ped`). `s_numPedGroups` (0x6F7B00) é zerado em `TheGame::InitBeforeDbLoad`.
- `pedsCreate(n, 14.0, 16.0, 0x80404040, &pos, 8, 8)`: um sistema de partícula (`fx[]`, desenho tipo 14 "peds") com n sprites de
  14 × 16, 8 variantes de cor e 8 quadros de animação; o primeiro grupo cria também o efeito de sangue compartilhado (0x91).
  `stampPacketInit` fixa `maxParticles` = 256 e o contador do pacote é de 8 bits. Se `fx[]` estiver cheio, `pedsCreate` devolve 0
  e o `scatter` lê o sistema por ponteiro nulo e divide por um valor lido de lá (risco de trap); na carga isso não acontece no
  disco (poucos sistemas ativos), mas um patch com muitos grupos tem de contar.
- três vetores na `MemoryStack::global`: 2 × n bytes (índices ordenados em x e em y, para achar vizinhos) e n × 0x30 (estado de cada
  pedestre: velocidade, força, índices, segmento, estado de fuga). Sem checagem de estouro.
- `+0x180 = max(1, n/15)` e `+0x188 = max(1, n/5)`: quantos pedestres por quadro recalculam vizinhança e quadro de animação (janela
  rotativa).
- liga o segmento: `PathNet::getClosestNode(ped, pos, -1)` (tipo -1 = qualquer; distância 3D; `<` estrito, então entre nós na mesma
  posição ganha o de índice menor), guarda o nó A, o ponteiro para `paths[0]` dele e o nó B = `nodes[paths[0].id]`. **Não olha
  `numPaths`**: nó sem link dá um B lixo (o `cutNets` já tira nós sem link por isso).
- `initPath`: direção A→B (3D, normalizada), comprimento, vetor lateral `crossUp(dir)` (horizontal) e meia-largura = `largura · 10`.
- `scatter`: cada pedestre em `A + dir · rand(0, comprimento) + lado · rand(-meia-largura, meia-largura)`, variante de cor `i % 8`.
- põe `update__8PedGroupPv` no `TaskManager` e chama `PedSound::initPedSound`.

Há um pool de 5 "PathDat" (0x30 cada, +0x10..+0x100, com lista livre em +0x100..+0x105) e 5 ponteiros em +0x110..+0x120, mas só o
primeiro é usado: nenhuma outra função do TU chama `getClosestNode` nem `initPath`. **[certo]** Sobra de um projeto em que o grupo
mudaria de segmento; no jogo, um grupo = um segmento.

## 3. Como andam e morrem

**[certo]** `PedGroup::update` (0x135738), tarefa por quadro:
- fora da vista (`isVisible`: `viewInFOV` da esfera do sistema com raio 800, em cada vista) ou com filme tocando: para o som e não
  mexe em nada (pedestre fora da tela fica parado);
- `handleEnv`: para cada monstro ativo do jogo, se ele está a menos de `fleeDist` (200) + caixa do grupo: com velocidade > 10 e no
  chão, esmaga (`squish`, raio `squashRadius` 15) em volta de 4 locators dos pés (`AnimContact::s_locatorId`) e toca o som; e
  sempre `flee` (empurra os pedestres a 200 para longe);
- flocking com 5 vizinhos (separação 196/750, coesão 1600/200, alinhamento 400/100, força máxima 100, velocidade máxima 30), mais as
  correções de volta para o eixo (`SIDE_CORRECTION` 0,5, fora de `±largura·10`) e para dentro das pontas (`END_CORRECTION` 0,5);
- posição += velocidade · dt em xyz. Não há `hdHatTest` nem nada de chão no TU: **a altura dos pedestres segue a reta A→B** (com a
  inclinação do segmento). [hipótese forte: as forças laterais e de fuga não sobem nem descem o pedestre de forma visível; não
  medido rodando]

Morte e desligamento:
- `squish` → `pedsTurnOff(pos, true)`: sangue e `pos.w = -1`; o pedestre não volta mais na luta (o `update` pula `w < 0`). O
  monstro que esmagou ganha **5 tokens "Peds"** por pedestre (`TokenManager::credit`); no modo Crush (9) o placar são os tokens.
  O `wotm_army.inc` conta cada `pedsTurnOff` como caos para chamar o exército.
- `squishGlobal(pos, r, monstro)` (todos os grupos): armas arremessadas (`Weapon::WeaponHD`, raios 15/30/2r), `ShockWave`, rugido
  sônico (`StateSonicRoar::testConeCollis`).
- `TurnOffAllPeds` (tira as tarefas, mata os sistemas, zera `s_numPedGroups`): `tokyoUpdate` quando a animação da onda passa de 60 %
  (`pedsTurnedOff`), e `threeMileUpdateCountDown` (troca da cratera).
- Cinema (`ActionDispatch` `doCinema`/`endCinema`): `startCinema` zera `numParticles` de cada sistema, `endCinema` devolve
  `newParticles`.
- RESTART da pausa: nenhuma referência a `PedGroup` no `TheGame` fora do `InitBeforeDbLoad`, então os esmagados continuam mortos e o
  patch não é reaplicado (`editor_pc.md`).

Desenho no port: `native/wotm_fx.hpp` (`FX_PEDS`), sprite em pé no Z do mundo, textura `t0289` da `.TEX` da fase (PNG de mod já
funciona, `MODS.md`). Tipos/modelos: **não há tipo de pedestre**; a "variedade" são as 8 linhas de cor da folha (`i % 8`) e os
8 quadros. Mudar o tamanho exigiria trocar os `14.0`/`16.0` do `pedsCreate` (gancho; não existe por fase).

## 4. A rede `.PED` no disco (medido, `peds.py`)

Formato e carga em `pts_fase.md` e `FORMATS.md` ("Stage points", "Path nets"): `InitBeforeDbLoad` → `PathTool` "Ped Path Tool"
(0x705BF0) → `PathNet::ped` (0x7D0000, `u16 numNodes`, nós de 0x30 a partir de +0x10, até 256; link `{u16 nó, u8 largura, u8 queda}`
em +0x12). **Só o `PedGroup` lê `PathNet::ped`** (as únicas referências ao símbolo são o inicializador do `PathTool`, o construtor do
`PedGroup` e o patch nativo). Tipo do nó e queda não são lidos para pedestres; `w` e o byte 0x3C do arquivo não são lidos por ninguém.

| Fase | `.PED` | nós | segm. | largura (×10 = meia-faixa) | z | grupos | pedestres (tamanhos) | segm. sem grupo | obs. |
|---|---|---|---|---|---|---|---|---|---|
| 3-MILE | sim | 10 | 5 | 3..5 | -44 | 5 | 60 (4×10, 1×20) | 0 | |
| AIRPORT | sim | 6 | 3 | 5..6 | -1..3 | 3 | 300 (3×100) | 0 | densidade até 38,7 |
| CANYON2 | sim | 4 | 2 | 1..4 | 7..333 | 0 | 0 | 2 | rede sem uso |
| CAPITOL | sim | 10 | 5 | 3..8 | 0..8 | 0 | 0 | 5 | rede sem uso |
| CENTRAL | sim | 32 | 16 | 4..5 | 0 | 16 | 1420 (4×100, 12×85) | 0 | `w` 45 em 24 nós |
| CRUSH | sim | 4 | 2 | 8 | 0..2 | 2 | 200 (2×100) | 0 | modo Crush: tokens |
| ISLAND | sim | 12 | 6 | 3..10 | -26 | 6 | 550 | 0 | |
| METRO | sim | 16 | 8 | 12 | 0 | 8 | 1000 (8×125) | 0 | |
| MINISF | sim | 6 | 3 | 12..18 | 330..987 | 6 | 1365 (3×255, 3×200) | 0 | **2 grupos por segmento** (455 por rua) |
| TOKYO | sim | 12 | 6 | 8..11 | 0 | 4 | 865 (1×100, 3×255) | 2 | onda desliga |
| TOKYODM | = TOKYO | 12 | 6 | 8..11 | 0 | 2 | 510 (2×255) | 4 | mesmo `.PED`, outros grupos |
| UFO | = ISLAND | 12 | 6 | 3..10 | -26 | 0 | 0 | 6 | rede sem uso |
| VEGAS | sim | 15 | 7 | 5 | 0 | 7 | 700 (7×100) | 0 | nó 4 = cópia do nó 3, sem link |
| BIGSHOT, CANYON1, DBALL, SANFRAN, TRAINING, VOLCANO | não | (2 padrão) | (1) | 250 | 250 | 0 | 0 | — | |

- Total: 13 arquivos `.PED` (11 distintos), 151 nós, 75 segmentos; 59 grupos em 10 fases, 6970 pedestres. Máximo: 16 grupos (CENTRAL),
  32 nós (CENTRAL), 255 pedestres num grupo (MINISF, TOKYO, TOKYODM).
- Todo nó de todo `.PED` tem tipo 0, 1 link, queda 0, e os links formam pares A↔B (exceto o nó 4 de VEGAS, sem link, na mesma posição
  do nó 3; o `<` estrito do `getClosestNode` faz o 3 ganhar). Segmentos de 215 a 1815 unidades.
- Distância grupo → nó ligado: 20 a 812 (o grupo fica perto do meio do segmento).
- Densidade (pedestres / (comprimento × 2·largura·10) × 10⁴): de 2,2 (3-MILE) a 38,7 (AIRPORT); CENTRAL 7..22.
- A rede padrão (sem `.PED`): nós (-100, 0, 250) e (100, 0, 250), largura 250 (meia-faixa de 2500!). Nenhuma fase do disco tem grupo
  sem `.PED`; um grupo acrescentado numa delas espalharia os pedestres num retângulo de 200 × 5000 a 250 de altura.

## 5. O que o patch atual cobre

- `wotm_levelpatch.inc` (`applyFor` na entrada do `dbInitDb` chamado pelo `Shell::LoadLevelFiles`): `PathNet::ped` já está pronta
  (o `InitBeforeDbLoad` vem antes) e nenhum `PedGroup` existe ainda (nascem na travessia logo depois). É **o ponto certo para tudo**
  que o editor quer de pedestres: a rede e os nós 0xBB8 da imagem podem ser editados ali, e o construtor do grupo lê o resultado.
- Hoje o patch só corta a rede (`cutNets`, espelhado em `lvl.py cut_nets`): pedestre só é cortado por parede (caixa que sobe 40),
  com folga de 15; pedaço de 30 ou mais fica; nós sem link saem e os índices são refeitos.
- A lista `objects` só aceita `TRANSLATE/ROTATE` com **um** pai (`apply`, nas duas implementações): os nós 0xBB8 são raízes (0 pais),
  logo não dá para mover um grupo pelo patch hoje.
- Um mod já pode trocar `PTS/<FASE>.PED` inteiro (o `cdRead` de `wotm_mods.inc` corta em 255 pontos), mas o `Check` do editor
  (`lvl.py patch`) lê o `.PED` do disco, não o do mod.
- O editor (`tools/level_editor/`) não mostra pedestres; só repassa as linhas do `cut_nets` no `Check`.

## 6. Achado: o corte deixa pedaços sem pedestres

`bind_after_cut.py CENTRAL mods/LevelTest/levels/CENTRAL.json` [certo, pela mesma regra do construtor]:
- o corte divide 12-13 (605) em 224 + 229 (par novo 32-33) e 14-15 (749) em 413 + 184 (par novo 34-35);
- o grupo A00910 (85 pedestres) passa a se ligar ao **32-33** (229 unidades) e o 12-13 fica vazio; o A00700 fica no 15-14 (413) e o
  34-35 fica vazio. Resultado: 18 segmentos, 16 com grupo; os 85 pedestres de cada grupo ficam espremidos num pedaço 2 a 3 vezes
  menor, de um lado só do prédio.
- Pior caso, não presente nos patches de teste: se o corte tira o segmento inteiro (prédio em cima da calçada toda), o grupo se liga
  ao nó mais perto que sobrou, **de outra rua**, e soma seus pedestres aos do grupo de lá.

Conserto possível (no hook e no `lvl.py`, igual): ao dividir, criar um grupo novo por pedaço extra e repartir N pelo comprimento; e
quando o segmento sai inteiro, apagar o grupo dele (ou deixá-lo explícito no log). Ambos dependem do mecanismo de "grupo novo" da
seção 8.

## 7. O que é editável e como

| Quer | Onde mexer | Quando | Observações |
|---|---|---|---|
| Ver as rotas | ler `PTS/<FASE>.PED` (do mod se houver, senão do disco) e as raízes 0xBB8 do `.NGP` | editor | mostrar a ligação grupo → segmento pela regra do construtor |
| Mover/acrescentar/apagar pontos e ligações | reescrever `PathNet::ped` na RAM (`writeNet`, já existe) | entrada do `dbInitDb`, antes do `cutNets` | até 256 nós; para pedestre só importa o link 0 de cada nó, então o modelo natural é **segmento** (par de nós + largura) |
| Largura da faixa | byte de largura do link | idem | meia-faixa = largura·10; 1..255 |
| Mudar quantidade | bits 7..14 da palavra 0 do nó 0xBB8 | idem | 1..255 por grupo; mais que isso = outro grupo no mesmo lugar (como MINISF) |
| Mover um grupo | translação (+0x10) do nó 0xBB8 | idem | raiz sem colisão nem esfera de GROUP: escrever basta; liga-se ao nó mais perto da posição nova |
| Apagar um grupo | trocar o id 0xBB8 por 0 na palavra 0 (fica um `TRANSLATE` comum) | idem | melhor que N = 0, que ainda gasta um `fx[]`, uma tarefa e um lugar dos 20 |
| Desligar todos | apagar todos os 0xBB8 | idem | a rede pode ficar como está (ninguém mais a lê) |
| Grupo novo | nó `TRANSLATE`#0xBB8 + `NULL` na arena, pendurado no mundo por um GROUP novo (seção 8) | idem | conta no limite de 20 |
| Velocidade, fuga, esmagamento | constantes globais do `.sdata` (`MAX_SPEED` 0x6F7ACC, `fleeDist` 0x6F7AF4, `squashRadius` 0x6F7AFC...) | qualquer hora | valem para todas as fases; por fase só se o hook gravar e restaurar a cada carga (fora do escopo) |
| Tamanho/aparência | `pedsCreate` (14 × 16) / textura `t0289` | gancho / PNG | PNG já funciona; tamanho pede gancho |

## 8. Plano para o editor

### 8.1 Formato do patch (`mods/<mod>/levels/<FASE>.json`)

Seção nova, ao lado de `objects`:
```json
"peds": {
  "off": false,
  "segments": [{"a": [487.0, 234.0, 0.0], "b": [487.0, -334.0, 0.0], "width": 4}],
  "groups": [
    {"handle": "0xA00640", "from": [487.0, -50.0, 0.0], "pos": [500.0, -60.0, 0.0], "count": 120},
    {"handle": "0xA00670", "from": [-29.6, -350.0, 0.0], "delete": true},
    {"pos": [900.0, 1043.6, 0.0], "count": 60}
  ]
}
```
- `segments` (opcional): **substitui** a rede de pedestres inteira (a do disco tem no máximo 32 nós; guardar a lista inteira é mais
  simples e mais robusto que um diff por índice). O hook monta os nós `2i`/`2i+1` ligados um ao outro (tipo 0, queda 0). Até 128
  segmentos (256 nós). Sem `segments`, fica a rede do disco (ou do `.PED` do mod).
- `groups`: as entradas com `handle` + `from` editam um grupo do disco (como em `objects`: o `from` confere que é o mesmo `.NGP`);
  `pos`, `count` (1..255) e `delete`. Entradas sem `handle` são grupos novos (`pos`, `count`).
- `off`: true apaga todos os grupos (do disco e novos).
- Ordem no hook: `objects` → `peds` (rede, depois grupos) → `cutNets` (o corte vale para a rede editada também).

### 8.2 Hook (`native/runtime/wotm_levelpatch.inc`)

Em `applyFor`, depois de `st.apply(v)` e antes de `cutNets`, uma `applyPeds(rdram, st, v)`:
1. `segments` → `writeNet(rdram, kPedNet, ...)` com nós novos (`raw` zerado, tipo 0); `numNodes` = 2 × segmentos (máx. 256).
2. grupos do disco: achar as raízes da imagem com `head >> 18 == 0xBB8` (a lista de raízes está em `img[0..N]` em 0xA00000, como no
   `dbInitDb`), conferir `from`, gravar `count` nos bits 7..14, `pos` na translação, `delete`/`off` → id 0.
3. grupos novos: na arena, um `TRANSLATE` (0x20 bytes: cabeçalho `0xBB8 << 18 | count << 7 | 3`, `numKids` 1 em +8, translação em
   +0x10, filho em +0x1C) e um `NULL` (op 9); pendurados no **GROUP do mundo** (raiz id 1) por um GROUP novo noFOV que segura o filho
   original do slot e os novos, o mesmo truque que o `copy` já usa. A lista de raízes da imagem não cresce no lugar, e o `dbInitDb`
   lê as raízes de `getNgpAddr`, não do argumento, então trocar a lista não dá. O `dbsTraverse` chama o callback para todo nó com id
   e acumula a translação, então o `dbProcInteractive` cria o grupo na posição certa. [hipótese: que o desenho do mundo atravesse o
   `TRANSLATE` com filho `NULL` sem efeito, como já faz com as raízes; conferir na tela]
   Alternativa B: depois do `dbInitDb` original, chamar o construtor do jogo (`wotm::fn::x__8PedGroupUcR8_fvector`) pela
   `wotmFinishGuestCall`, com 0x190 bytes tirados da `MemoryStack::global` como faz o `dbProcInteractive`. Evita mexer no mundo, mas
   pede o hook do `dbInitDb` com volta (hoje ele só chama o original em cauda) e não tem espelho no `lvl.py`.
4. Travas no hook (com linha no log `[fase]`): no máximo **20 grupos** no total (disco que sobram + novos; o 21º corromperia
   `PathNet::ped`); `count` 1..255; grupo cujo nó mais perto tenha 0 links ou cuja rede seja a padrão de 2 nós → recusado
   ("grupo sem calçada"); estimativa de memória (≈ 0x190 + 0x32 · N por grupo) contra o que sobra da `MemoryStack`.
5. Depois do `cutNets`, logar a ligação final de cada grupo (`grupo A00910: 85 pedestres em 32-33, 229`), para o log do jogo mostrar o
   achado da seção 6. Opcional: o conserto da seção 6 (grupo novo por pedaço extra).
6. `wotm_syms.h` já tem `x_7PathNet_ped`, `x_8PedGroup_s_allPedGroups`, `x_8PedGroup_s_numPedGroups` e o construtor.

### 8.3 `tools/lvl.py` (espelho, para o `Check` e o editor)

- `ped_groups(st)`: as raízes 0xBB8 (`node`, `pos` = translação, `count`).
- `ped_bind(net, pos)`: a regra do construtor (nó mais perto em 3D, `<` estrito, tipo ignorado, link 0; `None` se sem link).
- `LevelPatch.apply_peds(patch, nets)`: a mesma ordem e as mesmas travas do hook; grupos novos na arena da imagem, no GROUP do mundo.
- `cmd_patch` e o `Check` do editor: ler o `.PED` do mod quando existir (`mods/<mod>/PTS/<FASE>.PED`), aplicar `apply_peds` antes do
  `cut_nets` e imprimir a ligação final de cada grupo.
- `lvl.py peds <FASE>`: a tabela da seção 4 para uma fase.

### 8.4 Editor (`tools/level_editor/`)

- Servidor (`editor.py`): na carga da fase, mandar `{"net": [...nós com links/largura], "groups": [...], "default_net": bool}`; no
  `Check`, a rede depois de `apply_peds` + `cut_nets` e as ligações finais, para desenhar o que o jogo vai fazer.
- Camada "Pedestrians" (liga/desliga na barra):
  - cada segmento como uma fita no chão com a largura real (2 · largura · 10) e as pontas como alças; cor por estado: com grupo, sem
    grupo ("nobody walks here"), dividido pelo corte, removido pelo corte;
  - cada grupo como um marcador com o número de pedestres e uma linha tracejada até o nó a que se liga (recalculada ao vivo pela
    regra do construtor enquanto o grupo ou um nó se move); aviso quando dois grupos caem no mesmo segmento (válido, é o que MINISF
    faz) e quando o grupo se liga a um nó sem link ou à rede padrão.
- Edição:
  - nó: arrastar (G) no plano XY e **assentar no chão** (o raio para baixo que o editor já usa para "dropped ... on the surface
    below"), porque os pedestres seguem a reta entre as pontas sem olhar o chão;
  - segmento: largura no painel (1..255), apagar (Del), "novo segmento" com dois cliques no chão, "dividir" (clique no meio);
  - grupo: mover, `count` (1..255), apagar, "novo grupo" (clique; o painel mostra a que segmento ele vai se ligar e a densidade
    resultante, de referência 2..39 no disco);
  - fase: "Pedestrians off" e "× quantidade" (multiplica todos os `count`, limitado a 255; acima disso, sugere um segundo grupo).
  - contadores no painel: grupos x/20, pedestres totais, nós x/256.
- Gravar só o que mudou: `segments` só quando a rede foi tocada; grupos do disco por `handle` + `from`.

## 9. Riscos

- 21 grupos ou mais estoura `s_allPedGroups[20]` sobre o cabeçalho de `PathNet::ped` (0x7D0000): o hook tem de travar.
- `MemoryStack::global` (512 KB, compartilhada com pickups e `.PTR` de mod) sem checagem: 20 × 255 pedestres ≈ 250 KB.
- `fx[100]` cheio na carga faz o `scatter` ler por ponteiro nulo e dividir por zero.
- Grupo cujo nó mais perto não tem link (ou rede padrão) anda num segmento lixo ou numa faixa de 5000 a z 250.
- Segmento inclinado ou chão irregular: os pedestres flutuam ou afundam (a altura é a da reta).
- Mudar a quantidade muda os tokens "Peds" (5 por esmagado; placar do modo Crush) e o caos que chama o exército (`wotm_army.inc`).
- O corte de prédios move nós e pode religar um grupo a outro segmento (seção 6); o editor deve mostrar a ligação **depois** do corte.
- Tokyo (onda) e Atomic Island (cratera) apagam todos os pedestres no meio da luta: pedestre posto ali some na hora do evento.
- O RESTART não reaplica o patch nem traz de volta os esmagados.

## Em aberto

- Conferir na tela que um `TRANSLATE`#0xBB8 pendurado no mundo (grupo novo) não afeta o desenho e cria o grupo como uma raiz.
- Se as forças de fuga/flocking mexem no z de forma visível em segmento inclinado (MINISF tem z de 330 a 987).
- Memória livre real da `MemoryStack::global` na carga de cada fase (para a trava do hook usar o número de verdade).
