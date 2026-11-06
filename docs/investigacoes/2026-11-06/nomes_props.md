# Nomes legíveis para os objetos da fase no editor

Tema: o disco guarda algum nome para os prédios, pickups e props? O que dá para nomear sozinho, por objeto, e como guardar os
nomes que o usuário der. Hoje o editor mostra a alça e o tipo (`lvl.py objects`: `destr`/`pickup`/`prop`). Base: `objetos_fase.md`
e `editor_pc.md` (mesma pasta).

Marcação: **[certo]** = lido no código (C++ decompilado ou asm conferido); **[medido]** = contado nas 19 fases de `disc/` pelos
scripts abaixo; **[hipótese]** = dedução não conferida no jogo.

## Scripts (fora do repo)
Pasta `C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\nomes\`
(importam `tools/lvl.py`, `tools/ngp.py` e `tools/tex.py`; só leem o disco):
- `strs.py`: cadeias imprimíveis de cada `.NGP` (inflado), `.PTR`, `.TEX`, `.RTX`, `PTS/*` e `RST/*.MRS`; `words.py` procura extensões de
  arquivo, cadeias C alinhadas e palavras (build, bldg, tree, car...); `ctx.py FASE palavra` mostra o contexto de cada ocorrência.
- `elfgrep.py REGEX`: cadeias do `SCUS_971.97` que casam com a expressão.
- `censo.py [--csv todas.csv]`: um registro por objeto das 19 fases (os de `Stage.objects()`), com a **assinatura de conteúdo**
  (abaixo), tamanho, número de triângulos e texturas, `on`, `hitPointClass` e os ids. 62 s para as 19 fases (METRO 10 s, CENTRAL 6 s).
- `analise.py`, `solta.py`, `hpc.py`, `dids.py`, `cores.py`: as contagens deste relatório a partir do `todas.csv`.

## Resumo
1. **O disco não guarda nome de objeto nenhum** [medido]. Nos `.NGP` não há cadeia legível (o que parece palavra é ruído de vértice:
   "car", "bldg", "node" caem no meio de dados de malha); `.TEX` e `.RTX` só têm o próprio nome do arquivo (`central.tex`) e as
   texturas são números (`texId`); `.PTR`, `PTS/*` e `.MRS` não têm texto. Os ids dos nós também não ajudam: o `id1` do
   `INTERACTIVE_STATE` e do `DESTRUCTIBLE` é 0 em 1561 dos 1582 destrutíveis, a alça (TRANSLATE/ROTATE) tem id 0 em todos, e os props
   têm id 0 (menos um).
2. **O ELF dá nome a classes, não a objetos** [certo]. O que nomeia: o tipo do pickup (`id1 - 0x1388`, 27 tipos) pelas classes que o
   `createPickup` constrói (`CarPickup`, `TankPickup`, `HeliPickup`, `MissileTruckPickup`, `TurretPickup`, `UfoPickup`, `SubwayPickup`,
   `RigidPickup`) e pelos `switch (pickupType)` (tanque-tanqueiro, gerador, dardo, escudo); os 21 destrutíveis com `id1` especial
   (pilhas do reator, prédios do terremoto, pilares); o tamanho pelo `hitPointClass`. As dicas do treino nomeiam dois tipos de pickup
   ("radio tower" = tipo 7, "generator" = tipo 14).
3. **Nome automático bom** [medido]: dos 3744 objetos, **620** (599 pickups com classe certa, dos quais os 87 do tipo 2 com o nome
   "Truck" só pelo tamanho, + 21 destrutíveis especiais). Mais 303 destrutíveis aninhados e 363 pickups `on` ganham "em cima de
   <dono>". O resto (3124) só ganha nome descritivo (classe de vida + medidas).
4. **A chave estável é uma assinatura do conteúdo** (vértices no espaço da alça + pixels das texturas). Ela agrupa cópias do mesmo
   modelo na fase e entre fases: os 900 pickups são **60 modelos** (27 nomes cobrem 90%; 808 pickups têm o modelo também em outra fase);
   os 1255 props com malha são 365 modelos (8 nomes cobrem 50%: postes); os 1555 destrutíveis com malha são 829 modelos (os pequenos
   repetem: 815 dos 1033 de 10 hp estão em grupos; os prédios grandes são quase todos únicos).
5. **Proposta:** `tools/level_editor/names.json` (no repo: tipos de pickup, ids especiais e nomes de modelo por assinatura) + um
   arquivo do usuário com o mesmo formato por cima; o nome por instância vai no patch, numa chave que o jogo ignora (`"names"`). O
   gancho do jogo não muda.

## 1. O que o disco tem

### Cadeias nos arquivos de fase [medido, `strs.py`, `words.py`, `ctx.py`]
- `.NGP` inflado: 192 (TRAINING) a 16052 (METRO) trechos imprimíveis de 6+ caracteres, todos ruído. Nenhuma extensão de arquivo
  (`.tga`, `.max`, `.3ds`, `.tex`...), nenhuma cadeia C alinhada que seja palavra. As ocorrências de "bldg"/"node" (CANYON1, TOKYO)
  e "bill" (SANFRAN) estão no meio de pacotes VIF (`ctx.py`).
- `.TEX`/`.RTX`: o nome do próprio arquivo duas vezes (`3-mile.tex`); o resto é pixel. O `TexPkt` tem `texId`, `tbp` e bits de
  formato, sem nome (`tools/tex.py`, cabeçalho). Os PNGs de mod (`central_t0004.png`) são nomes nossos, pelo `texId`.
- `.PTR`: zero cadeias. `PTS/*`: só ruído de float (`'MusD-u'`). `RST/*.MRS`: zero. `LEVELS.TXT` lista os `.ngp`; `WORLDS.TXT` tem
  `entryA/B/3`.

### Ids dos nós [medido, `dids.py`]
- `DESTRUCTIBLE`: `id1` 0 em 1561, e os 21 especiais iguais aos do `INTERACTIVE_STATE` acima (lista na seção 3).
  `Destructible::takeHit` trata `id1` 0x1A90..0x1B57 como "não perde vida" (`src/game/Destructible.cpp`), mas nenhum nó do disco usa
  essa faixa.
- Alças: `id1` 0 nas 1395 dos destrutíveis e nas 886 dos pickups. Props: `id1` 0 em 1261, um com 0x13F6 (faixa dos power-ups).
- O `id2` só tem sentido nos pickups: o bit 0 escolhe o `CarPickup` nos tipos 2, 3, 15 e 26 (abaixo); o do `INTERACTIVE_STATE` é o
  slot que a carga escreve (`Interactives::addInteractive`).

### ELF [certo, `elfgrep.py`]
- `.mdebug` só tem tipos do `hieri.cpp` (`docs/ANALYSIS.md`); `ePickupType` não tem os nomes dos valores (só aparece em nomes
  mutilados: `createPickup__12LevelPickupsP9_hierheadR8_fvectorPA3_fb11ePickupType`). `LevelPickups::s_info` (0x7C9B90, 28 x 0x54) é
  só número (vida, dano, raio...; `include/game/level_pickups.h`).
- Classes com RTTI (`__tf`/`_vt$`): `CarPickup`, `TankPickup`, `HeliPickup`, `MissileTruckPickup`, `MilitaryPickup`, `TurretPickup`,
  `UfoPickup`, `UfoTokyo`, `SubwayPickup`, `RigidPickup`, `Airplane`, `HomingBug`, `Destructible`.
- Texto do treino (0x5EACA0..0x5EB7D0): duas dicas nomeiam um pickup (a torre de rádio que empala e o gerador que dá choque); as
  outras falam de "objects"/"buildings" em geral, e das pilhas do reator e dos pilares do Goliath. Não copiar o texto para o repo; o
  editor usa nomes nossos.
- Nenhuma mensagem de depuração cita objeto por nome (`Pickup::Init - Found a PickupId on something other than a GroupNode` é a mais
  perto).

## 2. Pickups: o tipo dá a classe [certo] e o tamanho dá o resto [medido]
O tipo é `id1 - 0x1388` do GROUP do pickup (`LevelPickups::createPickup`, `src/game/Pickup.cpp`; o `dbProcInteractive` manda para lá
0x12C0..0x13EB, e o disco usa 0x1389..0x13A3). Contagem nas 19 fases (`analise.py`); "car" = `id2` bit 0; "on" = em cima de outro
objeto (`lvl.py on_top`); "mod." = assinaturas distintas; medidas = mediana, em unidades do jogo.

| tipo | id1 | n | car | on | mod. | medidas | o que o código diz | nome proposto |
|---|---|---|---|---|---|---|---|---|
| 1 | 0x1389 | 231 | 2 | 170 | 15 | 64x48x22 | `Pickup` comum; `kill` com fx 0x20 e som 0x65 | Rooftop object [hipótese: 170 em telhados] |
| 2 | 0x138A | 87 | 28 | 29 | 9 | 36x82x23 | `CarPickup` se bit 0; som de motor 0x1B, o mesmo do caminhão de mísseis (`CarSound`) | Truck [hipótese pelo tamanho e som] |
| 3 | 0x138B | 56 | 4 | 16 | 5 | 39x84x24 | `kill`: onda de choque `tankerShock` e fogo | Tanker |
| 4 | 0x138C | 60 | 0 | 1 | 10 | 42x51x66 | `kill` só toca o som 0x81 (sem explosão) | Object (type 4) [28 no VOLCANO, 14 no ISLAND] |
| 5 | 0x138D | 6 | 0 | 3 | 1 | 62x133x29 | mesmo modelo do tipo 10; o `createPickup` **não constrói** 0x138D | Tank (inactive) |
| 7 | 0x138F | 55 | 0 | 50 | 2 | 63x70x177 | o único que empala (`StateJavelin`, `Weapon::DetonateWeapon`); a dica do treino chama de torre de rádio | Radio tower |
| 9 | 0x1391 | 4 | 0 | 0 | 1 | 68x104x37 | `MissileTruckPickup` (modo 1) | Missile truck |
| 10 | 0x1392 | 8 | 0 | 0 | 1 | 62x133x29 | `TankPickup` (modo 1); formações do CENTRAL/VEGAS | Tank |
| 11 | 0x1393 | 4 | 0 | 4 | 1 | 103x115x33 | `HeliPickup(false)` | Helicopter |
| 12 | 0x1394 | 23 | 0 | 0 | 1 | 103x115x33 | `HeliPickup(true)` (modo 1); som de metralhadora | Military helicopter |
| 13 | 0x1395 | 2 | 0 | 0 | 2 | 337x333x115 | `RigidPickup`, de duas mãos (`turnOff2Handeds`) | Two-handed object |
| 14 | 0x1396 | 24 | 0 | 21 | 2 | 121x85x26 | gerador (`Pickup::updateGenerator`, `Weapon` tipo 0xE); a dica do treino | Generator |
| 15 | 0x1397 | 40 | 16 | 10 | 1 | 32x48x13 | `CarPickup` se bit 0; som de motor do banco da fase em 1, 2, 4 e 6 | Car (type 15) |
| 21 | 0x139D | 9 | 9 | 0 | 2 | 61x60x61 | `RigidPickup` de duas mãos; no DBALL é a bola (`dodgeBallInst`, id1 0x139D) | Dodge ball (DBALL); Rolling rock [hipótese] (CANYON2, 8) |
| 24 | 0x13A0 | 8 | 2 | 0 | 3 | 36x75x34 | `TurretPickup` (modo 1) | Turret |
| 25 | 0x13A1 | 12 | 0 | 0 | 1 | 99x98x28 | `UfoPickup` (modo 1, ou `id2` com o bit de 0x4E840080) | Flying saucer |
| 26 | 0x13A2 | 265 | 133 | 59 | 4 | 23x47x13 | `CarPickup` se bit 0 | Car |
| 27 | 0x13A3 | 6 | 3 | 0 | 2 | 36x118x39 | `SubwayPickup`; som próprio | Subway car |

- Os tipos 6, 8, 16..20, 22 e 23 não aparecem no disco como objeto: saem dos destroços (`Debris.cpp`, `desc->pickup`) ou de código.
  O código conhece 6 (comprido, cinco esferas de colisão), 8 (arco elétrico), 16 (escudo: `StateShieldAttack`), 23 (helicóptero
  militar).
- `id2` bit 0 = **anda no trânsito** [certo]: o `CarPickup::initAfterDbLoad` liga `Vehicle::setCs` e `VehicleNavigator::init`, e o
  `update` dirige; sem o bit, é o `Pickup` parado. Sufixo proposto: "(traffic)" / "(parked)". 181 dos 448 pickups dos tipos 2, 3, 15 e 26 têm o bit.
- 14 pickups não têm alça (CANYON2 8, METRO 6): o editor já os mostra como fixos (`movable: false`); o nome vem igual.
- Total: 599 de 900 pickups com nome de classe certo (todos menos os tipos 1, 4, 13 e os 8 do tipo 21 no CANYON2).

## 3. Destrutíveis: 21 com papel no código, o resto só pelo tamanho

### Ids especiais [certo]
| fase | id1 | papel | onde | nome proposto |
|---|---|---|---|---|
| 3-MILE | 0x2712 | o respiro (vent): só o jogador 1 o derruba | `Destructible::takeHit` | Reactor vent |
| 3-MILE | 0x2715..0x2718 | as quatro pilhas do reator (`resetReactorSoundInfo(id1 - 0x2715)`) | `Destructible.cpp`, `Sound.cpp` | Reactor stack 1..4 |
| 3-MILE | 0x2265 | faixa de script (0x2260..0x2327); só registrado como destrutível | `dbProcInteractive` | Scripted object [hipótese] |
| SANFRAN | 0x275D..0x2762 | os seis que o terremoto derruba: `sanFranSaveInteractive` guarda o slot e `sanFranQuake` dá 100 de dano em cada um; o 0x2762 só cai com 100 exatos | `asm/game/sanfran.s`, `Destructible.cpp` | Quake building 1..6 |
| CANYON2 | 0x2775..0x2777 | pilares que voltam (`rebuildCanyon2Pillars`) | `Destructible.cpp` | Pillar 1..3 |
| CAPITOL | 0x27AE..0x27B3 | pilares do chefe final (`rebuildCapitolPillars`) | `Destructible.cpp` | Pillar 1..6 |

Mexer nesses no patch quebra a lógica da fase se apagar ou copiar (a cópia tem o `id1` zerado, `MODS.md`); o nome serve também de
aviso no editor.

### Classe de vida [certo pelo `AddDestruct`; medidas medidas]
| `hitPointClass` | vida | n | altura (mediana; p10..p90) | modelos | em grupos de 2+ | nome proposto |
|---|---|---|---|---|---|---|
| 0 | a do nó | 355 | 233 (114..575) | 335 | 30 | Building |
| 1 | 10, sem dano mínimo | 1051 | 48 (20..109) | 348 | 815 | Small object |
| 2 | 50 | 109 | 160 (17..710) | 92 | 22 | Building (50 hp) |
| 3 | 100 | 28 | 143 (41..406) | 26 | 2 | Building (100 hp) |
| 4 | 150 | 39 | 518 (328..924) | 39 | 0 | Tower |
| 5, outras | 250, 3000 | 0 | | | | |

- "Building"/"Small object"/"Tower" é nome de categoria [hipótese pelo tamanho], não de objeto. O editor deve acrescentar as medidas
  ("Building 120x90x233").
- 303 estão dentro de outro destrutível (placas e caixas d'água): o editor mostra "on <nome do dono>".
- 27 destrutíveis e 7 props não têm malha no estado 0 (SANFRAN 14, CAPITOL 3, CENTRAL 4...): ficam só com a regra.

## 4. Props: nada no código
Props (`lvl.py objects`, heurística) não têm id nem classe. Só a assinatura agrupa: 365 modelos para 1255; os maiores grupos são
postes (5x5x31, 156 no METRO/TOKYODM/VEGAS; 84 no TOKYO; 74 no CENTRAL; 50 no CAPITOL) e placas finas (2x33x30, 99) [hipótese pelas
medidas]. 185 props passam de 500 unidades no plano: pedaços de terreno. Nome automático: "Prop" + medidas, ou "Terrain piece" acima
de 500 [hipótese].

## 5. Assinatura de conteúdo (a chave que serve entre fases)
**Algoritmo** (`censo.py`, `Sig.of`), versão 1:
1. Pontos de partida: destrutível = o `INTERACTIVE_STATE` (`ngp.walk` já desce no estado 0); pickup = o GROUP do pickup (ou os
   filhos dele, quando o bit visual 0 de `+0xA` está desligado); prop = os filhos da alça (a alça fica de fora).
2. `ngp.walk(img, nó, visita, lod=99)` com a identidade: visita cada OBJECT do LOD mais detalhado com a matriz até ele, no espaço da
   alça. Assim a posição e o rumo da alça não entram (mover ou copiar não muda a assinatura).
3. `ngp.decode_mesh` de cada OBJECT; cada vértice dos triângulos vai pela matriz e é arredondado a 0,1; a lista ordenada vai num
   SHA-1 (12 hex) = **geo**.
4. Para cada triângulo com textura, (`texId`, `TEX0`) do slot (`+0x50 + 2s`, `+0x70 + 0x30s`, como o `lvlglb.py`) → `tex.Library.image`
   → MD5 dos pixels decodificados; o conjunto ordenado vai num SHA-1 (8 hex) = **tex**. Pixel e não `texId`, porque o `texId` muda de
   fase para fase.
5. Assinatura = `geo + tex` (20 hex). Prefixar a versão no arquivo (`"sig": 1`).

**Medido nas 19 fases** (`analise.py`, `solta.py`):
| tipo | objetos com malha | modelos (assinaturas) | só geo | objetos em grupos de 2+ | objetos cujo modelo aparece em 2+ fases | nomes para cobrir 50 / 80 / 90 / 100% |
|---|---|---|---|---|---|---|
| destr | 1555 | 829 | 813 | 889 | 183 (15 modelos) | 108 / 518 / 674 / 829 |
| pickup | 900 | 60 | 59 | 894 | 808 (30 modelos) | 7 / 16 / 27 / 60 |
| prop | 1255 | 365 | 364 | 995 | 275 (3 modelos) | 8 / 114 / 240 / 365 |

- Entre fases, além dos pickups: ISLAND e VOLCANO dividem 14 modelos, CRUSH e METRO 6, CRUSH e SANFRAN 2. TOKYO e TOKYODM dividem 5
  geometrias, mas com texturas diferentes; a assinatura os separa (certo: a pintura é outra).
- 16 geometrias aparecem com mais de um conjunto de texturas (o mesmo carro em outra cor): a assinatura separa, o que é o que o
  usuário quer ("Car (red)" e "Car (blue)"). Uma chave mais solta (`tex` + número de triângulos) junta props diferentes (79 modelos
  para 1255), então não serve.
- Os 187 destrutíveis sem alça têm a malha em coordenadas do mundo: a assinatura deles é, na prática, a da instância.
- A assinatura depende do decodificador (`ngp.decode_mesh`, `tex.Library`). Se ele mudar o que devolve, as assinaturas mudam: daí a
  versão, e a regra de que o editor guarda junto com cada nome o tipo e as medidas (seção 6), para reachar o modelo numa versão nova.

## 6. Esquema proposto

### Ordem de resolução do nome (o primeiro que existir)
1. Nome da instância no patch (`"names"[alça]`, ou `"name"` da cópia).
2. Nome do modelo no arquivo do usuário, pela assinatura.
3. Nome do modelo no `tools/level_editor/names.json` do repo, pela assinatura.
4. Id especial (`"<FASE>:<id1>"`, seção 3).
5. Tipo do pickup (+ "(traffic)"/"(parked)" nos tipos 2, 3, 15 e 26).
6. Regra por tipo: `hitPointClass` do destrutível, "Prop"/"Terrain piece"; sempre com as medidas.
Mais um subtítulo "on <dono>" para os aninhados e os `on`. Na lista, nomes repetidos ganham número na ordem do `objects()` ("Car 3").

### `tools/level_editor/names.json` (no repo) e o do usuário
Mesmo formato nos dois. O do usuário fica com a config (`~/.wotm_level_editor_names.json`, ao lado de `~/.wotm_level_editor.json`)
e passa por cima do do repo; um botão "Export names" grava a união num arquivo para o dono commitar. Só nomes nossos e hashes: nada do
jogo (nem miniatura, nem textura, nem texto do treino).
```json
{
  "version": 1,
  "sig": 1,
  "pickup_types": {"3": "Tanker", "7": "Radio tower", "14": "Generator", "26": "Car"},
  "special": {"3-MILE:0x2715": "Reactor stack 1", "SANFRAN:0x275D": "Quake building 1"},
  "hp_classes": {"0": "Building", "1": "Small object", "4": "Tower"},
  "models": {
    "a4b2493121910ad640af": {"name": "Radio tower", "kind": "pickup", "type": 7, "dims": [62.6, 70.0, 177.1]},
    "dab0453c800161c5cbe2": {"name": "Air conditioner", "kind": "pickup", "type": 1, "dims": [32.6, 27.2, 18.8]}
  }
}
```
(`"Air conditioner"` é exemplo de nome que o usuário dá olhando o modelo; não é achado.) `kind`, `type` e `dims` não decidem nada: servem
para reachar o modelo se a versão da assinatura mudar e para a lista "modelos sem nome" do editor.

### Nome por instância no patch
O jogo e o `lvl.py` só leem `objects[]` com `handle`, `from`, `pos`, `heading`, `delete` e `copies[].pos/heading`
(`native/runtime/wotm_levelpatch.inc`, `Stage::apply`, linha ~813; `tools/lvl.py`, `LevelPatch.apply`); chave desconhecida é ignorada
nos dois, e o `wotm_json.hpp` guarda as chaves que não lê. Então:
```json
{
  "objects": [
    {"handle": "0xB18310", "from": [697.7, 972.2, 0.5], "pos": [1098.2, 664.1, 0.5],
     "copies": [{"pos": [845.9, 1003.2, 0.5], "heading": 90.0, "name": "Hotel (copy)"}]}
  ],
  "names": {"0xB18310": "Hotel", "0xB31DC0": "Gas station"}
}
```
- `"names"` no topo, por alça (ou pelo `node` nos objetos sem alça, como o editor já os identifica). Não vai como `"name"` dentro de
  `objects[]` porque uma entrada só com nome seria validada pelo jogo à toa (`from`) e, com alça que segura vários filhos, ainda
  escreveria a linha "a alca ... segura N objetos" no log.
- Cópia: `"name"` dentro do item de `copies` (ela não tem alça no disco).
- O gancho do jogo **não precisa mudar**. Duas coisas no editor:
  - `editor.py` `/api/save` hoje grava só `if b["patch"].get("objects")` e apaga o arquivo se a lista está vazia: um patch só com
    `names` sumiria. Gravar quando houver `objects` ou `names`.
  - Sempre escrever `"objects": []` junto: sem a lista o jogo loga "sem a lista objects" (`wotm_levelpatch.inc`, linha 816). Com a
    lista vazia ele loga só "nada mudou".

## 7. Plano para o editor
1. `tools/lvlnames.py` (novo; ou dentro do `lvl.py`): `signature(st, obj, lib)` com o algoritmo da seção 5 (copiar o `Sig` do
   `censo.py`), `auto_name(st, obj, rules)` com a ordem 4..6, `load_names(repo_json, user_json)` e `resolve(...)`. Testar contra o
   `todas.csv` do rascunho (as mesmas assinaturas).
2. Cache: as assinaturas por fase em `%LOCALAPPDATA%\WotM\level_editor\<FASE>.sig.json`, com o mesmo carimbo do `.glb`
   (`Stages.glb`, tamanho + mtime do `.NGP` + mtime do script) mais a versão da assinatura. Melhor ainda: calcular dentro do
   `lvlglb.export`, que já decodifica cada malha e cada textura (o custo extra vira só o hash); senão, 1 a 10 s por fase na primeira
   vez.
3. `editor.py` `describe()`: acrescentar `sig`, `class` (regra 4..6, sem os nomes do usuário), `dims`, `name` e `name_src`
   (`instance`/`user`/`repo`/`rule`). Rotas novas: `GET /api/names` (os dois arquivos juntos) e `POST /api/names` (grava o do usuário:
   `{"sig": ..., "name": ...}`, nome vazio apaga).
4. `web/app.js`:
   - Lista: o nome no lugar da alça (a alça em cinza ao lado); o filtro procura no nome, na classe e na alça; um filtro "Unnamed
     models".
   - Painel da direita: campo **Name** (instância: vai para `patch.names[alça]` ou para o `name` da cópia, e entra no Undo e no
     "Changed") e campo **Model name** ("Name all N like this", com N na fase e M em outras fases, pelo `sig`), que chama o
     `POST /api/names`.
   - Rótulo 3D opcional sobre o selecionado.
5. `lvlglb.py`: pôr `sig` no `extras` de cada nó de objeto, para o clique já saber o modelo.
6. Documentação (quando entrar): `docs/guides/MODS.md`, "Level patches" (a chave `names` e o `name` das cópias, que o jogo ignora) e
   "Level editor" (os campos e os dois arquivos de nomes); `docs/FORMATS.md`, "Stage objects": o tipo do pickup por `id1` e o bit 0 do
   `id2`, com a tabela da seção 2 (os nomes de classe com evidência), e os ids especiais da seção 3.
7. Semente do `names.json` do repo: `pickup_types`, `special` e `hp_classes` com os nomes das seções 2 e 3 (são nossos); `models`
   começa vazio e o dono preenche olhando no editor (27 nomes dão 90% dos pickups; 8, metade dos props).

## Conta final [medido]
| | objetos | nome de classe certo | só descritivo | modelos para o usuário nomear |
|---|---|---|---|---|
| pickup | 900 | 599 | 301 (tipos 1, 4, 13; 8 do tipo 21) | 60 |
| destr | 1582 | 21 (ids especiais) | 1561 (classe de vida + medidas) | 829 (+27 sem malha) |
| prop | 1262 | 0 | 1262 | 365 (+7 sem malha) |
| total | 3744 | 620 | 3124 | 1254 |

Com os nomes por modelo, cada nome dado vale para todas as cópias na fase e nas outras: nomear os 60 modelos de pickup nomeia os 900;
os 1033 objetos de 10 hp com malha são 348 modelos (815 deles em grupos de 2+). Os prédios grandes (hpclass 0 e 2..4: 531
objetos, 522 com malha, 492 modelos, só 54 objetos em grupos) pedem nome um a um, por instância ou por modelo, dá no mesmo.

## Riscos
- Assinatura x decodificador: mudança no `ngp.decode_mesh`/`tex.Library` muda as assinaturas; guardar `kind`/`type`/`dims` em cada
  modelo e ter um passo de migração (recalcular as duas versões do disco e casar).
- Mod que troca o `.NGP` da fase: as alças mudam e os `names` por alça do patch deixam de casar (como o resto do patch, que já
  confere `from`); os nomes por modelo continuam valendo para os modelos que o mod não mudou.
- Os nomes "Truck", "Rooftop object", "Building", "Tower", "Rolling rock", "Terrain piece" são categorias por tamanho
  [hipótese]; o editor deve deixá-los com cara de regra (cinza/itálico) até alguém confirmar.
- Os nomes por instância só existem no patch de um mod: dois mods com o mesmo `levels/<FASE>.json` não juntam os nomes (o editor
  edita um mod por vez).
