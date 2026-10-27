# Campanha com até 4 jogadores (TODO 13), parte 1: o fluxo da aventura no Shell/TheGame

Levantamento só de leitura (sem rodar o jogo): `src/game/Shell.cpp`, `TheGame.cpp`, `screen.cpp`, o asm do retail e o C++ do
PS2Recomp (`wotm-recomp-win/gen/output_fix`, para o `game/ShellFinished`, que não tem asm separado), mais os ganchos
`native/runtime/wotm_split4.inc`, `wotm_modes4.inc`, `wotm_padsel.inc` e `wotm_save.inc`. Os endereços são os de
`native/wotm_syms.h`.

## 1. Como a aventura anda hoje (1 jogador)

**Entrada.** Menu principal (tela 1, `screenMain`): o item 1 grava `m_numPlayers = currentSelection` (1) e vai à tela 2.
`screenGameModes1P` (0x18F040), item 1 "adventure": `m_mode = 1`, `m_levelNum = 1` (se não veio da linha de comando),
`m_numAIs = m_levelNumAIs[1]`, `changeScreen(2, 3)`. Item 2 é o free for all de 1 jogador (modo 2) e o 3, o endurance (modo 4).

**Seleção.** `screenCharSelect1P` (0x18F628, tela 3): com `m_mode` 1, na primeira passada copia as IAs da fase de `levelMonsters`/
`levelMonsterModels` para `m_monsterSel[4..]`/`m_costume[2..]` e zera `Shell+0x2C18` e `+0x2B2C` (pontos acumulados). O X grava
`m_monsterSel[0]` e `m_costume[0]` e vai **direto à carga** (`changeScreen(3, 0x63)`): não há escolha de fase, de slot nem mapa. No
port a tela 3 é a seleção "solo" no telão (`wotm_padsel.inc`, `soloBegin(3)`).

**Carregar** (tela 9, "load game"): `screenLoadSave` (0x19BD00) chama `MonsterMc::updateStoryModeLoad` (0x168958); o
`setSaveData` (0x1679A8) põe fase, `m_monsterSel[0]`, `m_lives[0]`, pontos e `m_costume[0]`, mais `m_numPlayers = 1` e `m_mode = 1`;
as IAs vêm das tabelas da fase. No port isso é a lista de saves em JSON (`wotm_save.inc`).

**Modo no jogo.** `Shell::InitRTState` (0x1A7910) copia `m_mode` para `TheGame::m_gameMode` (+0x1203C8), `m_levelNum` para
`m_levelId` e `m_numPlayers` para `m_numMonsters`, e chama `InitPlayerLives` (0x1A8CC0): `m_lives[0..3] = NUM_LIVES[m_mode]`
(modo 1: 3 vidas; a tabela já preenche as 4 posições). Modo 5 é um **co-op de 2 jogadores abandonado**: `NUM_LIVES[5] = 1`,
`gameGetStartPoint` o trata com os modos 2 a 6 e `EvaluateTwoPlayerCoopStatus` (0x1A8230) é vazia.

**Sequência de fases** (índice de `GameLevelNames`; `m_levelNum++` a cada vitória): 1 central, 2 vegas, 3 canyon2 (chefe Goliath,
`assBoss`, `canyon2LevelProgression`), 4 metro, 5 airport, 6 3-Mile (chefe planta, `craterSwitched`, `numDeadHeads`,
`threeMileInitAi`), 7 sanfran, 8 island, 9 tokyo, 10 ufo, 11 capitol (chefe final: Jelly 0x1C0 e Cerebulon 0x1E0, `finalBoss+0x1C`
é a fase do chefe). As IAs de cada fase estão em `levelMonsters[fase][..]` (0x2A1BB0) com o número em `m_levelNumAIs` (Shell+0x29E0);
nas fases 3, 6 e 11 há tipos de chefe (0x1A0, 0x1C0, 0x1E0) que o `SetAIMonster` desliga e entrega ao script do chefe.

**Fim de rodada.** `TheGame::gameResolveLifeAndDeath` (0x138068), caso 1: qualquer `m_monsters[i]` com `m_unkF6` (fora de vez) dá
`rtReturnToShell(1)` (com `fadeOut` se `m_lives[0] == 1`, senão `fadeOutAndIn`); sem IA viva (e fora das fases 3, 6, 9, 10, 11, que
terminam por script) a pose de vitória é só de `m_monsters[0]`, e o fim dela dá `rtReturnToShell(0)`.
`Shell::EvaluateGameStatus` (0x1A7960) manda o modo 1 para `EvaluateOnePlayerStoryStatus` (0x1A7C08):
- `r == 1` (morte): sem IA viva vira vitória (3-Mile: só com mais de 2 cabeças mortas). Senão `--m_lives[0]`; com vida,
  `playerInit` do slot `m_viewSlot[0]`, sai da vista unificada se ligada (resto de co-op: com 1 jogador nunca liga) e avisa no HUD 0
  (mensagens 0x25/0x26). Sem vida: diálogo 1.
- `r == 0` (fase vencida): bônus por fase × dificuldade em `Shell+0x2B34`, `m_levelNum++`, IAs da fase nova, pontos
  `= m_tokens[0].grandTotal() + bônus` em `+0x2B28`, somados aos battle tokens (`+0x2B24`); diálogo 2.
- fase 11 vencida ou `r == 4`: bônus final, diálogo 3 e `GenesisMovie` (0x1AB4A0: o filme de final pelo `m_monsterSel[0]`,
  `exitFromAdvStory = 1`).
- `r == 3`: sair.

**Diálogos** (`displayDialog`, 0x1AC3E0; tabela de saltos lida do ELF): 0 pausa, 1 `lostLevelOverlay`, 2 `wonLevelOverlay`,
3 `wonStoryOverlay`, 4 `wonChallengeLevelOverlay`, 5 `finishedMultPlayBattleLevel`, 6 `allDeadMultPlayBattleLevel`,
7 `selectNextMultPlayBattleLevel`, 8 `selectNextMultPlayer`, 9 `lostEnduranceOverlay`, 10 online, 11 (0xB) `finishedMultPlayElim`,
13 a 15 minigames, 16 `areYouSure`.
- `wonLevelOverlay` (0x1AEDC0): roda `MonsterMc::updateStoryModeSave` (0x168708: "salvar?", cartão, slot, `McSaveAdventure`) e põe
  `m_inSession = 0`, `m_inMenus = 1`: a sessão seguinte carrega a fase nova (`m_levelNum` já incrementado).
- `wonStoryOverlay` (0x1ADEC0): `updateEndOfStorySave` (0x168280) e `m_inSession = m_inMenus = 0` (volta aos menus, depois o filme).
- `lostLevelOverlay` (0x1ADF40, "retry"/sair): no modo 1, por fase (`m_levelId`), ou recarrega a sessão (`m_restart = 1`,
  `m_inMenus = 1`, `m_inSession = 0`: o `InitRTState` refaz as vidas) ou chama `InitPlayers` no lugar e conta a tentativa em
  `Shell+0x2B60`.

**Fases de chefe com várias etapas.** `Shell::InitPlayers` (0x1A6D60) tem ramos próprios para `m_mode == 1` nas fases 3, 6 e 11:
chama `InitPlayerLives` (as vidas voltam a 3 a cada etapa), cria as IAs só na primeira etapa e, nas outras, refaz a vida do chefe e os
pilares (`rebuildCanyon2Pillars`/`rebuildCapitolPillars`, `HealthMeter::creditFull`).

**Gravação.** `MonsterMc::getSaveData` (0x167938) grava no slot de 0x3C bytes (10 por cartão em `savegame.dat` de 0x280) só
fase, `m_monsterSel[0]`, `m_lives[0]`, pontos e `m_costume[0]`: 0x14 bytes, **0x28 bytes livres por slot** (cabem monstro, traje e
vidas de mais 3 jogadores, 3 × 12 bytes). No port o slot vive no JSON (`adventureToJson`/`jsonToAdventure`, `rowFromRam`), que
pode ganhar um array `players` sem mexer no binário.

## 2. O que o caminho de 2 jogadores do retail faz e a aventura não

- **Vistas:** `TheGame::gameReestablishViews` (0x139530) cria as duas metades (viewport 1 e 2, mais a 5 da vista unificada) só nos
  modos 3, 6 e 7; modos 8 e 9, uma vista; **modo 1 (e 0, 2, 4, 5) cai no padrão: uma vista em tela cheia**. A aventura com
  `m_numPlayers` 2 teria a câmera 1 seguindo o jogador 2 sem vista nenhuma.
- **Câmeras:** `SetPlayerMonster` (0x1379C0) já faz `gameInitCamera(i, slot)` por jogador. `SetAIMonster` (0x137B10) põe a IA na
  câmera 1 quando `m_numMonsters < 2` (caso da aventura de 1) e na 3 senão: com 2 ou mais jogadores as IAs já saem da câmera 1.
- **Controles e trajes:** `InitPlayers` manda `m_costume[i != 0]` (todos depois do 1 com o traje do 2); `LoadMonstersDB` (0x1A9BF0)
  monta o caminho dos jogadores 3/4 com o monstro do jogador 2; `Monster::playerInit` lê `m_ctlrCfg[m_index]` (só 2). Os três já são
  contornados pelo `wotm_split4.inc` com 3 e 4 jogadores.
- **Regras:** a aventura não tem nenhuma das regras de 2 jogadores: as vidas, a morte, a vitória, os pontos e o final são todos do
  jogador 0 (seção 3).

## 3. Onde a aventura assume 1 jogador (lista de trabalho)

| Função (endereço) | O que assume | Para N jogadores | Risco |
|---|---|---|---|
| `screenGameModes1P` (0x18F040) / `screenGameModes2P` (0x191F08) | adventure só na lista de 1 jogador | entrada nova (pergunta 1): item "adventure" na lista da tela 5 depois da escolha de controles, que já dá `m_numPlayers` N e o mapa de controles | baixo; a lista da tela 5 já é texto nosso (`wotm_mainmenu.inc`) |
| `screenCharSelect1P` (0x18F628) | um jogador escolhe e vai à carga | usar a seleção de 3 e 4 do telão (`wotm_padsel.inc`, tela 6) também com 2, em modo 1, sem CPUs, saída `changeScreen(6, 0x63)` como nos minigames, e com as IAs da fase como a tela 3 faz | médio: a seleção de 2 do retail (`screenCharSelect2P`) não conhece o modo 1 |
| `Shell::InitRTState` (0x1A7910) / `InitPlayerLives` (0x1A8CC0) | nada (já preenche 4 vidas) | serve como está | baixo |
| `Shell::LoadMonstersDB` (0x1A9BF0) | jogadores 3/4 pelo monstro do 2 | o `loadLevelFiles` do `wotm_split4` já desvia 3/4 pelo caminho das IAs em qualquer modo exceto 6; com as IAs da aventura (até 4) cabe no limite de 10 | médio: memória nas fases de chefe (Capitol carrega Jelly e Cerebulon), testar com `WOTM_HIGHRAM` |
| `Shell::InitPlayers` (0x1A6D60) | ramos de chefe (fases 3, 6, 11) com `InitPlayerLives` a cada etapa | os jogadores já entram pelo laço comum; decidir se as vidas voltam a cada etapa para todos | médio |
| `TheGame::SetAIMonster` (0x137B10) | `m_numMonsters < 2` → IA na câmera 1 | com N ≥ 2 vai à 3; o `initCamera` do `wotm_split4` tira a IA da câmera 3 com 4 jogadores | baixo |
| `TheGame::gameReestablishViews` (0x139530) | modo 1 = tela cheia | gancho: com modo 1 e N ≥ 2, fazer as metades (2) ou os quadrantes (3, 4). O `splitPlayers` do `wotm_split4` só aceita 3/4 nos modos 3, 6, 7; para 2 jogadores na aventura não há gancho (o retail faz as metades só pelo `m_gameMode`) | médio: a vista unificada (câmera 2) e o `LeaveUnifiedView` (0x11C868) leem `m_gameMode` |
| `TheGame::gameGetStartPoint` (0x138D68) | modo 1: jogador na lista 0 no primeiro nascimento, renascimento na 3 | a lista 0 deve ter um ponto só: N jogadores nasceriam juntos (o empurrão dá NaN, já visto nos quadrantes). O `startPoint` do `wotm_split4` tem o deslocamento (`rotatedStart`, teste de 120 unidades), mas só age com `splitPlayers` | alto se esquecido; conferir os pontos das listas por fase (`WOTM_SPLITLOG` imprime as listas) |
| `TheGame::gameResolveLifeAndDeath` (0x138068), caso 1 | fade pelo `m_lives[0]`; pose de vitória só do `m_monsters[0]` | vitória de qualquer jogador vivo; um jogador fora (sem vidas) não pode prender a fase; fade pelo total de vidas | médio |
| `Shell::EvaluateOnePlayerStoryStatus` (0x1A7C08) | `m_lives[0]`, renasce `m_viewSlot[0]`, HUD 0, pontos de `m_tokens[0]` | achar o morto (`m_monsters[i]` com saúde ≤ 0), gastar a vida dele, `playerInit` dele, mensagem no HUD dele; sem vidas: sai como na eliminação de 3/4 (`wotm_modes4`: monstro escondido, câmera segue quem continua) ou diálogo 1 se todos acabaram; pontos = soma dos 4 `TokenManager` (já são por `m_index`) | alto: é o centro da mudança; o `displayDialog` é modal, chamar como cauda (o trampolim do `wotm_modes4` já resolve isso) |
| `lostLevelOverlay` (0x1ADF40) | "retry" refaz a fase do jogador 1 | o retry recarrega ou chama `InitPlayers`, que já percorre os N jogadores; só conferir as vidas | baixo |
| `wonLevelOverlay` / `updateStoryModeSave` / `getSaveData` (0x167938) | grava só o jogador 1 | gravar os N no JSON (`players`), e no binário nos 0x28 bytes livres do slot se for para manter o `savegame.dat` | baixo (o JSON é nosso) |
| `screenLoadSave` (0x19BD00) / `setSaveData` (0x1679A8) | carrega `m_numPlayers = 1` | carregar uma aventura de N pede o mapa de controles antes (pergunta 3) | médio |
| `Shell::GenesisMovie` (0x1AB4A0) | final pelo herói `m_monsterSel[0]` | escolher o final (pergunta 4) | baixo |
| `Hud::update` / `Hud::initAfter` (0x143188) | HUDs 0/1; barra do chefe (`drawBossHealthBar`) e mensagens no 0 | os HUDs 2/3 do `wotm_split4` valem se o modo 1 for aceito; conferir a barra do chefe em cada vista | médio |
| `Monster::playerInit` (0x165F10) | `clearListFlags` com 2+ jogadores e 2+ IAs | conferir o que a lista faz na aventura | baixo |
| `Ai::isMinion` (0x101EF8), `AiScript3Mile`, `FinalBoss`, `PlantBoss`, `HomingBug`, `PowerUps` | `m_gameMode == 1` com `m_levelId`; vários usam `m_monsters[0]`/`m_viewSlot[0]` como "o jogador" | fora desta frente (chefes e cinemas); `m_monsters[0]`/`m_viewSlot[0]` aparece em `PlantBoss.cpp` (8), `FinalBoss.cpp` (5), `Sound.cpp` (4), `Weapon.cpp` (3) e em funções ainda em asm | alto, mas é outra frente |

São 79 funções do asm que leem `m_gameMode` e 32 que leem `Shell::m_mode`; as que comparam com 1 são as listadas acima mais as de chefe.

## 4. Ganchos de 3/4 que já serviriam

O `wotm_split4.inc` decide tudo pelo `splitPlayers(rdram)`: `m_numPlayers` 3 ou 4 **e** `m_gameMode` 3, 6 ou 7. Aceitar o modo 1 ali
(e um caminho para N = 2) liga de uma vez: quadrantes (`reestablishViews`, `writeQuadDefs`), cinema em tela cheia (`cinemaViews`;
a aventura tem cinemas em quase toda fase), sem vista unificada (`okToUnify`, `leaveUnified`), HUDs 2/3 (`hudUpdate`,
`hudInitAfter`, `boostAndRage`), câmera 3 das IAs (`initCamera`), trajes 3/4 (`setPlayerMonster`), o borrão
(`blurInit`/`blurDraw`), `throwPreemption` e o deslocamento da largada (`startPoint`, que hoje só reescreve a lista 1 dos modos 2 a 6:
para a aventura a regra precisa ser outra, lista 0). Já valem sem mudança: carga dos jogadores 3/4 (`loadLevelFiles`/`initPlayers`,
sem teste de modo), controles 3/4 (`scePad*` no slot 1). Não servem: `evaluateBattle` (só modo 3) e as regras do `wotm_modes4.inc`
(eliminação e minigames), mas o trampolim dos diálogos modais e o "jogador sai, câmera segue quem continua" da eliminação são os
blocos a reaproveitar no `EvaluateOnePlayerStoryStatus`.

Cuidado ao aceitar o modo 1 no `splitPlayers`: o teste é "modo com tela dividida"; trocar o `m_gameMode` para 3 durante chamadas
(truque possível no `gameReestablishViews`) não serve fora dele, porque os chefes e scripts testam `m_gameMode == 1`.

## Perguntas para o dono

1. **Entrada:** "adventure" como quarto item da lista multijogador (tela 5, depois da escolha de controles), ou uma pergunta de
   número de jogadores dentro do 1 player → adventure?
2. **Vidas:** cada jogador com as suas 3 (o `m_lives[4]` já existe) ou um pote comum? Quem fica sem vidas sai (câmera segue os
   outros, como na eliminação de 3/4) até a próxima fase, ou volta quando alguém vence a fase? Fim de jogo só quando todos acabaram?
3. **Saves:** o slot guarda todos os jogadores (monstro, traje, vidas de cada um); carregar uma aventura de N jogadores passa pela
   escolha de controles? Pode-se carregar com menos jogadores do que foi gravado?
4. **Final:** o filme de final é o do herói do jogador 1, ou um por jogador em sequência?
5. **Dificuldade:** com mais jogadores, os chefes ganham vida (a do chefe final já é `75 × dificuldade + 100`) ou fica como o retail?
6. **2 jogadores:** tela dividida em metades (como o versus do retail, com a vista unificada quando estão perto) ou quadrantes como 3/4?
