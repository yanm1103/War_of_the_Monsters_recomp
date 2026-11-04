# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Ordem
| # | Item | Depende de | Custo |
|---|---|---|---|
| 8 | Mods que adicionam monstros (as skins já existem) | — | médio/alto |

### 8. Mods que adicionam monstros
Como funciona hoje: `docs/guides/MODS.md` ("Skins", "Monstros custom").
Levantamento (tipo livre descartado em favor de remapear por instância; níveis do kit; medidas): `docs/investigacoes/2026-11-04/monstro_novo.md`. Ordem proposta:
- **Kit L2, animações no mesmo esqueleto:** trocar as existentes, depois acrescentar; pede exportar um nó por ANIM_XFORM no `monglb.py`. ~1 a 2 semanas.
- **`"special"` no `monster.json`** (ponteiro em +0x7980 depois do `initDynamics`).
- **L3, esqueleto novo** (3 a 6 semanas): só se ainda fizer falta depois dos anteriores.

## Decomp
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Fora da ordem (até o dono escolher)
- **Port para Linux:** o jogo traduzido compila com gcc sem mudança; a camada nativa precisa de sete remendos pequenos (flag AVX2 no CMake, `#ifdef` nas declarações do Windows, recursos do `wotm.rc` em array C) e aí boota até o menu no WSLg. Falta primeira execução, pasta de dados, pilha da queda, vibração, scripts `.sh` e o pacote. Tabela, ordem e custo: `docs/investigacoes/2026-10-30/linux.md`.
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
