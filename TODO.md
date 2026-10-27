# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Ordem
| # | Item | Depende de | Custo |
|---|---|---|---|
| 8 | Mods que adicionam monstros (as skins já existem) | — | médio/alto |
| 10 | Chefes jogáveis | 8 | alto |
| 13 | Campanha (adventure) com até 4 jogadores | — | o mais alto |

10 é um caso particular de "adicionar monstro" (8). A campanha com 4 fica por último: cinema, chefes e regras foram feitos para 1 jogador.

### 8. Mods que adicionam monstros
Como funciona hoje: `docs/passos/MODS.md` ("Skins", "Monstros custom").
- **Monstros com arquivos próprios:** campo `files` no `monster.json`, com um tipo livre (6, 12 ou 16) ou remapeando um tipo. Hoje o custom usa o tipo de um monstro do jogo. Modelo novo depende de o `.NGP` voltar pelo kit (hoje ele só sai).
- **Skins no elimination (modo 6):** o `Shell::LoadMonstersDB` carrega o traje 0 dos 10 monstros uma vez, e os jogadores trocam de monstro entre as rodadas sem recarregar. Escolher traje ali exige mudar a carga (um arquivo por monstro escolhido), não só a tela.
- **Arquivo de mod maior que o do disco** fora de `MON/*`: o jogo carrega em regiões de tamanho fixo e pode sobrescrever o que vem depois (o log avisa). Abrir espaço por tipo de arquivo.
- **Aviso dos tetos para skin com `.TEX`/`.RTX`/`.PTR` próprios:** a skin não tem original para comparar, então hoje nada avisa. O log de carga deveria comparar com os tetos reais: `texInfo[1000]` (`maxTexId` final), VRAM residente do `.RTX` e o buffer de 0x30D40 do `.PTR` (`0f091a4:docs/investigacoes/2026-10-23/skins_mod_carga.md`, item 8).


### 10. Chefes jogáveis
- Como estão: `MODS.md`, "Monstros custom" (Goliath, Jelly e Cerebulon; mod `mods/Cerebulon/`). Plantboss não é viável.
- **Mecha** (primeira fase do Cerebulon): na branch `wip/mecha` (monstro montado por `tools/mechamon.py`, escudo funcionando). O laser sai reto e não mira (a torre não gira), não dá dano nem pega prédio, e o Mecha não pula; pistas em `docs/investigacoes/2026-10-27/mecha_progresso.md`.
- Seleção nativa: conferir o enquadramento do Jelly e do Cerebulon na casa CUSTOM (`wotm_savepics.hpp`, `kFraming`).

### 13. Campanha com até 4 jogadores
- A campanha é de 1 jogador: o modo de jogo (`m_gameMode`), as cenas de cinema e as fases com chefes (slots extras 0x1C0/0x1E0 no Capitólio, 0x1A0) precisam ser revistas.

## Brechas (baratas, em qualquer momento)

## Contínuas
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).



## Descartado pelo dono (não propor de novo)
- Tela de destrave para skins de mod (skin de mod fica sempre aberta) e som próprio por skin.
- 5 a 8 jogadores (câmera compartilhada).
- Opção no painel para fixar o LOD.
- Quadros dos jogadores da seleção nativa em 2D fora do telão (ficam melhor no telão).
- Entrada no modo treino pelo launcher (a linha TRAINING do menu e `WOTM_TRAINING=1` bastam).
- Netcode próprio (rollback): exigiria provar que a lógica dá sempre o mesmo resultado com os mesmos comandos.
- Fazer o Goliath Prime ou outros bosses funcionarem por inteiro (CPU, colisão, animações que o ASSBOSS não tem): monstros custom são mods/experimentais.
