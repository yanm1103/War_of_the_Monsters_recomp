# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Ordem
| # | Item | Depende de | Custo |
|---|---|---|---|
| 8 | Mods que adicionam monstros (as skins já existem) | — | médio/alto |
| 14 | Primeiro release (binário pronto) | — | médio |

### 8. Mods que adicionam monstros
Como funciona hoje: `docs/passos/MODS.md` ("Skins", "Monstros custom").
- **Monstros com arquivos próprios:** campo `files` no `monster.json`, com um tipo livre (6, 12 ou 16) ou remapeando um tipo. Hoje o custom usa o tipo de um monstro do jogo. Modelo novo depende de o `.NGP` voltar pelo kit (hoje ele só sai).
- **Arquivo de mod maior que o do disco** fora de `MON/*`: o jogo carrega em regiões de tamanho fixo e pode sobrescrever o que vem depois (o log avisa). Abrir espaço por tipo de arquivo.
- **Aviso do buffer do `.PTR` para skin com `.PTR` próprio:** o log de carga já mostra os texIds e a VRAM. Falta o buffer de 0x30D40 do `.PTR` (`0f091a4:docs/investigacoes/2026-10-23/skins_mod_carga.md`, item 8).

### 14. Primeiro release (binário pronto)
Decisão do dono: zip com o `wotm.exe` pronto (contém o código traduzido do jogo; vai só nos Releases do GitHub, nunca no repo). Levantamento do que falta e ordem sugerida: `docs/investigacoes/2026-10-29/release.md`.
- **Primeira execução:** o launcher pede a ISO, confere o SHA1 do `SCUS_971.97` (`tools/setup_runtime.ps1` já faz) e grava `discroot\SCUS_971.97`; o resto vem da ISO (`PS2X_CD_IMAGE`). Nada do disco no zip.
- **Build de release:** o `wotm.exe` com LTCG (o `build_dev` é sem), sem `ps2EntryRunner.exe`, sem variáveis de diagnóstico ligadas; `mods/` do repo dentro do zip.
- **Pacote:** script em `tools/` que monta o zip (exe, DLLs do SDL2/FFmpeg/raylib, launcher, `mods/`), com as licenças de terceiros e o GPL-3.0 (apontar o código-fonte: este repo + `tools/runtime_patch/`).
- **Conferir:** descompactar numa pasta limpa, numa máquina (ou conta) sem o ambiente de build, e jogar luta, aventura e save.

## Decomp
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).
