# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Decomp
- **`dbsTraverse` (`src/common/dbs.cpp`, rascunho `NON_MATCHING`):** o caso TRANSLATE não soma o `trans` na posição acumulada; o retail soma (`docs/investigacoes/2026-11-06/objetos_fase.md`, seção 2).
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Editor de fase
Rearranjar fases existentes (mover, girar, apagar, duplicar prédios e objetos, inclusive de outra fase) com um editor que grava o patch JSON por fase em `mods/<mod>/levels/` (`docs/guides/MODS.md`, "Level patches"; aplicado pelo `wotm_levelpatch.inc`). Fase nova do zero fica para depois.
1. **Teste do dono no mod de prova** (`mods/LevelTest/levels/CENTRAL.json`, fora do commit; branch `editor-fase`): falta ver os pedestres rente aos prédios (regra nova: só parede corta, a 15) e a caixa d'água da cópia (pickup copiado). Colisão, quebra, IA e carros o dono já aprovou.
   - Cópia profunda: copiar um prédio com destrutíveis dentro (no CENTRAL, a alça `0x0b65a70`), quebrar a cópia e ver que ela solta a própria fumaça e o original segue inteiro; depois quebrar o original e ver a fumaça dele. O que se vê só no original: esse prédio tem animação e evento de jogo (`ACT_ANIM`, `ACT_GENERICEVENT`) nas ações, que a cópia não leva; o corte de tráfego e a animação do lugar só acontecem quando o original quebra. Diário: `docs/investigacoes/2026-11-06/copia_profunda.md`.
2. **Editor de PC** (`tools/level_editor/editor.py`, `MODS.md`, "Level editor"): teste do dono (abrir, mover/girar/copiar/apagar, pedestres com P, painel HUD, Game look com L, Check, Save, Test in game, Reload in game com F10; o `WotM_LevelEditor.exe` do `tools/build_level_editor.ps1`). Falta o retoque visual da UX, plano em `docs/investigacoes/2026-11-06/ux_editor.md` (seção 6). Contexto: `docs/investigacoes/2026-11-06/editor_pc.md`.
   - Teste do dono dos pedestres no jogo: `"peds"` no patch (`MODS.md`, "Level patches"); o log bate com o `lvl.py patch`, falta ver os grupos andando.
3. **Fases custom** (`MODS.md`, "Custom stages"; plano em `docs/investigacoes/2026-11-06/fases_custom_plano.md`): o jogo já carrega pelo `WOTM_MATCH=3,1,@<id>`. Falta:
   - Teste do dono pelo menu (lista com as customs antes da RANDOM, selo azul CUSTOM, foto da base, cadeado de uma custom de base UFO num save sem a UFO, RANDOM, narrador, e a volta ao menu seguida de uma fase normal carregando a dela). Mod de prova local: `mods/StageTest/` (fora do commit).
   - Editor (passos 7a a 7c do plano): listar e abrir as customs, Test in game com `@<id>`, painel HUD, "New stage…".
   - Decisão do dono sobre a trava: hoje a custom herda a da base (alternativa: não listar a custom de base trancada).
4. **Objetos novos vindos do Blender** (plano em `docs/investigacoes/2026-11-06/objetos_blender_plano.md`; conversor `tools/lvlprop.py`, diário `props_passo0_1.md`; a carga na fase já existe, `MODS.md`, "props"): falta o editor (passo 2 do plano) e os passos 3 a 8. Pivô na base para o glb do `lvlglb` (`--units game`: o CAR em z 0 fica meio enterrado).
5. **Céu:** trocar o skybox da fase pelo editor (o céu é um nó próprio da fase, `lvlglb` já o separa).
6. **Tamanho da fase:** ver se há limite (grade de colisão, retângulo do `hdLosTest`, rede dos carros, câmera) e deixar definir ou mudar pelo editor.
7. **Paleta de props estilo Ctrl+P:** barra de busca que abre por atalho, com prévia, para achar e criar props (os da fase, os de outras fases e os importados do Blender).
8. **Capa da fase na seleção:** editor da foto da tela de seleção (`levelpics/<id>.png`), por exemplo uma captura da vista do editor recortada em 2:1.
9. **Depois:** fase do zero como "base + geometria trocada" (mesmo relatório, seção 6).
   - Cópia profunda: clonar as partes animadas (o destrutível debaixo de `CHAR_INSTANCE`/`ANIM_*`, hoje recusado, e os emissores de lá, hoje em comum: 6 no 3-MILE), com animação própria no lugar do `ACT_ANIM` que a cópia tira; segunda arena na memória alta para patch acima de 1 MB. Contexto: `docs/investigacoes/2026-11-06/copia_profunda.md`.

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
- **Linux, o que falta conferir:** controle físico (só um controle sintético foi testado), Wayland/XWayland e gerenciadores de janela além do i3, a campanha inteira. Publicar o AppImage depende de resolver o código-fonte correspondente e o código derivado do jogo (`docs/LINUX_MAINTENANCE.md`; diário em `docs/investigacoes/2026-11-04/port_linux.md`).

## Descartado
- **Customs de tipo chefe na eliminação:** a seleção segue recusando Goliath, Jelly, Cerebulon e Mecha (o diálogo 8 só conhece as colunas 1 a 10); o dono decidiu não fazer.