# HUD por fase: o que vem do arquivo da fase, como o jogo carrega e desenha, e o que o editor pode oferecer

Pesquisa para o editor de fase (só leitura; nada do jogo foi copiado). Fontes: `src/game/db.cpp` (`dbProcInteractive`),
`src/game/Hud.cpp` (`Hud::parseHudElements`), `include/game/hud.h`, `src/game/Shell.cpp` (`Shell::LoadLevelFiles`), o asm de
`game/Hud` (`initAfter__3Hudi`, `update__3Hud`, `addHealthIndicators__3Hud`, `updateTextBoxMessage__3Hud`,
`drawHealthIcon__3HudP7Monsteri`), `asm/data/game/Hud.data.s`/`Hud.sdata.s` (só nomes e tamanhos), `native/wotm_native.hpp`
(`Scene::drawCs`, `modTexture`, `residentTexture`), `native/wotm_texmods.hpp`, `tools/lvlglb.py`, `tools/level_editor/web/app.js`.

Scripts de prova (fora do repo, na pasta de rascunho
`C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\`):
`hudscan.py` (lista os nós de HUD de um `.NGP`), `hudcmp.py` (exporta `.TEX`/`.RTX` de todas as fases e compara textura e malha de
cada elemento entre fases), `hudtable.py` (texId de cada elemento por fase), `hudshare.py` (textura de HUD usada fora da HUD),
`hudvis.py` (o que o percurso do `lvlglb` alcança), `hudbox.py` (caixa das malhas de HUD), `animh.py` (handles de animação do
`Hud::initAfter`). Rodados contra as 19 fases de `disc/LVL`.

Legenda: **[certo]** lido no código e conferido no disco; **[hipótese]** não confirmado (não abri o jogo).

## Resumo

- **A HUD da luta mora dentro do arquivo da fase.** Cada `LVL/<FASE>.NGP` traz os 28 nós de HUD (ids 0x1F42..0x1F57), e as texturas
  deles estão no `LVL/<FASE>.TEX`/`.RTX` da mesma fase. Não existe arquivo de HUD separado; o nome sai do nome da fase
  (`Shell::LoadLevelFiles`: `LVL\<GetLevelName()>.NGP/.TEX/.RTX`). [certo]
- **O conteúdo é o mesmo em 17 das 19 fases**; **CANYON1 e TOKYODM** têm outra versão (mais antiga): outro atlas das barras, outro
  ícone "R", outra fonte, outro atlas do ícone de vida, barra de chefe sem o rótulo "BOSS" e malhas diferentes. Então "a HUD é por
  fase" é literal no arquivo (cada fase tem a sua cópia, com texIds próprios) e visível no jogo em 2 fases. [certo, por hash das
  texturas decodificadas e das malhas]
- **Minimapa/radar não existe** no jogo: nenhum nó, textura, símbolo ou função de mapa (só `isOverhead`/`okToDrawReticle`, que são
  outra coisa). [certo]
- O que muda por fase **no código** (não no arquivo): as mensagens de dica da aventura (tabelas no ELF escolhidas por `m_levelId`),
  as barras de chefe da aventura (CANYON2, 3-MILE, CAPITOL) e as dicas do tutorial do CENTRAL. [certo]
- **Um mod já troca toda textura de HUD hoje, sem mudar nada no jogo:** PNG com o nome da fase, `mods/<mod>/LVL/<fase>_t<id>.png`
  (`.TEX`) ou `<fase>_rtx_t<id>.png` (`.RTX`). Como o texId muda de fase para fase, uma HUD nova em todas as fases são 19 jogos de
  PNG com ids diferentes (tabela na seção 3). [certo no código; não testado no jogo para a HUD, mas é o mesmo caminho das
  partículas, que foi]
- O editor hoje provavelmente desenha os nós de HUD (e as armas/power-ups da mesma raiz) amontoados perto da origem: a raiz que os
  contém tem ident 0, e `app.js:256` deixa visíveis as raízes de ident 0 e 1. [hipótese: não abri o editor; o percurso do `lvlglb`
  alcança os 28 nós em todas as fases conferidas]

## 1. Como o jogo acha a HUD na fase

- `Shell::LoadLevelFiles` (src/game/Shell.cpp) monta `LVL\<nome>.NGP`, `.TEX` (e o `.RTX` via `LoadResTexture`) com
  `GetLevelName()`, carrega, reloca (`dbsRelocateFileZero`) e chama `dbInitDb(0xA00000, ...)`. [certo]
- `dbInitDb` percorre todas as raízes menos `ANIM_MGR` com `dbsTraverse`, chamando `dbProcInteractive` em cada nó.
  Id `0x1C20..0x2133` (14 bits altos da cabeça, `id1`), fora os opcodes 0x1F/0x27, vai para `Hud::parseHudElements`
  (`src/game/db.cpp`). Id 18 vai para `fontSetTexId` (a fonte da luta). [certo]
- `Hud::parseHudElements` (src/game/Hud.cpp, equivalente) guarda o que reconhece pelo `id1` (e `id2` 0/1 = HUD do jogador 1/2):

| id1 | Nó (todas as fases) | Para onde vai | O que é | Textura (CENTRAL) |
|---|---|---|---|---|
| 0x1F42 | SWITCH | `LevelPickups::createPickupHighlight` | anel em volta do objeto que dá para pegar | t0097, T4 128x128 |
| 0x1F43 | CHAR_INSTANCE + GROUP | `m_huds[0].m_cs1F43` (`Hud+0x98`), animação 0x1F43 com `AnimBlend` | moldura da caixa de dica (`updateTextBoxMessage` usa `+0x98`) | t0095, T8 64x64 |
| 0x1F44 | GROUP | `Letterboxing::setLetterboxCS` | tarjas pretas de cinema (aventura) | t0063, T4 64x64 |
| 0x1F46 | OBJECT | `ScreenPolys::initSourceObject(o, 5000)` → `gHudTextureObjNum` | fonte dos quadrados de tela: o ícone de vida sobre o inimigo (círculos e números 1-4), recortado com os retângulos de `s_hudGraphicData` (ELF) em `drawHealthIcon` | rtx t0071, T4HL 128x128 |
| 0x1F47 x2 | GROUP | `m_huds[id2!=0].m_node1F47` (`+0xE4`) | moldura da HUD de cada jogador | t0072 (atlas T8 256x256) e t0096 ("R", T4 64x32) |
| 0x1F48, 0x1F49, 0x1F4B, 0x1F4C, 0x1F4E, 0x1F4F x2 | CHAR_INSTANCE | não guardados no parse; `Hud::initAfter` pega as animações pelo id (`animationGetHandle`, handles em `Hud+0x130..0x1D0`; `+0x180` = 0x1F4E é o pisca da raiva, `+0x1D0` = 0x1F4F a morte) | barras animadas (vida, energia, raiva...) [qual é qual: hipótese] | t0072 |
| 0x1F4D x2 | SWITCH | `m_huds[...].m_node1F4D` (`+0xEC`) | ícone do especial ("R") | t0096 |
| 0x1F51..0x1F54 | GROUP | `McPageMgr::initButton` + `m_mcButtonCs[0..3]` | botões X/O/quadrado/triângulo (prompts) | t0094, T8 64x64 |
| 0x1F55 | GROUP | `m_huds[0].m_node1F55` (`+0xF0`) | barra de chefe com o rótulo "BOSS" (`drawBossHealthBar`) | t0064 ("BOSS", T4 128x64) e t0072 |
| 0x1F56, 0x1F57 | CHAR_INSTANCE | animações `Hud+0x1E0/0x1F0` | partes animadas da barra de chefe | t0072 |
| 18 (não é HUD, mas é da luta) | OBJECT | `fontSetTexId` | fonte do texto da luta ("WINS", combos, mensagens) | rtx t0068, T8H 512x512 |

- As animações das barras ficam nas próprias `CHAR_INSTANCE` de HUD do `.NGP` da fase; o `Hud::update` só põe a porcentagem
  (`animationSetToPercent`). [certo para as chamadas; o formato da animação é o de `MODELS_3D.md`]
- Posição na tela: o `Hud::update` põe o `_cs` de cada elemento no olho, com a rotação da vista, somando `R · gHudPos`, e `gHudPos`
  é zero no `.data` e nunca é escrito no asm (só lido em `update__3Hud`). O layout vem então das malhas e das animações do
  `.NGP` (as malhas estão no plano XZ local, X de −170 a +124, Z de −139 a +53 no CENTRAL; a distância à câmera vem das
  transformações animadas). [certo para `gHudPos`; o resto por `hudbox.py`]
- Retrato do monstro na HUD: **não é da fase**, vem do `.RTX` do monstro (raiz 1 do `.NGP` do monstro, `MODELS_3D.md`); o nome na
  HUD vem do código (`MonsterNewNames`). [certo]

## 2. O que muda por fase

### 2.1 No arquivo (o que um mod troca)
Conferido por `hudcmp.py` (hash dos pixels decodificados com a paleta, e das malhas decodificadas com as transformações):

- **Iguais nas 19 fases:** anel de pegar (0x1F42), moldura da caixa de dica (0x1F43), tarja de cinema (0x1F44), botões (0x1F51..54).
- **Duas versões, A em 17 fases e B em CANYON1 e TOKYODM:** atlas das barras, ícone "R", atlas do ícone de vida (`.RTX` 128x128),
  fonte da luta (`.RTX` 512x512), malhas de 0x1F47..0x1F4E e 0x1F55..0x1F57. Na B o atlas tem três barras coloridas em vez de quatro,
  a moldura é outra, a fonte é mais escura e só tem um jogo de pontuação, e a barra de chefe (0x1F55) não tem o rótulo "BOSS"
  (19 nós em vez de 21; usa só o atlas).
- **Todas as fases têm os mesmos 28 nós** (mesma contagem por id). A raiz que os contém é um GROUP de id 0, mas o índice dela muda
  (raiz 0 em AIRPORT/BIGSHOT/CANYON2, 1 em CENTRAL/DBALL/TRAINING/UFO, 2 em 3-MILE/CANYON1/ISLAND..., 3 em MINISF/TOKYO/VEGAS/VOLCANO):
  achar a HUD pelo id, nunca pelo índice da raiz. [certo]
- Textura de HUD usada fora da HUD: só a da caixa de dica (t0095 no CENTRAL), que o nó 0x262A (ponto final do Shell,
  `Shell::AddEpNode`) também usa, em todas as fases. As outras são exclusivas da HUD: trocar o PNG não muda nada do cenário. [certo]

### 2.2 No código (o arquivo não muda isso)
- `Hud::initAfter`: na aventura (`m_gameMode` 1), `m_levelId` 1..11 escolhe a tabela de mensagens de dica gravada em `Hud+0x2D4`:
  `centralParkMessagesA/B/C` (CENTRAL, pela etapa em `+0x672C` do `shell`), `canyon2Messages`, `threeMileMessages`,
  `islandMessages`, `capitolMessages`. As tabelas e os textos estão no ELF. [certo]
- `Hud::update`: `m_levelId` 3 (CANYON2, progressão do Goliath) e 1 (contador de dicas do CENTRAL); `addHealthIndicators`:
  3, 6 e 11 na aventura (barras de chefe do Goliath, da planta e do chefe final); `updateTextBoxMessage`: 1 (tutorial). [certo
  para os testes; o efeito exato de cada um: hipótese]
- Uma fase clonada herda tudo isso pelo índice da base (`fases_custom.md`, seção 2).

## 3. texId de cada elemento por fase (o nome do PNG de mod)

`<fase>_t<id>.png` para o `.TEX`, `<fase>_rtx_t<id>.png` para o `.RTX` (nome em minúsculas, como o `tools/tex.py export` grava).

| fase | atlas | R | BOSS | anel | caixa | tarja | botões | ícone (rtx) | fonte (rtx) |
|---|---|---|---|---|---|---|---|---|---|
| 3-MILE | 73 | 96 | 65 | 97 | 95 | 64 | 94 | 72 | 69 |
| AIRPORT | 72 | 94 | 64 | 95 | 93 | 63 | 92 | 71 | 68 |
| BIGSHOT | 70 | 77 | 64 | 78 | 76 | 63 | 75 | 69 | 66 |
| CANYON1 (B) | 67 | 74 | — | 75 | 73 | 61 | 72 | 66 | 63 |
| CANYON2 | 70 | 91 | 64 | 92 | 90 | 63 | 89 | 69 | 66 |
| CAPITOL | 72 | 93 | 64 | 94 | 92 | 63 | 91 | 71 | 68 |
| CENTRAL | 72 | 96 | 64 | 97 | 95 | 63 | 94 | 71 | 68 |
| CRUSH | 72 | 81 | 64 | 82 | 80 | 63 | 79 | 71 | 68 |
| DBALL | 70 | 78 | 64 | 79 | 77 | 63 | 76 | 69 | 66 |
| ISLAND | 73 | 96 | 65 | 97 | 95 | 64 | 94 | 72 | 69 |
| METRO | 72 | 94 | 64 | 95 | 93 | 63 | 92 | 71 | 68 |
| MINISF | 72 | 93 | 64 | 94 | 92 | 63 | 91 | 71 | 68 |
| SANFRAN | 72 | 93 | 64 | 94 | 92 | 63 | 91 | 71 | 68 |
| TOKYO | 72 | 93 | 64 | 94 | 92 | 63 | 91 | 71 | 68 |
| TOKYODM (B) | 70 | 92 | — | 93 | 91 | 62 | 90 | 69 | 66 |
| TRAINING | 70 | 77 | 64 | 78 | 76 | 63 | 75 | 69 | 66 |
| UFO | 70 | 78 | 64 | 79 | 77 | 63 | 76 | 69 | 66 |
| VEGAS | 72 | 93 | 64 | 94 | 92 | 63 | 91 | 71 | 68 |
| VOLCANO | 70 | 80 | 64 | 81 | 79 | 63 | 78 | 69 | 66 |

Os ids são os do disco (antes da relocação da carga), que é o que o `wotm_texmods.hpp` procura. No `.TEX` os ids do `.RTX` faltam
(ex.: CENTRAL não tem t0068 nem t0071 no `.TEX`): os dois arquivos dividem a numeração.

## 4. Como a camada nativa desenha a HUD

- Os nós de HUD passam pelo `Scene::drawCs` como qualquer `_cs`; o id 0x1C20..0x2133 (menos os botões 0x1F51..54) manda os itens
  para a lista `hud`, desenhada por cima da cena, sem profundidade, com `DrawItem::hudId` (`native/wotm_native.hpp`, `drawCs`).
  Os botões vão para `overlay`. [certo]
- O ícone de vida (0x1F46) vem do `ScreenPolys` (`Snapshot::screenPolys`, `drawScreenPolys`), e o texto da fonte da luta do
  `FontText`. Os dois pegam a textura por `residentTexture`, que chama `modTexture` (PNG de mod) antes de decodificar a do disco. [certo]
- `modTexture` acha o PNG pelo arquivo carregado (`fileStatus.names[i]`, ex. `CENTRAL`) e pelo deslocamento do pacote no `.TEX`
  (texId do disco); as residentes do `.RTX` pelo DBP (`rtxImgSrc_`). Então a troca vale para a HUD da fase com aquele nome. [certo]
- HUD dos jogadores 3 e 4 (`wotmPlayerHud`), barras sobre o inimigo (`WOTM_ENEMYBARS`) e números do treino: desenhados pelo nativo
  com cores do código, sem textura da fase (`NATIVE.md`). Não mudam por fase.

## 5. O que um mod já pode fazer hoje

1. **Trocar textura de HUD por PNG** (sem mexer no jogo): exportar com `python tools/tex.py export disc/LVL/CENTRAL.TEX saida/`
   (gera também as do `.RTX` com `export disc/LVL/CENTRAL.RTX`), editar e pôr em `mods/<mod>/LVL/central_t0072.png` (qualquer
   pasta do mod serve; `LVL/` é a convenção). Qualquer resolução (UV normalizado); manter a proporção. O kit (`modkit_gui.py`)
   já extrai as fases pelo nome. Para valer em todas as fases: um PNG por fase com o id da tabela da seção 3; nas fases B
   (CANYON1, TOKYODM) o atlas, o "R", a fonte e o ícone têm outro desenho, então o mesmo PNG da versão A sai torto ali (o anel, a
   caixa, a tarja e os botões servem igual).
2. **Trocar o arquivo inteiro** (`mods/<mod>/LVL/<FASE>.NGP/.TEX/.RTX`): troca malhas e animações da HUD também, mas leva a fase toda
   junto. Não é caminho para o editor.
3. **O patch de fase (`levels/<FASE>.json`) não toca na HUD**: ele só mexe em objetos com handle (`wotm_levelpatch.inc`).
4. Não dá para trocar por arquivo: as mensagens de dica da aventura, o nome do monstro, a posição base (`gHudPos`), as cores e o
   layout da HUD dos jogadores 3/4 (tudo no código).

## 6. Plano para o editor

### 6.1 Esconder a HUD da cena 3D (barato, recomendado já)
- Os 28 nós de HUD (e o resto da raiz de ident 0: armas, power-ups, texturas de partícula) saem no `.glb` sob `rootN` e a página
  deixa essa raiz visível (`app.js:256`, `u.ident === 1 || u.ident === 0`). Proposta: o `lvlglb.py` marca os nós com id
  0x1C20..0x2133 (fora 0x1F/0x27) com `extras.hud = <id>` e `hidden: true`, ou nem os exporta na cena; a página não os mostra
  na cena. Conferir antes abrindo o CENTRAL no editor e olhando perto de (0, 0, 0).

### 6.2 Painel "HUD" (sem mudança no jogo)
- **Ler:** uma função `lvl.hud_elements(img)` (ou `tools/hud.py`) com o percurso de `hudscan.py`: todos os nós com id1 em
  0x1C20..0x2133 e opcode ≠ 0x1F/0x27, mais o nó de id 18 (fonte); para cada um, `id1`, `id2`, opcode e os texIds dos OBJECT
  abaixo (`+0x50`, `u16[+0x5A]` slots). Agrupar por textura. As imagens vêm do `tex.Library` que o `lvlglb.py` já usa (ou do
  `tex.cmd_export` das duas texturas do `.RTX`).
- **Mostrar:** uma grade com nome amigável (tabela da seção 1), miniatura, tamanho, formato, de que arquivo vem (`.TEX`/`.RTX`) e o
  PNG do mod se existir (`<mods>/<mod>/**/<fase>_t<id>.png` ou `_rtx_`; o índice do `wotm_texmods.hpp` aceita qualquer pasta, o
  editor deve procurar no mod inteiro). Avisar "versão B" em CANYON1/TOKYODM.
- **Gravar:** "Replace…" grava `mods/<mod>/LVL/<fase>_t<id>.png` (PNG; JPG etc. convertidos; avisar se a proporção muda; alfa na
  escala do original, como o kit faz). "Export" devolve o PNG do disco. "Remove" apaga o PNG do mod. "Apply to all stages" grava
  uma cópia por fase com o id da fase (calculado pelo `hud_elements` de cada `.NGP`, nunca por tabela fixa), pulando as fases B
  para os elementos que mudam de desenho (atlas, R, fonte, ícone) ou pedindo um PNG próprio para elas.
- **Nada no `levels/<FASE>.json`.** O "Save" do editor continua gravando só o patch; os PNG são gravados na hora (ou na mesma
  ação de salvar, se o dono preferir um único momento de gravar).
- **Prévia:** reconstruir a HUD na página não compensa agora (as barras e a distância à câmera vêm de animação; o `lvlglb` só tem a
  pose de repouso). A prévia é o "Test in game" que já existe; uma prévia 2D das texturas basta para editar.

### 6.3 Opcional, com mudança no nativo
- **PNG único para todas as fases:** um apelido sem nome de fase, por exemplo `mods/<mod>/hud/<elemento>.png` (`atlas`, `special`,
  `boss`, `ring`, `textbox`, `letterbox`, `buttons`, `healthicon`, `font`). Precisa saber, na carga, que texId é de qual elemento:
  um gancho na saída de `Hud::parseHudElements`/`dbInitDb` (ou no `wotm_levelpatch.inc`, que já roda na entrada do `dbInitDb` com a
  imagem inflada) percorre os nós de HUD da imagem e anota texId do disco → elemento; o `modTexture`
  (`native/wotm_native.hpp`) cai no apelido quando não há PNG com o nome da fase. Evita 19 cópias e o problema dos ids.
  As fases B continuam precisando de PNG próprio (ou o apelido vale só para a versão A, detectada pelo tamanho/forma do nó).
- **Esconder elementos por fase** (ex.: sem o anel de pegar): campo `"hud": {"hide": ["0x1F42"]}` no `levels/<FASE>.json`, aplicado
  no desenho (filtro por `hudId`/`btnId` no `Scene::drawCs`), não trocando o nó por NULL na imagem (o `parseHudElements` guarda os
  ponteiros e o `Hud::update` usa os `_cs` todo quadro).
- **Mover/escalar a HUD:** possível em tese pelas transformações dos nós (ou pelo `_cs` no gancho do `Hud::update`), mas o widescreen
  e a HUD de 3/4 jogadores já reposicionam a HUD no nativo (`NATIVE.md`); não recomendo pelo editor de fase.
- **Mensagens de dica da aventura:** trocar a tabela `Hud+0x2D4` na saída do `Hud::initAfter` por textos do mod. É texto de
  aventura, não de fase; fora do escopo do editor.

### 6.4 Minimapa
Não existe no jogo, então não há o que editar nem refazer. Se o dono quiser um (mod novo, nativo): desenhar ao vivo a partir do que
o patch já mantém certo (caixas de colisão em XY, `FORMATS.md`, "Stage collision", e a grade `minX..maxY`), e não de uma imagem
gerada no editor, que ficaria velha a cada patch. O editor poderia só gravar opções (cor, escala) num campo do patch.

## 7. Pendências
- Testar no jogo um PNG de HUD (ex.: `mods/Teste/LVL/central_t0072.png` com as barras recoloridas) e um da fonte
  (`central_rtx_t0068.png`) e conferir o log `[mods]`. Não testado aqui (pesquisa sem abrir o jogo).
- Confirmar qual CHAR_INSTANCE é qual barra (0x1F48/49/4B/4C) lendo o `Hud::update` (os `animationSetToPercent` com os handles de
  `+0x130..0x160`), se o painel quiser nomes exatos.
- Ver no jogo como fica a HUD em CANYON1/TOKYODM (versão B) para descrever a diferença na referência.
