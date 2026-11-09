# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Decomp
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Editor de fase
- **Escala: destroços e câmera de um prédio escalado:** o `dbsTraverse` entrega aos destrutíveis a matriz já escalada (`AddDestruct`), e os destroços (`ActDebris`) e a câmera da quebra usam a posição e a matriz guardadas ali; conferir o tamanho e o lugar dos pedaços que voam com um prédio que solta destroços (o de CENTRAL que se quebrou nas provas vai direto para o estado final, sem pedaço voando). Escala de props novos do Blender não existe (o tamanho vem do modelo: `--height`).
- **UX do editor, marcadores na vista (decidido pelo dono: sim):** pontos de largada, power-ups e nós das redes selecionáveis e arrastáveis na vista, com o gizmo e o Desfazer (hoje só se editam pelas linhas do painel acoplado; as cenas já existem em `ptViews`/`nets[].view`, sem raycast). Rascunho e `isDirty` já cobrem esses arquivos; o Desfazer não (cada um tem o seu rascunho). Plano em `99e13ca:docs/investigacoes/2026-11-09/ux_consolidado.md`, seção 5.
- **UX do editor, acabamento (etapa 19 do consolidado):** alvos de 24 px, `aria-selected` nas abas do `#output`, `th`/`aria-label` nas tabelas (pontos e redes são grades de `div`). "Pickups" na lista x "Power-ups" no botão: o termo do jogo para vida e stamina não foi decidido.

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
- **Linux, o que falta conferir:** controle físico (só um controle sintético foi testado), Wayland/XWayland e gerenciadores de janela além do i3, a campanha inteira. Publicar o AppImage depende de resolver o código-fonte correspondente e o código derivado do jogo (`docs/LINUX_MAINTENANCE.md`; diário em `docs/investigacoes/2026-11-04/port_linux.md`).

## Descartado
- **Customs de tipo chefe na eliminação:** a seleção segue recusando Goliath, Jelly, Cerebulon e Mecha (o diálogo 8 só conhece as colunas 1 a 10); o dono decidiu não fazer.
- **LOD dos props do Blender:** o custo por quadro não pede (10 props de ~1000 triângulos não mudam o FPS); o dono decidiu não fazer.
