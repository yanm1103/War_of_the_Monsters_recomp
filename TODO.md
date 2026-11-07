# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Decomp
- **`dbsTraverse` (`src/common/dbs.cpp`, rascunho `NON_MATCHING`):** o caso TRANSLATE não soma o `trans` na posição acumulada; o retail soma (`docs/investigacoes/2026-11-06/objetos_fase.md`, seção 2).
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Editor de fase
Rearranjar fases existentes (mover, girar, apagar, duplicar prédios e objetos, inclusive de outra fase) com um editor que grava o patch JSON por fase em `mods/<mod>/levels/` (`docs/guides/MODS.md`, "Level patches"; aplicado pelo `wotm_levelpatch.inc`). Fase nova do zero fica para depois.
3. **Fases custom** (`MODS.md`, "Custom stages"; plano em `docs/investigacoes/2026-11-06/fases_custom_plano.md`): o jogo já carrega pelo `WOTM_MATCH=3,1,@<id>`. Falta:
   - Decisão do dono sobre a trava: hoje a custom herda a da base (alternativa: não listar a custom de base trancada).
4. **Objetos novos vindos do Blender** (plano em `docs/investigacoes/2026-11-06/objetos_blender_plano.md`; conversor `tools/lvlprop.py`, diário `props_passo0_1.md`; a carga na fase, o editor e a imagem própria do material já existem, `MODS.md`, "props" e "New props"): falta provar jogando a colisão dos props (`hull_*` e `coll_*` carregam e passam a regra da caixa e das células; ninguém bateu neles ainda; `coll_*`: matId e bits de caixa dos triângulos copiados do que os prédios usam) e faltam os passos 6 e 8; o destrutível (`state1_*`, `MODS.md`) lança escombros (`state1_debris_*`) e poeira (`state1_smoke_*`); a pancada de verdade e a colisão dos estados em jogo ainda não foram provadas (o 4 e o 7 saíram com o pacote CT32 na arena e o texId dado na carga, sem `.TEX` no mod). Textura própria: UV fora de 0..1 é preso na borda (sem repetir).
5. **Céu:** o painel Fight HUD and sky troca as imagens do céu por PNG (`lvlhud.py`, papel `sky`) e a entrada `{"model", "sky": true}` de `props` troca a malha do domo por um modelo do Blender (`MODS.md`, "Sky"; provado no jogo em CENTRAL). O editor escolhe o modelo (menu Add prop, "Sky dome"). Falta: ver o céu novo na vista do editor (só o jogo o mostra); trocar as nuvens (o ramo CHAR_INSTANCE); jogar com UFO, SANFRAN, DBALL e 3-MILE, de céu com outra estrutura (UFO tem 7 OBJECTs fora das nuvens); céu de outra fase (o modelo é de uma fase só).
6. **Tamanho da fase:** a chave `grid` e o botão do editor existem (`MODS.md`, "Level patches"). Falta jogar com um monstro andando fora do retângulo original (colisão, tiro e rede dos carros; a rede é do `.FOP`, os nós não mudam).
7. **Paleta Ctrl+P:** falta achar também os props originais de outras fases (hoje só objetos da fase e props do mod; a prévia dos props do mod existe).
9. **Depois:** fase do zero como "base + geometria trocada" (mesmo relatório, seção 6).
   - Patch de fase acima de 1 MB: segunda arena na memória alta (`WOTM_HIGHRAM`). Contexto: `docs/investigacoes/2026-11-06/copia_profunda.md`.

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
- **Linux, o que falta conferir:** controle físico (só um controle sintético foi testado), Wayland/XWayland e gerenciadores de janela além do i3, a campanha inteira. Publicar o AppImage depende de resolver o código-fonte correspondente e o código derivado do jogo (`docs/LINUX_MAINTENANCE.md`; diário em `docs/investigacoes/2026-11-04/port_linux.md`).

## Descartado
- **Customs de tipo chefe na eliminação:** a seleção segue recusando Goliath, Jelly, Cerebulon e Mecha (o diálogo 8 só conhece as colunas 1 a 10); o dono decidiu não fazer.