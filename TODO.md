# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Decomp
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Editor de fase
- **Escala: destroços e câmera de um prédio escalado:** o `dbsTraverse` entrega aos destrutíveis a matriz já escalada (`AddDestruct`), e os destroços (`ActDebris`) e a câmera da quebra usam a posição e a matriz guardadas ali; conferir o tamanho e o lugar dos pedaços que voam com um prédio que solta destroços (o de CENTRAL que se quebrou nas provas vai direto para o estado final, sem pedaço voando). Escala de props novos do Blender não existe (o tamanho vem do modelo: `--height`).
- **UX do editor, acabamento (etapa 19 do consolidado):** alvos de 24 px, `aria-selected` nas abas do `#output`, `th`/`aria-label` nas tabelas; a lista dos marcadores (`#mkList`, modos 3 a 6) anda pelo teclado, mas o foco entre ela e o painel direito ainda não foi conferido. "Pickups" (o chip do modo Objetos: carros, árvores e objetos de telhado) x "Power-ups" (modo 4): o chip ainda não foi renomeado.
- **Marcadores: rascunho no navegador:** o rascunho de pontos de início, power-ups e redes (modos 3 a 6) fica só na memória da página; o `beforeunload` e os avisos de troca de fase ou mod já contam esses rascunhos, mas uma queda da aba perde as edições que não foram salvas (o rascunho do navegador, `writeDraft`, guarda só o patch). Começar por guardar `pt`/`nets` ao lado do patch no mesmo `localStorage`.
- **Céu: o estilo do pedaço original na vista:** o jogo força no modelo de céu o estilo do pedaço que ele troca (mistura, alfa e campos de `sky_swap`); a vista usa o estilo do próprio modelo, então uma camada de nuvens translúcida no jogo pode sair opaca no editor. Começar por copiar `userData.gs` do primeiro mesh original do tipo para os materiais de `skyDrawing` (`updateSkyView` em `web/app.js`), conferindo com um modelo translúcido.

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
- **Linux, o que falta conferir:** controle físico (só um controle sintético foi testado), Wayland/XWayland e gerenciadores de janela além do i3, a campanha inteira (o AppImage já sai nos releases como preview; `docs/LINUX_MAINTENANCE.md`, diário em `docs/investigacoes/2026-11-04/port_linux.md`).

## Descartado
- **Customs de tipo chefe na eliminação:** a seleção segue recusando Goliath, Jelly, Cerebulon e Mecha (o diálogo 8 só conhece as colunas 1 a 10); o dono decidiu não fazer.
- **LOD dos props do Blender:** o custo por quadro não pede (10 props de ~1000 triângulos não mudam o FPS); o dono decidiu não fazer.
