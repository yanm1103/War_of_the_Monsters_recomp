# TODO

O que falta fazer, e só isso. Relatos de bug passam na frente. Item terminado sai deste arquivo: a história vai para a mensagem do commit e o que passou a existir vai para o doc de referência (no presente). Regras dos docs: `CLAUDE.md`, "Documentação".

## Ordem
| # | Item | Depende de | Custo |
|---|---|---|---|
| 8 | Mods que adicionam monstros (as skins já existem) | — | médio/alto |
| 13 | Campanha (adventure) com até 4 jogadores | — | o mais alto |

### 8. Mods que adicionam monstros
Como funciona hoje: `docs/passos/MODS.md` ("Skins", "Monstros custom").
- **Monstros com arquivos próprios:** campo `files` no `monster.json`, com um tipo livre (6, 12 ou 16) ou remapeando um tipo. Hoje o custom usa o tipo de um monstro do jogo. Modelo novo depende de o `.NGP` voltar pelo kit (hoje ele só sai).
- **Arquivo de mod maior que o do disco** fora de `MON/*`: o jogo carrega em regiões de tamanho fixo e pode sobrescrever o que vem depois (o log avisa). Abrir espaço por tipo de arquivo.
- **Aviso dos tetos para skin com `.TEX`/`.RTX`/`.PTR` próprios:** a skin não tem original para comparar, então hoje nada avisa. O log de carga deveria comparar com os tetos reais: `texInfo[1000]` (`maxTexId` final), VRAM residente do `.RTX` e o buffer de 0x30D40 do `.PTR` (`0f091a4:docs/investigacoes/2026-10-23/skins_mod_carga.md`, item 8).

### 13. Campanha com até 4 jogadores
Contexto: `23b8a40:docs/investigacoes/2026-10-27/campanha4_0{1..4}_*.md` (fluxo, cinemas e vistas, chefes e fases, menu e save). Decisões do dono:
- **Entrada:** linha ADVENTURE na tela 5 (multijogador), no molde da TRAINING: escolha de controles, seleção de 2 (tela 6) ou de 3 e 4 (nativa), depois a fase 1 (`changeScreenHook` 6→0x63, recopiar as IAs que o `selectFinish` apaga).
- **Tela:** 2 jogadores em metades, 3 e 4 em quadrantes (`splitPlayers`/`gameReestablishViews` do `wotm_split4.inc` aceitando o modo 1; 2 jogadores na aventura ainda não têm gancho). Largada: a lista 0 é um ponto só, precisa de regra para N.
- **Vidas:** cada jogador com as suas; quem fica sem sai até o fim da fase (molde da eliminação do `wotm_modes4.inc`). Hoje `EvaluateOnePlayerStoryStatus` (0x1A7C08) e o caso 1 do `gameResolveLifeAndDeath` (0x138068) só contam e só reerguem o jogador 1, e a vitória vem só da pose dele.
- **Chefes:** alvo sorteado entre os jogadores vivos, sem vida extra. Hoje miram o jogador 1: Mecha e jelly (`m_monster7C`, gravado uma vez), cabeças/raízes/base da planta, giro e mísseis do Goliath (o golpe comum só fere o Goliath vindo do jogador 1). Troca barata: outro jogador em `m_monsters[0]` durante a chamada (como o TopSpin do `wotm_custom.inc`). Na cratera da fase 6 só o jogador 1 é levado ao ponto fixo (os outros ficam fora do mapa); em Tóquio o fim exige o jogador 1 vivo. 0x1A0/0x1C0/0x1E0 são códigos de monstro (Goliath, jelly, final), não slots.
- **Tokens:** só os do jogador 1, multiplicados pelo número de jogadores (2: 1,2x; 3: 1,3x; 4: 1,4x), gancho no `grandTotal` (0x1D67E8) chamado do `EvaluateOnePlayerStoryStatus`.
- **Cinemas:** os roteiros animam só `m_monsters[0]` (os outros ficam parados na cena), o `cleanUpForMovie` limpa só o 0, só os controles 0 e 1 pulam, o `endCinema` refaz só as câmeras 0 e 1; `doCinema`/`endCinema` decidem a divisão por `m_numMonsters`. Barra do chefe posta na câmera 0 (errada nas outras vistas: gancho no `drawBossHealthBar`); dicas e vidas num HUD só.
- **Final:** um `GenesisMovie` atrás do outro, na ordem dos jogadores.
- **Save:** guarda os jogadores e os monstros (chave `coop` no `save.json`, binário igual; `adventureToJson` precisa preservá-la). Carregar um save co-op leva à escolha de controles e dali direto à fase. `setSaveData` (0x1679A8) força 1 jogador.
- **Depois:** entrar e sair do grupo entre fases (join/leave).
- **Medir na carga:** `texInfo[1000]` e VRAM das `.RTX` com 4 jogadores + 4 IAs (Canyon com `ASSBOSS1` é o pior caso).

## Decomp
- **Fora do caminho crítico:** oracle para funções com chamadas; cobertura com gameplay para realimentar o `boot_coverage_asm.csv` (`57d4afd:docs/investigacoes/2026-10-19/pendencias_oracle_cobertura.md`, seções 2 e 3).

## Fora da ordem (até o dono escolher)
- **4 jogadores sempre a 60:** com 4 vistas, ~80% dos quadros saem a 60 e o resto a 30; quem segura é o EE (mediana ~14 ms, p90 ~17,5 ms). Candidatos, pelo perfil das amostras do EE (`PS2X_HOSTPROF=2` + `_RAW=1`, resolvido pelo `wotm.map` do mesmo build): o envoltório do VU0 traduzido (`executeVU0Microprogram` copia ~1 KB de estado em cada sentido, ~3%) e o `checkpointDue` fora de linha (~3%), ambos costura do PS2Recomp; a montagem do pacote do VU1 que o `wotm.exe` descarta (`pktAddVu1ObjAsm`, ~4%, arriscado: item G3 de `723b110:docs/investigacoes/2026-10-23/performance_vistas.md`). Na janela, as partículas ainda no modo imediato do rlgl (~0,6 ms por vista). Comparar alternando na mesma corrida (a luta IA x IA não se repete).