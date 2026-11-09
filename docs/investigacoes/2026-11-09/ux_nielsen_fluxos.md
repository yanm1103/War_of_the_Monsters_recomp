# UX do editor de fase: as 10 heuristicas de Nielsen nos fluxos principais (angulo 2)

Data: 2026-11-09. Trabalho so de leitura: `tools/level_editor/web/` (index.html 662 linhas, app.js 4790, style.css 814, tex.js, light.js,
terrain.js, hud.js, testopts.js, arrange.js) e o servidor (`editor.py`) quando precisei confirmar o que um botao faz. Nada foi
executado nem editado; as capturas de `docs/screenshots/2026-11-09/` serviram de prova visual. Ids dos achados: NH-n.
Severidade na escala de Nielsen (0 cosmetico .. 4 catastrofico); esforco S (horas), M (1-2 dias), L (mais).

Onde um comportamento depende de como o navegador trata algo que nao li no codigo, o achado diz "verificar".

## Resumo

O nucleo de edicao de objetos (selecionar, mover, girar, duplicar, apagar, restaurar, desfazer, Arrange, pedestres) esta bem
resolvido: tudo passa por `snapshot()` (uma foto do patch inteiro), Desfazer/Refazer dizem o que desfazem, apagar mostra toast com
"Undo", perda de trabalho ao trocar de fase/mod/configuracao pergunta (Cancel / Discard / Save). Os problemas graves estao fora desse
nucleo, nos "arquivos do mod que nao sao o patch" (pontos de partida, power-ups, redes de IA e de veiculos, luz e neblina,
texturas, HUD, nome de modelo, altura de prop, imagem da fase), e nas costuras entre fluxos:

1. Cada um desses arquivos tem uma semantica de gravar diferente (Save no dialogo, "Save to mod", "na hora", "no nome do modelo em
   todas as fases") e nenhuma entra em Desfazer nem no aviso de "alteracoes nao salvas". Fechar o dialogo ou apertar Esc descarta em
   silencio (NH-1), "Back to the disc's" apaga o arquivo do mod sem perguntar (NH-2), e os dialogos modais bloqueiam a vista justo
   onde os botoes "Aim" precisam dela (NH-3).
2. Ha um caminho que grava o patch de uma fase no arquivo de outra (NH-4) e um dialogo ("Stage size") cujos botoes agem na hora
   enquanto Cancel/Apply sugerem o contrario (NH-5).
3. O catalogo de todos os objetos do jogo (o "Object Window" do editor) so abre por Ctrl+P, sem botao (NH-7), e as ferramentas de
   fase estao espalhadas em cinco lugares (NH-6).
4. Pequenos desencontros entre dica e comportamento: caixa nao seleciona props novos (NH-13), campo vazio teletransporta (NH-14),
   "Test without them" seguido de "Reload" grava mesmo assim (NH-15), "pick or type" num `<select>` (NH-22).

Total: 30 achados (7 de severidade 3, 17 de severidade 2, 6 de severidade 1; nenhum 4). O que passa esta na secao "O que passa" no fim.

---

## Cobertura dos fluxos

| Fluxo | Passa | Falha |
|---|---|---|
| Abrir fase | busy com texto e "demora na primeira vez" (`#busy`); erro de disco abre Settings (app.js:4747) | falha de carga deixa o estado da fase anterior (NH-4) |
| Trocar fase / mod com mudanca nao salva | `askUnsaved`: Cancel / Discard / Save and switch (app.js:2700, 2772) | criar fase nova descarta calado (NH-16) |
| Selecionar (clique, lista, Shift, Ctrl+A) | Shift/Ctrl alternam; chips de tipo limitam caixa e Ctrl+A | caixa ignora props novos (NH-13) |
| Mover / girar (gizmo, campos, setas, scrub, snap) | arrastar a letra X/Y/Z; snap vale para gizmo e setas; hint contextual | campo vazio vira 0 (NH-14); clique no gizmo suja o arquivo (NH-17); Enter nao devolve o foco (NH-23) |
| Duplicar | Ctrl+D, toast com Undo, contagem do que vai junto | orcamento so avisa depois (NH-21) |
| Apagar / restaurar | rotulo diz quantos ("Delete 5 (+3 on top)"), Undo no toast, restaurar desabilitado com motivo | "Delete every object" fica dentro de "Stage size" (NH-5) |
| Desfazer / refazer | cobre mover, girar, nudge, soltar, copiar, apagar, Arrange, pedestres, tamanho da fase, ceu/nuvens, nome, hp, scale, carro | nao cobre o resto (NH-11) |
| Salvar | Ctrl+S sempre intercepta; falha vira erro com acao | quatro semanticas de gravar (NH-10); Salvar tudo revertido apaga o arquivo (NH-27) |
| Check | nao bloqueia; problemas clicaveis; resumo na barra | titulo com jargao (NH-22) |
| Test / Test from here / Reload / Stop | `askUnsaved` antes do teste; dialogo "game already open"; rotulo "Save & reload" quando sujo | NH-15, NH-23 |
| Empacotar mod | aviso de que o que nao foi salvo nao entra | o aviso vem depois do zip (NH-26) |
| Criar fase / editar fase | validacao no dialogo, id travado ao editar | NH-16, NH-19 |

---

## Achados

### NH-1. Fechar ou Esc num dialogo de arquivo do mod descarta as edicoes sem avisar
- Onde: `#sptDlg` (app.js:3589), `#netDlg` (app.js:3904), `#lightDlg` (light.js:104). Heuristica N5 / N3. Severidade 3. Esforco S.
- Problema: Start points, Power-ups, AI net e Vehicle net editam uma copia em memoria; o botao Close, o X e Esc disparam o evento
  `close`, que recarrega o arquivo salvo e joga fora tudo. O dialogo Light faz o mesmo (`restore()`). Nao ha pergunta, nao ha
  indicador de "alterado" no dialogo e a barra de status continua dizendo "No unsaved changes" (o `dirty` so conta o patch).
  Uma rede de IA editada por meia hora some com um Esc.
- Proposta: no `close` de cada dialogo, se `ptKeep(kind) !== ptSaved[kind]` (e o analogo em `netKeep` e `same()` da luz), chamar
  `askUnsaved(...)` com Cancel / Discard / Save em vez de recarregar; trocar o botao "Close" por "Discard" quando ha alteracao;
  marcar o rodape com "N changes not saved". Interceptar o `cancel` do `<dialog>` (Esc) para reabrir o dialogo se o usuario escolher
  Cancel.
- Prova: app.js:3589 `if (ptKeep(ptKind) !== ptSaved[ptKind]) loadPoints(ptKind)`; app.js:3904-3907; light.js:104; captura 13 (rodape
  "Back to the disc's / Close / Save", sem estado de alterado).

### NH-2. "Back to the disc's" apaga o arquivo do mod na hora, sem confirmacao e sem Desfazer
- Onde: `#sptReset` (app.js:3585), `#netReset` (app.js:3900), `.lt-reset` (light.js:198), "Clear the net" `#netClear` (app.js:3872).
  Heuristica N5 / N3. Severidade 3. Esforco S.
- Problema: o botao fica no rodape, ao lado de Close e Save, com rotulo que parece "voltar/cancelar" ("Back to the disc's"). O que
  faz: pede ao servidor para remover o arquivo do mod (`reset: true`), o dialogo e as edicoes do arquivo somem. O title diz "Takes
  the mod's file away", mas so aparece ao passar o mouse. "Clear the net" zera todos os nos (recuperavel so por fechar sem salvar,
  o que o NH-1 torna silencioso).
- Proposta: `ask('Use the disc's AI net?', 'Deletes mods/<mod>/PTS/CENTRAL.AIP (N nodes). This can't be undone.', [Cancel, 'Delete the mod's file'])`;
  renomear o botao para "Delete the mod's file" (o verbo diz o que ocorre) e usar a classe `danger` (ja existe em `#del`).
- Prova: app.js:3585-3588, 3900-3903, light.js:198-204; index.html:520 e 546 (`title="Takes the mod's file away..."`).

### NH-3. Os dialogos modais bloqueiam a vista, mas "Aim" e "Add at the view's aim" dependem dela
- Onde: `#sptDlg`, `#netDlg` (`showModal()` em app.js:3549 e 3733); `viewPoint()` usado em app.js:3514, 3556, 3684, 3837.
  Heuristica N7 / N3. Severidade 3. Esforco M.
- Problema: para pôr um no "onde a vista aponta" o usuario precisa mirar a camera, mas com o dialogo modal nao ha como mover a camera
  nem clicar na fase. O unico jeito de mudar a mira e fechar o dialogo, o que descarta (NH-1) ou, se salvar antes, grava a
  cada no. Resta digitar x/y/z de cabeca: a tabela da captura 13 tem 20+ linhas de numeros sem nenhuma pista visual da posicao.
  Editores maduros (Blender, Unity, Creation Kit: a janela de Render continua viva ao lado de qualquer editor de dados) mantem a
  vista interativa.
- Proposta: abrir esses quatro dialogos como o painel Light (`dialog.show()`, nao modal, docado), manter a vista clicavel e
  tratar os cliques: com o painel aberto e uma linha selecionada, um clique na fase vira "Move this point here"; clique num cone da
  vista seleciona a linha. Isso dispensa o botao "Aim" para o caso comum e elimina a necessidade de Save para mudar a mira. Junto,
  corrigir o NH-8 (atalhos mortos com dialogo aberto).
- Prova: app.js:3549, 3733 (modal); 3514 e 3837 (leem a camera); captura 13 (tabela cobrindo a vista; o botao "Aim" em cada linha).

### NH-4. Falha ao abrir uma fase deixa o estado da fase anterior e o Save grava na fase nova
- Onde: `loadStage` (app.js:1024-1110). Heuristica N5 / N9. Severidade 3. Esforco S.
- Problema: `stageName = name` e a troca do `<select>` acontecem antes da leitura (linha 1035). `recs`, `history`, `dirty` e `keep`
  so sao refeitos depois que `/api/stage` e o GLB carregam (linha 1056). Se qualquer um falhar (fase custom com base quebrada,
  mod mudou o disco), o `catch` (linha 1104) so mostra a mensagem: a cena foi removida, mas `recs` da fase velha continuam, e o
  `Save` (`buildPatch()` com os recs velhos, `name: stageName` novo) escreveria o patch da fase A em `levels/<B>.json`. Se o
  usuario tinha escolhido "Discard" ao trocar, `dirty` continua true e o botao Save fica aceso.
- Proposta: no `catch`, zerar o estado da fase (recs, history, keep, dirty, `stageName = ''` ou o nome anterior de volta no
  `<select>`), desabilitar Save/Check/Test enquanto nao houver fase carregada e dizer isso na mensagem de erro ("Midtown is still
  open"). Mais barato ainda: so atribuir `stageName` depois que o carregamento terminar bem.
- Prova: app.js:1035, 1056, 1090, 1104-1106; app.js:2392-2401 (`post` usa `stageName` e `buildPatch()` atuais).

### NH-5. "Stage size" e um dialogo que mistura tamanho com acoes destrutivas imediatas
- Onde: `#gridDlg` (index.html:549-569), `#gdDelAll` (app.js:3345), `#gdFloor` (app.js:3909). Heuristica N4 / N5 / N2. Severidade 3.
  Esforco M.
- Problema: o titulo e o botao da vista dizem "Stage size", mas o dialogo tambem tem "Take out the stage's bare scenery", "Turn
  off boundary walls", "Delete every object" e "Apply and add a floor over it". O rodape e Cancel / Apply, o que ensina que nada
  vale ate Apply, mas "Delete every object" age no ato (snapshot + apaga os 331 objetos) e Cancel nao o desfaz; "Apply and add a
  floor" grava arquivos de prop no mod (`/api/props/floor`) e nao entra em Desfazer. O texto de ajuda manda "Start from an empty
  stage: this, Delete every object, a bigger size and a floor", ou seja, o fluxo real do usuario ("quero uma fase vazia") esta
  escondido num dialogo cujo nome nao o sugere. Equivalentes: Creation Kit tem "New Cell"/"Exterior" na barra de menus.
- Proposta: dividir em dois: (a) "Stage size" so com o retangulo (min/max, "Grow", "Back to the disc's") e Apply; (b) um comando
  "Start from an empty stage..." (no menu Stage proposto no NH-6) com checklist das quatro coisas, resumo do que vai acontecer
  ("deletes 331 objects, adds a floor prop to mods/<mod>/props/FLOOR") e um unico botao confirmando. "Delete every object" fica fora
  do dialogo de tamanho.
- Prova: index.html:557-567; app.js:3345-3352 (age sem Apply), 3909-3923 (grava prop); app.js:3419 (mensagem so depois).

### NH-6. Onde esta cada ferramenta: o posicionamento e arbitrario (stage-level espalhado em cinco lugares)
- Onde: `#bar` (index.html:19-43), `#rail` (45-52), `#toolBar` + `#propMenu` (98-130), `#overlay` (132-153). Heuristica N4 / N6.
  Severidade 3. Esforco M.
- Problema: tudo que e da fase inteira, nao da selecao, esta em lugares sem criterio:
  - Terrain from a heightmap, Sky dome, Cloud layer e Import .glb ficam no menu "Add prop" (index.html:126-129). Terreno e ceu nao
    sao "props que se adiciona onde a vista olha": o terreno e posto no meio da fase, o ceu substitui o da fase.
  - Stage size, Start points, Power-ups, AI net, Vehicle net e Stage picture estao na barra de **vista** `#overlay` (index.html:
    144-149), junto de Fly/Orbit/Frame (camera) e Game look/Grid (sobreposicoes). Sao 16 botoes so de icone, de quatro tipos de
    coisa (captura 01, no alto da vista).
  - Textures, Light and fog e Fight HUD ficam na trilha de **modos** `#rail`, sem estado "ativo" mesmo quando abertos; so
    Objects/Pedestrians sao modos.
  - New stage, Edit stage, Pack mod estao na barra do topo; Check/Save/Test, nas mesmas barra. A aba "HUD and sky" de Textures e o
    dialogo Fight HUD sao dois caminhos para as mesmas figuras.
  Em Blender/Unity/Godot/Creation Kit isso se divide assim: o viewport so manipula (camera, sobreposicoes, ferramenta), os dados da
  fase ficam em menu de nivel (Level/World/Scene/Cell) e as propriedades do objeto no painel direito. Aqui, quem procura "onde
  edito os pontos de partida?" nao tem categoria para procurar.
- Proposta (incremental, sem trocar tecnologia): (1) criar um menu "Stage" na barra do topo, ao lado do seletor, com: Stage size...,
  Start points..., Power-ups..., AI net..., Vehicle net..., Light and fog..., Textures..., Fight HUD and sky..., Sky dome/Cloud
  layer (submenu), Stage picture..., Terrain from a heightmap..., Start from an empty stage..., Edit stage.... (2) `#overlay` fica
  so com camera e sobreposicoes (Game look, Grid, Layers, Fly/Orbit, Focus, Frame, Ruler, Screenshot): 8 botoes. (3) `#propMenu`
  fica so com adicionar objetos (props do mod, prefabs, Import .glb) e ganha "Browse every game model..." (NH-7). (4) `#rail` so
  com os dois modos. (5) o menu usa o mesmo `showLayers`/`showPropMenu` (um popover simples ja existe) e a mesma lista de `title`
  atuais; nada de reescrever.
- Prova: index.html:126-129, 144-149, 49-51; captura 04 (menu Add prop com Sky dome, Cloud layer, Import, Terrain) e captura 01.

### NH-7. O catalogo de todos os objetos do jogo so abre por Ctrl+P, sem botao
- Onde: `openPalette` (app.js:4240), atalho em app.js:4675; menu `#propMenu` (index.html:115-130). Heuristica N6 / N7. Severidade 3.
  Esforco S.
- Problema: a paleta "Add to the stage" lista props do mod e **todos os modelos de todas as fases do jogo** (app.js:4073-4075;
  `addOriginal` os constroi como prop do mod). E o recurso que mais se parece com o "Object Window" do Creation Kit, e o unico caminho
  e uma tecla. O botao "Add prop" mostra so os props do mod (na captura 04: "No props for Midtown yet") e nao diz que existe mais.
  O texto "Ctrl+P: Find an object or add a prop" so aparece no dialogo de atalhos e no rodape da paleta, ja aberta. O proprio
  atalho sequestra a impressao do navegador.
- Proposta: um botao "Add object..." (icone de lupa/cubo) no `#toolBar` que abre a paleta, ou, no `#propMenu`, primeiro item
  "Browse every game model... (Ctrl+P)"; `title` do botao Add prop passa a citar Ctrl+P. A aba "Find in this stage" vira "Find" na
  caixa de busca da lista da esquerda (ja existe `#filter`) ou ganha atalho proprio na dica.
- Prova: app.js:4073-4075, 4240-4250; index.html:581 (dica so dentro do dialogo); captura 04.

### NH-8. Com o painel Light aberto, todos os atalhos de teclado morrem (inclusive voar com WASD)
- Onde: handler global de teclado (app.js:4673-4679) x `#lightDlg` (light.js:206-211, `dlg.show()`). Heuristica N4 / N7.
  Severidade 2. Esforco S.
- Problema: o painel Light foi feito nao modal justamente para a vista ficar a mao ("so the view stays in sight"), mas o handler
  global faz `if (document.querySelector('dialog[open]')) { ...; return; }`. Com o painel aberto, G/R/F/V/L/Del/Ctrl+Z/Home e
  WASD nao fazem nada; Ctrl+S faz `preventDefault()` e nao salva nada (o usuario aperta Ctrl+S e nada acontece, sem mensagem).
  O botao X do painel diz "Close (Esc)", mas nao ha tratamento de Esc para ele em app.js/light.js e um `<dialog>` aberto com
  `show()` nao fecha com Esc no Chromium (verificar no navegador).
- Proposta: trocar o teste por `document.querySelector('dialog:modal')` (so os modais seguram o teclado); em `Ctrl+S` com o painel
  Light aberto, salvar a luz (ou o patch e a luz, ver NH-10); adicionar `keydown` Esc no `#lightDlg` ou tirar "(Esc)" do title.
- Prova: app.js:4676-4679; light.js:41, 206-211; style.css:781.

### NH-9. Dialogo de teste: o botao do topo `Test options` abre uma janela chamada "Test in game" que tambem testa
- Onde: `#testOpts` (index.html:37), `#testDlg` (testopts.js:121, 146-149). Heuristica N4. Severidade 1. Esforco S.
- Problema: tres botoes com "Test": "Test in game", "Test from here" (so icone de marcador), "Test options" (so icone de ajuste). O
  dialogo de opcoes se chama "Test in game" e repete "Test from here" e "Test in game" no rodape, com Close. Quem abre as opcoes
  nao sabe que ali tambem se inicia o teste; quem so quer mudar o monstro precisa ler o rodape. As escolhas se guardam no navegador
  (`localStorage`), nao no mod, e isso nao e dito.
- Proposta: titulo do dialogo "Test options"; rodape "Close" + "Test in game" so; "Test from here" vira checkbox ou fica no
  botao da barra. Uma linha de ajuda: "Saved in this browser, not in your mod."
- Prova: testopts.js:30, 121, 146-149; captura 10.

### NH-10. Quatro semanticas de "gravar" e nenhuma foto do que esta gravado onde
- Onde: Save (`#save`, app.js:3009), "Save to mod" (light.js:75), Save/Close dos dialogos de pontos e redes (index.html:520, 546),
  Textures/HUD "na hora" (tex.js:33-35, hud.js:31). Heuristica N4 / N1. Severidade 2. Esforco M.
- Problema: o usuario aprende que `Save` (e Ctrl+S) grava "a fase" e que a barra diz "No unsaved changes". Mas: o patch grava com
  Save; a luz com seu proprio botao; pontos e redes gravam e fecham o dialogo; texturas e HUD gravam ao clicar; nome de modelo e
  altura de prop gravam na hora e valem para todas as fases. Nada disso aparece em "No unsaved changes" nem no `beforeunload`.
  As caixas de aviso dizem a regra em cada dialogo ("Changes are written to your mod right away; Undo does not reach them"), mas
  so quem le.
- Proposta: dar ao dialogo/painel um rodape uniforme: "Writes mods/<mod>/PTS/CENTRAL.SPT when you press Save here" ou "Written
  right away"; e estender a barra de status: um chip "Not saved: AI net" enquanto houver edicao pendente (liga ao NH-1), e
  Ctrl+S salvar tudo o que estiver pendente (patch + painel aberto).
- Prova: app.js:2429-2440 (so o patch marca `dirty`/`savedAt`); tex.js:33-35; light.js:136 ("Save, outside Undo"); app.js:3578-3584.

### NH-11. Desfazer cobre o patch; o resto nao, e nada na interface diz quais sao
- Onde: `snapshot` (app.js:862), lista de `snapshot(` em app.js (738-4545). Heuristica N3 / N1. Severidade 2. Esforco S.
- Problema: Desfazer cobre posicao, rotacao, nudge, soltar, copiar, apagar, restaurar, Arrange, pedestres (inclusive sliders e
  campos), tamanho da fase, ceu/nuvens, nome da instancia, hit points, scale, tipo de carro. **Nao** cobre:
  nome do modelo (grava `names.json` global, para todas as fases, app.js:4569-4587), altura/forca/titulo de um prop novo (reescreve
  os arquivos do prop, app.js:4550-4568), trocas de textura e HUD, luz, pontos, power-ups, redes, imagem da fase, "Apply and add a
  floor", Import .glb, Terrain (os arquivos do prop ficam, so a entrada some), nova fase, Edit stage. Alguns tem o aviso "Not
  undoable" no texto de ajuda (app.js:1388, 1407) ou no title do campo; o botao Undo continua habilitado e o usuario acha que
  "Ctrl+Z" desfaz o ultimo golpe, e desfaz o ultimo golpe do patch, nao o que acabou de fazer.
- Proposta: (1) marcar visualmente essas acoes ("not undoable" em texto fraco ao lado do campo, ja existe para `pModel`, estender
  para `pPropH` e `pPropStr`); (2) no toast de cada uma, nao oferecer "Undo"; (3) para o nome de modelo global e a altura do
  prop, uma confirmacao `ask` curta quando o modelo e usado por mais de uma entrada ("changes all 4 entries in 2 stages").
- Prova: app.js:1388, 1407, 1416, 1425, 4569-4587; texto "Undo does not reach them" (tex.js:33).

### NH-12. Remover uma textura/HUD apaga o PNG do mod na hora; substituir sobrescreve sem guardar o anterior
- Onde: `remove(r)` (tex.js:172-180), botao `.danger` (tex.js:94), `replace` (tex.js:150-170) e o analogo de hud.js. Heuristica N5.
  Severidade 2. Esforco S.
- Problema: o botao com icone de lixeira, sem confirmacao, apaga o arquivo `<mod>/LVL/<stage>_t<id>.png`. Trocar uma imagem ja
  trocada tambem substitui o PNG anterior sem copia. Ha confirmacao para proporcoes diferentes (bom), mas nao para perder o
  trabalho.
- Proposta: `ask('Take it out of the mod?', 'Deletes LVL/CENTRAL_t0123.png. The disc picture comes back.', [Cancel, 'Remove'])` ou
  manter o PNG num `.bak` do mod e oferecer "Undo" no toast por 8 s (o toast com acao ja existe em `toast(text, level, action)`).
- Prova: tex.js:94-95, 172-180; app.js:197 (toast com acao: 8 s).

### NH-13. Caixa de selecao ignora props novos (divergencia entre dica e comportamento)
- Onde: `pointerup` da caixa (app.js:2248-2256) x `listed()` (app.js:1156) x `makeProp` (app.js:707). Heuristica N4. Severidade 2.
  Esforco S.
- Problema: a lista e Ctrl+A tratam `kind === 'newprop'` como `'prop'` (linha 1156); a caixa filtra com
  `kinds.has(it.rec.info.kind)` direto (linha 2250). Um prop adicionado pelo editor tem `kind: 'newprop'` (linha 707), e a caixa
  nunca o inclui. Shift+arrastar em volta de um prop novo nao o seleciona, enquanto o dialogo de atalhos promete "Box select (the
  kinds shown in the list)" e Ctrl+A o seleciona.
- Proposta: reusar a mesma funcao de chave de tipo nos dois pontos: `const kindKey = (i) => (i.kind === 'newprop' ? 'prop' : i.kind)`
  e chamar de `listed()` e da caixa.
- Prova: app.js:1156 e 2250; index.html:468 (tooltip do atalho).

### NH-14. Esvaziar um campo X/Y/Z/Heading move o objeto para zero
- Onde: `editFromInputs` (app.js:2003), ligado a `px py pz ph pp pr` em app.js:4512. Heuristica N5. Severidade 2. Esforco S.
- Problema: `[+$('px').value, +$('py').value, +$('pz').value]`: um `<input type="number">` vazio vale `''` e `+'' === 0`. Selecionar
  o conteudo, apertar Delete e sair do campo (ou o campo deixar de ser valido) manda o objeto para X=0 (ou heading 0), registrado
  como "edit Hotel". E desfazivel, mas o usuario ve o predio pular para a origem sem ter digitado numero. `tiltFromInputs` usa
  `|| 0` por campo, o mesmo problema.
- Proposta: em `editFromInputs`, se algum dos tres for `NaN`/vazio, repor o campo com o valor atual (`showProps()`) e nao aplicar
  nada; mensagem breve no proprio campo (borda `--err`) em vez de teletransportar.
- Prova: app.js:2003, 4512, 1676.

### NH-15. "Test without them" seguido de "Reload in game" grava as alteracoes mesmo assim
- Onde: `testGame` (app.js:3010-3023) x `askRunning` (app.js:2524-2531). Heuristica N4 / N5. Severidade 2. Esforco S.
- Problema: com alteracoes nao salvas e o jogo ja aberto, o usuario escolhe "Test without them" (`save=false`). O servidor responde
  `running`, abre `askRunning(save=false)` e, se ele escolher "Reload in game", o codigo chama `post('reload', true, ...)`: o
  parametro `save` e jogado fora e o patch e gravado no mod. A primeira pergunta disse que nao.
- Proposta: passar `save` adiante (`post('reload', save, ...)`) e, se `save===false`, avisar no dialogo "Reload loads the saved file, not
  your changes" ou desabilitar "Reload" e deixar so "Restart".
- Prova: app.js:3020-3021 (`nosave` -> `post('test', false)`), app.js:2530 (`post('reload', true, ...)`).

### NH-16. Criar uma fase nova pode descartar em silencio o que ficou na fase aberta
- Onde: `$('nsOk')` (app.js:4340-4366), `$('newStage')` (app.js:4316-4320). Heuristica N5 / N9. Severidade 2. Esforco S.
- Problema: depois de `/api/stage/new` o codigo faz `dirty = false` e carrega a fase nova (linhas 4362-4364), sem perguntar o que
  fazer com as alteracoes da fase atual. Se "Start with the changes I have now" esta marcado e a base e a mesma, elas viajam para a
  fase nova e **sai** da fase velha sem ser salvo. Se esta desmarcado, ou a base e outra, elas simplesmente somem. Alem disso, sem
  mod escolhido o botao so mostra um toast "Choose a mod first" (linha 4318), enquanto Save abre o seletor de mod (`pickMod`).
- Proposta: se `dirty`, usar `askUnsaved('...', 'Save and create', ...)` antes de criar (as tres saidas ja existem); sem mod, abrir
  `pickMod('save', 'Continue')` como faz `openImport`. Dizer no dialogo de nova fase o que acontece com o que esta aberto.
- Prova: app.js:4318, 4346-4364; app.js:2394-2398 (como Save lida com "sem mod").

### NH-17. Clicar no gizmo sem arrastar suja o arquivo; "Drop" sem nada embaixo tambem
- Onde: `dragging-changed` (app.js:1952-1959), `dropToGround` (app.js:2035). Heuristica N1 / N3. Severidade 2. Esforco S.
- Problema: `snapshot()` e chamado quando o botao desce no gizmo, antes de saber se houve movimento. Um clique perdido numa seta
  deixa `dirty = true`: Save fica aceso, a barra diz "Unsaved changes", o `beforeunload` pergunta antes de fechar e o Desfazer tem
  um degrau vazio ("move Hotel") que nao muda nada. O mesmo em `dropToGround` quando `n === 0` ("Nothing under ... to drop onto"):
  o snapshot ja foi tirado. (O `scrub` das letras X/Y/Z faz certo: so tira o snapshot depois de 3 px.)
- Proposta: tirar o snapshot no primeiro `objectChange` (ou ao soltar, comparando `JSON.stringify(buildPatch())` com o anterior) e
  descartar o degrau se o patch nao mudou; em `dropToGround` e analogos, fazer `history.pop()` quando `n === 0`.
- Prova: app.js:862-869, 1952-1959, 2035-2056; app.js:2894 (o scrub atrasa o snapshot).

### NH-18. "Stage picture" so falha depois do clique e sobrescreve o arquivo sem perguntar
- Onde: `#picBtn` (index.html:149), `$('picBtn').onclick` (app.js:3926-3939), `save_levelpic` (editor.py:785-805). Heuristica N5 / N4.
  Severidade 2. Esforco S.
- Problema: o botao fica habilitado em qualquer fase, mas so serve para fase custom e tres fases do jogo; em Midtown o servidor
  responde "make a custom stage on it first (+)" depois do clique (a captura 01 mostra a fase CENTRAL aberta). O title lista as
  fases validas em vez de o botao se desabilitar. Para as fases validas, grava por cima de `levelpics/<nome>.png` sem
  confirmar e sem Desfazer. O icone (`image-edit-outline`) e o mesmo do botao Textures (index.html:49 e 149), em barras diferentes.
- Proposta: desabilitar com title "Only a custom stage... " quando a fase nao serve (ja ha `stageMeta`, `LEVELPIC_BASE` no servidor,
  exportar a lista em `/api/stage`); se o arquivo ja existe, `ask('Replace the stage picture?')`; trocar o icone (ex.: `image-frame`).
- Prova: editor.py:781-800; app.js:3926-3938.

### NH-19. "Edit stage" desabilitado sem dizer por que; fase custom nao tem como ser apagada
- Onde: `#editStage` (index.html:24, app.js:1036); rotas do servidor (editor.py:1977, 1982, so `new` e `edit`). Heuristica N1 / N3.
  Severidade 2. Esforco S (dica) / M (apagar).
- Problema: nas fases do jogo o lapis fica cinza (`disabled`), com o mesmo `title` de quando esta ativo ("Edit stage: the name,
  base, narrator and RANDOM of this custom stage"). Botao desabilitado nao recebe foco, entao o teclado pula (captura 15) e a
  explicacao so existe por hover; quem usa teclado nem sabe que ele existe. Nao ha comando para renomear a id nem para apagar uma fase
  custom criada por engano: a lista de fases so cresce (`/api/stage/new` sem inverso).
- Proposta: atualizar o `title` em `loadStage`: "Only a custom stage can be edited: make one with +". Para apagar, adicionar
  "Delete this stage..." no dialogo Edit stage (com `ask` listando os arquivos: `levels/<ID>.json`, `stages/<id>/`).
- Prova: index.html:24; app.js:1036; editor.py:1977-1985; captura 15 (Edit stage pulado no Tab).

### NH-20. Informacao importante em toasts de 4 s ou so na barra de status
- Onde: `toast` (app.js:170-199, `arm(action ? 8000 : 4000)`), avisos em app.js:3582, 3897, 4523, 4533, 4561, 4048. Heuristica N9 / N1.
  Severidade 2. Esforco S.
- Problema: (a) `r.warnings` do servidor ao salvar pontos/redes entram no mesmo toast de sucesso de 4 s
  ("Saved N start points in the mod. <aviso longo>"); (b) a validacao de Hit points, Scale e Height emite `msg(..., {level:'warn'})`,
  que sem dialogo aberto nao vira toast e so aparece na barra de status (um texto cinza de rodape) enquanto o campo volta ao valor
  anterior sem outra pista; (c) "Building X from Stage..." (`addOriginal`, que chama o servidor para construir um prop) so poe
  uma linha na barra, sem indicador de espera; (d) "Starting the game on ..." e "Reloading ..." so escrevem na barra, o botao
  durante a espera o unico sinal e o ponto pulsante do `#gameInd`; (e) o toast de aviso some tao rapido quanto o de sucesso.
- Proposta: toasts `warn`/`err` ficam ate serem dispensados (`arm` nao dispara); validacao de campo inline (borda `--err` e a
  frase na linha `.help` do campo); `#busy` ou um spinner no botao para chamadas de servidor longas (`addOriginal`,
  `doImport`, `openTerrainDlg` ja tem o texto "Building…" no botao, so falta nas outras); "Restart the stage to see them" vira
  botao no toast ("Reload in game").
- Prova: app.js:197, 3582, 4523-4535, 4561, 4048.

### NH-21. O orcamento de slots/arena so avisa depois; nada impede Duplicar ou Arrange de estourar
- Onde: `refreshBudget`/`showBudget` (app.js:942-970), `MAX_MADE = 500` (arrange.js:10), `copyGroup` (app.js:2083). Heuristica N5.
  Severidade 2. Esforco S.
- Problema: a barra mostra "348/1024 slots · arena 0" e fica vermelha quando passa, mas `duplicate()` e `arrangeCopies()` nao
  consultam `ui.budget`. O limite de 500 por Apply do Arrange e fixo, independente dos 1024 slots do jogo menos o que ja esta em
  uso. O usuario descobre em Check ("It ran out") ou no jogo. A nota de Arrange nao diz quantos slots sobram.
- Proposta: ler `ui.budget` ao calcular `plan()` em arrange.js e desabilitar Apply (com a razao no `.ar-note`: "needs 300 slots,
  676 left"); em `duplicate()`, avisar por toast `warn` quando `sl.used + n > sl.total`.
- Prova: app.js:962-970, 2061-2075; arrange.js:10, 116-120.

### NH-22. Vocabulario do jogo e do codigo no texto de interface
- Onde: varios. Heuristica N2. Severidade 2. Esforco S.
- Problema: termos que um modder que vem do Blender/Unity nao tem como adivinhar:
  - "handle" em `placeholder="Search by name or handle"` e no chip `0x101E340` em cada linha da lista (a captura 01 e quase so
    handles); no titulo "Copy the handle". Nao ha uma frase que diga "o id do objeto no jogo".
  - "RANDOM can pick this stage" (index.html:598): o menu aleatorio do jogo; "Narrator", "Based on".
  - "patch" na barra de status e nos toasts ("Patch applied in game", "Patch ignored in game"), enquanto o resto da interface diz
    "your mod's file" / "changes".
  - "slots · arena", "Interactives" no title; "grid" (index.html:134 a grade visual e 144 a grade de colisao: duas coisas com a mesma
    palavra na mesma barra), "Collision boxes", "Damage states", "coll_ meshes / hull_ meshes" (index.html:157), "Entry id".
  - botao "Aim" (app.js:3513) e coluna "Ground" (coluna de Start points, app.js:3484), "List" (lista de pontos) e "Type"
    numerico de 0 a 255 na AI net sem nomes.
  - "Restart the stage to see them" depois de salvar pontos (app.js:3582): o botao existente se chama "Reload in game" e nao
    recarrega pontos; nao ha "Restart the stage".
  - texto "pick or type one in Mod" (app.js:1286) mas `#mod` e `<select>`: nao se digita nada, ha so "New mod…".
  - o produto se chama "Level Editor" e salva em `levels/`, mas a interface diz "stage" em quase todo lugar (seletor, botoes,
    mensagens): duas palavras para a mesma coisa.
- Proposta: (1) trocar "handle" por "ID" nos rotulos visiveis e deixar "handle" so no title; (2) uma linha de ajuda por termo
  (title `?`): usar o mesmo `help` das linhas de dialogo; (3) renomear "Aim" para "Move to view center" (ja explicado no title) e
  "Patch applied in game" para "Your changes loaded in the game"; (4) corrigir o texto de "pick or type"; (5) em "Restart the
  stage" dizer exatamente o que fazer ("Test in game restarts the stage"); (6) na AI net, o `Type` pode ganhar nomes como o FOP ja
  tem (`n.lists`).
- Prova: index.html:57, 134, 144, 157, 598; app.js:1286, 3582, 3897; captura 01.

### NH-23. Atalhos faltando, dicas incompletas e o foco que fica preso no campo numerico
- Onde: handler de teclado (app.js:4673-4725); botoes `#check #test #reload #stop` (index.html:31-39). Heuristica N7 / N6.
  Severidade 2. Esforco S.
- Problema: (a) nao ha atalho para Check, Test, Test from here, Reload, Stop; as dicas dos botoes nao tem sufixo (so Save tem
  `(Ctrl+S)`). O ciclo editar->testar e o mais repetido de um editor de fase e so tem mouse. (b) Ctrl+P (paleta) e Ctrl+Shift+Z (refazer)
  existem mas nao estao na dica de nenhum botao. (c) o passo do nudge sem snap (10 unidades) nao esta em lugar nenhum (o dialogo
  de atalhos diz "Nudge along the axis nearest to the view"). (d) depois de digitar um valor em X/Y/Z e apertar Enter, o foco fica
  no campo (so `pPropH` faz `blur()` no Enter, app.js:1418); `if (field) return;` na linha 4696 vem antes de Escape e das letras,
  entao G, R, F, Del, Esc e as setas (que passam a mudar o numero) ficam mortas ate o usuario clicar fora; Esc nao sai do campo.
  (e) nao ha Ctrl+C/Ctrl+V (so Ctrl+D e prefab); nao ha tecla para girar 90 (ha botoes `#rotL/#rotR`). (f) o dialogo de atalhos
  nao lista Esc para sair da Regua nem "Hold Shift to go faster" nos campos.
- Proposta: Ctrl+Enter = Test in game, Ctrl+Shift+Enter = Reload in game, F7 ou Ctrl+K = Check (evitar F5 e Ctrl+R do navegador;
  `preventDefault` ja e usado em F1/Ctrl+S/Ctrl+P); colocar o atalho no `title` e no rotulo do botao quando houver espaco.
  Para o foco: no `keydown` de `.xyz input` e `#ph`, Enter faz `blur()`; Esc tambem (e devolve o foco ao canvas). No
  dialogo de atalhos, acrescentar as linhas que faltam.
- Prova: app.js:4673-4725, 1418; index.html:31-39, 28-29, 41; style.css:328 (`.scrubbable`).

### NH-24. Icones repetidos para funcoes diferentes, e 16 botoes da vista so de icone
- Onde: index.html:49 x 149; 110 x 147; 124 x 146; 203 x 232 x 240; 22 x 159; 41 x 152. Heuristica N4 / N6. Severidade 1.
  Esforco S.
- Problema: `image-edit-outline` e Textures **e** Stage picture; `vector-polyline-plus` e "Add sidewalk" **e** AI net;
  `package-variant-closed` e Power-ups, "Save selection..." (prefab) e a secao Pickups da lista; `restore` e "Restore original",
  "Level the prop" e "Restore"; `road-variant` e Vehicle net e Sidewalks; `map-outline` e o seletor de fase e a camada Minimap.
  O botao "Keyboard shortcuts" existe duas vezes (`#help` no topo e `#keysBtn` na vista). Na vista (captura 01) ha 16 botoes de icone
  sem rotulo.
- Proposta: icones distintos (ex.: Textures `image-multiple-outline`, Stage picture `image-frame`, AI net `robot-outline`,
  Power-ups `heart-plus-outline`); tirar `#keysBtn` da vista (fica o `?` do topo); com o menu Stage (NH-6) a maioria dos icones
  deixa de ser necessario.
- Prova: index.html:41, 49, 110, 124, 145-149, 152, 203, 225-232; captura 01.

### NH-25. A mesma informacao aparece em tres lugares e a barra de status tem seis botoes
- Onde: `#count` (index.html:74), `#sumCounts` (183-184), `.st.count` (index.html:362); `#status` (354-363). Heuristica N8. Severidade 1.
  Esforco S.
- Problema: "331 objects · 0 changed" aparece no rodape da lista, na coluna direita sem selecao e na barra de status ("0 changed ·
  1 selected"). O botao `.st.count` abre a aba Messages (nada a ver com contagem). A barra tem: ultima mensagem, salvar, check,
  orcamento, jogo, contagem: seis alvos clicaveis, todos abrindo o Output em abas diferentes, sem rotulo visivel do que e cada um
  alem do texto.
- Proposta: tirar a contagem do `#status` (fica a da lista); fazer `.st.count` nao ser botao; agrupar orcamento e check num unico
  chip "Check · 348/1024" com tooltip detalhado.
- Prova: capturas 01 e 02 (rodape esquerdo, coluna direita e barra de status com a mesma contagem); app.js:917-931.

### NH-26. "Pack mod" avisa depois de empacotar, nao antes; e a gravacao do download nao diz onde foi
- Onde: `$('packMod').onclick` (app.js:3819-3832). Heuristica N5 / N1. Severidade 1. Esforco S.
- Problema: com alteracoes nao salvas, o zip e gerado sem elas e o toast `warn` diz "The changes you have not saved are not in it"
  por 4 s. O usuario ja tem o arquivo. Tambem nao diz onde o zip foi parar (diferente da captura de tela, que diz "your
  downloads").
- Proposta: se `dirty`, `askUnsaved('...', 'Save and pack')` antes de gerar; no toast, "Packed X.zip (in your downloads)".
- Prova: app.js:3819-3832.

### NH-27. Salvar depois de desfazer tudo apaga o arquivo da fase no mod
- Onde: `save_patch` (editor.py:1519-1531), `post` (app.js:2433-2434). Heuristica N5 / N1. Severidade 1. Esforco S.
- Problema: se o patch ficou vazio (o usuario desfez tudo ou restaurou tudo) e o arquivo existia, Save o remove e o toast diz
  "Removed mods/<mod>/levels/CENTRAL.json: your mod no longer changes Midtown". Esta tudo dito, mas o botao se chama Save, a mensagem
  chega como sucesso (`level: 'ok'`) e a unica diferenca esta na frase.
- Proposta: quando o resultado e remover, mostrar `ask` antes ("Your mod has no changes left for Midtown. Delete mods/<mod>/levels/
  CENTRAL.json?") ou no minimo trocar o nivel do toast para `warn`.
- Prova: editor.py:1519-1531; app.js:2433-2437.

### NH-28. Ajuda e documentacao: so a lista de atalhos
- Onde: `#help` (index.html:41), `#keysDlg` (445-505), `#noSel` (173-195). Heuristica N10. Severidade 2. Esforco M.
- Problema: a ajuda e uma tabela de teclas. "Getting started" (3 passos) so aparece com nada selecionado e some no primeiro
  clique. Nao ha explicacao de conceitos que o modder precisa (o que e o patch, o que Check verifica, o que Test from here faz, a
  diferenca entre salvar o patch e os outros arquivos do mod, o que significam os numeros do orcamento) nem ligacao com
  `docs/guides/MODS.md`. Os dialogos de dados (pontos, redes, luz, texturas) trazem explicacoes boas no texto, mas cada um as
  repete em seu estilo.
- Proposta: um dialogo "Help" (F1) com abas "Shortcuts" (o atual), "What gets saved where" (tabela: arquivo, quem grava, desfaz?
  ver NH-10/NH-11) e "Glossary" (handle, patch, slot, arena, base stage), com um link para `MODS.md`. O botao `?` do topo abre esse
  dialogo em vez de so as teclas. Manter "Getting started" visivel no painel vazio.
- Prova: index.html:173-195, 445-505.

### NH-29. Ha quatro jeitos de dizer "escolha um mod primeiro"
- Onde: Save (app.js:2394 `pickMod`), Nova fase (4318 toast), Pack mod (3820 toast), Stage picture (3928 toast), Textures/HUD/Light
  (callout com `<select>` embutido), pontos e redes (`mod: modNow || ''`, erro bruto do servidor, app.js:3568, 3885). Heuristica N4.
  Severidade 2. Esforco S.
- Problema: sem mod, o mesmo estado exige leituras diferentes. Save abre um dialogo que pergunta; as caixas de Textures/HUD/Light
  trazem um seletor embutido; Nova fase, Pack e Stage picture dizem so "Choose a mod first" num toast e o usuario precisa ir ao
  seletor no topo; pontos e redes deixam digitar e so falham ao Save, com o erro do servidor.
- Proposta: um helper unico `needMod(label)` (ja existe `pickMod('save', label)`) usado em todos esses pontos: se nao ha mod,
  abre o seletor "Save into which mod?" e continua a acao depois.
- Prova: app.js:2394-2398, 3820, 3928, 4318; tex.js:36-37; light.js:47-48; app.js:3568.

### NH-30. "Stop" so de icone, que aparece e some, e fecha o jogo sem perguntar
- Onde: `stopGame` (app.js:2546-2552), `#stop` (index.html:39, `hidden` ate o jogo abrir). Heuristica N6 / N3. Severidade 1.
  Esforco S.
- Problema: o botao (so icone de quadrado, sem rotulo como Check/Save/Test) so existe enquanto o jogo roda, entao o layout da
  barra do topo muda quando o jogo abre ou fecha, e fecha o jogo na hora, mesmo no meio de uma luta de teste. O estado do jogo vem de
  um `setInterval` de 4 s (linha 2696): se o jogo for fechado por fora, "Game running" fica velho ate 4 s.
- Proposta: reservar o lugar do botao (`visibility`, nao `hidden`), dar-lhe `.lbl` "Stop" em janelas largas como os vizinhos; a
  confirmacao nao e necessaria (um teste e descartavel). Baixa prioridade.
- Prova: index.html:39; app.js:2546, 2696.

---

## O que passa (e deve ficar como esta)

1. **N1, estado**: a barra de status mostra salvo/nao salvo, Check passou/nao verificado desde a ultima mudanca (`stale`), orcamento
   (com tom amarelo/vermelho), jogo fechado/abrindo/recarregando/aplicado; `#gameInd` pulsa enquanto inicia; o botao Reload muda para
   "Save & reload" quando ha alteracao; a lista tem o ponto "Holds selected objects" quando o grupo esta fechado.
2. **N3, controle**: Desfazer/Refazer com rotulos ("Undo: move Hotel (Ctrl+Z)"); uma rajada de setas e um degrau; arrastar um
   slider e um degrau; Esc e Cancel em todo `ask`; Restore de um objeto apagado direto no painel; toasts de apagar/copiar/adicionar
   com "Undo".
3. **N5, prevencao**: `askUnsaved` ao trocar fase, trocar mod, mudar disco/pasta de mods, testar; `beforeunload` com `dirty`; botao
   de Arrange com texto "Make 2 copies" ou "Select something first" e razao no `.ar-note`; Restore desabilitado com "Nothing to
   restore"; mod novo valida o nome e avisa "mod X already changes this stage; saving will replace its file"; Delete do prefab e
   Replace de prop/terreno/prefab pedem confirmacao.
4. **N6, reconhecimento**: o chip de dica contextual no canto da vista muda com a selecao e a ferramenta (HINTS, app.js:541-553) e
   some em 8 s; a lista tem estado vazio com "Clear filters"; o painel vazio lista o que o clique/Shift/Ctrl+A fazem; os campos
   X/Y/Z tem as cores do gizmo.
5. **N9, erros**: o Check nao bloqueia o Save; "Saved, but the check found 3 problems" abre com botao "Show"; cada problema do
   Check e uma linha clicavel que seleciona e enquadra o objeto; erros abrem o Output na aba certa; `cleanError` separa o texto do
   tipo da excecao (detail em dobra); erros de caminho/mod trazem acao ("Open settings", "Choose mod"); o dialogo "game is already
   open" explica a diferenca entre Reload e Restart.
6. **N7, flexibilidade**: atalhos G/R/F/V/L/C/P/N/M, 1/2, End, Home, Ctrl+D/A/S/Z/Y; arrastar a letra X/Y/Z e qualquer campo
   numerico (`scrubField`); snap valendo para gizmo, setas e scrub; Ctrl+P com teclado completo (setas, PageUp/Down, Tab).
7. **N8, minimalismo**: painel direito dobravel (Arrange/Behavior/Name/Details), Name e Details fechados por padrao, dados de
   pedestres so no modo Pedestres.
8. **Acessibilidade basica**: foco visivel em todos os botoes (captura 15), `aria-live` nos toasts e na dica, tema claro/escuro/
   sistema, `prefers-reduced-motion`.
