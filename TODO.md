# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Ordem
| # | Item | Depende de | Custo |
|---|---|---|---|
| 8 | Mods que adicionam monstros (as skins já existem) | — | médio/alto |
| 15 | Exército em qualquer fase (etapa 3) | — | médio/alto |

### 8. Mods que adicionam monstros
Como funciona hoje: `docs/guides/MODS.md` ("Skins", "Monstros custom").
- **Monstros com arquivos próprios:** campo `files` no `monster.json`, com um tipo livre (6, 12 ou 16) ou remapeando um tipo. Hoje o custom usa o tipo de um monstro do jogo. Modelo novo depende de o `.NGP` voltar pelo kit (hoje ele só sai).
- **Arquivo de mod maior que o do disco** fora de `MON/*`: o jogo carrega em regiões de tamanho fixo e pode sobrescrever o que vem depois (o log avisa). Abrir espaço por tipo de arquivo.
- **Aviso do buffer do `.PTR` para skin com `.PTR` próprio:** o log de carga já mostra os texIds e a VRAM. Falta o buffer de 0x30D40 do `.PTR` (`0f091a4:docs/investigacoes/2026-10-23/skins_mod_carga.md`, item 8).

### 15. Exército em qualquer fase
Como funciona hoje: `docs/guides/NATIVE.md`, "Army in versus fights" (Vegas, e helicópteros em Metro e Tóquio). Contexto: `docs/investigacoes/2026-10-25/militares_spawn.md` e `docs/investigacoes/2026-11-04/militares_{ia,caos_modos}.md`.
- **Tanque e caminhão fora de Vegas:** tirar de VEGAS os nós e as texturas dos três veículos para um arquivo de mod (`tools/ngp.py`, `tools/tex.py`), copiar para a RAM alta na carga, rebasear, subir as texturas (espaço na VRAM e `TBP0`) e criar o pool. Só o `VEGAS.FOP` tem nós do tipo 3: `.FOP` de mod (`mods/PTS/<FASE>.FOP`) ou gancho no `getClosestNode`.
- **Helicóptero sem rede:** 3-MILE, SANFRAN, MINISF, CANYON2 e CAPITOL não têm nó do tipo 1; nas seis fases sem `.FOP`, só helicópteros (decisão do dono).
- **Motor do tanque fora de Vegas:** toca o som 0 do banco da fase; cópia do `VEGAS_S.BNK` como o `panelShellBank`, ou sem motor.

## Decomp
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Fora da ordem (até o dono escolher)
- **Port para Linux:** o jogo traduzido compila com gcc sem mudança; a camada nativa precisa de sete remendos pequenos (flag AVX2 no CMake, `#ifdef` nas declarações do Windows, recursos do `wotm.rc` em array C) e aí boota até o menu no WSLg. Falta primeira execução, pasta de dados, pilha da queda, vibração, scripts `.sh` e o pacote. Tabela, ordem e custo: `docs/investigacoes/2026-10-30/linux.md`.
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
