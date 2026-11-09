# Editor de fase: pontos de início, power-ups e redes viram modos da barra lateral

Relato do dono (item 8, e a base do item 4): "Pedestres são uma sessão à esquerda, ótimo, porém enfiou AI net, Vehicle net, power ups, start points, tudo na direita dentro de stage. não faz sentido, podem ser categorias separadas à esquerda". O `ux_consolidado.md` (4.1, decisão L) tinha adiado "marcadores como modos"; isto é essa etapa.

## O que mudou

- **Quatro modos na barra** (`#modeSpt`, `#modePup`, `#modeAip`, `#modeFop`, teclas 3 a 6), depois de um separador, como Pedestres. A aba Stage perdeu a seção Gameplay e os diálogos `#sptDlg` e `#netDlg` (com o CSS dos docks de linhas) foram embora; os docks que ficam são Stage size e Light and fog.
- **Painel esquerdo por modo:** barra Add (tipo + Add no ponto para onde a vista olha; nas redes, com um nó selecionado o novo se liga a ele), medidores (pontos n/255; power-ups n/100; nós n/255 e caminhos n/450), lista em seções (SPT por lista com `n / sala`, PUP por grupo, redes por tipo de nó) e rodapé com estado (salvo ou não), Revert, Back to the disc's, Clear net e Save to mod.
- **Painel direito (aba Selection):** o marcador selecionado, no molde do `#pedProps`: tipo/lista, X Y Z, heading e "Drop to the floor" (SPT) ou Shadow (PUP), links do nó (largura, drop no FOP, Two-way, cortar, ligar de volta, "To" + Link), Aim, Drop, Duplicate e Delete. Sem seleção: legenda das cores (gerada da mesma tabela que a vista e a lista usam) e uma linha de ajuda.
- **Vista:** o marcador só é pego no próprio modo (clique perto, 18 px; anel branco no que está sob o ponteiro); nos outros modos as camadas continuam como Display > Overlays, agora apagadas (opacidade 0,4). No modo da camada ela aparece inteira, com a chave marcada e travada, como Pedestres.
- **Arquitetura:** `web/markers.js` (regras puras: nomes, grupos, cores, contagens, ligações) com teste em node (`test_markers.mjs`); o resto fica no `app.js` (`lay(kind)` dá a mesma cara às quatro camadas, `pt`/`nets` guardam o rascunho).

## Defeitos que o caminho antigo escondia

- `applyPatch` chamava `loadSpt`: todo Undo, Redo e "Move my changes to mod" recarregava os quatro arquivos do disco e apagava rascunhos. Agora quem carrega é o `loadStage`; `moveTo` mantém o rascunho sujo (`saved = null`, o rodapé diz "Not in this mod yet").
- `isDirty` e `unsavedCount` ignoravam os rascunhos: trocar fase ou mod, Test in game, Pack mod e fechar a página perdiam as edições sem aviso. Agora contam (um por camada suja), o Save da barra superior (Ctrl+S) grava as camadas sujas junto com o patch e um erro numa camada não impede as outras.
- Um clique em nada com o painel de marcadores aberto caía no seletor de objetos e escolhia um prédio. Nos modos de camada o clique termina ali.

## Desfazer

Os rascunhos não entram no Undo do patch (são arquivos próprios). Cada camada tem o seu histórico (cópias em JSON dos pontos, até 100 passos): com um modo de camada aberto, Ctrl+Z, Ctrl+Y e os botões da barra superior agem nessa camada, com os rótulos dela; o Undo do patch fica como estava e não mexe nos rascunhos. A dica do painel direito diz isso.

## Testes

`node tools/level_editor/test_ui.mjs` (mod temporário `smoke`, portas e config isoladas): ids da barra e do painel; modos 3 a 6 por tecla e por botão; Add, selecionar, mover pelo gizmo e pelos campos, setas, Ctrl+D, Delete, Ctrl+Z/Y e Revert nos quatro tipos; rascunho conta como não salvo, a troca de fase pergunta (Cancel mantém, Discard abre a outra fase sem o rascunho), Save grava `PTS/<FASE>.SPT` com a contagem certa e Back to the disc's apaga; Undo do patch não leva o rascunho; "Move my changes" mantém o rascunho; ligações de um nó; clique no marcador só no modo certo e clique em nada sem pegar prédio; sobreposições apagadas/inteiras. `node tools/level_editor/test_markers.mjs` roda as regras puras.

## Fora desta etapa

Rótulos de texto sobre os cones (item 5), as cores e o cubo do cloak (item 6) e a ferramenta de ligar nó a nó pelo clique (item 7) ficam para as etapas seguintes; a tabela única `markerColor` e a seção Links do painel direito já são a base delas. O rascunho dos marcadores não vai para o `localStorage` (está no `TODO.md`).
