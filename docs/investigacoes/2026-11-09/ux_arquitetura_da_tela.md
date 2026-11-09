# Editor de fase: arquitetura de informação e mapa da tela (ângulo 1)

Auditoria somente de leitura de `tools/level_editor/web/` (index.html, app.js, style.css, mais tex/light/hud/terrain). Heurísticas de Nielsen
(N1 estado do sistema, N2 mundo real, N3 controle, N4 consistência, N5 prevenção de erro, N6 reconhecer em vez de lembrar, N7 flexibilidade,
N8 minimalismo, N9 erros, N10 ajuda). Severidade 0 a 4 (escala de Nielsen). Esforço S/M/L. Capturas em `docs/screenshots/2026-11-09/`.

Observação de contagem: o enunciado fala em 17 botões no `#overlay`; em `index.html:133-151` há 16 (look, grid, layersBtn, vFly, vOrbit,
focusSel, frame, gridBtn, sptBtn, pupBtn, aipBtn, fopBtn, picBtn, rulerBtn, shotBtn, keysBtn). Sete deles são de vista.

## 1. Inventário: o que cada controle faz e onde deveria estar

Grupos lógicos usados como régua (inspirados em CK, Blender, Unity, Godot, Unreal, Tiled):
**Projeto** (qual fase, qual mod, salvar, empacotar), **Edição** (desfazer, duplicar, apagar), **Criar** (colocar coisas novas),
**Propriedades** (da seleção e da fase), **Vista** (câmera, camadas, medir; nunca grava nada), **Teste** (rodar no jogo), **Configuração do programa**.

### 1.1 Barra superior `#bar` (index.html:19-43), 16 controles

| Controle | Faz | Grupo certo | Onde está |
|---|---|---|---|
| `#stage` (select), `#newStage` (+), `#editStage` (lápis) | escolhe a fase; cria fase custom; edita nome/base/narrador/RANDOM | Projeto | ok no lugar; "+" e lápis só com ícone |
| `#mod` (select, inclui "New mod") | mod onde se grava | Projeto | ok; criar mod escondido dentro do select |
| `#packMod` | baixa a pasta do mod em .zip | Projeto (File > Pack) | ícone solto ao lado do select |
| `#undo`, `#redo` | desfazer/refazer | Edição | entre "Pack mod" e "Check": longe da área de trabalho, perto do projeto |
| `#check` | aplica o patch ao disco e confere colisão | Teste/validação (secundário) | par do Save, com peso visual igual |
| `#save` | grava `mods/<mod>/levels/<STAGE>.json` | Projeto | ok |
| `#gameInd` | estado do jogo | Teste | ok (some quando fechado) |
| `#test`, `#testHere`, `#testOpts`, `#reload`, `#stop` | rodar, rodar daqui, opções, recarregar, fechar | Teste | 5 controles para um conceito |
| `#help`, `#settings` | atalhos; disco/pasta de mods/exe | Configuração/ajuda | ok |

### 1.2 Trilho `#rail` (index.html:45-52), 5 controles

`#modeObjects`, `#modePeds`: modos reais (exclusivos, `aria-pressed`, atalhos 1/2, trocam o painel esquerdo e o direito).
`#texBtn`, `#lightBtn`, `#hud`: abrem diálogos de propriedades **da fase** (texturas, luz/névoa, HUD e céu). Não são modos.

### 1.3 Painel esquerdo `#left` (index.html:53-92)

Modo Objects: busca `#filter`, chips de tipo/Changed, lista `#list`, `#count`. Modo Peds: medidores, `#pedLive`, `#pedList`. Está no lugar
(é a "hierarquia"/Object Window). Falta o botão de **criar** aqui.

### 1.4 Vista `#view` (index.html:93-171)

| Elemento | Conteúdo | Julgamento |
|---|---|---|
| `#toolBar` (98-114) | Move/Turn, Snap + passos, `#pedBar` (Add group, Add sidewalk) ou `#propBar` (Add prop) | Move/Turn/Snap são manipulação (certo). "Add" é criar, não manipular |
| `#propMenu` (115-130) | props do mod, Prefabs (+Turn, Save selection), Sky dome, Cloud layer, Import .glb, Terrain | 4 trabalhos diferentes (IA-2) |
| `#overlay` (132-153) | 16 botões | 7 de vista, 9 de outra natureza (IA-1) |
| `#layersMenu` (154-161) | Pedestrians, Collision, Minimap, Vehicle net | mistura dado e widget (IA-13) |
| `#minimap`, `#band`, `#hint`, `#notes`, `#busy` | widgets e avisos | ok |

### 1.5 Painel direito `#right` (index.html:172-335)

`#noSel` (atalhos, resumo da fase, Getting started), `#props` (Transform, Actions, Arrange, Behavior, Name, Details), `#delProps`, `#pedNoSel`,
`#pedProps`. A ordem do inspetor da seleção é boa (o que mais se usa primeiro). O vazio (`#noSel`) é o único lugar com contexto da fase.

### 1.6 Saída `#output` e rodapé `#status` (337-363)

Abas Messages/Check/Game; o rodapé tem 6 botões que abrem essas abas. Padrão de editor (VS Code/Godot): está bom.

### 1.7 Diálogos

`#dlg` Settings, `#modDlg`, `#keysDlg`, `#sptDlg` (Start points e Power-ups, mesmo diálogo), `#netDlg` (AI net e Vehicle net), `#gridDlg` (Stage size),
`#palDlg` (Ctrl+P), `#stageDlg`, `#propDlg` (Import), `#pfDlg`, `#askDlg`, e por JS: Textures, Light, HUD, Terrain, Test options.
Todos `showModal()`, exceto Light (`light.js:210`, `dlg.show()`, não modal, com prévia ao vivo).

## 2. Comparação com os editores maduros

| Função | CK | Blender | Unity/Godot/Unreal | Aqui |
|---|---|---|---|---|
| Criar objeto | arrastar da Object Window; menu Render Window | menu **Add** no header da vista | **+** na Hierarchy / Place Actors | botão na barra de manipulação da vista, menu com 4 assuntos |
| Propriedades da fase/mundo | World/Cell settings, Lighting em janela própria | abas Scene/World no Properties | World Settings (Unreal), WorldEnvironment (Godot) | 4 contêineres diferentes (IA-3) |
| Vista | só câmera, shading, overlays | header: Viewport Shading e **Overlays** em popovers | idem, Gizmos/Overlays em popover | 16 botões chatos numa barra |
| Marcadores/entidades (spawns, redes) | objetos na Render Window | objetos | nós/atores na hierarquia | diálogos modais com tabela de números |
| Comando de projeto | menu File | menu File | menu File | ícones soltos na barra |
| Rodar | Play / Ctrl+F5 etc. | n/a | Play com ▾ de opções | 5 botões |
| Configuração do programa | Preferences | Preferences | Preferences | `#settings` (certo) |
| Propriedades de mapa sem seleção | n/a | n/a | **Tiled: propriedades do mapa quando nada está selecionado** | `#noSel` com atalhos |

A regra comum: **a vista só olha e manipula; criar fica num menu Add; propriedades do mundo ficam num painel de propriedades; marcadores são
objetos da cena**. O editor viola as três nos pontos listados abaixo.

## 3. Achados

### IA-1 `#overlay` mistura seis famílias em 16 botões (sev 3, esforço M)
- **Onde:** `index.html:132-153`, `style.css:423`.
- **Heurística:** N8, N6, N4.
- **Problema:** só estes são de vista: `#look`, `#grid`, `#layersBtn`, `#vFly`, `#vOrbit`, `#focusSel`, `#frame`, `#rulerBtn`, `#shotBtn`. Os outros são de outra natureza e
  **gravam coisas**: `#gridBtn` (tamanho da grade de colisão), `#sptBtn`/`#pupBtn` (arquivos do mod), `#aipBtn`/`#fopBtn` (redes do mod), `#picBtn` (grava
  `levelpics/` na hora), `#keysBtn` (ajuda). Na barra "View" um botão pode alterar a fase ou o disco do mod e ele tem o mesmo aspecto de um toggle de vista.
  Tudo só com ícone (16 alvos de 28 px; cinco deles com ícones de outras coisas, IA-14).
- **Proposta:** a barra superior direita da vista fica só com Camera (`#vFly`, `#vOrbit`, `#focusSel`, `#frame`), um popover **Display** (look, grid, camadas, minimapa; IA-13),
  `#rulerBtn` e `#shotBtn`: 7 controles. Os seis de dados vão para a aba **Stage** do painel direito (IA-3); `#keysBtn` sai (IA-9).
- **Evidência:** capturas 01, 05; `index.html:144-152`.

### IA-2 O menu "Add prop" faz quatro trabalhos que não são "adicionar prop" (sev 3, esforço M)
- **Onde:** `#propMenu`, `index.html:115-130`; `app.js:3107-3151`.
- **Heurística:** N4, N2, N6.
- **Problema:** (a) lista de props do mod e Prefabs: colocar objeto (certo); (b) `#pfTurn` e `#pfSave`: parâmetro de colar e **salvar** seleção, no meio do menu de colocar;
  (c) `#pmSky` e `#pmClouds`: **configuração da fase** (substituem o domo do céu e a camada de nuvens), selects com estado persistente num menu que parece de ações;
  (d) `#pmImport` e `#pmTerrain`: geradores (importar .glb, gerar relevo de PNG). O dono acertou: o terreno não é "um prop que você coloca", é uma ferramenta que
  *produz* um prop. Mas Import .glb tem o mesmo defeito e fica ao lado sem ninguém reclamar: os dois são o grupo "criar modelo a partir de arquivo".
  Além disso, depois de gerado o terreno, o único caminho para reeditar é abrir o gerador e escolher "Start from" (`terrain.js:29`); não há "Edit terrain…" ao selecionar a peça.
- **Proposta:** menu **Add** no cabeçalho do painel esquerdo (IA-8). Seções: *Browse models… Ctrl+P* (a paleta), *Props of <mod>*, *Prefabs* (com Turn e "Save selection…" no
  rodapé da seção), e uma seção separada *New model from a file* com **Import .glb…** e **Terrain from a heightmap…**. `#pmSky` e `#pmClouds` vão para o diálogo de luz (renomear para
  "Environment": fog, background, lights, sky, clouds; o diálogo já é não modal e já usa o Game look). Para um item `rule === 'Terrain piece'` (`app.js:1169`), acrescentar a ação
  "Edit terrain…" em Actions.
- **Evidência:** captura 04; `app.js:3142-3149` popula os selects de céu dentro de `showPropMenu`.

### IA-3 As propriedades da fase estão espalhadas por quatro contêineres (sev 3, esforço M)
- **Onde:** `#bar` (`#editStage`), `#rail` (`#texBtn`, `#lightBtn`, `#hud`), `#overlay` (`#gridBtn`, `#sptBtn`, `#pupBtn`, `#aipBtn`, `#fopBtn`, `#picBtn`), `#propMenu` (`#pmSky`, `#pmClouds`).
- **Heurística:** N4, N6, N8.
- **Problema:** doze coisas são "da fase" (nome/base, tamanho da grade, luz, névoa, céu, nuvens, texturas, HUD, imagem de seleção, pontos de largada, power-ups, redes) e cada uma mora num
  lugar diferente. Quem procura "onde mudo o céu" precisa adivinhar entre rail, menu Add prop e diálogo de luz. Nenhum editor de referência faz assim: Unreal tem **World Settings**,
  Blender tem **World/Scene**, Tiled mostra as propriedades do mapa quando nada está selecionado, o CK tem um menu World.
- **Proposta:** o painel direito ganha duas abas fixas, **Selection** e **Stage** (a aba é a mesma estrutura de `#noSel`/`#props`, só que visível). A aba Stage, em seções dobráveis
  `details.fold` (já existe o padrão em `#secArrange`): *Stage* (nome, base, o resumo e o "Saves to" que hoje só aparece em `#sumStage/#sumPath`, e o botão do lápis), *Size and collision* (`#gridBtn`),
  *Environment* (`#lightBtn`, céu, nuvens), *Pictures* (`#texBtn`, `#hud`, `#picBtn`), *Gameplay* (`#sptBtn`, `#pupBtn`, `#aipBtn`, `#fopBtn`). Selecionar um objeto muda para Selection; Esc
  volta ao estado anterior. Os botões são **movidos** no HTML mantendo o `id` (os handlers são por `$('id').onclick`), com `<span class="lbl">` visível: viram linhas rotuladas, não ícones.
- **Evidência:** capturas 01, 02, 03, 14.

### IA-4 O trilho mistura modos com lançadores de diálogo (sev 2, esforço S)
- **Onde:** `index.html:45-52`; `style.css:228-238`.
- **Heurística:** N4, N6.
- **Problema:** `#modeObjects`/`#modePeds` ficam acesos e trocam todo o contexto; `#texBtn`, `#lightBtn` e `#hud` abrem um diálogo e voltam. O mesmo estilo visual (36 px, só ícone) para
  as duas coisas. O comentário em `style.css:228` ("Objects and Pedestrians are modes; Fight HUD opens its dialog") mostra que o autor sabe da diferença. Além disso, **Textures e HUD se sobrepõem**: o diálogo
  Textures tem o filtro "HUD and sky" (captura 07) e `hud.js` grava os mesmos `<mod>/LVL/<stage>_t<id>.png`; são dois caminhos para trocar a mesma imagem (a verificar em `hud.js`: o `every` "igual em todas as fases"
  talvez não exista em `tex.js`; se não existir, o HUD fica como atalho do Textures com o filtro já aplicado).
- **Proposta:** o trilho fica só com modos (Objects, Peds, e depois os de IA-5). Textures, Light e HUD vão para a aba Stage (IA-3). Se `tex.js` cobrir o `every`, o HUD vira o filtro do Textures.
- **Evidência:** `hud.js:1-2`, `hud.js:105`, captura 07.

### IA-5 Peds é um modo; os outros marcadores da fase são tabelas modais (sev 3, esforço L)
- **Onde:** `#sptDlg` (`app.js:3533-3550`), `#netDlg` (`app.js:3733`), `index.html:506-548`.
- **Heurística:** N4, N3, N7.
- **Problema:** grupos de pedestres, pontos de largada, power-ups, nós da rede de IA e da rede de veículos são o mesmo tipo de coisa (entidades posicionadas na fase), mas só pedestres têm modo,
  lista, gizmo e inspetor. Os outros quatro abrem um diálogo **modal** com uma tabela de números (captura 13: 20+ linhas de #, Type, x, y, z, Links) e um botão "Aim" por linha; a vista, que é onde se faz
  a tarefa espacial, fica bloqueada. Cada um tem seu próprio Save no diálogo (IA-6). Em qualquer referência (CK, Unreal, Godot) um spawn ou um nó de navegação é um objeto que se pega e arrasta.
- **Proposta (incremental):** passo 1, mover os botões para a aba Stage (IA-3) sem mudar o diálogo. Passo 2, tornar o diálogo não modal e ancorado no painel esquerdo, como `#pedPanel`, com o modo
  ativado pelo rail (`setMode('spt')` etc., reaproveitando `enterMode`/`applyPedsView`, `app.js:1704-1721`). Passo 3, marcadores selecionáveis na vista com o gizmo, e o inspetor mostrando x/y/z, Type, Links.
- **Evidência:** capturas 03 (peds, modo completo) e 13 (rede, diálogo).

### IA-6 Há três modelos de gravação e a tela não diz qual é qual (sev 3, esforço M)
- **Onde:** botões Save: `#save`, `#sptSave`, `#netSave`; imediatos: Textures, HUD, `#picBtn`, Import/Terrain/Floor; `app.js:901,1270` (`.dirty` só reflete o patch).
- **Heurística:** N1, N5, N4.
- **Problema:** (1) patch da fase: Undo/Redo, `#save`, indicador de sujo; (2) arquivos do mod por diálogo: cada um com Save e "Back to the disc's", fora do Undo e do `#save`; (3) gravação imediata:
  Textures ("Changes are written to your mod right away; Undo does not reach them", captura 07), HUD, imagem da fase, props importados/gerados, "Apply and add a floor". O botão Save e o texto "No unsaved changes" do
  rodapé só falam do modelo 1. Quem fecha uma rede de IA com Close perde as edições (o `close` recarrega o arquivo, `app.js:3904-3907`) e quem muda uma textura não tem Undo; ambos em controles que parecem do mesmo tipo.
- **Proposta:** (a) agrupar por consequência na aba Stage: *Writes to your mod now* (Textures, HUD, imagem), *Has its own Save* (pontos, redes), *Part of the stage* (tamanho, luz, céu); (b) rótulo
  curto em cada linha ("saves now"/"own Save"); (c) quando as redes/pontos virarem modos (IA-5), entram no patch e no Undo e o modelo 2 desaparece.
- **Evidência:** captura 07, `index.html:520,546`.

### IA-7 `#toolBar` e `#overlay` colidem; em 1100 px a barra de manipulação some (sev 3, esforço S)
- **Onde:** `style.css:416-426,449-453`, posicionamento absoluto independente (`#toolBar{left:8px}`, `#overlay{right:8px}`).
- **Heurística:** N3, N5.
- **Problema:** captura 14 (1100x650): o `#overlay` tem ~510 px com só ícones e a vista tem 480; ele começa em x=278, 38 px **fora** da vista, e cobre Move/Turn/Snap/Add prop inteiros (só G/R e Ctrl+P restam).
  Captura 03 (1600x900, Pedestrians): "Add sidewalk" fica atrás do overlay (o ícone aparece cortado em x≈693). Por conta, em Objects com Snap ligado (+~116 px de campos) o "Add prop" também fica sob o overlay a 1600
  (não capturado). Os dois são `z-index:2`, o último do DOM vence.
- **Proposta (correção rápida, S):** envolver os dois num `#viewTop { position:absolute; top:8px; left:8px; right:8px; display:flex; justify-content:space-between; gap:8px; flex-wrap:wrap; pointer-events:none }`
  com filhos `pointer-events:auto` e `position:static`; o `#layersMenu` e o `#propMenu` continuam com `top:48px`, mas passam a depender da altura real da faixa (`top:calc(var(--viewTopH))`). A correção de fundo é IA-1 e IA-8, que
  encolhem os dois lados.
- **Evidência:** capturas 03, 14.

### IA-8 A paleta Ctrl+P, o caminho mais rico de "adicionar", não tem ponto de entrada visível (sev 3, esforço S)
- **Onde:** `app.js:4240-4249,4675`; `index.html:570-584`; nenhum elemento chama `openPalette`.
- **Heurística:** N6, N7.
- **Problema:** a paleta tem as abas "Add to the stage" (props do mod **e** todos os modelos das outras fases do jogo) e "Find in this stage". O único botão visível, "Add prop" (`#addProp`), mostra só os props do mod:
  quem não leu o diálogo de atalhos nem sabe que dá para pôr um prédio de outra fase. A única pista é a dica em `keysDlg` e no `help` da própria paleta.
- **Proposta:** primeira linha do menu Add: "Browse models… (Ctrl+P)"; e no cabeçalho do painel esquerdo, junto da busca, o ícone `+` (Add) e a lupa "Find…" (mesma paleta, aba Find). Reaproveita `openPalette`/`setPalTab`.
- **Evidência:** captura 04 (menu só com props do mod e "No props for Midtown yet").

### IA-9 Comandos duplicados em dois ou três lugares (sev 2, esforço S)
- **Onde / o quê:**
  - Atalhos: `#help` (`index.html:41`) e `#keysBtn` (`:152`), além de `?` e F1.
  - Testar daqui: `#testHere` (`:36`) e o botão "Test from here" dentro do diálogo Test options (captura 10).
  - Adicionar: `#addProp`, a paleta (Ctrl+P, aba Add) e `#pmImport`/`#pmTerrain` (IA-2).
  - Pedestres: modo `#modePeds` e camada `#pedsOn` (esta desabilitada dentro do modo, `app.js:1689`); aceitável, mas a tecla P só funciona em Objects (`app.js:4718`).
  - Rede de veículos: `#fopBtn` (diálogo), `#fopOn` (camada) e o próprio diálogo "mostra" a rede (`index.html:160`); a rede de IA só tem diálogo, sem camada (assimetria, IA-13).
- **Heurística:** N4, N8.
- **Proposta:** manter `#help`, remover `#keysBtn`; manter `#testHere` ou o botão do diálogo, não os dois (IA-11); uma só entrada de "adicionar" com o resto como itens do mesmo menu (IA-2, IA-8); camada de rede de IA no mesmo menu Display.

### IA-10 A mesma informação aparece quatro vezes; o contexto essencial fica no estado vazio (sev 2, esforço S)
- **Onde:** "0 changed" em `#count` (esquerda, base), `#sumCounts` (direita, `#noSel`), chip `#changedOnly` (`.n[data-n=changed]`) e `.st.count` (rodapé); captura 01 mostra os quatro ao mesmo tempo.
  "Saves to mods/ZZNetTest/levels/CENTRAL.json" e "Mod: ZZNetTest" só existem em `#sumMod`/`#sumPath` (`index.html:182-188`), que **somem ao selecionar algo**.
- **Heurística:** N8, N1.
- **Proposta:** manter o chip (filtra) e o rodapé (estado); remover `#count` e `#sumCounts`; levar "fase · mod" para o início do rodapé (`.st.msg` já mostra "Midtown: 331 objects…") com o caminho no `title`, e o bloco "Stage" para a aba Stage (IA-3).

### IA-11 O grupo de teste tem cinco controles e um botão sempre desabilitado (sev 2, esforço M)
- **Onde:** `index.html:35-39`; `app.js:2674-2686`.
- **Heurística:** N8, N6, N4.
- **Problema:** `#test`, `#testHere` (ícone de pino de mapa, ambíguo), `#testOpts` (ícone `tune`, igual ao de "configurações" ao lado da engrenagem), `#reload` (desabilitado e visível enquanto o jogo está
  fechado, ocupando ~120 px, captura 01), `#stop`. O código já alterna Test/Reload como "primary" conforme o estado (`app.js:2676-2678`): são dois estados do mesmo botão.
- **Proposta:** um botão dividido **Test in game ▸ | ▾** (o ▾ abre: Test from here, Test options…); com o jogo rodando o mesmo botão vira **Reload in game** (ou "Save & reload", como já faz) e aparece `#stop`.
  Reload e Stop só existem enquanto o jogo roda (já é assim para Stop). Fecha de 5 controles visíveis para 2 (3 com o jogo aberto) e libera ~170 px.

### IA-12 Check pesa como Save, mas Save e Test já o executam (sev 2, esforço S)
- **Onde:** `index.html:31-32`; texto `#checkOut` (`:351`: "Check, Save, Test in game and Reload in game check your changes"); "Getting started" (`:193`: "Save, then Test in game").
- **Heurística:** N4, N8.
- **Problema:** Check é uma validação que o fluxo normal faz sozinho, mas ocupa o botão mais próximo do Save com o mesmo estilo. O rótulo "Check" não diz o que checa (a dica explica: aplica ao disco e
  confere colisão). O rodapé tem outro Check ("Not checked", `.st.check`) que só abre a aba.
- **Proposta:** `#check` perde o destaque (ghost, só ícone) ou vai para um menu "⋯" da barra (junto de Pack mod e Settings); o item do rodapé passa a *executar* a checagem quando está "Not checked". Rótulo: "Check collision".

### IA-13 O menu Layers mistura dado com widget e não bate com os botões ao lado (sev 2, esforço M)
- **Onde:** `index.html:133-135,154-161`.
- **Heurística:** N4, N6.
- **Problema:** `#look` (aparência) e `#grid` (guia) são toggles soltos; `#layersBtn` abre outro grupo de toggles. Dentro do menu: Pedestrians, Collision, Vehicle net são camadas de dado; Minimap é widget de interface. Há
  camada para a rede de veículos mas não para a de IA, nem para pontos de largada/power-ups (que, pelas capturas, aparecem como marcadores na vista mas sem toggle).
- **Proposta:** um único popover **Display** (como Viewport Shading + Overlays do Blender): *Look* (Game look), *Guides* (Grid, Minimap), *Overlays* (Collision, Pedestrians, Vehicle net, AI net, Start points e Power-ups). Mesmas teclas L/C/P/N.

### IA-14 Ícones iguais para coisas diferentes (sev 2, esforço S)
- **Onde / o quê:** `account-group-outline`: `#sptBtn` (Start points) e grupos de pedestres (`#pdIcon`, `app.js:1763`, `PED_SECTIONS`); `vector-polyline-plus`: `#aipBtn` (rede de IA) e `#pedAddSeg` (Add sidewalk);
  `road-variant`: `#fopBtn` (rede de veículos) e calçadas (`app.js:1756`); `image-edit-outline`: `#texBtn` (Textures) e `#picBtn` (imagem da fase); `package-variant-closed`: `#pupBtn`, `#pfSave` e o orçamento no rodapé;
  `map-outline`: `#stage` e `#miniOn`.
- **Heurística:** N6, N4.
- **Proposta:** como a maioria dessas ações sairá de barras só com ícone (IA-1/IA-3) e ganhará rótulo, o problema diminui; mesmo assim, trocar `sptBtn` (ex.: `flag-outline`), `pupBtn` (`heart-plus-outline`),
  `aipBtn` (`graph-outline`), `picBtn` (`image-frame`) por ícones únicos; precisam entrar em `vendor/mdi/`.

### IA-15 O diálogo "Stage size" é uma gaveta de bagunça com semântica mista (sev 2, esforço S)
- **Onde:** `index.html:549-569`; `app.js:3345-3352,3909-3923`.
- **Heurística:** N5, N4, N2.
- **Problema:** o título diz tamanho, mas dentro há: retângulo da grade, "Grow", "Back to the disc's", "Apply and add a floor", desligar muros, "Take out the stage's bare scenery" e **"Delete every object"**. Apply/Cancel sugerem transação;
  porém "Delete every object" age na hora (com `snapshot`, então desfaz) e "Apply and add a floor" grava um prop no disco do mod. Cancel não desfaz o que "Delete every object" já fez.
- **Proposta:** renomear "Stage size and scenery"; "Delete every object" e "Take out bare scenery" viram uma seção "Start from empty" na aba Stage com confirmação por `ask()` (como o resto do editor); o diálogo fica com retângulo, muros e o botão do piso.

### IA-16 "Pickups" e "Power-ups" designam coisas diferentes com nomes quase iguais (sev 2, esforço S)
- **Onde:** chip `Pickups` (`index.html:63`, 115 itens na captura 01) e botão `#pupBtn` "Power-ups: the health, stamina and other pickups lying in the stage" (`:146`).
- **Heurística:** N2, N4.
- **Problema:** a própria dica do `#pupBtn` usa "pickups" para explicar "power-ups", e a lista de Objects também tem "Pickups" (carros, objetos de telhado, geradores). Quem procura onde colocar vida/stamina vê "Pickups" na lista e entra por ela.
- **Proposta:** na lista, renomear a seção para algo como "Grabbable" ou "Things to pick up" (a confirmar com o dono o termo do jogo), ou rotular `pupBtn` "Health and stamina items"; ao menos uma das duas dicas explica a diferença.

### IA-17 Cada popover reimplementa abrir/fechar/foco/Esc (sev 1, esforço M, habilitador)
- **Onde:** `app.js:3079-3087` (Layers), `3107-3151` e `3335` (Add prop), Esc em `4698-4699`, foco inicial em `3150`.
- **Heurística:** N4 (consistência de comportamento).
- **Problema:** hoje há dois menus; os passos IA-1/IA-2/IA-13 criam mais três (Display, Add, Test ▾). Sem um componente único cada um terá seu próprio fechar-ao-clicar-fora, Esc, `aria-expanded`, foco e navegação por setas.
- **Proposta:** extrair `menu.js` (`openMenu(btn, panel)`, fecha os outros, `aria-expanded`, setas, Esc devolve o foco ao botão) e portar `#propMenu` e `#layersMenu` como primeiro passo, sem mudar o visual.

### IA-18 Comandos de projeto são ícones soltos e "New mod" está escondido (sev 2, esforço S)
- **Onde:** `index.html:22-26`; `app.js:4741` (opção NEW_MOD no select).
- **Heurística:** N6, N7.
- **Problema:** "+" é *New stage*, mas lido da esquerda para a direita o "+" depois de "Stage" lembra "criar"; o lápis é *Edit stage* e fica desabilitado em fase do disco sem dizer por quê na hora (só o `title`); `#packMod` é um
  pacote sem rótulo; "New mod" só existe como opção dentro do select. Não há nada com cara de menu File.
- **Proposta:** mover New stage, Edit stage, New mod e Pack mod para um botão `⋯` (ou "File ▾") ao lado dos selects, com rótulo e o motivo quando desabilitado ("Disc stages can't be renamed"). Os selects ficam na barra (é o contexto atual).

## 4. O que está bom (não mexer)

- A ordem do inspetor da seleção (Transform, Actions, Arrange, Behavior, Name, Details) e as seções dobráveis `details.fold`.
- Rodapé com abas de saída (Messages/Check/Game) e o painel de saída redimensionável: convenção de IDE.
- O slot do Add em `#toolBar` trocar de conteúdo com o modo (`#pedBar` x `#propBar`): a ideia de ação de criar **contextual ao modo** é certa; só o lugar (barra de manipulação) é discutível.
- Atalhos nas dicas e nos menus (`<kbd>`), `aria-pressed`/`aria-expanded` nos toggles, Ctrl+Z/Y/S, Home, F.
- O modo Pedestrians completo (lista, medidores, gizmo, inspetor) é o modelo de como os outros marcadores deveriam ser (IA-5).
- Estado vazio útil com atalhos e "Getting started"; ele só precisa de duas abas para dividir a vaga com o contexto da fase.

## 5. Novo mapa da tela

### Antes (1600x900, Objects)

```
┌ #bar ───────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ Level Editor │ [Stage v] + ✎ │ [Mod v] pack │ ↶ ↷ │ ········ │ Check Save │ ● [Test] pin tune [Reload*] ■ │ ? cog │
├────┬─────────────┬──────────────────────────────────────────────────────────────────┬──────────────────────────┤
│rail│ #left       │ #toolBar: Move Turn Snap [Add prop v]       #overlay (16):       │ #right                    │
│Obj │ search      │   (propMenu: props, Prefabs, Sky, Clouds,    look grid layers    │ Nothing selected          │
│Ped │ chips       │    Import, TERRAIN)                          Fly Orbit focus     │  keys, summary, Saves to  │
│ -- │ list        │                                              frame SIZE START    │  ...or the Selection:     │
│Tex │             │                                              POWER AI VEH PIC    │  Transform Actions        │
│Lit │             │                                              ruler shot keys     │  Arrange Behavior Name... │
│HUD │ N objects·  │                                              (layersMenu)        │                           │
│    │ 0 changed   │ (* = disabled until the game runs)                               │                           │
├────┴─────────────┴──────────────────────────────────────────────────────────────────┴──────────────────────────┤
│ v  message                                   Saved?  Check  budget  Game  N changed                              │
└───────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Depois

```
┌ #bar ────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ Level Editor │ [Stage v] [Mod v] [File v] │ ↶ ↷ │ ········ │ Save │ [> Test in game | v]  (■ Stop, Reload: running) │ ? cog │
│   File v: New stage, Edit stage, New mod, Pack mod, Check collision      Test v: Test from here, Test options  │
├────┬─────────────┬──────────────────────────────────────────────────────────────────┬──────────────────────────┤
│rail│ #left       │ #viewTop                                                         │ #right: [Selection|Stage]│
│MODES│ [+ Add v]  │ [Move Turn │ Snap steps]            [Fly Orbit focus frame]       │ Selection: as today      │
│Obj │ [search] [find] │                                [Display v] ruler shot        │ Stage (details.fold):    │
│Ped │ chips       │   Add v: Browse models… Ctrl+P                                   │  Stage      name, base, saves │
│(Spw│ list        │          Props of <mod>, Prefabs                                 │  Size and collision      │
│ AI │             │          New model from a file:                                  │  Environment light fog   │
│ Veh│             │             Import .glb…, Terrain from a heightmap…              │             sky clouds   │
│later)            │   Peds: [Add group] [Add sidewalk]                               │  Pictures   Textures HUD │
│    │             │   Display v: Look | Grid, Minimap |                              │             stage pic    │
│    │             │     Overlays: Collision, Peds, Vehicle, AI, Spawns               │  Gameplay   Start points │
│    │ 331 objects │                                                                  │   Power-ups AI Vehicle   │
├────┴─────────────┴──────────────────────────────────────────────────────────────────┴──────────────────────────┤
│ v  Midtown · ZZNetTest  message                  Saved  Check  budget  Game  N changed · M selected            │
└───────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

Regras do mapa novo:
- **Vista**: câmera, aparência, camadas, régua, captura. Nada que grave. 7 controles à direita, Move/Turn/Snap à esquerda.
- **Criar**: o menu Add no painel esquerdo (junto da lista que ele povoa); no modo Pedestrians o mesmo slot mostra Add group/Add sidewalk.
- **Propriedades**: abas Selection (o que está selecionado) e Stage (o que é da fase), no painel direito.
- **Projeto**: `[Stage] [Mod] [File]` + Save; **Edição**: Undo/Redo; **Teste**: um botão dividido.
- **Trilho**: só modos, que trocam painel esquerdo, direito e ferramentas.

## 6. Ordem de migração (cada passo deixa o editor funcionando)

Os handlers são ligados por `id` no carregamento; **mover** um elemento dentro de `index.html` mantendo o `id` não exige tocar no JS, e as receitas de teste por CDP e o `window.wotmDebug` continuam valendo. Cada passo é um commit.

1. **IA-7, correção de colisão**: `#viewTop` (CSS + dois `div` no HTML), sem mexer em comportamento. S.
2. **IA-9/IA-14, limpeza**: remover `#keysBtn`; trocar os ícones repetidos. S.
3. **IA-11, Reload fora quando fechado**: esconder `#reload` com `s === 'closed'` (uma linha em `showGame`). S.
4. **IA-17, `menu.js`**: componente de popover e porte de `#propMenu`/`#layersMenu`, sem mudança visual. M.
5. **IA-3 (primeira metade), aba Stage com botões movidos**: criar a aba, **mover** `#gridBtn`, `#sptBtn`, `#pupBtn`, `#aipBtn`, `#fopBtn`, `#picBtn`, `#texBtn`, `#lightBtn`, `#hud` do overlay/rail para as seções, com rótulo visível, no mesmo commit em que saem dos lugares antigos (nunca dois lugares). M.
6. **IA-2/IA-8, menu Add no painel esquerdo**: mover `#addProp` e o conteúdo de `#propMenu`/`#pedBar`; adicionar "Browse models…"; deslocar Sky/Cloud para o diálogo de luz (renomeado Environment). M.
7. **IA-13, Display**: fundir `#look`, `#grid`, `#layersMenu` num popover; adicionar camada de AI net/Start points. M.
8. **IA-11/IA-12/IA-18, barra**: botão dividido de teste; Check e comandos de projeto para `File ▾`. M.
9. **IA-10/IA-15/IA-16, texto e informação**: remover contagens repetidas, reorganizar Stage size, ajustar termos. S.
10. **IA-5, marcadores como modos**: um por vez (Start points primeiro, tabela mais simples), depois as redes. L.
11. **IA-6, gravação**: rótulos de "grava agora/Save próprio" no passo 5; fim do modelo 2 no passo 10. M.

Risco principal: o passo 5 muda a posição de nove botões de uma vez para quem já os conhece; mitigar com o resumo da mudança nas notas de release e mantendo as dicas (`title`) como estão.
