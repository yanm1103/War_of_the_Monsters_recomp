# Modo Arcade: investigação e plano

Pedido do dono (2026-11-11): modo Arcade com fase e monstros sorteados, 7 lutas, cutscene de final como na história.
Luta 4 contra o Goliath Prime (Metro City, Capitol, Midtown Park, Gamblers Gulch, Baytown ou Club Caldera); luta 7 contra o
Cerebulon na fase 3 (o alien em forma crua, tipo 15 `FINAL`, 0x1E0), nas mesmas fases ou no UFO. Tokens como na história, sem save.

Legenda: **[C]** confirmado lendo código/asm; **[I]** inferência, a validar.

## 1. Decisão de base: modo 2 encadeado, não o modo 1

Duas formas de montar:

- **(A) Reaproveitar a aventura (`m_mode` 1) com a sequência de fases trocada.** Ganha de graça os tokens do
  `EvaluateOnePlayerStoryStatus`, o `GenesisMovie` e o fluxo de vidas/continue. Mas o modo 1 arrasta tudo o que é da
  história, preso ao índice da fase [C]: roteiros de `central`/`vegas`/`canyon2`/`ufo`/`finalBoss` (o UFO espera as 3 IAs da
  tabela e encerra por cinema; Vegas desliga o Robo-47 IA e o traz por roteiro), `s_attackOtherAi` desligado, power-ups de
  história (tipos 10..19), exército da aventura em Vegas/Metro/Tokyo, `Count_s.bnk`/`Final_s.bnk`, `InitPlayerLives` nas
  fases 3/6/11, o tutorial da fase 1 e o diálogo de save (`displayDialog(2)`). E o principal: no modo 1 o Goliath só
  existe como chefe de Rosdale Canyon (com as Mutant Ants e o bunker) e o Cerebulon só no Capitol (Mecha → Jelly → Final).
  O pedido é justamente o chefe numa fase qualquer, que o modo 1 não faz.
- **(B) Encadear lutas do modo 2 (1 jogador contra CPU) com um condutor nosso.** É o caminho do modo treino
  (`wotm_training.inc`: modo 2 + flag) e do `WOTM_MATCH`. A fase é uma fase de versus comum, a IA é a genérica, e o
  Cerebulon forma final já luta como CPU numa fase comum (commit 9c6c3c0: `1,cerebulon,1` em Midtown Park). O que a
  história dá de graça (tokens, filme) é pouco código para refazer.

**Recomendação: (B).** O resto do plano assume (B).

## 2. Como o encadeamento funciona

### Entrada
- [C] Linha nova na tela 2 (lista de 1 jogador) de `wotm_mainmenu.inc`: `kArcadeRow = -5` depois de `kTrainingRow`, no mesmo
  esquema (a linha nossa grava `currentSelection[2] = kFreeForAll`; X arma a flag e desarma o treino, e vice-versa).
  "arcade" só usa letras que a fonte do menu tem.
- [C] Molde em `wotm_padsel.inc` `changeScreenHook`: o ramo `from==2 && to==12 && g_menuArmed` do treino vira o do
  Arcade, com `m_numAIs = 1`, `m_killTarget = 1`, `m_monsterSel[4..]` zerados, destino trocado para a tela 6 (seleção).
- [C] Na saída da seleção, `6 → 8` (escolha de fase) vira `6 → 0x63` (carga), como o `adventureStart` do co-op: o
  condutor sorteia o plano inteiro aqui e grava a luta 1 no Shell.
- [C] A flag liga em `rtMainTapped` (`wotm_overrides.cpp`), desarma em `menuDisarm`, `mainHook` e no menu principal.
- Novo `native/wotm_arcade.h` + `native/runtime/wotm_arcade.inc` (estado: plano das 7 lutas, índice da luta atual,
  tokens da corrida, vidas/continues).

### Campos do Shell por luta [C]
`m_levelNum` (+0x2BB0), `m_monsterSel[4]` (+0x2948, tipo<<5), `m_costume[2]` (+0x2BE8), `m_numAIs` (+0x2BB8) = 1,
`m_mode` (+0x2C10) = 2, `m_battleMode` (+0x2B54), `TheGame::m_gameMode` (game+0x1203C8) = 2, `m_killTarget` (+0x2A44) = 1.

### De uma luta para a próxima sem menu
- [C] O laço do `main` (`Shell.cpp:229-333`) recarrega a fase com o que estiver no Shell quando `m_inSession = 0` e
  `m_inMenus = 1`. O modo demo e a história fazem isso; o `dialogAdventure` (`wotm_overrides.cpp:457-469`) e o
  `levelreload::dialogHook` (`wotm_levelreload.inc:114-141`) são os precedentes no port.
- [C] Fim de luta no modo 2: `EvaluateOnePlayerChallengeStatus` (0x1A7FD8) acha o morto e o matador; com
  `m_killTarget = 1` e o matador com `wins > 0`, grava `GM(0x12043C) = matador` e chama `displayDialog(5)`
  (PLAY AGAIN / REBUILD / QUIT).
- [I] Gancho no `displayDialog(5)` com o Arcade ligado, no lugar do diálogo:
  - **vitória** (matador = jogador): soma os tokens (seção 4), grava a luta seguinte no Shell, faz as escritas do
    `levelreload::dialogHook` (`m_restart = 1`, `m_inMenus = 1`, `m_inSession = 0`, pausa 0, `snd_StopAllSounds`,
    `gExitDialog = 0`) e devolve para o `ra`;
  - **derrota**: vidas/continue (seção 5);
  - **luta 7 vencida**: final (seção 6).
- [C] Cuidado: a ordem dos ganchos de `displayDialog` importa (pausa, `dialogAdventure`, levelreload, modes4, split4:
  `wotm_overrides.cpp:3211-3217`). `InitPlayers` zera os `wins`.
- [C] Pausa QUIT: o caminho do treino (`sureHook`, `rtMain` devolve 3, volta ao menu). Descarta a corrida.

## 3. Sorteio

- [C] Monstros: `wotm_padsel::columnOpen` (só Raptros e Zorgulon dependem de destrave), `costumeOpen`, `randomPick`.
  O `Shell::RandomlySelectAI` do retail sorteia entre os grupos destravados.
- [C] Fases: `wotm_levelsel::unlocked` (UFO, Capitol, Mini Baytown e Volcano têm trava); `resolveRandom` já monta o pool do
  RANDOM do versus (tira Training, Tokyo DM, minigames, custom com `"random": false` e as travadas). Metro sem `.AIP`
  entra no 1 x 1 [I, pelo código: o retail só o tira com 2+ CPUs].
- Proposta (a confirmar na seção 9):
  - lutas 1-3 e 5-6: oponente destravado, sem repetir na corrida, traje do disco destravado; fase do pool do RANDOM,
    sem repetir;
  - luta 4: Goliath (tipo 13, traje 1) numa fase de {4 Metro, 11 Capitol, 1 Midtown Park, 2 Gamblers Gulch, 7 Baytown,
    8 Club Caldera};
  - luta 7: Cerebulon final (tipo 15) numa fase da mesma lista mais o 10 UFO;
  - o sorteio sai inteiro na entrada (semente guardada), o que dá para mostrar a "escada" das 7 lutas e repetir uma
    corrida num teste.
- O Capitol e o UFO nas lutas de chefe entram mesmo travados no versus? Proposta: sim (é o palco do chefe).
- [C] No modo 2, o Capitol é uma fase de versus comum: o `FinalBoss` não roda fora do modo 1.

## 4. Tokens

- [C] Na história, por fase vencida: `Shell+0x2B28 = TokenManager::grandTotal(P1) + diff × bonus[fase]`, com
  diff = 10/15/20 pela dificuldade (game+0x1203CC) e bonus = {200, 400, 600, 400, 400, 1500, 400, 400, 400, 500}; o
  `Shell+0x2B24` (battle tokens, a moeda da loja) recebe a soma. No final: `diff × 3000`, mais 10000 se a corrida não
  veio de um save.
- [C] No modo 2 nada vai ao `+0x2B24`; o jogo só credita no `TokenManager` da luta (destrutíveis 50, uso de pickup 10).
- [C] A vigia do `wotm_save.inc` (`poll`, a cada `sceGsSyncV`) grava no `save.json` qualquer mudança de `+0x2B24`. Basta o
  condutor somar; "sem save" vale só para o progresso da corrida.
- Proposta: luta n → `grandTotal(P1) + diff × bonusArcade[n]`; final → `diff × 3000` (sem os 10000? decisão do dono).
  `wotm_modes4::tokens(rdram, ctx, rt, 0)` já chama o `grandTotal`.
- Tela de placar: na história a contagem animada faz parte do "salvar?" (`McAskToSave::update`, cartão). Para o
  Arcade, um placar nosso na fonte do jogo (`GameText` de `wotm_trainview.inc`), com os sons de contagem do jogo
  (`updateTokenSound`, `playTokenCountFinishedSound`), entre a vitória e a carga. Pode ficar para a etapa 4.

## 5. Derrota

- [C] Na história: 3 vidas por fase (`NUM_LIVES`, repostas em toda carga) e, sem vidas, `displayDialog(1)`
  (`lostLevelOverlay`: CONTINUE recarrega a mesma fase, sem limite; o outro item volta ao menu).
- Proposta: 3 vidas por luta como na história (morte do jogador com vidas: `reinitSlot`/`playerInit` no lugar e aviso
  `Hud::addMessage` 0x25/0x26, como o co-op faz); sem vidas: CONTINUE (mesma luta, mesmo oponente, tokens da luta
  zerados) ou QUIT. [I] Conferir se o `lostLevelOverlay` serve no modo 2 ou se o diálogo tem que ser nosso.

## 6. Final

- [C] `Shell::GenesisMovie` (0x1AB4A0) grava `exitFromAdvStory = 1`, `needOutro = 1` e `nextMovie` pelo herói; o `main`
  toca a cadeia `_p` → `_g` → créditos com `m_inMenus = 0` e volta à tela 1. Terminar a história não destrava nada.
- O condutor, depois da luta 7: `m_inSession = 0`, `m_inMenus = 0`, chama o `GenesisMovie` (ou grava os três globais).
- [C] O `GenesisMovie` compara o `m_monsterSel[0]` inteiro; o hook do co-op já resolve o filme pelo tipo
  (`endingMovie(sel)`, `wotm_coop.inc:1716-1767`): reaproveitar.
- [C] Raptros e Zorgulon: o disco não tem `DRAGON_P`/`ALIEN_P`, só `DRAGON_G`/`ALIEN_G`; o retail mostra só os créditos.
  Proposta: tocar o `_G` deles antes dos créditos (decisão do dono).
- Custom como herói: o filme do tipo base (CJ → Ultra V); chefe custom → só os créditos.
- [I] Com `nextMovie = 0`, conferir se o `uiOutro` não fica esperando um filme que não começa.

## 7. Os chefes como CPU

### Cerebulon fase 3 (tipo 15, 0x1E0): quase pronto
- [C] É um `Monster` com a IA genérica (AiBrain); na fase 2 do `FinalBoss` o roteiro só faz `aiInit`, `drawMe` e
  `m_playerNum` 2. Fora da aventura o `wotm_custom::setAI` já devolve `m_playerNum` 2, corpo e sombra, e carrega
  `Final_s.bnk` (`gSoundBanks[24]`).
- Faltam:
  - **`.AIC`**: fora da aventura ele lê `rst/final<d>.aic`, que não existe (só `CAPITOL/FINAL0-2.AIC`): a IA roda sem os
    ajustes. Desviar a leitura no host para `capitol/final<d>.aic` (sem copiar arquivo do jogo para o repo).
  - **Barra de chefe**: `Hud::drawBossHealthBar` só desenha com `m_gameMode` 1; gancho que chama com o modo trocado
    durante a chamada, com a vida do CPU.
  - Retrato do HUD cinza (usar o do mod `mods/Cerebulon`).
  - Navegação: o `CAPITOL.AIP` tem só 2 nós; CPU grande pode encalhar no Capitol.

### Goliath Prime (tipo 13, 0x1A0): o trabalho grande
- [C] Por que hoje "não aparece como CPU": `TheGame::SetAIMonster` chama `disableMonster` em todo 0x1A0, e o
  `wotm_custom::setAI` só restaura jogador, Jelly e Final (`wotm_custom.inc:83-97`, de propósito).
- [C] Só religar não basta: o 0x1A0 tem IA própria. `Ai::updateInputs` desvia para `Ai::updateAssBoss` sem AiBrain;
  o `PatternZero` prende o Goliath em (952, -26, 292) (o bunker de canyon2) e lê as bombas da fase; os `PatternOne/Two`
  usam `retreatPts` (coordenadas de canyon2) e a máquina `setAssBossState` (15 estados, mira `m_monsters[0]`).
- Duas formas:
  - **G1, IA genérica**: fora de canyon2/modo 1, `updateAssBoss` faz o ramo do AiBrain (`AiBrain::update`, já previsto
    em `18bb1d2:docs/investigacoes/2026-10-23/goliath_codigo.md`, seção 6). Barato, mas ele luta como um monstro comum
    com animações faltando no `ASSBOSS1` (escalar, dash, contra-ataque, 61-64...): os estados sem animação não começam
    e a IA pode insistir.
  - **G2, a luta de chefe**: pular o `PatternZero` (bunker e bombas) e rodar os padrões do chão (`One`: agarrão e
    investida dos propulsores; `Two`: giro em "T" e mísseis), com `retreatPts` trocados durante a chamada por pontos da
    fase sorteada (pontos de início, `PathNet` do `.AIP`). Mais fiel; Metro sem `.AIP` e o Capitol de 2 nós pedem
    pontos à mão. Sem os pilares de canyon2 a investida não tem o que bater.
  - Recomendação: G1 primeiro (para ele aparecer e lutar), G2 depois.
- Também faltam: `.AIC` (`rst/assboss0.aic` só existe para a dificuldade 0; desviar para `canyon2/assboss<d>.aic`),
  barra de chefe, retrato (o `ASSBOSS1.RTX` t6 tem a cara do Congar; o mod `mods/Goliath Prime` tem o PNG), global
  `assBoss` (0x6F7970) apontando para o CPU e limpo entre lutas, banco `Assemb_s.bnk` (`BANK_MECH`).
- [C] Dano: no 0x1A0 o `Monster::takeHit` só aceita golpe de `m_monsters[0]` (ou `attackState` 4/10): no 1 x 1 basta.
- Riscos: tamanho (~2,5x, altura 250; a investida atravessa prédios), sem fúria e sem pegar vida/stamina.

### Entre lutas
- [I] Os bancos `BANK_MECH` (luta 4) e `BANK_FINAL` (luta 7) entram por gancho; conferir que a carga da luta seguinte
  os solta.

## 8. Etapas propostas

1. **Esqueleto**: linha ARCADE, `wotm_arcade.inc`, sorteio, 7 lutas comuns encadeadas (chefes ainda como monstros
   comuns no lugar), vitória → próxima, derrota → vidas/continue, QUIT, tokens no `+0x2B24`, final com o filme. Variável
   de teste para partir direto da carga (por exemplo `WOTM_ARCADE=<semente>[,<luta inicial>]`, na tabela do
   `NATIVE.md`), para pular o menu e testar a luta 7 sem jogar as 6.
2. **Cerebulon na luta 7**: `.AIC`, barra de chefe, retrato, UFO no pool, banco de som.
3. **Goliath na luta 4**: restaurar no `setAI`, G1, `.AIC`, retrato, barra; depois G2.
4. **Acabamento**: "LUTA N DE 7" e a escada das lutas na carga (por cima do fade de `DisplayLoadBackground`,
   `wotmCaptureFadeStep`), placar de tokens com a contagem, `_G` do Raptros e do Zorgulon.
5. Doc de referência no `NATIVE.md` ("Arcade"), em inglês.

## 9. Decisões do dono

1. O jogador escolhe o monstro (com RANDOM na seleção) ou o dele também é sorteado?
2. Uma luta: um KO decide, ou 3 vidas como na história? Sem vidas: CONTINUE infinito, CONTINUE limitado, ou acabou?
3. Oponentes: só monstros do disco, ou também customs com `"ai": true` e skins de mod? Pode repetir oponente/fase na
   corrida? Pode o oponente ser o mesmo monstro do jogador (outro traje)?
4. Fases das lutas comuns: o pool do RANDOM do versus (destravadas, com as custom) ou só as 9 do disco sem trava?
5. Dificuldade: a das opções do jogo nas 7 lutas, ou subindo ao longo da corrida?
6. Goliath: G1 (IA comum) basta para começar, ou só aceita com os padrões de chefe (G2)?
7. Tokens: mesmas tabelas da história? O bônus de 10000 do final entra?
8. Raptros e Zorgulon no final: `_G` + créditos, ou só os créditos como no retail?
