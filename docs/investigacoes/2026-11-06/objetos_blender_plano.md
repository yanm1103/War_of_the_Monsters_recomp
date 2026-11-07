# Plano final: objetos novos vindos do Blender numa fase (item 5 do TODO)

Diário. Plano revisto a partir do rascunho e de duas críticas, cada crítica conferida no código (só leitura; nada compilado nem aberto).
Medições do rascunho (script em rascunho `props/medir.py`, fora do repo):
- ida e volta do `vifpkt` nos pacotes de fase: CENTRAL 1970 de 2112, CANYON1 1160 de 1267 (falham os sprites e um UNPACK fora do padrão);
- 19 fases: 2824 de 3899 POLY_COLL sem árvore (`numBBoxes` 0), 141 deles com mais de 12 triângulos, maior objeto 476 triângulos (UFO);
- maior texId de fase 328 (VEGAS), 6 a 8 bins por `.TEX`; 45 nós com mais de um pai no CENTRAL (ops abaixo deles não medidos).

## 1. Resumo

Um prop é uma mini-imagem pré-ligada (NGP + PTR) gerada de um `.glb` por `tools/lvlprop.py`, carregada pelo gancho do `dbInitDb` na
arena dos patches e instanciada por uma chave nova `props` no `levels/<FASE>.json`. O primeiro passo entregável já traz colisão por
caixa automática (casco de 6 planos), corte de carros e pedestres, "Add prop" no editor e o Import .glb no editor (que é o que o modder
do release usa, sem Python). Texturas novas e destrutível vêm depois, com os limites que as críticas acharam: o Registry de `mods/`, o
índice de PNG e o `texPackets_` são montados uma vez no boot, então `.TEX` e PNG novos só entram reiniciando o jogo até haver releitura;
o destrutível clona a espinha inteira por instância; as ações do `breakLike` passam pelo `rewrite_act`.

Um prop é de uma fase: o gabarito do cabeçalho, os ctx do GS e os texIds vêm daquela fase. O `prop.json` diz a fase e o editor recusa o
prop em outra.

## 2. O que existe e serve (conferido)

Patch da fase
- Gancho na entrada do `dbInitDb` chamado pelo `Shell::LoadLevelFiles`, antes de qualquer registro (`wotm_levelpatch.inc`, `applyFor`);
  ordem de hoje: objetos, `applyPeds`, `cutNets`. O JSON do patch é procurado a cada carga.
- Arena de 1 MB refeita a cada carga; `kLevelArena = PS2_RAM_SIZE - 2 MB` e `kHighCeiling = kLevelArena` (`wotm_mods.inc:30`): as cargas
  altas (monstros, texturas, `.PTR`) param no começo da arena.
- Espelho em Python `LevelPatch` (`tools/lvl.py`): `alloc`, `_link`/`set_cells`, `redo`, `grow_spheres`, `place`, `copy`, `spine`,
  `deep_clone`, `rewrite_act` (`:1136`, DOMINO para o próprio destrutível vai para a cópia, os outros caem), `check_collision`,
  `leaf_points` (ops 42 e 10). GROUP-envelope noFOV (7), cota de Interactives, caminhos novos por cópia, pendurar no primeiro slot do
  GROUP do mundo (pedestres).
- O jogo trata casco (op 10) e polígono (op 42) em toda consulta: `hdTestNode` (`src/common/hd.cpp`), chamado por hdHat, hdLos, hdSphere.

Malha e textura
- `ngpmesh.Glb`, `ngpmesh.build_packets`, `vifpkt.encode`. O `encode` exige normais em todo lote com posições (`vifpkt.py:286`), então
  todo pacote de fase que passa na ida e volta tem a seção VU_NRM: a seção de normais existe nos pacotes de fase.
- `ngprig.rigid_object` (`:1378`): OBJECT rígido a partir de triângulos, mas preso ao monstro (tags GIF de `base.gif_first/gif_rest`,
  ponteiros em `mb.ptr`, `mscal = None`). Serve de modelo, não como está.
- `ngprig.add_textures`: cópia de um pacote do mesmo bin com id novo (troca só o id em +8 e a contagem do bin; não mexe em CBP nem bin
  +0xC, ao contrário do `mechamon.build_tex/build_rtx`). `tex.encode`.
- `Scene::modTexture` (`wotm_native.hpp:1062`): o PNG é achado por (stem do arquivo do `fileStatus` onde o pacote mora, texId do disco
  tirado do deslocamento do pacote no `.TEX`); `texmods::Index` indexa todo `*_tN.png` de `mods/` pelo nome base, uma vez.
- `texmActivateTexture` (`texm.cpp:376`) remenda o TBP0 do TEX0 do chamador a cada upload: o TBP0 do ctx não importa para textura em bin.
  O CBP (paleta) não é remendado ali.
- Convenção do editor: materiais `tex<id>` (`gltf.py`), `lvlglb` grava `COLOR_0 = RGBA cru / 255`, `gl_matrix` troca os eixos.

Diagnóstico: `WOTM_LEVELPATCH_BREAK` (quebra as cópias e conta o que mudou de estado fora do lado quebrado), `g_loads`.

## 3. Desenho

### 3.1 No mod
- Fonte: `mods/_<mod>_src/props/<nome>.glb`. Gerado: `mods/<mod>/props/<NOME>/` (até 8 letras):
  - `<NOME>.NGP`: mini-imagem pré-ligada em 0xA00000 (palavra 0 = 1, palavra 1 = raiz);
  - `<NOME>.PTR`: listas no leiaute do `.PTR` de monstro: (0) cabeçalho, (1) ponteiros (somar `destino - 0xA00000`), (2) texIds u16 cujo
    valor é o slot de textura, (3) ctx do GS (+0x70, 0x30 bytes) cujo valor é o slot;
  - `<NOME>.preview.glb`: os pacotes gerados decodificados de volta (o que o jogo desenha);
  - `prop.json`.
- A pasta `props/` fica fora do Registry de sobreposição (como `SKINS/` e `MONSTERS/` em `wotm_mods.hpp`): é lida pelo gancho direto do
  sistema de arquivos, na mesma busca de pastas do `levels/<FASE>.json`, com nome sem diferença de maiúsculas (Linux). Assim um prop
  importado com o jogo aberto entra no F10.
- `prop.json`:
```json
{"format":1,"name":"Water tower","stage":"CENTRAL","source":"../../_meumod_src/props/torre.glb","units":"blender",
 "kind":"static",
 "textures":[{"slot":0,"texId":87},{"slot":1,"png":"TORRE_t1.png","like":120}],
 "template":{"rule":"first-object-roundtrip","object":"0xB5C2A0"},
 "light":"vertex","bounds":{"min":[0,0,0],"max":[0,0,0]},
 "collision":{"auto":"box","poly":0,"hull":0,"tris":0},
 "hp":{"class":0,"hitPoints":100,"threshold":5},"breakLike":{"handle":"0xB18310"}}
```
  `stage` é obrigatório. O gabarito é guardado por regra (o primeiro OBJECT da fase que passa no `vifpkt` com os bits pedidos) e o
  endereço achado vai junto só para o log; se outro mod trocar o NGP da fase, a carga refaz a busca pela regra. Os PNGs das texturas
  novas ficam pelo slot (`TORRE_t1.png`); o nome `<FASE>_t<id>` é gerado na hora (3.4).
- Patch da fase, chave nova ao lado de `objects` e `peds`:
```json
"props":[{"id":"torre1","model":"TORRE","pos":[845.9,1003.2,0.5],"heading":90,"name":"Torre do posto"}]
```
  `model` é procurado no mesmo mod do JSON. O `lvl.py patch` ganha `--mod <pasta>` e o `LevelPatch.apply_props` recebe a pasta; o editor
  passa sempre.

### 3.2 Conversão: `tools/lvlprop.py build <glb> <pasta do prop> --stage CENTRAL [--units blender|game] [--scale S]`
- Unidades: `--units` explícito, padrão `blender` (Z para cima, metros); `game` para um glb vindo do `lvlglb`. Nada de heurística pelo
  nome da raiz. O fator metro para unidade segue a convenção de `--scale/--height` do `ngprig` (monstro custom e prop na mesma escala).
  Transformações de objeto do glb são assadas no mundo. A escala vai nos vértices (visual e colisão), nunca num nó SCALE (o
  `hdTraversePath` não trata SCALE, FORMATS "SCALE is not handled").
- Nomes das malhas: `coll_*` (POLY_COLL op 42, até 256 vértices por objeto, sem árvore, até ~64 triângulos até haver gerador),
  `hull_*` (COLL_VOL op 10, planos de uma malha convexa), `state<N>_*` (estado do destrutível), o resto é visual. Sem `coll_*` nem
  `hull_*`, uma caixa automática (op 10, 6 planos) com os bounds do visual.
- Visual: uma função nova `level_object(tris, gabarito)` separada do `rigid_object`, que tira as tags GIF, os bits de cabeçalho (neblina,
  `fogBurnThru`, `dontCastShadow`, `dontShadowMe`) e o leiaute de normais do OBJECT gabarito da fase, e não escreve em `mb.ptr` de
  monstro. Um OBJECT por textura; `numVerts` tem 13 bits, então dividir. `build_packets` já respeita 190 posições e 44 vértices.
- Cor: origem `game`, cru x255; origem `blender`, `COLOR_0` linear para sRGB x128, saturado em 0xFF (0x80 = 1,0 no PS2); sem cor, 0x80.
  `"light":"dir"` liga `dirLight` 0x100.
- Materiais: `tex<id>` reaproveita a textura da fase; o sufixo `.NNN` do Blender é ignorado. Outra imagem vira slot novo (3.4). Aviso
  quando há UV fora de 0..1 e o ctx escolhido é CLAMP.
- Árvore da mini-imagem: só GROUP, OBJECT e folhas de colisão no que é comum entre instâncias (o conversor recusa outros ops).
  - estático: GROUP raiz (flags sem byte alto) -> OBJECTs + GROUP 0x102 -> folhas;
  - destrutível: a espinha (GROUP -> INTERACTIVE_STATE -> DESTRUCTIBLE) é modelo a clonar por instância; os GROUPs de estado, com visual e
    colisão próprios, ficam comuns.
  - Folhas e OBJECTs com id1 = id2 = 0 (o id da folha vence o do caminho, `hdTraverse.cpp:188-189`).
- O build roda dentro do editor (POST que chama `lvlprop.build` no mesmo processo) e precisa do NGP e do `.TEX` da fase, que o editor já
  resolve pela ISO. Dependências do `lvlprop` entram no exe do release.

### 3.3 Na carga: `applyProps` no `.inc` e `LevelPatch.apply_props` no `lvl.py`
1. Ordem: objetos, props, `peds`, `cutNets`, igual nos dois lados.
2. Por modelo: ler NGP e PTR da pasta do mod, alocar na arena (alinhado a 0x80), relocar a lista 1, trocar os slots da lista 2 pelo texId
   real e preencher a lista 3: para texId da fase, o ctx vivo de um OBJECT da fase com aquele texId (o primeiro achado; o log diz qual);
   para slot novo, o ctx do OBJECT do texId `like` (mesmo bin, TW/TH/PSM e MIP iguais). O TBP0 é remendado pelo `texmActivateTexture`;
   o CBP é risco R4. Antes de alocar, conferir que todo texId usado existe no `.TEX` realmente carregado (`fileStatus`); se não, a
   entrada é recusada com uma linha no log.
3. Por instância:
   - estático: TRANSLATE (heading nulo) ou ROTATE (0x60+4n, alinhado a 16) cujo filho é a raiz comum do modelo;
   - destrutível: a espinha inteira clonada por instância (T|R -> GROUP -> IST -> DESTR), pelo `spine`/`deep_clone` que já existem, não por
     lista própria; entra na cota de Interactives e na de Destructibles (teto 1500 do `AddDestruct`), conferidas antes;
   - todas num GROUP "props" novo (7, `ctx` = mundo+4, sem byte alto nas flags), no primeiro slot do GROUP id 1, como os pedestres.
4. Colisão por instância e folha: caminho `[mundo, props, T|R, raiz, (GROUP, IST, DESTR, estado), GROUP 0x102, folha]`. Caixa, esfera e
   footprint calculados pelo caminho com a matriz do T/R da instância, nunca por `world(raiz)` (o espelho usa `st.parents[h][0]`, o
   primeiro pai). Caixa pela regra dos 8 cantos (`redo`), células por `_link`, a caixa sólida em `st.footprints` para o `cutNets`.
   Prop com caixa fora do retângulo da grade é recusado (some para o `hdLosTest`, FORMATS).
5. `breakLike`: as ações do estado vêm do destrutível de origem passadas pelo `rewrite_act` com `twins` vazio (todo DOMINO cai), nos dois
   lados. DEBRIS e TRANSFORM que referem nós ou posições do prédio de origem: listar antes do passo 5 (R7).
6. Log: `[fase]   props: TORRE x3 (N bytes)`, igual ao `lvl.py patch`; mais os bytes da arena usados e a folga até `kHighCeiling` da
   maior carga alta.
7. Recarga: o gancho chama `scene.invalidate()` (ou soma `g_loads` à `levelSignature`), porque o `packetSig` só olha tag e duas palavras
   e o `tex_` só se limpa no `invalidate()`; o `texPackets_` passa a ser limpo também no `invalidate()`.

### 3.4 Texturas novas
- Ids depois do maior da fase (até 328 no disco), alocados na geração; o PNG é copiado como `<FASE>_t<idNovo>.png` (a única chave que o
  `modTexture` acha). O `stage-tex` grava `mods/<mod>/LVL/<FASE>.TEX` = disco + pacotes, no formato que o jogo lê (cru ou no zip IE,
  conferir com `tex.zip_ie` e com como o jogo lê `LVL/*.TEX` antes de gravar).
- Limites conhecidos desse caminho, ditos no MODS.md e no editor: um `.TEX` inteiro por fase e por mod; dois mods com props na mesma fase
  ou um mod de textura da fase brigam (o último em ordem alfabética vence, `[mods] ... vence ...` no log); o `modkit check` acusa dois
  `.TEX` da mesma fase. Ids 329+ de dois mods colidem.
- O `.TEX` e os PNGs só entram com o jogo reiniciado (Registry com tamanho e LSN fixados no scan; `texmods::Index` montado uma vez). O
  editor avisa "reinicie o jogo" quando o `.TEX` muda; o Test in game por F10 não serve para este passo.
- Gerar o `.TEX` na busca do arquivo (disco + pacotes de todos os props ativos na fase, ids alocados na carga) resolve os conflitos e o
  reinício; fica para o passo 7, junto com a releitura do Registry e do Index.
- O PNG original do modder é mantido para o port (o `texelRect` escala); só o conteúdo do pacote no `.TEX`, que o GS emulado vê, é
  reduzido ao tamanho e psm do gabarito.
- Orçamento do `texInfo[1000]` (999 reservado ao filme do menu): o `stage-tex` soma a fase, os novos e os monstros mais pesados (até 10 na
  eliminação, com trajes de mod) e recusa acima de ~990.

### 3.5 Editor
- `editor.py`: `/api/props?mod=` lista `props/*/prop.json` da fase aberta; serve o `preview.glb`; POST de Import .glb que chama o build.
- `app.js`: paleta "Props", "Add prop" na mira da vista, instância como clone three.js do preview com os gizmos, Drop, Snap, Undo/Redo,
  Delete; seleção pelo `id` da entrada (não por handle); grava e lê `props` preservando o resto do arquivo do mod sem perdas; o harness
  `_test.html` cobre Add/Delete de prop.
- Check e Test passam pelo `lvl.py patch --mod` e pelo jogo.

## 4. Riscos e provas

- R1. OBJECT na arena acima de 32 MB. Provavelmente baixo: o DMA do runtime mascara em 64 MB e o nativo decodifica por
  `polyPkt & 0x0FFFFFFF`; os monstros custom já desenham pacotes rígidos acima de 32 MB. Prova: foto no `wotm.exe` e VU1CHECK no
  `ps2EntryRunner`, esperando igualdade.
- R2. Arena e teto alto. A arena de 1 MB é também o `kHighCeiling`: aumentá-la tira dos monstros. Medir bytes por triângulo com o blob real
  no passo 1 e a folga da maior carga alta na eliminação (10 monstros) com `WOTM_HIGHRAM` ligado e desligado. Prop que não cabe é recusado
  com uma linha de log. "Depois da imagem" não serve (abaixo de 32 MB vêm o `.TEX` e o `.RTX` da fase); se um dia faltar, o começo é o
  maior endereço das três listas do `fileStatus` e precisa de dump depois do `LoadMonstersDB` com 4 monstros e mod de skin.
- R3. Recarga. Prova no passo 1: trocar o prop (cor, posição) com o jogo aberto e F10; prova no passo 4: trocar um PNG e ver que só entra
  reiniciando (até o passo 7).
- R4. ctx de slot novo. TBP0 é remendado pelo `texmActivateTexture`; o CBP da paleta de textura indexada não. Antes do passo 4, medir num
  dump se o CBP de uma textura de bin é fixo no ctx ou vem do pacote; se fixo, o GS emulado (VU1CHECK) mostra a paleta do gabarito e o
  corpus vai acusar a diferença; dizer isso no VU1CHECK.md e no prop.json. Também: o `add_textures` não remapeia CBP no `.RTX` nem o bin
  +0xC, como o `mechamon` precisou fazer; ver se o `.TEX` de fase precisa.
- R5. Colisão. `check_collision` com zero fora da regra; no treino: monstro de pé no topo (`hdHatTest`), empurrar, câmera atrás
  (`hdLosTest`), carros voltando antes do prop. O `hdTraversePath` para em todo GROUP do caminho com `flags & (option << 8)`
  (`hdTraverse.cpp:133-136`): documentar o `option` de cada consulta (hdHat, hdLos, hdSphere) e dar ao GROUP "props" e à raiz flags sem
  byte alto; o 0x102 copia o dos prédios.
- R6. hatId de prop estático. Com folhas em id 0, o `hatId1/hatId2` vem do caminho (o do GROUP mundo ou o T|R). Medir o que isso dá de
  som e partícula de passo e o que é `surface`; até lá, o campo não existe no `prop.json`.
- R7. Destrutível e ações. `spine`/`deep_clone` dão a espinha; `rewrite_act` com twins vazio derruba os DOMINOs. Antes do passo 5, listar
  com `lvl.py` o que as ações de um destrutível pequeno apontam (DEBRIS, TRANSFORM, emissores) e os offsets de class, hitPoints e
  threshold no DESTRUCTIBLE. Prova: `WOTM_LEVELPATCH_BREAK` com "fora do lado quebrado mudaram de estado: 0".
- R8. Escala, eixos, cor. Duas provas: (a) ida e volta `lvlglb` -> `lvlprop --units game`, um carro ao lado do original; (b) cubo de 1 m e
  quads branco e cinza 50% do Blender ao lado de uma parede e de um prédio de altura conhecida.
- R9. Pacote gerado. A ida e volta do `vifpkt` prova o leitor e o gravador, não o pacote novo. Prova: o mesmo OBJECT gabarito recodificado
  a partir dos seus triângulos decodificados bate byte a byte com o original.
- R10. Raiz com vários pais. Só GROUP, OBJECT e folhas no comum. Conferir os ops abaixo dos 45 nós compartilhados do CENTRAL.
- R11. Paridade Python x C++. Teste do passo 1: log `props:` e bytes da arena iguais entre `lvl.py patch` e o jogo, com 2 instâncias do
  mesmo modelo.
- R12. Custo por quadro. Props sob GROUP noFOV nunca são cortados pelo campo de visão: medir o FPS do `wotm.exe` com 10 props de ~1000
  triângulos.
- R13. IA. O `.AIP` não conhece o obstáculo: luta CPU x CPU em `tools\lote.ps1` com o vigia.
- R14. Linux. Pasta `props/` com maiúsculas diferentes; `native/` compila nos dois sistemas (`LINUX_MAINTENANCE.md`).

## 5. Passos em ordem

| # | Passo | Arquivos | Custo | Como conferir |
|---|---|---|---|---|
| 0 | Medições sem código no jogo: gabaritos que passam no `vifpkt` por fase e seus bits; R9 (recodificar o gabarito byte a byte); ops sob os 45 nós de vários pais | rascunho | P | saída dos scripts |
| 1 | Prop estático com texturas da fase e caixa automática: `lvlprop build` com `level_object`, `--units`, cor; `apply_props` nos dois lados com caminhos, células, footprints, invalidate na recarga | `tools/lvlprop.py`, `tools/lvl.py`, `wotm_levelpatch.inc`, `wotm_native.hpp` (texPackets_ no invalidate) | M | R11 (paridade), R1 (foto e VU1CHECK), R3 (F10), R5 no treino, R8, arena medida (R2) |
| 2 | Editor: paleta, Add prop, gizmos, salvar `props`, Import .glb no editor, `lvl.py patch --mod` | `editor.py`, `web/app.js`, empacotamento do exe | M | harness `_test.html`; Check; Test in game; exe do release gera um prop |
| 3 | Colisão `coll_*` e `hull_*` (até ~64 triângulos sem árvore), recusa fora da grade | `lvlprop.py`, `lvl.py`, `.inc` | M | `check_collision` zero; R5 com câmera e LOS |
| 4 | Texturas novas: `stage-tex`, PNG `<FASE>_t<id>`, orçamento do texInfo, aviso de reinício, conflito no `modkit check` | `lvlprop.py`, `modkit.py`, MODS.md | M | R4; foto; log do texmods; monstros com as texturas certas na eliminação |
| 5 | Destrutível: espinha por instância, estados, hp, colisão por estado, `breakLike` via `rewrite_act`, teto de 1500 | os três | M | R7; linha de interactives e destructibles; foto do estado 1 |
| 6 | Polimento: `dirLight`, LOD, árvore de caixas, comando no `modkit.py` e na GUI, R12, R13 | vários | M | FPS; `lote.ps1` |
| 7 | `.TEX` gerado na busca do arquivo e releitura do Registry/Index na carga da fase (sem `.TEX` inteiro no mod, sem reinício) | `wotm_mods.hpp`, `wotm_texmods.hpp`, `wotm_mods.inc` | M | dois mods com props na mesma fase; F10 com PNG trocado |
| 8 | Fase do zero = base + geometria trocada (`stage.json` do item 4, apagar raiz ou GROUP sem alça, terreno em objetos de 256 vértices com árvore, grade refeita, PTS novos) | ver item 4 do TODO | G | luta inteira, `lote`, fotos |

Referência a atualizar junto com cada passo: MODS.md ("Level patches" com `props` e o `prop.json`, os limites de 3.4), TOOLS.md
(`lvlprop`), NATIVE.md e `tools/wotm_env.py` se entrar variável de diagnóstico.

Menor passo útil: 1 + 2. Prop estático do Blender com textura da fase, colisão de caixa, carros e pedestres desviando, posto pelo editor e
importado pelo editor.

## 6. Críticas rejeitadas ou ajustadas

- "Não foi medido se os pacotes de fase têm VU_NRM": rejeitada em parte. O `vifpkt.encode` recusa lote sem normais (`vifpkt.py:286`), e
  ~93% dos pacotes de fase passam na ida e volta, então têm normais. Fica aceito o resto (tags GIF e bits do gabarito de fase, função
  separada do `rigid_object`, prova byte a byte R9).
- "Copiar o ctx do gabarito deixa o TBP0 apontando para a textura do gabarito": rejeitada para o TBP0, que o `texmActivateTexture` remenda
  a cada upload (`texm.cpp:376-386`). Aceita para o CBP (R4).
- "O 1a é dispensável": aceita. A prova do R1 entra no passo 1 com o blob real.
- "Pôr os blobs depois da imagem": aceita a crítica; a saída fica descrita em R2 só para quando faltar, e o primeiro remédio é recusar.
- "Textura nova não tem ctx": aceita; ctx do `like`.
- "Prop reutilizável entre fases": não vale o custo agora; o prop é de uma fase e o formato diz isso. A busca do gabarito por regra fica,
  porque protege contra outro mod trocar o NGP.

## 7. Fora do escopo

- Pickups novos (arremessáveis: `_cs`, tipo, SWITCH); um "pickupLike" por clone pode vir depois.
- Props animados (CHARACTER_INSTANCE, ANIM_MGR) e emissores próprios: dependem da cópia profunda do item 3 do TODO.
- LIGHT, lens flare, céu, `.MRS` nova.
- Pontos de IA (`.AIP`) desviando do prop.
- Paleta exata no `.RTX` para o GS emulado além do que R4 medir: o port desenha o PNG.
