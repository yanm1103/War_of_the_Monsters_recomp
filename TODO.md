# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Decomp
- **`dbsTraverse` (`src/common/dbs.cpp`, rascunho `NON_MATCHING`):** o caso TRANSLATE não soma o `trans` na posição acumulada; o retail soma (`docs/investigacoes/2026-11-06/objetos_fase.md`, seção 2).
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Editor de fase
Rearranjar fases existentes (mover, girar, apagar, duplicar prédios e objetos, inclusive de outra fase) com um editor que grava o patch JSON por fase em `mods/<mod>/levels/` (`docs/guides/MODS.md`, "Level patches"; aplicado pelo `wotm_levelpatch.inc`).

Ideias para facilitar a vida de quem faz mod (o que o editor ainda não tem; cada uma só no editor, salvo onde disser), na ordem sugerida:
8. **Precisam do runtime:** pontos de vida das construções originais, tipo do pickup (carro estacionado para carro no trânsito, `id2` bit 0), escala por objeto.

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
- **Linux, o que falta conferir:** controle físico (só um controle sintético foi testado), Wayland/XWayland e gerenciadores de janela além do i3, a campanha inteira. Publicar o AppImage depende de resolver o código-fonte correspondente e o código derivado do jogo (`docs/LINUX_MAINTENANCE.md`; diário em `docs/investigacoes/2026-11-04/port_linux.md`).

## Descartado
- **Customs de tipo chefe na eliminação:** a seleção segue recusando Goliath, Jelly, Cerebulon e Mecha (o diálogo 8 só conhece as colunas 1 a 10); o dono decidiu não fazer.
- **LOD dos props do Blender:** o custo por quadro não pede (10 props de ~1000 triângulos não mudam o FPS); o dono decidiu não fazer.
