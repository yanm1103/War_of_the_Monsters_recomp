# Campanha com até 4 jogadores: cinemas, câmeras, vistas e HUD

Frente 2 do item 13 do `TODO.md`. Só leitura de `src/`, `asm/` e `native/runtime/wotm_split4.inc`; nada foi rodado. Endereços de
`native/wotm_syms.h`.

## 1. Como a aventura (modo 1) monta a tela hoje

- **Vistas:** o `TheGame::gameReestablishViews` (0x139530) só divide nos modos 3, 6 e 7 (e 8/9/11 têm casos próprios). Nos modos 1
  (aventura), 2, 4, 5 (o coop de 2 jogadores, cujo `Shell::EvaluateTwoPlayerCoopStatus` 0x1A8230 é vazio) e 10: uma vista cheia.
- **`m_numMonsters` = 1** na aventura. Vários caminhos escolhem o comportamento por ele, não pelo modo:
  - `doCinema`/`endCinema` só chamam o `viewToggleSplitScreen` com `m_numMonsters >= 2`;
  - `SetAIMonster` (0x137B10) põe as IAs na câmera 1 com `< 2` e na câmera 3 com `>= 2`;
  - `gameCheckForCloseCombat` (0x138840) com 2 monstros junta `players[0]` e `players[1]`; senão, o `players[0]` com cada IA;
  - `InitCCCamera` (0x11C2B0) e `LeaveUnifiedView` (0x11C868): com 2 faz fade; senão a câmera 2 sai da posição da 0 (e volta para ela);
  - `rtMain` (0x188318): vista unificada com `m_numMonsters == 2` roda os dois HUDs e as duas miras na vista 0.
- **Vista unificada na aventura:** é a câmera de combate próximo. A câmera 2 enquadra o jogador 1 e a IA mais próxima (POV 1) e o
  `viewGrappleConfig` (0x229838) põe a vista 0 no `viewDef[5]` com o `_cs` da câmera 2. Também é usada na morte (`StateDeath::transitionInto`
  0x1529D8: câmera 2 no monstro que morre) e na vitória (`StateVictory::transitionInto` 0x159BB0: câmera 2 no POV 7).

## 2. Cinemas dentro da fase

- **Início:** um `ActCinema` (tipo 9) dentro de um roteiro de ações da fase. Os roteiros vêm do banco da fase (intro) ou de
  `ActionDispatch::callScriptFromId` (0x1001C8) chamado pelos chefes: Planta 0x2264 (fim), Final 0x226A (intro), 0x226B (fim do Mecha),
  0x226D (final); vegas, tokyo e ufo têm o deles (`*Update`).
- **`doCinema` (0x100940):** `moviePlaying = 1`, letterbox (`Hud::m_letterbox`, um só), `startCinema` de todo slot, `m_playerMask ^= 7`
  (todos param: o `TheGame::Update` 0x1372B8 passa a chamar o `updateCinema`), power-ups/pedestres/pickups escondidos,
  `hierSetLosPointsPerView(0, 0)`, colisão da câmera 0 desligada (`+0xD90`), toggle se `m_numMonsters >= 2`, `shell->m_inCinema = 1`, música.
- **Câmera do cinema:** não é do `Cameras`. Um nó de câmera animado na cena chama o `hierSetCamera` (0x204F50) durante a travessia
  da vista em curso; o `rtMain` aplica o resultado depois do `gameUpdate`, com `hierSetCamera(0, 0, 0)`, **só na vista 0**. Com mais
  de uma vista as outras seguem as câmeras dos monstros.
- **Monstros no cinema:** `ActAnim` com `ACTANIM_PLAYER` anima `m_monsters[obj]` (`obj < m_numMonsters`), `ACTANIM_AI` anima
  `m_lists.ais[obj]`; `doSetPlayerPos` põe `m_monsters[idx]` ou `ais[idx]` num ponto. O `Monster::updateCinema` (0x15BA10) segue o
  `AnimPappy` quando há canal e, senão, fica parado. Os jogadores que o roteiro não cita (2 a 4) ficam no estado de cinema, parados
  onde nasceram, **visíveis na cena**. Não se sabe ainda quais `obj` os roteiros da aventura usam: precisa de uma listagem dos
  `ActCinema`/`ActAnim`/`ActPlayerPos` por fase (ferramenta sobre o banco da fase, sem rodar o jogo).
- **Pular:** o `updateCinema` lê `inputGetInput(8, 0)` e `(8, 1)`: só os controles 0 e 1 (jogadores 1 e 2) pulam.
- **`cleanUpForMovie` (0x162E90)** (desliga camuflagem, fogo, efeitos presos) é chamado só para `m_monsters[0]` /
  `m_lists.players[0]` por Planta, Final, vegas, tokyo e ufo: os outros jogadores entrariam no cinema com fogo ou camuflagem.
- **`endCinema` (0x100B20):** desfaz tudo, toggle se `m_numMonsters >= 2`, e só as câmeras 0 e 1 recebem `+0xD98`/`+0xD90` = 1
  (posição refeita e colisão de volta). As câmeras 2 e 3 não são tocadas.
- **Armadilha do toggle:** o `viewToggleSplitScreen` (0x229788) alterna (1 vira 2, 2 vira 1; com 3 ou mais vistas não faz nada).
  Se a aventura tiver `m_numMonsters` 2 sem a tela dividida montada, o cinema **divide** a tela e o fim dele volta à cheia: os dois
  (número de monstros e vistas) têm que mudar juntos.

### O que o `wotm_split4.inc` já resolve
- `cinemaViews` (dentro do gancho do `SetOkToUnify`, começo do quadro): com `moviePlaying` e sem vista unificada, uma vista cheia
  (`viewNumViews` 1, `viewDef[0]`); sem cinema, os quadrantes refeitos (inclusive depois do toggle do `endCinema`). O
  `viewGrappleConfig` reaplica a mesma coisa. Isso basta para a câmera do cinema na aventura com 3 ou 4.
- Tudo isso só vale com `splitPlayers()` != 0, que exige modo 3, 6 ou 7. **Para a aventura, a primeira mudança é aceitar o modo 1**
  no `splitPlayers` e no gancho do `gameReestablishViews` (que no modo 1 não cria divisão nenhuma: o gancho teria de montar os
  quadrantes ou as metades ele mesmo).
- Com 2 jogadores na aventura o caminho do retail (toggle) funciona se as metades forem criadas como no modo 3 e `m_numMonsters` = 2.

## 3. Câmeras especiais da aventura

| Função | End. | O que assume | O que mudar | Risco |
|---|---|---|---|---|
| `Cameras::InitPlantBossCamera` | 0x11CEC8 | sempre a câmera 0 (POV 3 no marcador do volume onde ela está). Chamada pelo `PlantBossHead::update` (0x184560) quando uma cabeça morde o monstro `m` | usar a câmera `m->m_cameraView` (gancho: trocar a câmera 0 pela do monstro durante a chamada) | médio |
| `PlantBoss::update` / `PlantBossHead::update` | 0x183560 / 0x184560 | `SetCameraPOV(0, 5 ou 0)` ao soltar ou na morte do `players[0]`; morte do jogador = `players[0]->m_dead`; vibração no controle 0 | câmera e vibração do monstro mordido; "jogador morto" vira "todos mortos" | médio |
| `FinalBoss::updateMecha` | 0x1320D8 | o Mecha mira `m_monsters[0]` (`m_monster7C`, travado no estado 1); `cleanUpForMovie` só nele | escolher alvo (mais próximo / rodízio) | baixo para a câmera (não mexe em câmera), alto para o equilíbrio |
| `FinalBoss::update` / `updateJelly` | 0x131CC0 / 0x133538 | fase 2: o último monstro (`m_monster78`) vira IA contra o jogador 1; debug pelo byte 0xFD do `m_monsters[0]` | sem mudança de câmera | baixo |
| `vegasUpdate`, `tokyoUpdate`, `ufoUpdate` | 0x1DD7B0, 0x1D6B88, 0x1D74F8 | leem `m_monsters[0]` (cleanUpForMovie, maré do tokyo, fim) | `cleanUpForMovie` em todos os jogadores | baixo |
| `AiScript3Mile::update` | 0x10F030 | IA do chefe da fase 6 usa `getClosestMonster` e `ais[0]` | nada de câmera | baixo |
| `doCamVolume` | 0x16DF88 | usa a câmera `m_cameraView` do monstro | nada | — |
| `Cameras::Update` / `Camera::checkCollision` | — / 0x120720 | colisão só para câmeras de humanos (`m_playerNum == 1`); pontos de visada da unificada com `m_numMonsters == 2` tratam os dois humanos | nada, se a unificada ficar desligada | baixo |
| `StateDeath::transitionInto`, `StateVictory::transitionInto` | 0x1529D8, 0x159BB0 | morte e vitória ligam a unificada (câmera 2, tela cheia) | como no split4: tela cheia na câmera 2 e volta aos quadrantes; a câmera 2 é a do jogador 3 | médio (o split4 já trata no modo 3) |
| `gameCheckForCloseCombat` + `InitCCCamera` | 0x138840, 0x11C2B0 | combate próximo = jogador 1 contra IA (com `m_numMonsters` 2: jogador 1 contra jogador 2, que na aventura são aliados) | com 2+ jogadores, desligar a unificada (o split4 zera `m_camUnify` a cada quadro) | médio: muda a sensação do 1 jogador; com 2 jogadores aliados viraria câmera de briga entre eles |
| Câmera da torre do Mecha custom | `wotm_custom.inc` | já usa a câmera do `m_cameraView` do dono | nada | — |

## 4. HUD da aventura

- **Um HUD por vista** (`m_huds[0..3]`, o split4 faz o 2 e o 3 copiarem o da coluna). Mas o conteúdo próprio da aventura é de um só:
  - **Caixas de dica** (`Hud::addTextBoxMessage` 0x145F78 / `updateTextBoxMessage` 0x146030): sempre `m_huds[0]`, disparadas por
    qualquer humano no Central (tutorial: agarrão, energia, power-ups) e pelo canyon com a configuração de controle do
    `m_monsters[0]`. O estado mora em estáticas (`m_textBoxVertMax`, `textBoxOffset`, `increment`, `D_00709C50..80`) e a caixa é
    medida por `viewGetCurView`/`viewGetWH`: sai no quadrante 0. As dicas do `Hud::update` (0x143698, `curMessage`, `hintCount`,
    `message.2673..2676`) também são estáticas: com o update rodando em 4 HUDs, contariam 4 vezes por quadro e repetiriam a caixa.
  - **Mensagens das fases** (`Hud::initAfter` 0x143188 carrega as listas por fase: central, capitol, canyon2, island, threemile).
  - **Vidas** (`EvaluateOnePlayerStoryStatus` 0x1A7C08): "2/1 vidas" no `m_huds[0]`; `m_lives[0]` é a única conta de vidas.
  - **Mensagens por jogador** que já usam `m_huds[m_cameraView]` (power-up, especial, vulcão do island): funcionam com N.
- **Barra do chefe:** `Hud::addHealthIndicators` (0x144948) de cada vista chama `drawBossHealthBar` (0x1452A0) no modo 1 (chefe da
  fase, as 3 cabeças da fase 6, o Final). A barra (`_cs` em +0xFC do HUD) é posta **na frente da câmera 0** (ou da 2 com a
  unificada), não da câmera do HUD: nas vistas 1 a 3 ela ficaria fora do lugar. O gancho do `Hud::update` do split4 troca o `_cs`
  da câmera só durante o update, não durante o `addHealthIndicators`. Correção: gancho no `drawBossHealthBar` que troca o `_cs` da
  câmera 0 pelo da vista em curso durante a chamada (mesmo truque), ou barra do chefe desenhada pelo nativo uma vez, em cima de tudo.
- **Letterbox:** um `Letterboxing` só, alinhado à câmera 0 (ou 2) no `update` (0x147008). Durante o cinema a tela é cheia: certo.
  Um `ActCinema` com o bit 1 deixa o letterbox ligado depois do fim; aí, em quadrantes, ele apareceria só na vista 0 e no tamanho
  da tela cheia. Verificar se algum roteiro da aventura usa o bit 1.

## 5. Filmes (FMV)

- Na aventura não há FMV entre fases: entre uma fase e outra vêm o diálogo de placar (`displayDialog(2)`) e a tela de carga. As
  apresentações e os finais de chefe são cinemas dentro da fase (seção 2).
- O único FMV da campanha é o final: `Shell::GenesisMovie` (0x1AB4A0) escolhe o filme pelo monstro do jogador 1 (`m_monsterSel[0]`),
  grava `genesisMov` (o desbloqueio daquele herói) e o `uiMain` (0x1D93D8) o toca em tela cheia, fora do `rtMain`; só o controle 0
  pula. Nada de vista ali; a questão é de regra (de quem é o final e o desbloqueio).

## 6. Opções de desenho

1. **Cinema em tela cheia e volta à divisão** (o que o split4 faz no modo 3). Com 2 jogadores: metades do retail + `m_numMonsters` 2
   (o toggle do retail já faz). Com 3 e 4: `splitPlayers` aceita o modo 1. Falta: esconder ou reposicionar os jogadores que o roteiro
   não cita (drawMe 0 no `doCinema` e, no `endCinema`, pô-los perto do jogador 1 ou nos pontos de largada), `cleanUpForMovie` em
   todos, pular com qualquer controle, câmeras 2 e 3 refeitas no fim (`+0xD98`/`+0xD90`, como a 0 e a 1).
   Custo baixo; reaproveita quase tudo.
2. **Tela única compartilhada** (estilo câmera unificada, N monstros num quadro só): uma câmera nova que enquadra todos (como o
   Crush, POV 9, que enquadra o par mais distante). Cinemas, letterbox, barra do chefe, caixas de dica e o `drawBossHealthBar` ficam
   como no 1 jogador, sem nenhuma troca. Perde-se a liberdade de cada um ir para um lado da fase (fases grandes; o "caminho" do
   chefe da Planta e do Final pressupõe um alvo). Precisa prender os jogadores a um raio (ou teleportar quem fica para trás). Custo
   médio: câmera nova e regra de raio; HUD dos 4 jogadores na mesma tela (4 barras em cantos).
3. **Híbrido:** tela única enquanto todos estão perto (a unificada do retail generalizada para N) e divisão quando se afastam. É
   o mais bonito e o mais caro: transições, HUD que muda de forma, `viewGrappleConfig` só conhece a vista 0.

Para qualquer opção, o que é de 1 jogador e precisa de regra nova: barra do chefe por vista, caixas de dica (só na vista do jogador
que disparou, `m_huds[m_cameraView]`, ou numa faixa central), vidas e "continue" (`m_lives[0]`), alvo do Mecha e da Planta,
`InitPlantBossCamera` na câmera do monstro mordido, e o filme final.

## Perguntas para o dono

1. Tela dividida (opção 1), tela única com todos no quadro (opção 2) ou híbrida (3)? Com 2 jogadores, metades lado a lado como o
   2 jogadores do retail?
2. Nos cinemas, os jogadores extras somem (só o jogador 1 aparece, como no original) ou devem aparecer ao lado dele?
3. Combate próximo (a câmera que junta o jogador e a IA): desligar com 2 ou mais jogadores, como no free-for-all de 3 e 4?
4. Caixas de dica do tutorial (Central): na vista de quem as disparou, em todas, ou só com 1 jogador?
5. Barra do chefe: uma por vista (cada um vê a do chefe na sua vista) ou uma só, centralizada sobre a tela inteira?
6. Final da campanha: o filme e o desbloqueio do herói do jogador 1, ou de cada jogador (vários filmes em sequência)?
7. Pular cinema: qualquer jogador pode pular, ou só o jogador 1?
