# Props do Blender: passo 0 (medições) e conversão do passo 1

Diário dos passos 0 e 1 (metade "conversão") de `objetos_blender_plano.md`. Tudo em Python, lido do `disc/`; nada no jogo ainda
(o `apply_props` e o editor vêm depois). Scripts de medição na pasta de rascunho (`props/medir0.py`, `medir0b.py`,
`medir_grupos.py`, `medir_cv.py`, `medir_tex.py`, `medir_misc.py`, `numverts.py`, `r9.py`, `r9diff.py`, `r8a.py`, `mkcube.py`,
`alturas.py`, `clamp.py`, `cullmon.py`), fora do repo. Ferramenta nova: `tools/lvlprop.py` (`build`, `info`, `load_prop`).

## Passo 0

### 1. Gabaritos por fase (regra `first-object-roundtrip`)
Ida e volta do `vifpkt` nos pacotes rígidos (todos os OBJECTs da imagem):

| Fase | rígidos | passam | falhas |
|---|---|---|---|
| CENTRAL | 2107 | 1965 | 75 "section before positions", 67 ADC de 3 elementos |
| VEGAS | 2030 | 1674 | 302 + 52, 2 com bytes diferentes |
| UFO | 1513 | 1502 | 9 + 2 |
| METRO | 5750 | 4648 | 976 + 126 |

Bits dos que passam, sob a raiz do mundo: neblina em todos os grupos grandes (PRIM.FGE 1, `fogBurnThru` 1,0), `dirLight` 0,
`dontCastShadow` 1 em todos, `dontShadowMe` 1 em ~90% (0 no resto), ABE 1 na maioria (CENTRAL 439 x 71 com ABE 0 entre os
candidatos, VEGAS 413 x 69, UFO 191 x 48, METRO 2022 x 198).

Regra que ficou no `lvlprop` (o primeiro por endereço sob a raiz do mundo): rígido, ida e volta exata, 1 textura, só seções de
tira com uma só palavra 1 da tag GIF, FGE 1, TME 1, ABE pedido (padrão 1, cai para o outro se não houver), `fogBurnThru` < 255,
sem `alphaSort`, `dirLight`, mapa de ambiente nem UV scroll, sem animação de cor/UV (+0x30..+0x3A = -1), `+0x68 = -1`, e nenhum
ANIM_XFORM, CHAR_INSTANCE, SKEL_BONE, ANIM_SCALE nem nó de id 2 (céu) no caminho. Sem a última condição a primeira passada
achava OBJECTs debaixo de CHAR_INSTANCE (CENTRAL 0xA91270 sob `CHAR_INSTANCE#2200`).

Gabarito ABE 1 de cada fase (palavra de bits em +0xC; 0x200 `dontShadowMe`, 0x80 `pointLight`, 0x10 `simpleObj`,
0x20000000 `dontCastShadow`, numVerts nos bits 16..28):

| Fase | candidatos ABE 0 / 1 | gabarito ABE 1 | bits |
|---|---|---|---|
| 3-MILE | 52 / 165 | 0xA769C0 | 0x200A0290 |
| AIRPORT | 56 / 110 | 0xA8B510 | 0x206E0090 |
| BIGSHOT | 0 / 0 | nenhum | |
| CANYON1 | 158 / 367 | 0xA1C140 | 0x200E0290 |
| CANYON2 | 0 / 63 | 0xC0A380 | 0x20434210 |
| CAPITOL | 83 / 357 | 0xCF5C50 | 0x20440010 |
| CENTRAL | 71 / 439 | 0xB08290 | 0x20210090 |
| CRUSH | 42 / 145 | 0xA48150 | 0x20040090 |
| DBALL | 74 / 26 | 0xA35640 | 0x20060290 |
| ISLAND | 137 / 729 | 0xA89550 | 0x20100290 |
| METRO | 198 / 2022 | 0xA997F0 | 0x20040090 |
| MINISF | 87 / 544 | 0xA482B0 | 0x200A0290 |
| SANFRAN | 92 / 298 | 0xAD47D0 | 0x20240290 |
| TOKYO | 51 / 520 | 0xB4A050 | 0x20080210 |
| TOKYODM | 34 / 764 | 0xA16FD0 | 0x20120210 |
| TRAINING | 0 / 0 | nenhum | |
| UFO | 48 / 191 | 0xC75A60 | 0x20480290 |
| VEGAS | 69 / 413 | 0xAB0850 | 0x203E0090 |
| VOLCANO | 70 / 254 | 0xA619A0 | 0x20040290 |

Tags GIF dos gabaritos: palavra 1 0x303E4000 (tira, Gouraud, textura, neblina, ABE) ou 0x301E4000 (ABE 0), palavra 2 0x412
(ST, RGBAQ, XYZF2), palavra 3 = 1 na primeira seção e 0 nas outras. Com ABE 0: CENTRAL 0xB06CC0 (0x200C0210), VEGAS 0xAB7AF0
(0x20141290, com `backFaceCull`), UFO 0xC75810 (0x200C0290), METRO 0xA9CB20 (0x200A0210).

Ponto aberto para o dono: o "primeiro" decide `dontShadowMe` e `pointLight` do prop, e eles variam (CENTRAL, VEGAS e METRO
saem com `dontShadowMe` 0; UFO com 1). BIGSHOT e TRAINING não têm gabarito: o `build` recusa.

Outras medições que a conversão usa:
- texId x ctx: cada texId tem um só ctx no disco (CENTRAL 0 de 264 com mais de um, VEGAS 0 de 280); "o primeiro OBJECT com aquele
  texId" não é ambíguo. ABE por texId é misto em 90 de 260 (CENTRAL) e 81 de 274 (VEGAS): o ABE vem do gabarito, não da textura.
- OBJECTs sem textura existem (CENTRAL 33, VEGAS 82): palavra 1 0x30364000 (a do gabarito ABE 1 sem TME) e seletor 0 em todas as
  seções. O `lvlprop` faz igual para material sem imagem.
- Lote depois do primeiro: a primeira seção recarrega a textura (seletor 1 + 3k) em quase todos (CENTRAL 154 de 163 lotes com textura); o
  `ngprig.rigid_object` usa 0 ali, o `lvlprop` segue a fase (1 em todo lote).
- ctx do OBJECT: TEX0_1, TEX1_1 e MIPTBP1_1 (0x34) ou 0; nenhum CLAMP. O GROUP do mundo não tem ctx (0) no CENTRAL, VEGAS, UFO e
  METRO, então o aviso de UV fora de 0..1 com CLAMP nunca dispara nessas fases (o CLAMP vem do ctx padrão do jogo, não medido).
- `numVerts` = soma dos vértices das seções em 1709 de 1709 OBJECTs de tira do CENTRAL (os 178 outros são de sprite).
- Folhas de colisão: pai GROUP 0x0102 em 212 de 220 POLY_COLL do CENTRAL (216 de 269 no VEGAS); caminhos de prédio começam em
  GROUP 0x0003 e terminam em 0x0102. Pai GROUP dos OBJECTs: 0x0001 (1562) ou 0x0003 (14).
- COLL_VOL (op 10) nas fases: 3-MILE 5, AIRPORT 2, CAPITOL 2, TOKYO 1, zero nas outras (CENTRAL, VEGAS, METRO e UFO não têm
  nenhum). Todos com dentro = `n . p < d`, `useNormal` -1, `plExclude` 0, sem `actTree`, matId 1 (3-MILE, CAPITOL), 518 (AIRPORT)
  e 3329 (TOKYO). Os dos monstros (ROBOT0, LIZARD0): 6 planos na ordem +z, +y, -z, -y, +x, -x, matId 1, como o `ngprig` grava. A
  caixa do prop usa esse leiaute. Risco para o R5: o CENTRAL não tem casco nenhum, a prova no jogo é a primeira.
- Culling: bit 15 do seletor em 412 de 5338 seções rígidas do CENTRAL; 0 nas seções skinned dos monstros. O gabarito não tem culling.

### 2. R9: o gabarito recodificado dos seus triângulos
Nos 8 gabaritos (4 fases x ABE 0 e 1) os bytes **não** batem. A diferença está só nas tiras: o empacotador do jogo fez outras
tiras (outra ordem e outros cortes) que o `ngpmesh._strips` guloso. A primeira palavra diferente é a 0 (o QWC da tag DMA, porque
o tamanho muda) ou a 3..6 (NLOOP e índices). Tamanhos: CENTRAL 688 -> 816 bytes, 352 -> 352; VEGAS 1248 -> 1280, 512 -> 544; UFO
1568 -> 2048, 352 -> 384; METRO 160 -> 160, 304 -> 336.

O que bate:
- As **mesmas tiras do disco** com as tags GIF refeitas pela regra do `lvlprop` (NLOOP, seletor 1 na primeira seção de cada lote,
  0 nas outras, palavras 0..2 do gabarito): byte a byte nos 8.
- O multiconjunto dos triângulos decodificados (bits da posição, normal, UV, RGBA e ordem dos cantos) do pacote novo é igual ao
  do original nos 8.
- Cabeçalho do OBJECT: diferem só +0x4 (polyPkt), +0x50 (o texId vira o slot) e, quando as tiras mudam, +0xC (numVerts: 33 -> 44
  no CENTRAL) e o raio (+0x8, +0x1C). O centro da esfera é o mesmo; o raio do disco é mais folgado (CENTRAL 262,6 contra 224,7 do
  centro da caixa ao vértice mais longe).

Achado no caminho: as tiras do `ngpmesh.build_packets` que começam em posição ímpar da seção saem com a ordem dos cantos trocada
(o decodificador alterna a partir do começo da seção; METRO 0xA9CB20: 3 de 6 triângulos virados). O jogo não descarta nada nessas
seções (sem o bit 15), e nos monstros nenhuma seção skinned tem culling, então o `ngpmesh` ficou como está. O `lvlprop` tem o
próprio empacotador (`_sections`): uma tira que começaria em posição ímpar repete o primeiro vértice (um triângulo degenerado, que
o GS desenha como nada). Com isso o item "mesmos triângulos" passou nos 8 (antes falhava em 3).

### 3. R10: nós com vários pais no CENTRAL
Pela hierarquia pura (filhos, sem as arestas de destroço das ações): **45** nós (os 45 do plano). Deles: GROUP 17, LOD 11, OBJECT
11, CONTROL 4, CHAR_INSTANCE 1, TRANSLATE 1. Abaixo deles: GROUP 62, OBJECT 51, ATTACHPT 29, NULL 18, ROTATE 16, TRANSLATE 14, LOD
7, ANIM_XFORM 5, CHAR_INSTANCE 3, POLY_COLL 1. Só 13 dos 45 têm só GROUP/OBJECT/folhas embaixo; 32 têm transformações, LOD,
ATTACHPT ou instância animada. Mais 60 nós só têm um segundo "pai" por ação de destroço. Outras fases: VEGAS 55, METRO 56, UFO 3.
Nenhum é INTERACTIVE ou DESTRUCTIBLE. Conclusão: o próprio jogo divide subárvores bem mais ricas que GROUP -> OBJECT/COLL_VOL;
a raiz comum do prop com vários T|R por cima está dentro do que o motor já faz. O que tem estado por instância (INTERACTIVE,
DESTRUCTIBLE, SWITCH do pickup) é que não pode ser comum.

### 4. Bytes por triângulo (R2)
Recodificando todo OBJECT de uma textura que passa na ida e volta (pacote com o alinhamento de 0x80; o cabeçalho do OBJECT é
mais 0xA0):

| Fase | OBJECTs | triângulos | disco | `lvlprop` |
|---|---|---|---|---|
| CENTRAL | 1097 | 33929 | 35,0 B | 38,3 B |
| VEGAS | 987 | 48715 | 33,6 B | 37,1 B |
| UFO | 1075 | 62126 | 30,3 B | 33,9 B |
| METRO | 3147 | 65787 | 38,1 B | 41,1 B |

Fixo por prop: 16 bytes de cabeçalho, GROUP raiz, GROUP 0x0102 e a caixa (0x80), ~0x100 no total, mais 0xA0 por OBJECT. Props
de teste: cubo 1216 bytes (12 triângulos), poste 1408 (31), carro 8864 (160, 55 B por triângulo). Com ~40 B por triângulo a
arena de 1 MB comporta ~25 000 triângulos de modelo, antes do custo por instância (T|R, caminhos, células) e do que o patch já
usa da arena.

## Passo 1, conversão: `tools/lvlprop.py`
- `build <glb> <pasta> --stage <FASE> [--units blender|game] [--scale S | --height H] [--name] [--title] [--light vertex|dir]
  [--preview-tex]`: grava `<NOME>.NGP`, `<NOME>.PTR`, `<NOME>.preview.glb` e `prop.json` (seção 3.1 do plano, mais `model`,
  `scale`, `objects`, `triangles`, `bytes`, `root`, `ctxFrom`/`tex0` por textura e `bits` do gabarito). ~2,5 s no CENTRAL.
- Unidades: `blender` gira o glTF (Y para cima) para Z para cima sem trocar X/Y (o X/Y da cena do Blender são os do jogo; o
  `ngprig` gira o monstro 180° para olhar +Y, o prop não, porque o patch já tem `heading`). Escala padrão 18 unidades por metro:
  os carros do CENTRAL medem 82-93 x 36-39 x 19-24 (um sedã de 4,5 x 1,8 x 1,5 m dá ~18-20 por metro), os prédios 200-600 de
  altura, os monstros 85-124. A calibração no jogo é o R8b com foto.
- Cor `blender`: COLOR_0 linear x `baseColorFactor` -> sRGB x 128, saturado em 0xFF; alfa x 128. `game`: cru x 255. Material
  sem imagem: OBJECT sem textura (TME 0). Material com imagem que não é `tex<id>`: recusado.
- `preview.glb` sem imagem por padrão (materiais `tex<id>` com os extras do `lvlglb`): assim o arquivo não leva textura do jogo
  quando o mod sobe para o repo; `--preview-tex` embute as do disco do usuário.
- `info <pasta> [--at ADDR]` e `check(prop, at)`: reloca, percorre (só GROUP, OBJECT, COLL_VOL), ida e volta de cada pacote,
  `numVerts`, slots das listas 2 e 3, bounds decodificados x `prop.json`, caixa x bounds.

## Provas
- **R8a** (`r8a.py`): `lvlglb` do CENTRAL, objeto recortado com a matriz da alça zerada, `lvlprop --units game`:
  - carro (pickup tipo 2, alça 0xFC95A0): 160 triângulos no original e no prop, bounds (-19,279 -46,355 -9,363) .. (19,279 46,355
    9,363), maior diferença 0,00000; 2 OBJECTs (tex89, tex87); cores cruas preservadas ((64, 64, 64, 66) em 282 cantos).
  - poste (prop 0xFFAC40, 5 x 5 x 31): 31 triângulos nos dois, bounds (-1,364 -2,385 0) .. (2,811 2,385 31,337), diferença 0.
  - segunda volta (o `preview.glb` do carro de volta pelo `build --units game`): mesmos 160 triângulos e bounds; bytes do `.NGP`
    diferentes (o preview junta os vértices de outro jeito e as tiras saem em outra ordem).
- **R8b** (`mkcube.py`, sem Blender): cubo de 1 m (Blender x, y -0,5..0,5, z 0..1; material `tex270.001`): bounds (-9 -9 0) .. (9
  9 18), 12 triângulos, `tex270` achado (sufixo ignorado). Quads branco (linear 1,0) e cinza 50% sRGB (linear 0,214): RGBA cru
  (128, 128, 128, 128) e (64, 64, 64, 128).
- `info` sem erro nos 4 props em 0x3E00000, 0xA00000 e 0x1F00000; os 4 `preview.glb` abrem no `ngpmesh.Glb` com os mesmos
  triângulos.
- Testes em `mods/_proptest_src/` (os `.glb`) e `mods/PropTest/props/` (CUBE, QUADS, CAR, POLE), locais, fora do commit.

## O que ficou de fora
- Textura nova, `coll_*`/`hull_*` (pulados com aviso), destrutível (`state<N>_*` recusado), malha com skin (recusada).
- Uma caixa de espessura zero para prop plano (os quads: y 0..0); talvez valha uma espessura mínima no passo 3.
- Tudo o que é no jogo: `apply_props`, R1, R3, R5, R11, R12; a escala padrão só se confirma com o R8b no jogo.

## Passo 1, carga: a chave `props` no jogo e no `lvl.py`
Código: `native/runtime/wotm_levelprops.inc` (`applyProps`, incluído pelo `wotm_levelpatch.inc` entre `objects` e `peds`) e
`LevelPatch.apply_props` no `tools/lvl.py` (`lvl.py patch --mod <pasta>`; sem `--mod`, a pasta acima do `levels/` do JSON). Mesma
sequência de alocações nos dois lados: por modelo (na ordem da primeira aparição) a imagem (0x80) e os T|R das entradas (16), depois o
GROUP "props" (flags 7, ctx = o do mundo, no slot 0 do GROUP do mundo) e, por entrada e folha, o caminho `[mundo, props, T|R, raiz,
GROUP 0x102, COLL_VOL]` e os nós de lista das células. Entrada fora da grade e modelo que não cabe devolvem a arena (`rollback`), então
recusado não deixa nada. O `lvl.py` passou a começar a arena alinhada a 0x80 (o fim da imagem do CENTRAL é 0x11ABBC0, 0x40 fora do
alinhamento; a do jogo, `kLevelArena`, é alinhada a MB); com alocação de 16 nada muda nos patches que já existiam.

Outras mudanças: o `apply` do gancho aceita patch sem `objects` (antes logava "parou no meio: sem a lista objects" num patch só de
`props`/`peds`; o `lvl.py` já lia a lista que falta como vazia). Recarga: contador `g_applied` (cargas com o patch aplicado) somado à
`levelSignature` (`wotmLevelPatchLoads`), e o `texPackets_` limpo no `Scene::invalidate()`. A fase custom aceita prop da base
(`aliasOf("LVL/<id>.NGP")`); o `lvl.py` não tem esse caso.

Props de teste (locais, fora do commit): `mods/PropTest/props/BOX` (cubo de 1 m com `--scale 72`, sem cor de vértice, tex270) e `BOXD`
(o mesmo com `--light dir`); patch `mods/PropTest/levels/CENTRAL.json`: BOX em (770, 630, 579,6) e BOXD em (830, 545, 579,6) no
telhado do ponto de partida do jogador (SPT 0, lista 1; o telhado tem topo em 579,6 pela caixa do caminho 0x11A8E30), BOX girado 30°
na rua em (760, -423, 0,5). Teste com `WOTM_MODS` numa pasta só com o PropTest (o `Teste` e o `LevelTest` também têm CENTRAL.json).

### Provas
- **R11** (paridade): `lvl.py patch` e o jogo dão as mesmas linhas e números: `.TEX` com 310 texturas, `BOX tex270 <- OBJECT
  0b08290`, `BOX x2 (1216 bytes), BOXD x1 (1216 bytes); 2932 bytes da arena`, resumo `arena bytes 2932, new list nodes 6, prop
  models 2, prop paths 3, props 3`, o mesmo link de veículo cortado e a regra da caixa "213 certas, 0 fora" / "240 de 240". Com
  outro patch de recusas (pasta `modsbad`, rascunho) também: entrada sem `pos`, entrada fora da grade (caixa (1614 -36)-(1686 36)
  contra a grade até x 1662,6), modelo `NOPE` ausente, `heading` 360 virando TRANSLATE e o CAR (2 texturas, 87 e 89, contextos de
  0x1118C00 e 0xA5DC50): `BOX x1 (1216 bytes), CAR x1 (8864 bytes); 10436 bytes da arena` dos dois lados.
- **Arena (R2)**: 3 entradas de 2 modelos de 1216 bytes tomam 2932 bytes: ~500 além das imagens (T 0x20 ou R 0x54, 4 no GROUP, caminho
  0x3C, 8 por célula). A carga alta termina em 0x23ADA80 com 1 x 1: 26953 KB abaixo da arena (`[fase]   props: arena ...`, só no
  jogo).
- **R1**: `docs/screenshots/2026-11-06/2026-11-06_props_central.png` (wotm.exe, `WOTM_AIVSAI=0`, 18 s): os dois cubos no telhado,
  ao lado do Togera parado. VU1CHECK no `ps2EntryRunner` não foi feito.
- **Luz**: `2026-11-06_props_luz_dir.png` (recorte da mesma foto): à esquerda o BOXD (`dirLight`: topo claro, faces com tons
  diferentes), à direita o BOX (0x80 em todo canto, as faces iguais, puxadas pelo verde das luzes pontuais da fase). Fica para o dono
  escolher o padrão do `--light`.
- **R3** (recarga): com o jogo aberto o BOX foi refeito com `--scale 108` e a recarga pedida pelo `WOTM_LEVELRELOAD` (sem tecla); depois
  da recarga o BOX aparece maior ao lado do BOXD de 72 (`2026-11-06_props_recarga_f10.png`). A prova negativa (sem a soma na
  `levelSignature`) não foi feita.
- **R5**: o monstro do jogador acabou de pé no topo do BOXD (`2026-11-06_props_monstro_no_topo.png`, 34 s): `hdHatTest` acha o
  casco. Câmera: na foto de 18 s ela foi empurrada pelos cubos (outro enquadramento que sem eles). Veículos: o BOX girado na rua corta
  o link 9-14 (no BOX de 108, também o 9-10), e o carro volta no nó antes. Empurrar, câmera atrás e IA não foram medidos à parte.
- `check_collision`: 0 fora da regra nos dois lados em todos os patches de teste.

### O que ficou de fora
- O editor (passo 2) ainda não chama `apply_props` (é do outro agente: `editor.py` usa `LevelPatch` direto).
- O pivô de um prop vindo do `lvlglb` (`--units game`) é o centro do objeto recortado: o CAR em z 0 fica meio enterrado (caixa z
  -9,4..9,4). Talvez o `build` deva pôr o pivô na base.
- VU1CHECK (R1 no GS emulado).
- **R12 e R13 (medidos em 2026-11-08)**: CENTRAL com 10 esferas de 1008 triângulos (59 KB cada, 60,5 KB da arena), `WOTM_FIELDLOG=1`, luta
  IA x IA de 100 s com e sem o mod, mesmo monstro e mesma IA, quadros a partir do 600: 60 fps nos dois (0,1% dos quadros a 30 contra
  0,0%), `host` mediana 16,60 ms nos dois e p90 17,60 contra 17,70 ms, captura nativa p90 0,24 contra 0,22 ms. Ou seja, custo não
  medível. `tools\lote.ps1` com o mod em 4 combinações de 150 s (1 a 3 IAs): zero `CONGELOU`, zero `CRASH`, nenhuma saída antes da hora.
  Não se olhou se a IA anda travada nos props (o `.AIP` não os conhece).
