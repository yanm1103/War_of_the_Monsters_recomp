# Mod do exército: ganchos de caos, modos de jogo, início e fim da luta, rede de caminhos (2026-11-04)

Continuação de `docs/investigacoes/2026-10-25/militares_spawn.md` (plano e decisões do dono). Só investigação: nada de código mudou.
Endereços são os de `native/wotm_syms.h` (`wotm::fn::<nome>` para funções, `wotm::sym::<nome>` para dados); offsets do `TheGame`
são relativos ao objeto (`wotm::sym::shellGame` = 0x2B35A0; o ponteiro `game` fica em `wotm::sym::game`).

## 1. Caos: o que enganchar

Regra geral para os três: **decidir na entrada da função**, não depois da original. O `NATIVE.md` avisa que a retomada de uma
função interrompida entra no meio do código recompilado sem passar pelo embrulho; um "depois" pode não rodar. Os três eventos abaixo
dão para saber na entrada.

### 1a. Destrutível (prédio, objeto pequeno)

| Função | `wotm::fn::` | Quando roda |
|---|---|---|
| `Destructible::ChangeState(int state, bool rebuild, bool reset)` | `ChangeState__12DestructibleibT2` (0x130B90) | uma vez por mudança de estágio |
| `Destructible::GenericTakeHit(dir, dmg, attackerId, unused)` | `GenericTakeHit__12DestructibleP8_fvectorfii` (0x130AD0) | a cada golpe que tira vida (chama o `ChangeState`) |

- Registradores na entrada do `ChangeState`: `a0` = `Destructible*`, `a1` = estado pedido, `a2` = rebuild, `a3` = reset.
- **É evento** (a mesma condição do retail para rodar as ações, `shedMonsters` e os 50 tokens) quando
  `a1 > whichChild` **e** `a1 > m_newState`, com `whichChild` = u32 em `m_istate + 4` (`m_istate` em `+0x28`) e `m_newState` em `+0x90`.
  Fora disso (golpe sem mudar de estágio, `rebuildCanyon2Pillars`/`rebuildCapitolPillars` voltando ao 0) não conta. Cada golpe
  avança no máximo um estágio, então um prédio de 3 estágios gera 2 eventos.
- Chamadores: `GenericTakeHit` (golpe), os dois `rebuild*Pillars` (descida, não conta) e `doDomino` do `ActionDispatch` (destruição
  em cadeia, `ChangeState(estado, false, true)`; conta).
- Campos para classificar (`Destructible`, 0xA0 bytes, tabela `Destructibles` em `wotm::sym::shellDestructibles`, até 1500):
  `m_maxHealth` `+0x18`, `m_hier` `+0x24` (nó `DESTRUCTIBLE`: `hitPointClass` u8 `+4`, `numKids` u16 `+0xA`, id1 = u32 `+0` >> 18),
  `m_hitType` `+0x30` (= `hitPointClass`), `m_pos` `+0x40` (x, y, z; Z para cima).
- **Prédio x objeto pequeno** (contagem nos 19 `.NGP`):

| `hitPointClass` | vida | estados (`numKids`) | quantos | leitura |
|---|---|---|---|---|
| 1 | 10, dano mínimo 0 | 2 (1047), 3 (4) | 1051 | objeto pequeno (poste, placa...) |
| 0 | do nó (`hitPoints`: 311 entre 51 e 200, 40 acima de 200) | 2 (133), 3 (205), 4+ (17) | 355 | prédios e estruturas |
| 2, 3, 4 | 50, 100, 150 | 2 ou 3 | 109, 28, 39 | intermediários |

  Proposta: `classe == 1` = objeto pequeno (peso baixo); outra classe = estrutura; `a1 == numKids - 1` = **caiu** (último estágio,
  peso alto); estágio intermediário = dano (peso médio).
- **Atacante:** o retail guarda o último em `s_lastAttacker` (`D_006F8BE4`, sem nome em `wotm_syms.h`; o `GenericTakeHit` grava o
  `attackerId` ali antes do `ChangeState`). Sem endereço cru: gravar o `a2` na entrada do `GenericTakeHit` e usar no `ChangeState` que
  vem em seguida (no `doDomino` vale o último atacante, como no retail). O id é índice de `Interactives`
  (`wotm::sym::x_12Interactives_s_dbInteractive`, ponteiros; contagem em `x_12Interactives_s_numInteractives`); é monstro se o
  `DbInteractive` tem `+0` == 1. Os tokens do retail ("Destructibles", 50) só vão a monstro com `m_playerNum == 1` (jogador humano),
  por isso não servem de contador para luta com CPU.

### 1b. Carro e outros veículos civis

| Função | `wotm::fn::` | Quando roda |
|---|---|---|
| `LevelPickups::killPickup(Iterator, int regen)` | `killPickup__12LevelPickupsGQ2t10LinkedList1ZP6Pickup8Iteratori` (0x182D20) | uma vez por objeto destruído |

- Entrada: `a0` = nó da lista (`ListNode*`; o `Pickup*` é o u32 em `a0 + 0`), `a1` = regen. Se `Pickup + 0xC` (`cs`) for 0 a função
  sai sem fazer nada (já morto): só contar com `cs != 0`. Tipo em `Pickup + 0xA0` (`pickupType` = id1 do nó − 0x1388), posição em
  `cs + 0x10`, quem segura em `Pickup + 0xD8`.
- Por que esta e não `CarPickup::kill` (0x1226B0): carro parado é `Pickup` simples (não passa pelo `CarPickup::kill`), e os
  `kill` de helicóptero, metrô, tanque etc. são virtuais diferentes. Todos chegam pelo `killPickup` (que chama o `kill` virtual).
  Chamadores: `LevelPickups::update` (vida acabou), `Monster` (4 lugares), `MonsterStates`, `SpecialStates`, `Weapon`, `FinalBoss`
  e `VehicleNavigator::unalignedAvoid` (veículo que bate em outro objeto o destrói).
- Tipos de veículo (pelo `createPickup` e pelo `Pickup::kill`; contagem por fase no fim):

| Tipo | Id do nó | O que é | Observação |
|---|---|---|---|
| 2, 15, 26 | 0x138A, 0x1397, 0x13A2 | carro | `CarPickup` se o bit 0 do id2 estiver ligado (anda), senão parado |
| 3 | 0x138B | caminhão-tanque | explode com onda de choque e fogo |
| 11 | 0x1393 | helicóptero civil (`HeliPickup` não militar) | só CENTRAL (4); criado em qualquer modo |
| 27 | 0x13A3 | metrô (`SubwayPickup`) | só METRO (6) |
| 9, 10, 12, 23, 24, 25 | 0x1391, 0x1392, 0x1394, 0x139F, 0x13A0, 0x13A1 | caminhão de míssil, tanque, helicóptero militar (2), torre, disco voador | **excluir da conta de caos** (são o próprio exército); o mesmo gancho detecta o veículo do mod destruído (reciclagem) |

  Os demais tipos (1, 4, 7, 13, 14, 21...) não são veículo; o significado de cada um não foi conferido. Barco não existe como
  pickup (nenhum tipo novo em ISLAND); se houver, é destrutível.
- Carro destruído é regenerável (`bits & 1`): vai à lista de regeneração e volta 0xA8C quadros depois (`Pickup::regen`), então pode
  contar de novo. O exército (sem o bit) vai à `deadList`.
- **Atacante:** não há campo direto. Opção: gancho no `Interactives::dispatchHit(dir, dmg, attackerId, targetId)`
  (`dispatchHit__12InteractivesP8_fvectorfii`, 0x147240) guardando o último atacante por alvo (o id do objeto está em bits 7..17 do
  primeiro word do `cs->epNode`). Não cobre carro pisado ou destruído por veículo; para o mod é opcional.

### 1c. Pedestre

Pedestres são grupos de partículas (`PedGroup`, nó 0xBB8 na fase; `common/peds`), não interativos.

| Função | `wotm::fn::` | Quando roda |
|---|---|---|
| `pedsTurnOff(_fvector *pos, bool splat)` | `pedsTurnOff__FP8_fvectorb` (0x21B9F0) | **uma vez por pedestre morto** (só o `squish` chama, com `splat` 1) |
| `PedGroup::squish(_fvector &pos, float raio, Monster *m)` | `squish__8PedGroupR8_fvectorfP7Monster` (0x136270) | uma vez por grupo e por esmagamento; devolve quantos morreram |

- `squish`: `a0` = `PedGroup*`, `a1` = posição, `f12` = raio, `a2` = `Monster*` (pode ser 0). Chamado pelo `PedGroup::handleEnv`
  (pés dos monstros, a cada quadro, mas só mata quem está no raio) e pelo `squishGlobal` (`ShockWave`, `SpecialStates`, `Weapon`).
- Contagem sem "depois": gravar o `a2` na entrada do `squish` e somar 1 em cada entrada do `pedsTurnOff`. O retail credita
  5 tokens "Peds" por pedestre, de novo só para `m_playerNum == 1`.
- Grupos ativos em `wotm::sym::x_8PedGroup_s_allPedGroups` (20 ponteiros) e `x_8PedGroup_s_numPedGroups`. Pedestres por fase
  (soma dos grupos): CENTRAL 1420, MINISF 1365, METRO 1000, TOKYO 865, VEGAS 700, ISLAND 550, TOKYODM 510, AIRPORT 300, CRUSH 200,
  3-MILE 60; as outras fases não têm.

## 2. Modos de jogo

`TheGame::m_gameMode` (`+0x1203C8`) é cópia de `Shell::m_mode` (`shellInfo + 0x2C10`), feita por `Shell::InitRTState` antes de cada
sessão. Fase em `TheGame::m_levelId` (`+0x1203D0`, índice do `GLOSSARY.md`).

| Modo | Nome | De onde vem | Exército? |
|---|---|---|---|
| 0 | demo (fase e monstro sorteados) | atração do menu | não |
| 1 | aventura (também o co-op do port, `WOTM_PLAYERS` + `WOTM_MODE=1`) | 1 jogador, ADVENTURE | não (decisão do dono) |
| 2 | free-for-all de 1 jogador contra CPU | 1 jogador, FREE-FOR-ALL; `WOTM_MATCH`; TRAINING do port | sim (exceto treino) |
| 3 | free-for-all multijogador (com ou sem CPU) | multiplayer, FREE-FOR-ALL; `WOTM_PLAYERS=2` | sim |
| 4 | endurance (5 CPUs em sequência, sem alvo de vitórias) | 1 jogador, ENDURANCE | configurável |
| 5 | co-op de 2 do retail (`EvaluateTwoPlayerCoopStatus`) | nenhum caminho de menu encontrado | não |
| 6 | eliminação | multiplayer, ELIMINATION | sim |
| 7 | dodge ball (fase 25, DBALL) | minigames | não |
| 8 | big shot (fase 26, BIGSHOT) | minigames | não |
| 9 | crush (fase 27, CRUSH) | minigames | não |
| 10 | sem nome (só o `gameGetStartPoint` o trata) | nenhum menu | não |
| 11 | online (nunca roda; o `wotm_modes4.inc` usa a função dele de trampolim) | nenhum | não |
| 0x3F, 0x40 | shell (menus) | — | não |

- Treino: `wotm_training::g_on` (`native/wotm_training.h`, atômico). A linha TRAINING escolhe o free-for-all (modo 2) e o
  `rtMain(first)` liga o `g_on` (`wotm_overrides.cpp`, `rtMainTapped`); o menu principal desliga. Excluir quando `g_on`.
- Minigames: modo 7, 8 ou 9 (ou fase 25, 26, 27). A fase TRAINING (12) do disco não tem `.FOP`, pickups nem pedestres.
- Os modos 2 e 3 também mudam os scripts de fase: o `dbProcInteractive` só monta o script 0x2134 no modo 2, 0x2135 no 3 e 0x2136
  no 1 (são as cenas de abertura).

## 3. Início e fim da luta, carga da fase

### Sequência da sessão (`main`, `src/game/Shell.cpp`)
`TheGame::InitBeforeDbLoad` → `Shell::LoadLevelFiles` → `FinishLoadBar` → `FadeScreen` → `TheGame::InitAfterDbLoad` →
`Shell::InitPlayers` → ... → laço `rtMain(first)` / `Shell::EvaluateGameStatus(r)` / (`TheGame::ResetLevel` ou `UnpauseLevel`).

| Momento | Gancho | `wotm::fn::` / dado |
|---|---|---|
| Carga começa (zerar o estado do mod) | entrada de `TheGame::InitBeforeDbLoad` | `InitBeforeDbLoad__7TheGame` (0x136560); zera `+0x120468`, põe `m_playerMask` = 7 e lê o `.FOP` (a `PathNet::fodder` fica pronta aqui) |
| Pickups criados | `LevelPickups::createPickup` chamado de `dbProcInteractive` dentro do `dbsTraverse` do `LoadLevelFiles` | `createPickup__12LevelPickupsP9_hierheadR8_fvectorPA3_fb11ePickupType` (0x181750); `$ra` = 0x12E818 quando vem da carga |
| Pool pronto | entrada de `Shell::InitPlayers` (logo depois do `InitAfterDbLoad`, que liga `+0x120468`, roda `LevelPickups::initAfterDbLoad` e dá a cada pickup o seu slot em `Interactives`) | `InitPlayers__5Shell` (0x1A6D60); alternativa: `rtMain(first)` |
| Luta começa | entrada de `rtMain` com `a0` = 1 | `rtMain__Fb` (0x188318); o port já engancha (`rtMainTapped` em `wotm_overrides.cpp`) |
| Quadro de luta | entrada de `TheGame::Update` (não roda na pausa: o `rtMain` sai e o diálogo de pausa roda fora dele) | `Update__7TheGame` (0x1372B8) |
| Rodada acabando | `gRtReturn.active` != 0 (fade de saída) | `wotm::sym::gRtReturn` (+0 active, +4 delay, +8 code); escrito por `rtReturnToShell__Fii` |
| Rodada acabou | entrada de `Shell::EvaluateGameStatus` (`a1` = código; 2 = pausa) | `EvaluateGameStatus__5Shelli` (0x1A7960) |
| Reinício sem recarga (RESTART, RESET POSITIONS) | entrada de `TheGame::ResetLevel`, depois `rtMain(0)` | `ResetLevel__7TheGame` (0x139200) |

- **Luta valendo** (contar o timer, soltar leva), avaliado na entrada do `TheGame::Update`:
  `moviePlaying == 0` (`wotm::sym::moviePlaying`), `shell->m_inCinema == 0` (`shellInfo + 0x2BA4`),
  `(TheGame::m_playerMask & 7) == 7` (`+0x120444`; o `doCinema` o zera, o `endCinema` volta a 7; com o bit 4 desligado nem o
  `LevelPickups::update` roda), `gRtReturn.active == 0`, sem fade (`+0x120454` e `+0x120458` em 0, os mesmos que o treino e o co-op
  testam) e `Shell::m_inSession` (`+0x2BCC`) ligado. A apresentação dos monstros no começo é uma `ActCinema`, coberta pelos três
  primeiros testes.
- Não há relógio de luta no versus. Timer do mod: somar `1 / timerGetUpdateRate()` (o valor está em `D_006F8DB8`; o port impõe um
  piso, `WOTM_RATEMIN`) ou o relógio do host com teto por quadro, só quando a luta vale. O `g_frame` não serve: o `rtMain` o zera
  também na volta da pausa.
- `createPickup` em plena luta: o `Debris` também chama (pedaços de prédio viram pickup; `$ra` 0x12F768, 0x12FD74, 0x12FE54) e aí
  `+0x120468` vale 1 (o `createPickup` já chama o `initAfterDbLoad` do novo). Fingir o modo 1 só com `+0x120468 == 0` ou `$ra` no
  `dbProcInteractive` (0x12E5F8..0x12EA38).
- No versus o `vegasUpdate` não roda (só no modo 1): os militares criados pelo gancho ficam ativos e visíveis depois da carga. O mod
  tem que escondê-los (como o `turnOffMilitary`, `turnOffMilitary__12LevelPickups` 0x181008) no "pool pronto".

## 4. Rede de caminhos e posição dos monstros

### `PathNet::fodder` (`wotm::sym::x_7PathNet_fodder`, 0x7CA4D0, 0x3010 bytes)
- `+0x00` u16 `numNodes` (até 256); nós a partir de `+0x10`, **stride 0x30** (não 0x40: o `PathNode` de 0x40 é o do `AiPathNet`;
  0x10 + 256 × 0x30 = 0x3010).
- Nó `i` em `fodder + 0x10 + 0x30 * i`: `pos` x, y, z (`+0x0`, `+0x4`, `+0x8`, floats, Z para cima), `type` u8 `+0x10`,
  `numPaths` u8 `+0x11`, ligações a partir de `+0x12`, 4 bytes cada (`id` u16 do nó vizinho, largura u8 (× 10 = raio da faixa),
  outro u8), até 6.
- **O tipo do nó decide quem usa:** `VehicleNavigator::init` procura nós do tipo **3** para caminhão de míssil e tanque (id1 0x1391 e
  0x1392) e do tipo 0 para os carros; `HeliVehicleNav::init` usa o tipo **1**; o disco voador usa 1 ou 2. Sem nó do tipo pedido, o
  `getClosestNode` devolve o nó 0 e o veículo segue as ligações dele (não trava, mas anda pela rede errada).

| `.FOP` | nós | tipo 0 | tipo 1 (heli) | tipo 2 | tipo 3 (tanque/caminhão) |
|---|---|---|---|---|---|
| VEGAS | 64 | 12 | 24 | 20 | **8** |
| CENTRAL | 60 | 15 | 18 | 27 | 0 |
| METRO | 41 | 11 | 13 | 17 | 0 |
| ISLAND | 150 | 31 | 56 | 63 | 0 |
| AIRPORT | 46 | 2 | 14 | 30 | 0 |
| TOKYO, TOKYODM | 63 | 16 | 10 | 37 | 0 |
| UFO | 97 | 0 | 13 | 84 | 0 |
| 3-MILE | 138 | 118 | 0 | 20 | 0 |
| SANFRAN | 111 | 12 | 0 | 99 | 0 |
| MINISF | 35 | 6 | 0 | 29 | 0 |
| CANYON2 | 20 | 18 | 0 | 2 | 0 |
| CAPITOL | 14 | 12 | 0 | 2 | 0 |

  Consequência para a etapa 3: tanque e caminhão só têm rede própria em VEGAS; helicóptero tem rede em 8 fases. Um `.FOP` de mod
  resolve: o `wotm_mods.inc` já serve arquivo novo (que não existe no disco) pelo `fileCdSearchFile`, então `mods/PTS/<FASE>.FOP`
  substitui ou cria a rede (cuidado: a mesma rede guia os carros civis).
- Escolha do ponto de entrada: percorrer os nós do tipo do veículo e pegar o de maior distância mínima aos monstros vivos (ou o
  primeiro numa faixa de distância). `PathNet::getRandNode` (0x17ED58) e `getClosestNode(pos, tipo)` (0x17ED98) existem, mas o
  primeiro ignora o tipo.
- Há uma segunda rede igual para pedestres: `wotm::sym::x_7PathNet_ped` (0x7D0000, `.PED`).

### Monstros
- Por slot: `TheGame::m_slots` em `+0xB80`, stride 0x11190, `m_numSlots` em `+0x1203D4`; vale quem tem `m_playerNum` (`+0x18`) != 0
  (1 = jogador humano, 2 = CPU, pelo `TheGame::Update`). Ou por lista: `m_monsters` (`+0x120380`, ponteiros): `[0..m_numMonsters)`
  são os jogadores (`m_numMonsters` `+0x1203D8` = `Shell::m_numPlayers`), `[4..4 + m_numAIs)` as CPUs (`m_numAIs` `+0x1203E0`).
- Posição: `m_cs` (`+0xC`) → `cs + 0x10` (x, y, z). Morto: `m_dead` (`+0xE8`) != 0; fora da luta: `+0xF6` != 0; invisível: `m_cloaked`
  (`+0xEF`). O port já tem o ajudante `targetPos` (`wotm_overrides.cpp`).
- **Risco para "ataca o monstro mais próximo":** `TankPickup`, `MissileTruckPickup` e `HeliPickup` (todos no
  `updateAttackBehavior`) escolhem o alvo com `TheGame::getClosestPlayer` (`getClosestPlayer__7TheGameR8_fvectorfRf`, 0x1390F8),
  que só percorre `m_monsters[0..m_numMonsters)`, isto é, **só jogadores humanos**. Numa luta 1 contra CPU o exército nunca mira a
  CPU. Saída: gancho no `getClosestPlayer` que, chamado desses três `updateAttackBehavior` (pelo `$ra`), devolve o mais próximo entre
  todos, como o `TheGame::getClosestMonster` (0x139060, que percorre `m_slots` com `m_playerNum != 0`). Torre e disco voador
  (`TurretPickup`, `UfoPickup`) também chamam o `getClosestPlayer`.

## 5. Fase sem `.FOP`: a rede de reserva existe

Conferido no asm do `PointToolKit::loadPoints` (0x21DD80) e do `TheGame::InitBeforeDbLoad`:
- `loadPoints` zera `numPoints` (`+0x4000`) antes de tudo. Arquivo ausente: `fileReadf` devolve 0 (o `fileCdSearchFile` não acha),
  o código imprime "PointToolKit file does not exist, you will get 0 points" e segue com 0 pontos.
- Com `notify` ligado chama o virtual da entrada 3 da vtable (`+0x18`/`+0x1C` da vtable em `+0x4044`), que no `PathTool` é o
  `exportPoints` (`_vt$8PathTool`). O `InitBeforeDbLoad` chama `loadPoints(0, true)` nas duas redes (`fodder` e `ped`).
- `exportPoints` com 0 pontos monta os 2 nós (-100, 0, 250) e (100, 0, 250) ligados entre si (largura 250). Logo, nas seis fases sem
  `.FOP` (BIGSHOT, CANYON1, CRUSH, DBALL, TRAINING, VOLCANO) a `fodder` tem esses 2 nós. Detalhes: z = 250 é altura; o `type` dos dois
  nós não é escrito (fica o que a fase anterior deixou nos nós 0 e 1, ou 0 depois do boot); a marca alta da `MemoryStack` não é
  devolvida nesse caminho (perda pequena, igual ao retail).
- No port vale o mesmo caminho (o `fileReadf` é código do jogo); a linha "file does not exist" no log da carga de CANYON1 confirma.

## Pickups por fase (nós dos `.NGP`)

Tipo:quantidade, "m" = bit 0 do id2 ligado (carro que anda). Destrutíveis por `hitPointClass`.

| Fase | Pickups | Destrutíveis |
|---|---|---|
| CENTRAL | 1:39 2:5 2m:4 3:4 3m:2 7:5 11:4 14:3 15m:5 26:20 26m:24 | c0:35 c1:29 c2:3 |
| VEGAS | 1:19 2:2 2m:7 3:3 4:2 7:6 9:4 10:8 12:5 14:4 15:1 15m:3 26:12 26m:41 | c0:40 c1:68 c2:12 c3:5 |
| METRO | 1:64 2:18 2m:3 3:5 3m:1 4:9 5:6 7:4 12:12 14:5 15:15 26:33 26m:20 27:3 27m:3 | c0:104 c1:75 c2:26 c3:1 |
| TOKYO | 1:11 2:4 2m:1 3:3 4:2 7:4 12:6 14:3 15:1 15m:2 26:4 26m:4 | c0:19 c1:75 c2:11 |
| TOKYODM | 1:17 1m:2 2:2 2m:2 3:3 4:2 7:5 14:1 15m:2 26:8 26m:8 | c1:106 c2:16 |
| ISLAND | 1:17 2:4 2m:8 3:1 3m:1 4:14 7:2 14:2 15:1 15m:2 25:7 26:10 26m:26 | c0:16 c1:151 c2:1 c3:2 |
| 3-MILE | 2:5 2m:3 3:4 15m:2 26:10 26m:10 | c0:21 c1:162 c2:10 |
| AIRPORT | 1:15 2:4 3:12 7:7 14:1 26:8 | c0:24 c1:2 c2:3 |
| SANFRAN | 1:4 2:12 3:8 7:10 14:1 15:3 26:12 | c0:3 c1:94 c2:6 c4:39 |
| MINISF | 1:7 2:1 3:3 7:7 26:7 | c0:33 c1:16 |
| CAPITOL | 1:17 2:2 3:1 4:2 7:5 14:4 15:3 26:8 | c0:41 c1:25 |
| CANYON1 | 3:5 7:2 13:1 | c0:7 c1:72 |
| CANYON2 | 1:6 4:1 13:1 21m:8 24:2 | c0:4 c1:12 |
| UFO | 24:4 24m:2 25:5 | c1:102 c2:20 |
| VOLCANO | 1:13 4:28 | c0:8 c1:60 c2:1 |
| CRUSH | — | c1:2 c3:20 |
| DBALL | 21m:1 | — |
| BIGSHOT, TRAINING | — | — |

O tipo 5 (0x138D, 6 em METRO) o `createPickup` ignora; o `turnOffMilitary` o trata como tanque.

## Riscos
- Alvo: sem o gancho no `getClosestPlayer`, o exército só ataca jogadores humanos (seção 4).
- Tanque e caminhão fora de VEGAS: sem nós do tipo 3, andam pela rede a partir do nó 0 (etapa 3 precisa de `.FOP` de mod). Helicóptero
  sem nós do tipo 1 em 3-MILE, SANFRAN, MINISF, CANYON2, CAPITOL e nas seis sem `.FOP`.
- Contar caos só pelos ganchos de entrada (o "depois" do embrulho pode não rodar numa retomada).
- `killPickup` também mata o próprio exército: filtrar os tipos 9, 10, 12, 23, 24, 25 (e usar o mesmo gancho para reciclar).
- Os militares criados fora da aventura aparecem logo depois da carga; escondê-los no "pool pronto".

## Como foi medido
Leitura de `src/game/{Destructible,Pickup,CarPickup,TheGame,Shell,PathTool,PathNet,db,ActionDispatch,rt,screen}.cpp`,
`src/common/{peds,PointToolKit,file}.cpp` e do asm de `PedGroup`, `TokenManager` (chamadores do `credit`), `killPickup`, `loadPoints`,
`getClosestNode`/`getClosestPath`, `VehicleNavigator::init`, `HeliVehicleNav::init`, `UfoNav`, `TheGame::InitBeforeDbLoad` e da
vtable do `PathTool`. Contagens por scripts de rascunho (fora do repo): descida genérica pelos nós dos 19 `.NGP` com `tools/ngp.py`
(pickups 0x1388..0x13EB, grupos 0xBB8, nós `DESTRUCTIBLE`) e leitura direta dos `.FOP`/`.PED` de `disc/PTS/` (cabeçalho 0x12282000,
pontos de 64 bytes, tipo no byte `+0x32`).
