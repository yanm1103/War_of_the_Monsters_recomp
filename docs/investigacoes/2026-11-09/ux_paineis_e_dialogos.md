# Editor de fase: auditoria de UX, painéis de conteúdo e diálogos (ângulo 3)

Escopo: Textures (`tex.js`), Light and fog (`light.js`), Fight HUD (`hud.js`), Terrain from a heightmap (`terrain.js`), Arrange (`arrange.js`),
Behavior/Name/Details do painel direito, Pedestrians (`peds.js`, `#pedPanel`, `#pedProps`), Start points / Power-ups / AI net / Vehicle net
(`sptDlg`, `netDlg`), Stage size (`gridDlg`), Test options (`testopts.js`), New/Edit stage (`stageDlg`), Add prop (`propMenu`, `propDlg`,
`palDlg`, `pfDlg`), Settings (`dlg`), atalhos (`keysDlg`), `askDlg`. Somente leitura: nada do editor foi alterado nem executado.
Fontes: `tools/level_editor/web/` (index.html, app.js, style.css, os .js de cada painel), `tools/level_editor/editor.py`, e as capturas em
`docs/screenshots/2026-11-09/`. Heurísticas: N1 visibilidade do estado, N2 correspondência com o mundo real, N3 controle e liberdade,
N4 consistência e padrões, N5 prevenção de erros, N6 reconhecer em vez de lembrar, N7 flexibilidade e eficiência, N8 estética e minimalismo,
N9 ajudar a reconhecer/diagnosticar/recuperar de erros, N10 ajuda e documentação. Severidade 0 a 4 (Nielsen). Esforço S/M/L.

## Resumo

O editor já tem um molde de diálogo decente (`.dlg-head` / `.dlg-body` / `.dlg-foot`, `askDlg` bem feito, Settings com validação inline, texto
todo em inglês sem mistura de português). O problema é que o molde é usado para **três trabalhos diferentes** sem distinção:

1. **Formulário** (cria/aplica algo e some): New stage, New mod, Import a prop, Save as prefab, Terrain, Settings. Aqui o modal é certo.
2. **Navegador/gerenciador** (Textures, Fight HUD): lista grande, grava no mod na hora.
3. **Ferramenta de edição da cena** (Start points, Power-ups, AI net, Vehicle net, Stage size, Light and fog): precisam do viewport e deveriam
   ser painel/modo, não modal.

O achado mais grave (PN-1): quatro dos diálogos da categoria 3 são **modais mas dependem de onde a câmera aponta** ("Add at the view's
aim", "Aim"), e o modal trava a câmera. O segundo grupo de achados é de **semântica de salvar**: há quatro modelos de gravação (imediato fora
do Undo, rascunho com Save próprio, patch com Undo + Save global, localStorage) sem sinal visual de qual é qual, e "Close"/Esc/X descartam
trabalho em silêncio em uns diálogos e não em outros. O terceiro é de **organização**: o que é "da fase" (luz, céu, nuvens, tamanho, pontos,
redes, texturas, HUD, figura da fase) está espalhado em cinco lugares (barra superior, trilha lateral, barra do viewport, menu Add prop,
menu Layers). Seção "Moldes propostos" e "Migração" no fim.

O que está bom e deve ser mantido: o painel Light (preview ao vivo, `restore` ao fechar, rótulos em palavras: "Starts at / Ends at"); o
`askDlg` (foco no botão primário seguro, Esc vira 'cancel', ícone âmbar, uma única instância); Settings (tema em preview com Cancel
desfazendo; badge por caminho; erro explicado sob o campo; Advanced só em clone); Textures (busca por número/tamanho, filtros com contagem,
arrastar PNG sobre o cartão, aviso de proporção antes de aplicar); Arrange (rótulo do botão muda para "Make 2 copies", limite de 500 explicado,
campos preenchidos pela seleção e que preservam o que a pessoa digitou); Pedestrians (medidores contra o limite do jogo, "Walks on sidewalk 1
[Show]"); Test options (clampa os números, guarda a escolha, resumo em uma linha); `newStage` bloqueia com mensagem em vez de criar sem mod.

---

## Achados

### PN-1 Pontos e redes são modais, mas dependem do aim da câmera que o modal trava (sev 4, esforço L; interino M)
- **Onde:** `#sptDlg` (index.html:506-522), `#netDlg` (523-548); `openPoints` showModal app.js:3549, `openNet` app.js:3733; botões `#sptAdd`
  "Add at the view's aim" (3553), `Aim` por linha (3513, 3683), `#netAdd` (3833); guarda de teclado `dialog[open]` app.js:4676; nós/cones com
  `raycast = () => {}` (3465, 3646); `dialog.spt { width: min(96vw, 860px) }` style.css:464. Captura 13 (AI net cobre quase todo o viewport).
- **Heurística:** N3, N7, N2 (e convenção de editor: a vista nunca fica inoperante enquanto se edita a cena).
- **Problema:** o trabalho dessas telas é colocar coisas *onde a câmera olha*. Com `showModal()` o canvas fica inerte (backdrop), o handler
  de teclado devolve cedo (WASD e voo mortos) e o diálogo de 860 px cobre o viewport. Para pôr o próximo nó a pessoa tem de **Save** (que já
  fecha o diálogo, 3581/3896), voar, reabrir, Add, e repetir; **Close**, Esc e X descartam o rascunho (3589/3904, ver PN-2). Uma rede de 20 nós
  vira 20 ciclos e 20 gravações. Os nós/cones são desenhados no viewport mas não se pode clicá-los nem arrastá-los (raycast desligado):
  só linhas de tabela com números. Em Pedestrians (que é um modo, com gizmo) o mesmo problema está resolvido; aqui não.
- **Proposta:** (interino, M) transformar os quatro em painel acoplado não modal como o Light (`class="dlg dock"`, `show()` em vez de
  `showModal()`), tirar o `Save` do `close()`, e armar um "Add" que pega o próximo clique no terreno (`viewPoint` já existe). (L) criar um
  modo "Pontos e redes" na trilha (como Pedestrians): lista à esquerda, propriedades do nó/ponto à direita (tipo, lista, heading, links com
  largura), nós clicáveis e arrastáveis com o gizmo, `Delete` apaga, Shift+clique em outro nó liga (cria o link) e a tabela de 860 px some.
  Referência: Creation Kit (travel/nav markers colocados no Render Window, janela de dados modeless) e Blender (edição de arestas no viewport,
  números no painel N).
- **Evidência:** capturas 13 e 13b; o código do guarda de teclado e do `showModal`; comentário do próprio `light.js:3-4` ("a panel docked at
  the right, not a modal dialog, so the view stays in sight") mostra que o motivo já foi reconhecido para a luz.

### PN-2 Fechar descarta em silêncio; "Back to the disc's" apaga o arquivo do mod sem perguntar (sev 3, esforço S)
- **Onde:** descarte no `close`: sptDlg app.js:3589, netDlg 3904, Light `light.js:104` e `lightClose` 216 (troca de fase, app.js:1033);
  reset: `#sptReset`/`#netReset` (index.html:520, 546) e `.lt-reset` (light.js:74), servidor `editor.py:741` (`os.remove(path)`).
- **Heurística:** N5, N3, N9.
- **Problema:** o botão "Close" do rodapé e o Esc são o mesmo gesto, mas o rascunho de nós/pontos/luz some sem confirmação, enquanto a fase
  toda pergunta "Unsaved changes" (`askUnsaved`). Apertar Esc por hábito perde uma rede inteira. E "Back to the disc's" fica ao lado de
  Close/Save, apaga o arquivo do mod na hora, fora do Undo, sem confirmação, e (em spt/net) a gravação anterior não volta.
- **Proposta:** rodapé padrão `[Cancel] [Apply]` (ver molde); com rascunho sujo, X/Esc/Cancel abrem `askDlg` "Discard 12 changes?" com
  [Keep editing] [Discard]. "Back to the disc's" vira item de menu "⋯" do painel e usa `ask('Delete your version?', ..., [['cancel','Cancel'],
  ['reset','Delete my version', true]])`. Light: ponto sujo no título do painel e a mesma confirmação ao fechar/trocar de fase.
- **Evidência:** código citado; `ask()` e `askUnsaved` já existem em app.js:2471-2522.

### PN-3 Quatro modelos de gravação sem sinal visual (sev 3, esforço M)
- **Onde:** (a) imediato no mod, fora do Undo: Textures/HUD (callout "Changes are written to your mod right away; Undo does not reach them",
  tex.js:33, hud.js:31), Terrain/Import (cria a pasta do prop), "Model name" e "Height/Strength" de prop (app.js:1388, 1416, 4550), Pack mod;
  (b) rascunho no diálogo + Save próprio: spt/net/Light ("Save to mod"); (c) no patch com Undo e o Save global: Stage size, Arrange, Name,
  Scale/Hit points; (d) localStorage: Test options. `unsavedCount()` (app.js:2505) só conta (c). Ctrl+S dentro de qualquer diálogo é engolido
  (app.js:4677). Status bar "No unsaved changes" e o botão Save da barra superior (title: só `levels/<STAGE>.json`).
- **Heurística:** N1, N4, N5.
- **Problema:** a pessoa não sabe, olhando, se o que acabou de fazer está salvo, é desfazível, e que botão grava. Efeito concreto: edita a
  luz, não aperta "Save to mod", aperta **Test in game**: o jogo lê o arquivo, não o rascunho, e `testGame` (app.js:3015) só avisa de mudança
  do patch. Mesmo com spt/net.
- **Proposta:** (1) classificar na UI: "Stage" (patch, Undo, Save global) versus "Files in your mod" (assets). (2) A barra de status
  mostra rascunhos pendentes de painéis ("Unsaved: Light, AI net"); `testGame`/`Save`/troca de fase os incluem na pergunta `askUnsaved`
  ("Save and test" salva também esses). (3) Verbos fixos: **Apply** = vale para o rascunho; **Save** = global; "Written to mod" como toast
  curto nos imediatos (já existe) e o callout fixo sai do corpo (PN-6). (4) Ctrl+S dentro de painel não modal grava o global (PN-4).
- **Evidência:** capturas 07 (callout), 08 (rodapé "Save to mod" desabilitado), status bar das capturas ("No unsaved changes").

### PN-4 O painel Light, "não modal", mata todos os atalhos e botões (sev 3, esforço S)
- **Onde:** guarda `if (document.querySelector('dialog[open]'))` em app.js:4676 (teclas), 4596 (`openSettings`), 4241 (`openPalette`), 3089
  (`showKeys`); `light.js:206-211` usa `dlg.show()`, que também põe o atributo `open`.
- **Heurística:** N4, N7, N1.
- **Problema:** com o Light aberto (e a razão de ele ser docked é continuar editando com a vista), G, R, F, Del, Ctrl+Z, WASD, Home, 1/2
  **não funcionam** e o botão Settings e Ctrl+P ignoram o clique em silêncio. Ctrl+S é engolido. O X do painel promete "Close (Esc)", mas Esc
  só fecha diálogo *modal* na plataforma (conferir no Edge/Chromium; no código não há tratador de Esc para o painel).
- **Proposta:** trocar os quatro `dialog[open]` por `dialog:modal` (seleciona só os modais) e dar ao painel docked o tratador próprio de
  Esc e de Ctrl+S (gravar o global). Uma linha por local. Mantém o contrato de "diálogo tem as teclas" só para modal de verdade.
- **Evidência:** código citado; captura 08.

### PN-5 Light cobre o painel de propriedades e a barra do viewport (sev 2, esforço M)
- **Onde:** `#lightDlg { position: fixed; inset: 56px 16px auto auto; width: 360px }` style.css:782; captura 08.
- **Heurística:** N8, N4.
- **Problema:** o painel flutua *por cima* do `#right` e da ponta direita da barra de vista (na captura 08 o título aparece cortado em
  "and fog · Midtown" e os botões Screenshot/Atalhos da barra ficam sobre o canto do painel). Com Light aberto não dá para ver nem editar
  o objeto selecionado. A altura (`100vh - 120px`) obriga a rolar para ver a Light 3, e a dica de como ler Heading/Pitch fica *abaixo* dos
  controles que explica (light.js:68).
- **Proposta:** acoplar dentro de `#right` como aba/painel (Blender: abas World/Object; Unity: janela Lighting acoplada), no lugar do
  "Nothing selected" quando nada está selecionado (PN-8) e como aba "Stage" quando há seleção. Dica de direção sob o título "Lights",
  não depois. Ligar "Game look" automaticamente ao abrir (o aviso "The view shows the fog and the background only with Game look on [Turn it
  on]" é um clique que o editor poderia dar sozinho e desfazer ao fechar).

### PN-6 Textures: modal que esconde a vista e sem preview na cena (sev 2, esforço M)
- **Onde:** `#texDlg` (tex.js:28, `showModal` 186, largura 920 px style.css:740); callout fixo tex.js:33-35; rodapé "The pictures show in
  the next fight on this stage." tex.js:130; `openTex` em app.js:4503 não passa nenhum callback que atualize o viewport (compare `preview`
  do Light).
- **Heurística:** N1, N8, N6.
- **Problema:** trocar uma textura grava o PNG no mod mas o viewport do editor não é avisado (o ctx de `openTex` não tem `refresh`); o efeito
  só se vê reabrindo o jogo ("Reload in game" no rodapé). O callout de três linhas ("Changes are written...") é permanente e ocupa o topo
  do corpo toda vez. "In the scene 252 / Other 58 / HUD 11" é bom, mas não há caminho da seleção para suas texturas: Details de um objeto
  mostra Kind/Model id/Damage states e não os `tex<id>` que ele usa.
- **Proposta:** (1) `ctx.refreshTexture(id)` atualizando o `map` em `stageTex` (já é um Map id → textura, app.js:596/1081): o viewport vira
  preview ao vivo. (2) Painel acoplado largo (esquerda, no lugar da lista, ou aba do `#right`) em vez de modal. (3) Callout vira um
  texto de 1 linha no rodapé ("Writes to your mod now. Undo doesn't cover it."). (4) Em Details do objeto: linha "Pictures: t0012, t0031"
  que abre Textures filtrado (Blender: slot de material seleciona a textura; CK: o Object Window abre o texture set).
- **Evidência:** captura 07; tex.js.

### PN-7 Fight HUD duplica Textures com capacidades diferentes (sev 2, esforço M)
- **Onde:** botões `#hud` e `#texBtn` na trilha (index.html:49, 51); `tex.js:17` ("The fight HUD, its font and the sky (also in the
  Fight HUD dialog)"); `hud.js` e `tex.js` compartilham helpers (`export { el, icon, asPng, pick, psmText }`, hud.js:198).
- **Heurística:** N4, N8.
- **Problema:** duas portas para a mesma coisa. HUD tem o que Textures não tem (lado a lado "Original → In your mod", "Apply to all N stages
  with this picture"); Textures tem busca, filtros e arrastar. Os dois repetem o callout "Choose or create a mod" com a lista, o rodapé do
  jogo e o Replace/Save original/Remove. Ícones novos para ações parecidas.
- **Proposta:** um só diálogo Textures; a categoria "HUD and sky" ganha o "Apply to all stages" e o antes/depois (vale para qualquer
  cartão substituído: mostrar miniatura do original no hover do cartão); `#hud` vira atalho que abre Textures com o filtro HUD (ou some).
  Reduz uma entrada na trilha e ~200 linhas.

### PN-8 O que é "da fase" está em cinco lugares; o "Nothing selected" desperdiça o painel (sev 3, esforço L)
- **Onde:** barra superior (stage/New/Edit/mod, index.html:22-26); trilha (Textures, Light, HUD, 49-51); barra do viewport (Stage size,
  Start points, Power-ups, AI net, Vehicle net, Stage picture, 144-149); menu Add prop (Sky dome, Cloud layer, Terrain, 126-129); menu Layers
  (Vehicle net, 160). `#noSel` (173-195): lista de atalhos que repete `keysDlg` + "Getting started" + "Saves to".
- **Heurística:** N6, N4, N7. **Convenção:** Blender (propriedades de Scene/World), Unity (Scene settings), Creation Kit (dados da Cell).
- **Problema:** o dono notou com o Terrain dentro de Add prop; é um sintoma. As configurações da fase se espalham porque cada função nova
  ganhou o botão mais próximo livre. Seis botões de ícone na barra do viewport (cada um com ícone repetido de outro lugar: `picBtn` e `texBtn`
  usam `image-edit-outline`; `pupBtn` e "Save selection…" usam `package-variant-closed`; `aipBtn` e "Add sidewalk" usam
  `vector-polyline-plus`; `lightBtn` repete o sol do tema) obrigam a ler tooltip.
- **Proposta:** transformar o estado "nada selecionado" do `#right` no inspetor **Stage**, com seções recolhíveis: *Stage* (nome/base/
  narrador/RANDOM = Edit stage, tamanho, figura da fase), *Environment* (fog e luzes, sky dome, cloud layer), *Pictures* (Textures: botão que
  abre o navegador; HUD), *Spawns and paths* (Start points, Power-ups, AI net, Vehicle net: botões que entram no modo do PN-1),
  *Pedestrians* (atalho para o modo). A barra do viewport fica só com visualização (look, grid, layers, câmera, foco, frame, régua,
  screenshot). Os atalhos saem do `#noSel` (existe `keysDlg` e a dica de contexto `#hint`). Faz de "Add prop" um botão só de adicionar.
- **Evidência:** capturas 01, 04, 08; ids acima.

### PN-9 O menu Add prop mistura colocar, criar e configurar (sev 3, esforço M)
- **Onde:** `#propMenu` (index.html:115-130), `#palDlg` (570-584), `#pfDlg`/`#propDlg`.
- **Heurística:** N4, N6, N8.
- **Problema:** um popup com três naturezas: *colocar* (props do mod, prefabs, Turn do prefab), *criar assets* ("Import .glb…", "Terrain from a
  heightmap…", "Save selection…" para prefab) e *configurar a fase* (selects "Sky dome" e "Cloud layer", que aplicam na hora, dentro de um
  menu que fecha ao clicar fora). Além disso o `palDlg` (Ctrl+P) já faz "Add to the stage" com busca, cartões e modelos de outras fases, então
  há **duas** formas de adicionar, com comportamentos distintos (menu fecha ao escolher; paleta também). O campo "Turn" do prefab é um parâmetro
  de operação sem lugar claro (e sem unidade além de ° no sufixo).
- **Proposta:** (1) "Add prop" abre a paleta (`palDlg`) em vez do menu; Props do mod e Prefabs viram seções/abas dela. (2) Rodapé da
  paleta com os dois criadores: "Import .glb…" e "Terrain from a heightmap…". (3) "Save as prefab…" entra em **Actions** do painel direito
  (onde a seleção já está, ao lado de Duplicate). (4) Sky/Cloud vão para Stage > Environment (PN-8). (5) "Turn" do prefab vira campo do
  painel de "Place prefab" (ou a leitura do último valor, estilo Adjust Last Operation do Blender): mostrar como chip depois de colocar,
  com R para girar o fantasma antes de clicar. Resultado: o menu desaparece.

### PN-10 Terrain from a heightmap: bom fluxo, sem preview e sem reedição no objeto (sev 2, esforço M)
- **Onde:** `terrain.js` (`#terrainDlg`, 480 px); botão em app.js:4460; "Start from" (terrain.js:29, 115).
- **Heurística:** N1, N6, N7.
- **Problema:** funcional (validação por campo com mensagem clara, Enter no nome cria, arrastar PNG, thumbnail, contagem de células/triângulos),
  mas: nenhum preview no viewport antes de criar (só thumbs 44 px); campos "Width (X)", "Depth (Y)", "Height (white)", "Brightness" sem unidade
  visível (as unidades estão só no parágrafo de ajuda e em tooltips); parâmetros só reeditáveis por "Start from" dentro do diálogo, não do
  objeto na cena; o botão "Create" rebuilda e reposiciona, sem "Apply" nem "Revert".
- **Proposta:** sufixos de unidade nos campos (mesmo `field suf` do Snap e do Transform); contorno da área (retângulo) no viewport enquanto
  o diálogo está aberto (reaproveita `updateGridView`); "Edit terrain…" no painel de um prop que é terreno (Blender: *Adjust Last
  Operation* / modificador editável), abrindo o diálogo preenchido, com o "Start from" some.

### PN-11 Stage size agrupa cinco operações com três semânticas de commit (sev 2, esforço M)
- **Onde:** `#gridDlg` (index.html:549-569); captura 12; `gdApply` app.js:3404, `gdFloor` 3909, `gdDelAll` 3345.
- **Heurística:** N5, N8, N4.
- **Problema:** o título diz "Stage size", mas o diálogo também: apaga a cena de fundo ("Take out the stage's bare scenery"), desliga as
  paredes, cria um prop de piso ("Apply and add a floor over it": aplica e cria um prop e fecha) e **"Delete every object"**, que age na
  hora (não espera "Apply") enquanto os dois checkboxes só valem em Apply. Quatro parágrafos de ajuda a 11 px, o do "clear" remete ao
  "botão abaixo". "Grow every side by 1000" e "Back to the disc's" só preenchem os campos (sem efeito até Apply). O retângulo azul do viewport
  só muda depois de Apply, porque o modal cobre a vista.
- **Proposta:** dividir em (1) Stage size: quatro campos com preview ao vivo do retângulo no viewport (painel/inspector, PN-8), Grow, Reset,
  e "Add a floor…" como botão separado; paredes ao lado; (2) "Start from an empty stage…" (clear + delete every object + floor) como
  receita explícita em New stage ou em menu "Stage", com `askDlg` de confirmação listando o que será removido. Cada botão com efeito imediato
  usa rótulo de ação ("Delete 331 objects…").

### PN-12 Redes: ligar por número, notação cifrada e jargão (sev 2, esforço M)
- **Onde:** `#netLink`/`#netUnlink`/`#netTo` (index.html:538-541), coluna "Links to" (app.js:3682), cabeçalho 3663, labels Width/Drop/Type
  (532-535). Captura 13.
- **Heurística:** N2, N6, N4.
- **Problema:** "Selected node and node # [__] Link / Unlink" obriga a ler o número de outra linha; a coluna mostra `2(w32) 4(w9) 9(w9)` com
  `w` = largura (×10 unidades) sem legenda no cabeçalho (só tooltip da célula), `→` para mão única, `d` para drop; "Drop ... times 10" só
  em tooltip. AIP "Type" é um número cru de 0 a 255 sem nome, FOP é um select com nomes. Colunas `x y z` em minúsculas enquanto o resto usa
  X Y Z. `netRows()` reconstrói a tabela inteira em cada clique de linha (app.js:3668), perdendo o foco do campo.
- **Proposta:** ligar clicando: nó A selecionado + Shift+clique no nó B (PN-1); a lista de links do nó selecionado como mini-tabela no
  painel direito (To / Lane width / Drop / ⇄) com nomes ("Lane 20 → ±200 units"); cabeçalhos "Width (×10 u)" legíveis; nomes para os
  tipos AIP quando existirem; atualizar só a linha alterada.

### PN-13 Arrange é um operador sem preview e sem ajuste da última operação (sev 2, esforço M)
- **Onde:** `arrange.js` (`#secArrange`); botão `#arApply` classe `wide primary` (arrange.js:60); captura 02 e 02c.
- **Heurística:** N1, N3, N7.
- **Problema:** é o mesmo caso do operador "Array" do Blender mas sem o painel *Adjust Last Operation*: nada mostra os N itens antes de Apply
  (sem fantasma), e errar o Count exige Desfazer e refazer; o botão azul primário ("Make 2 copies") tem o mesmo peso visual que "Test in game"
  numa operação que o painel mostra aberta por padrão (captura 02) e ocupa a metade da altura do painel direito.
- **Proposta:** depois de Apply, manter o painel em estado "Adjust": mudar Count/Distance/Arc desfaz o passo e refaz com os novos valores
  enquanto a seleção for a mesma (como F9 do Blender); desenhar fantasma (caixas) dos itens enquanto se digita (as caixas já existem,
  `boxes()`); botão sem estilo primário (`ghost` com ícone) e a seção fechada por padrão como Behavior/Name/Details já guardam em
  `store.get('fold.…')` (app.js:2860).

### PN-14 Behavior mistura Scale (transformação) com Hit points/Car; "em branco" é um estado invisível (sev 2, esforço S)
- **Onde:** `#secBehavior` (index.html:249-265); `showBehavior` app.js:1462-1489; handlers 4519-4548; captura 02c.
- **Heurística:** N4, N2, N1, N5.
- **Problema:** (1) Scale é transformação: o lugar é Transform (ao lado de Heading), onde está Tilt. (2) Em Behavior os valores "1" e "10" da
  captura são *placeholders* cinza alinhados à direita que parecem valor; só o `title` diz "Blank: its own size". Não há sinal de campo
  sobrescrito, nem botão de voltar ao valor do disco. (3) Valor inválido: `msg(...)` e o campo é reposto em silêncio (4523, 4533), sem erro no
  campo. (4) A seção some em seleção múltipla mesmo que Scale valha para todos.
- **Proposta:** Scale ao lado de Heading com o mesmo `field suf` ("×"); um ponto de "sobrescrito" no rótulo (reusa o `.dot` de "Changed") e
  um botão ↺ por campo (Blender: valor em azul/amarelo + reset); erro inline sob o campo (`.perr` de Settings) e mantém o texto digitado;
  Scale funciona em grupo (aplica a todos).

### PN-15 Name e Details misturam escopo de objeto, de modelo (em todas as fases) e leitura (sev 3, esforço S)
- **Onde:** `#secName` (index.html:267-273), `#secDetails` (274-277), app.js:1388, 1405-1440 (Height/Strength), `resizeProp` 4560,
  `editPropModel` 4550; `pModel.onchange` 4569.
- **Heurística:** N5, N4, N2.
- **Problema:** o painel é "deste objeto", mas "Model name" grava em `names.json` e vale para **todas as fases** ("Not undoable") e, para props
  novos, **Height** e **Strength** *reconstroem o modelo a partir do .glb* para todas as entradas, sem Undo, dentro de **Details** (recolhida
  por padrão e lida como lista de fatos). Os campos têm o mesmo visual dos outros e salvam no `change` (um scrub de Height dispara). O texto
  de aviso é uma linha cinza de 11 px sob o campo.
- **Proposta:** seção própria **Model** (rótulo "All N copies in this stage · every stage"), com Model name / Height / Strength, ícone de
  globo e confirmação só na primeira alteração por sessão (`ask` com "Don't ask again"); Details volta a ser somente leitura. O campo
  "Name" fica sozinho na seção do objeto (e "Name > Name" redundante vira apenas "Name").

### PN-16 Paridade entre os painéis de objeto e de pedestres (sev 1, esforço S)
- **Onde:** `#props` x `#pedProps` (index.html:205-278 x 293-334).
- **Heurística:** N4.
- **Problema:** mesma ideia, marcação duplicada: "Transform" com letra arrastável (`.scrub`) versus "Position" sem `.scrub` nos X/Y/Z
  (app.js:2914 só seleciona `#props`); Actions repetido com ids próprios (`pdDrop/pdDup/pdDel`); "Add group"/"Add sidewalk" estão na barra
  do viewport longe da lista que eles acrescentam (a lista tem as seções GROUPS e SIDEWALKS com cabeçalho livre para um "+"). Positivos:
  People com slider+número, Width com unidade, e o callout "Walks on sidewalk 1 [Show]".
- **Proposta:** componente único "XYZ + Actions" para os dois painéis (função JS que devolve o HTML); `+` no cabeçalho de cada seção
  da lista (padrão Unity: hierarquia com "+"); manter o resto.

### PN-17 Test options: título diferente do botão, contagens inconsistentes, resumo escondido (sev 2, esforço S)
- **Onde:** `#testOpts` título "Test options" (index.html:37) abre `#testDlg` com `<h2>Test in game</h2>` (testopts.js:121); seções "You /
  Players / CPUs / Training mode"; `to-ring` testopts.js:144; `testSummary()` só alimenta o `title` do botão (app.js:3024).
- **Heurística:** N4, N1, N2.
- **Problema:** o botão chama-se Test options e o diálogo Test in game (captura 10). "Players: How many [1] 1 to 4, you included" contra
  "CPUs: How many [0] 0 to 7": a primeira conta você, a segunda não; "Players = 1" soa como "outro jogador". A checkbox "Test from here: put
  the others around me" cai embaixo de Training mode, longe do botão "Test from here" que a usa. "Costume 0" é termo do jogo sem explicação. O
  resumo ("Togera and 2 CPUs, training mode") só aparece no tooltip.
- **Proposta:** título "Test options"; reorganizar "You" (monstro, costume), "Opponents" (CPUs; Humans adicionais como "Extra human players"
  com a nota do controle); ring checkbox sob o botão "Test from here"; linha de resumo no topo do diálogo e como chip ao lado do botão
  Test in game (hoje só tooltip).

### PN-18 Enter e Esc: o comentário promete o que só metade dos diálogos faz (sev 2, esforço S)
- **Onde:** comentário index.html:365-366 ("so that Enter in a field takes the dialog's main button"); botão principal `type="button"` em
  `nsOk`, `gdOk`, `sptSave`, `netSave`, `ipOk`, `pfOk`, `tr-ok` (index.html), tratadores avulsos só em `pfName` (app.js:3245), `ipName`
  (4461), `tr-id` (terrain.js:69).
- **Heurística:** N7, N4.
- **Problema:** Enter funciona em Settings e New mod (o primário é `submit`), e em três campos específicos; em New stage (Id/Name/Narrator),
  Stage size, Height do Import, os números do Terrain e nas tabelas de pontos/redes não faz nada (form sem botão submit e vários campos não
  tem submissão implícita).
- **Proposta:** um tratador genérico: `document.addEventListener('submit', ...)` e `keydown Enter` em `dialog.dlg input:not([type=
  checkbox])` que chama `.primary.click()` quando ele não está desabilitado; remove os três tratadores avulsos. Esc já fecha todos os modais
  (nativo) e o `ask` converte em 'cancel': manter.

### PN-19 Seis jeitos diferentes de dizer "você precisa de um mod" (sev 2, esforço S)
- **Onde:** `newStage` toast + retorna (app.js:4318); Import/Terrain abrem `pickMod('save', 'Choose')` (4374, 4442); Textures/HUD/Light
  abrem e mostram callout com select (tex.js:140-144, hud.js:141-148, light.js:155-159; textos "to replace pictures", "to replace HUD
  pictures", "to save these"); Pack mod toast (3820); Prefab save toast (3213); Stage picture toast (3928); Add floor erro no diálogo (3910).
- **Heurística:** N4, N5.
- **Problema:** a mesma pré-condição muda de comportamento (toast que some em 4 s, modal que ensina, callout com select, erro dentro de
  outro diálogo) e há três cópias de código para montar a lista de mods.
- **Proposta:** `needMod(label)` único em app.js que abre `pickMod('save', label)` e continua a ação quando há mod; remove os callouts
  duplicados dos três painéis (fica um só: o select de mod da barra, que já existe, abre aceso com o mesmo `pickMod`).

### PN-20 New stage / Edit stage (sev 2, esforço S)
- **Onde:** `#stageDlg` (index.html:585-606), `nsOpen`/`nsOk` app.js:4293-4366, `#editStage` desabilitado (index.html:24).
- **Heurística:** N1, N2, N5.
- **Problema:** (1) "Start with the changes I have now" vem marcado e é **ignorado em silêncio** quando a base escolhida não é a da fase aberta
  (`withPatch`, app.js:4346); (2) "RANDOM can pick this stage" usa a palavra do menu do jogo sem contexto; (3) Edit stage desabilitado sem
  dizer por quê (só fases custom; foco do teclado pula, captura 15); (4) "Narrator" só explica o caminho no tooltip; (5) mudar a base com
  mudanças não salvas dá erro e manda "Save or undo" em vez de oferecer salvar.
- **Proposta:** desabilitar a checkbox (com a razão "Only when the base is Midtown") quando a base diferir; "Include this stage in the RANDOM
  pick"; no botão desabilitado, tooltip "Only custom stages can be edited. New stage (+) makes one."; botão "Save and continue" no erro.

### PN-21 Primeira execução: o erro de disco é um toast de 8 s (sev 2, esforço S)
- **Onde:** `init()` app.js:4747-4750 (toast com ação "Open settings"); `arm(action ? 8000 : 4000)` app.js:197.
- **Heurística:** N9, N1.
- **Problema:** sem disco configurado (ou ISO errada), o editor abre vazio e a causa aparece num toast que some; o único caminho é achar a
  engrenagem. `openSettings` ainda ignora o clique se houver qualquer `dialog[open]` (4596).
- **Proposta:** quando `discError`/sem fases, abrir Settings sozinho focado em "Game disc" com a mensagem de erro do servidor no campo
  (o `showPaths` já faz o badge e o texto); e um callout fixo na área de vista até resolver.

### PN-22 Settings e atalhos: bons, mas fora do molde comum (sev 1, esforço S)
- **Onde:** `#keysDlg` (index.html:445-505) sem `.dlg`, sem rodapé, botão de fechar sem `type="button"`; lista duplicada em `#noSel`
  (173-181); Settings (367-428): Save/Cancel corretos, Enter salva (submit) mas nada bloqueia salvar um caminho marcado "Not found".
- **Heurística:** N4, N10.
- **Problema:** `keysDlg` usa o estilo padrão de `dialog` (padding 20) e não o `.dlg`, então seu corpo não rola dentro de um quadro com altura
  máxima em telas baixas (captura 14: 1100x650). As duas listas de atalhos (cheat sheet do estado vazio e o diálogo) são escritas à mão e já
  divergem (Esc "Deselect" aparece, Ctrl+P só em um).
- **Proposta:** `class="dlg"` no `keysDlg` com corpo rolável; uma tabela JS `KEYS = [...]` que gera as duas; em Settings, um aviso "You can
  save, but Test in game won't work" quando o executável do jogo está "Not found".

### PN-23 Vocabulário, unidades e jargão (sev 2, esforço S)
- **Onde:** vários, listados abaixo. Não há mistura de português no texto da UI (conferido por busca de caracteres acentuados nos seis
  arquivos de painel e no index.html): isso está certo.
- **Heurística:** N4, N2.
- **Problema:** (a) A mesma ação com quatro nomes: "Back to the disc's" (spt/net/Light/Stage size), "Restore original" (`#reset`), "Restore"
  (`#restore`, `#tiltOff` com o mesmo ícone) e "The disc's picture is back" nos toasts. (b) Unidades: ° (Transform, Tilt), "degrees" (Arrange),
  "Heading °" (Light), "u" (Snap, Width), "units" (Light), nenhuma (Stage size, Terrain, Test aim). (c) "x, y, z" minúsculo nas tabelas
  contra "X Y Z" nos painéis; separador decimal do `input[type=number]` segue o idioma do sistema ("-39,69", captura 13). (d) Termos internos:
  "handle" (hex), "patch", `RST/<STAGE>.MRS` e `clear` no tooltip do Light, "coll_ floor" e "hull_" nas ajudas, "slots", "RANDOM",
  "Costume", "Thickest". (e) Rótulos que repetem a seção: "Name > Name", "Fog and background > Color", "Background > Color" (duas seções
  alternativas com títulos parecidos).
- **Proposta:** glossário curto no começo de `index.html` (comentário) e uma passada de texto: "Restore disc's version" em todos; unidade por
  sufixo `field suf` em todo campo numérico; `x/y/z` maiúsculos; "Fog color", "Background color", "Fog density (0 to 255)" no lugar de
  "Thickest"; mover jargão de arquivo (.MRS, `clear`) para o tooltip de "ⓘ" e deixar o texto da UI em linguagem de fase.

### PN-24 Rodapés e verbos sem molde (sev 2, esforço M)
- **Onde:** rodapés: `[Cancel][Apply]` (gridDlg), `[Cancel][Create]` (stage/terrain/mod), `[Cancel][Import]`, `[Cancel][Save]` (Settings/Prefab),
  `[Close]` primário (Textures/HUD), `[Back to the disc's][Close][Save]` (spt/net), `[Close][Test from here][Test in game]` (testDlg), nenhum
  (Light, keysDlg, palette); `Replace`: "Cancel/Replace" (terrain/import) contra "Keep it/Replace it" (prefab, app.js:3234).
- **Heurística:** N4.
- **Problema:** "Close" ora descarta (spt/net), ora conserva (Textures/HUD/Test). "Save" ora grava o diálogo, ora o arquivo do mod, ora a
  configuração. Ver molde abaixo.

### PN-25 "Remove" de textura apaga o PNG do usuário sem confirmar (sev 2, esforço S)
- **Onde:** `remove` em tex.js:172, hud.js:181 (inclusive com "Apply to all N stages"); servidor `hudapi.py:110-144`
  (`lvlhud.remove_mod_png`). Tooltip: "Take the PNG out of your mod (the disc's picture comes back)".
- **Heurística:** N5, N3.
- **Problema:** o PNG que a pessoa pintou some da pasta do mod; o aviso diz que o editor não tem Undo ali; não há lixeira. Com
  "Apply to all N stages" o mesmo vale para N arquivos.
- **Proposta:** confirmar com `ask` ("Delete your picture t0012 from mod X? The file is removed."), ou mover para `mods/<mod>/_removed/`
  e permitir "Undo" por toast ("Removed. [Undo]") durante alguns segundos (o toast com ação já existe: `toast: ['Undo', undo]`).

### PN-26 Light: pontos finos do conteúdo (sev 1, esforço S)
- **Onde:** light.js:51-70; `#ltFogC`, `#ltBgC`.
- **Heurística:** N2, N6, N1.
- **Problema:** "Fog and background / Color" altera a cor do fog *e* do fundo ("The background takes the fog's color", em cinza abaixo); quando
  não há fog aparece outra seção "Background / Color". Strength vai de 0 a 400 % sem cor de aviso quando passa de 100; "Heading °" e "Pitch °"
  não explicam o eixo no mesmo lugar; "Turn it on" para Game look (PN-5). Os três blocos de luz não têm nome nem ícone de direção (só
  números): um mini dial de direção ou mostrar a seta da luz no viewport quando o painel está aberto ajudaria.
- **Proposta:** unificar os dois títulos em "Fog and background" com o campo de cor rotulado conforme o que existe; marcar >100 % com
  um sufixo "(over-bright)"; mostrar no viewport um gizmo/seta da luz selecionada.

---

## Moldes propostos

### Molde A: diálogo de formulário (modal)
Para tarefas curtas com início, meio e fim: New stage, New mod, Import a prop, Save as prefab, Terrain, Settings, (Stage size se continuar
modal), e as perguntas (`askDlg`).
- Estrutura que já existe: `.dlg-head` (título + ×), `.dlg-body`, `.dlg-foot`. Largura por tipo (460/560/760).
- Rodapé: **`[Cancel] [Verbo]`**, o verbo no imperativo e específico (Create, Import, Apply, Save). O primário é `type="submit"`: Enter em
  qualquer campo o aciona (PN-18). `Esc` e `×` = Cancel; se o formulário está sujo, `askDlg` "Discard changes?".
- Erro: o `callout.err` no pé do corpo, com texto do servidor e foco no campo; nunca toast só.
- Sem botão destrutivo no rodapé; ações destrutivas ficam no corpo e confirmam com `ask`.
- Campos: sempre `label` + `field suf` com unidade; ajuda de uma linha (`help`), ainda mais longa em ⓘ; primeiro campo com foco.

### Molde B: painel acoplado (não modal) para editar a cena
Para Light and fog, Textures/HUD (versão navegador), Start points, Power-ups, AI/Vehicle net, Stage size, Arrange. Vive dentro de `#right`
(ou da esquerda, no caso de listas largas) e **nunca** cobre o viewport.
- Cabeçalho: título + ponto "sujo" + menu ⋯ (Restore disc's version, Copy, Help). Corpo com seções recolhíveis (`details.fold`).
- Rodapé fixo: `[Revert] [Apply]` para rascunho que grava arquivo; **sem rodapé** para editores no patch (Undo global cobre) ou
  para navegadores de gravação imediata (só a linha "Writes to your mod now").
- A vista é o preview: cada campo muda a cena enquanto se digita (Light já faz); Esc volta o foco ao viewport, não fecha.
- Teclado: atalhos globais continuam valendo (PN-4); Ctrl+S grava o global e, se houver rascunho de painel, pergunta.
- Substitui os modais que dependem do viewport (PN-1), e o painel de propriedades do objeto continua à direita quando o painel é do tipo
  "aba" (abas no topo do `#right`: **Properties | Stage | Light | Pictures**).

### Molde C: paleta (`palDlg`)
Para achar/inserir (Ctrl+P): fica como está (pesquisa, Tab alterna Add/Find, setas, PageUp/PageDown). Passa a ser o único caminho de
colocar prop/prefab (PN-9) e ganha o rodapé "Import .glb… · Terrain from a heightmap…".

### Molde D: pergunta (`askDlg`)
Já é um molde bom. Regras: o primário é a ação segura quando houver mais de uma ("Keep editing"), vocabulário fixo "Cancel / Discard /
Replace / Delete my version" (PN-24), ícone âmbar para perda de dados.

## Migração (o que vira o quê)

| Hoje | Vai para | Por quê | Esforço |
|---|---|---|---|
| Start points, Power-ups, AI net, Vehicle net (modais de 860 px) | Modo "Points and nets" ou painel B | precisam do viewport (PN-1) | M a L |
| Light and fog (dock flutuante) | Aba do `#right` (B) | cobre propriedades/barra (PN-5), atalhos mortos (PN-4) | M |
| Textures e Fight HUD (dois modais) | Um navegador B (aba "Pictures") com preview ao vivo | PN-6, PN-7 | M |
| Stage size (modal) | Seção Stage > Size com preview do retângulo; "Empty stage" à parte | PN-11 | M |
| Sky dome, Cloud layer (menu Add prop) | Stage > Environment | PN-9 | S |
| Terrain / Import .glb / Save as prefab (menu Add prop) | Rodapé da paleta; prefab em Actions | PN-9 | M |
| Arrange (seção do painel) | Painel B com Adjust e fantasma | PN-13 | M |
| Test options (modal) | Fica modal (A) com resumo e títulos corrigidos | PN-17 | S |
| New/Edit stage, New mod, Import, Prefab, Terrain, Settings | Ficam modais (A) | formulários | S |
| `keysDlg` | Fica; `.dlg` + fonte única de atalhos | PN-22 | S |
| `askDlg` | Fica | PN-24 | S |

## Ordem sugerida (valor por esforço)
1. PN-4 (guarda `dialog:modal`, 4 linhas) e PN-18 (Enter geral): ganho imediato, quase sem risco.
2. PN-2, PN-25 (confirmações) e PN-19 (`needMod`): pequenos e eliminam perda de trabalho.
3. PN-17, PN-20, PN-22, PN-23: texto e rótulos.
4. PN-3: modelo de gravação e a barra de status com rascunhos pendentes.
5. PN-1 (interino: dock) + PN-5 + PN-6/7 (Textures com preview) + PN-9.
6. PN-8 (inspetor Stage) e o modo Points and nets (PN-1 completo): a grande reorganização, depois que os moldes A e B existirem.

## O que não é problema (conferido)
- Sem mistura de português no texto da UI.
- `askDlg` estável: uma instância reutilizada, o `then` roda uma vez, Esc vira 'cancel', foco no primário.
- Settings valida em tempo real com badge e razão, e o tema é um preview reversível.
- Textures renderiza os 312 cartões com `loading="lazy"` e preserva o scroll em `render()` (tex.js:113-122).
- Test options: valores clampados no `onchange` e escolha persistida em `localStorage` com `try/catch`.
- O dock Light restaura o estado ao fechar (`ctx.restore`) e avisa quando os números dão fog que termina antes de começar.
