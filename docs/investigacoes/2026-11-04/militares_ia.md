# Mod do exército: estados, alvo, morte e reciclagem, crédito do KO, som (2026-11-04)

Continuação de `docs/investigacoes/2026-10-25/militares_spawn.md` (plano e decisões do dono) e irmã de
`docs/investigacoes/2026-11-04/militares_caos_modos.md` (caos, modos, rede de caminhos). Só investigação: nada de código mudou.
Endereços de `native/wotm_syms.h` (`wotm::fn::` / `wotm::sym::`). Offsets relativos ao objeto. Lido o asm de `TankPickup`,
`TankVehicle`, `MissileTruckPickup`, `MilitaryPickup`, `HeliPickup`, `HeliVehicle`, `vegas`, `Pickup`, `VehicleNavigator`,
`HeliVehicleNav`, `MilitaryFormation`, `PathNet`, `StateDeath`, e o C++ de `src/game/{Pickup,HeliPickup,TankPickup,
MissileTruckPickup,MilitaryPickup,TheGame,Monster,Shell,Sound}.cpp`.

## 0. Layout (o que o mod lê e escreve)

`Pickup` (base, 0xE0): `kind` +0x00 (= 5; monstro = 1), `flags` u16 +0x04, `cs` +0x0C, vptr +0x10, nó da lista +0x14, posição de
casa +0x20 (a do `.NGP`), rotação de casa +0x30, `maxHealth` +0x40, `speed` +0x44, `health` +0x48, `regenTimer` +0x4C,
`bits` u64 +0x50 (bit 0 = regenerável), `pickupType` +0xA0, `heldState` +0xD8.

| Campo | Tanque (`TankPickup`, 0x2B0) | Caminhão (`MissileTruckPickup`, 0x2A0) | Helicóptero (`HeliPickup`, 0x300) |
|---|---|---|---|
| tipo (`pickupType`) / id do nó | 10 / 0x1392 | 9 / 0x1391 | 12 / 0x1394 (23 / 0x139F; 11 / 0x1393 é o civil) |
| alvo | `focus` +0x170 (`Monster*`) | `focus` +0x170 | `target` +0x178 (`Monster*`); `canFire` +0x170 |
| estado | +0x174 | +0x174 | +0x174 |
| formação | `formation` +0x178, `formationPos` +0x180, `mode` +0x2A0 (0 solto, 1 líder, 2 membro) | +0x178, +0x180, `mode` +0x290 | — |
| veículo | `TankVehicle` +0x1A0 (arma +0x60 = 30, alvo da arma +0x5C, vel. máx. +0x28 = 88) | `TankVehicle` +0x190 (arma = 26) | `HeliVehicle` +0x190 (arma 32 fixa) |
| navegador | `VehicleNavigator` +0x210 (modo +0x21C) | +0x200 (modo +0x20C) | `HeliVehicleNav` +0x200 (`active` +0x214) |
| tempos | próximo tiro / fim da espera +0x2A4, rajada +0x2A8 | +0x294, +0x298 | `nextBurstTime` +0x17C, `stateTime` +0x180, `burstCount` +0x184, `nextShotTime` +0x188 |
| som | `TankSound` +0x190 (handle +0x194) | nenhum | `HeliSound` +0x2E4 |
| vtable | `x_vt_10TankPickup` 0x6F15A0 | `x_vt_18MissileTruckPickup` 0x6EB160 | `x_vt_10HeliPickup` 0x6E98F8 |

Vtable: pares `{s16 delta, fn}` de 8 bytes; fn em vt+off+4. Slots: `takeHit` 0x0C, `initAfterDbLoad` 0x34, `regen` 0x3C, `grab` 0x44,
`drop` 0x4C, `kill` 0x54, `init` 0x5C, `update` 0x64, `setTrans` 0x6C (tanque/caminhão), `leadFormation` 0x74, `followFormation` 0x7C,
`resignFormation` 0x84. O helicóptero não tem `setTrans`.

## 1. Estados

### Tanque e caminhão (`MilitaryPickup::State`, mesma máquina; `enterState` 0x1D0170 / 0x14E2E0, `updateAttackBehavior` 0x1CFE58 / 0x14DFD8)

| Estado | Entrada (`enterState`) | A cada quadro (`updateAttackBehavior`) |
|---|---|---|
| 0 vagar | `nav.wander()` | alvo = `getClosestPlayer(cs->trans, s_fireRange)`; achou → 3; senão, navegador parado → 0 de novo |
| 1 perseguir | `nav.seek(pin do focus, s_fireRange)` | navegador terminou → 3 (nada no código entra no 1) |
| 2 seguir formação | `nav.arrive(formationPos, 20)` | líder em 3 → copia o `focus` do líder, vai a 3 |
| 3 mirar (wind up) | `nav.target(pin do focus)` (gira no lugar); fim = agora + `windUp` | passou o tempo → 4 |
| 4 rajada | `burstCount` = rand(`burstCountMin`, `Max`) | rajada vazia ou alvo sem `m_flags & 2` → 5; distância 2D > `fireRange` + 100 → 0; na hora do tiro, alinhado (`fireAngleTol`) e `isTargetClear` → `fire` |
| 5 esfriar | `nav.disable()`; fim = agora + `windDown` | passou o tempo → 0 |
| 6 | nada | `nav.disable()` (não usado) |
| 7 estacionado | `nav.disable()` | `nav.disable()`; o `update` nem chama o comportamento |

- **"Parado/escondido" = 7** (`turnOffMilitary`, e o `MilitaryPickup::grab` escreve 7 direto). **"Ativo" = 0** (vaga e entra em 3→4→5→0 sozinho).
- **`enterState(0)`**: com `mode == 2` (membro de formação) qualquer pedido de estado 0 ou 1 vira 2; senão `nav.wander()` (modo do
  navegador 1, zera +0x58/+0x5A do navegador) e `state = 0`. Não toca no `focus` (estados 1 e 3 o leem: `focus` nulo neles quebra).
- `update` (0x1CFC08 / 0x14DDB0): estado 7 pula tudo. Senão: pad zerado; `mode` 0 → `nav.f10 = 0.2`, `f14 = 0.8` e
  `updateAttackBehavior`; 1 → `updateLeadBehavior`; 2 → `updateFollowBehavior` (= attack); depois `VehicleNavigator::update`,
  `TankVehicle::updateControls` (o tiro), `Vehicle::updatePosition`, `updateCollision`; `speed` = vel. do veículo; `health > 0` liga o
  `s_militaryStillAlive`. Tanque: `TankSound::updateTankSound`. Devolve `health > 0`.
- `fire` (0x1CFB90 / 0x14DD38): alvo da arma = id de `Interactives` do `focus`; `pad.fire = 0xFF`; próximo tiro = agora + `fireDelayMin`; rajada−1.
- Constantes (`.sdata`): tanque `fireRange` 750, `fireAngleTol` 0.99, wind up/down 96 fields, rajada 10..20, 15 fields entre tiros
  (`s_seekRange` 1000 não é usado). Caminhão: `fireRange` 1000, tol 0.97, wind up 50..120, rajada 1, wind down 255. Mira do míssil
  = max(pitch, 25°) ± 10°; do canhão, só atira com pitch em (0, 25°).

### Helicóptero (`HeliPickup::State`; `enterState` 0x13F3E8, `updateAttackBehavior` 0x13EE30; C++ em `src/game/HeliPickup.cpp`)

| Estado | Entrada | A cada quadro |
|---|---|---|
| 0 vagar | `target = 0`, `nav.wander()` | `target = getClosestPlayer(s_seekRange)`; perto demais (`s_fleeRange`) → 3; `nextBurstTime` vencido e há alvo → 1 |
| 1 perseguir | `nav.seek(pin do alvo, s_fireRange)` | alvo sem `m_flags & 2` → 0; dist 2D < `fireRange` e \|dz\| < 150 → 2; navegador parou → 0 |
| 2 pairar e atirar | `burstCount` 10..20; `stateTime` = rajada × 5 fields (sem arma: 300..600) | rajada vazia, navegador parado ou tempo vencido → 3; longe → 1; alinhado (0.99) e `isTargetClear` → `pad.fire` |
| 3 fugir | `nav.flee(pin do alvo, s_safeRange)`; `nextBurstTime` = agora + 600..1200 | sem jogador em `s_safeRange` (500) → 0 |
| 4 caindo | `nav.disable()`, giro aleatório | gira; bateu em algo que não é chão (`hdSphereTest`) → 5 |
| 5 estacionado/morto | `nav.disable()` | `nav.disable()`; o `update` não roda nada |

- **Parado = 5**, **ativo = 0**. `update` (0x13ECC0) devolve `health > 0 || state == 4` (caindo segue vivo até bater).
- `s_seekRange` 999999 (o mapa todo), `fireRange` 450, `fleeRange` 0 (nunca foge por proximidade), `safeRange` 500.
- `takeHit` (0x13EA78): golpe ≤ 0.1 vale 0.05; zera a vida com golpe < 5 → estado 4 (cai); golpe ≥ 5 que zera → morre no ar no
  próximo quadro.

## 2. Alvo

- **Só jogadores humanos.** Os três chamam `TheGame::getClosestPlayer(pos, maxDist, &distSq)` (0x1390F8), que percorre
  `m_monsters[0 .. m_numMonsters)` (`+0x120380`, `+0x1203D8`); o `Shell::InitPlayers` põe ali `Shell::m_numPlayers`. CPUs ficam em
  `m_monsters[4..]` e **nunca** são alvo. Filtros: `m_playerNum != 0` (+0x18) e `m_cloaked == 0` (+0xEF). **Não** filtra morto: um
  monstro em KO (o `StateDeath::transitionInto` apaga o bit 1 de `m_flags`) pode ser escolhido, e o tanque perde 3→4→5 (~4 s) até
  largar.
- Onde fica: `focus` +0x170 (tanque/caminhão), `target` +0x178 (helicóptero). Escolhido só no estado 0 (e no 1/2/3 do helicóptero).
  O navegador guarda o **ponteiro do pin** do alvo ao entrar no estado (seek/target/flee), então trocar o campo no meio do estado não
  redireciona a mira até a próxima entrada.
- **Onde enganchar:** o próprio `getClosestPlayer`. Chamadores: só `updateAttackBehavior` de tanque, caminhão, helicóptero,
  `TurretPickup::updateBehavior` e `UfoPickup::updateAttackBehavior`. Substituto: percorrer `m_slots` (game +0xB80, passo 0x11190,
  `m_numSlots` +0x1203D4) com `m_playerNum != 0`, `m_cloaked == 0`, `m_dead` (+0xE8) == 0, `m_unkF6` (+0xF6) == 0, `m_flags & 2`,
  `cs->drawMe`; o mais perto dentro de `maxDist`, e escrever `distSq` (o helicóptero usa). O `getClosestMonster` (0x139060) já faz o
  laço em `m_slots`, sem os filtros de capa e morte. Ligar só com o mod ativo (ou pelo `$ra` nos três chamadores): o disco voador
  também existe fora da aventura (nó 0x13A1 com bit 0 do id2) e passaria a mirar CPUs.
- `Pickup::isTargetClear` (usado antes de cada tiro) só considera **CPUs** (`m_monsters[4 + i]`) como obstáculo na linha de tiro.
  Com CPU como alvo, outra CPU na frente bloqueia; jogador humano na frente não.

## 3. Morte e reciclagem

Caminho: `update` devolve falso → `LevelPickups::update` (0x181EC0; no port substituído pelo `pickupsHook` de
`native/runtime/wotm_coop.inc`, mesmos passos) → `killPickup(nó, 1)` (0x182D20): tira o cs da grade (`hdRemoveCsFromGrid`),
`drawMe = 0`, chama `kill` virtual, solta quem segurava, move o nó para `regenList` se `bits & 1` (e `regen` = 1), senão para
`deadList`; limpa os destaques. A vaga em `Interactives` fica (nunca é devolvida).

| `kill` | Tanque 0x1CFB08 | Caminhão 0x14DCB8 | Helicóptero 0x13EC18 |
|---|---|---|---|
| desenho/colisão | `drawMe = 0`, `testCollision = 0` | idem | idem |
| efeito e som | partícula 0xB (escala 8), `playDestructibleSound(0x65)` | idem | idem + `terminateHeliSound`, `particleKillFx(fx)` |
| som do motor | `terminateTankSound` | — | — |
| resto | `MilitaryPickup::kill` (0x14D928): sai da formação, `flags &= ~2`, **se `bits & 1 == 0`: `CsPool::csDeactivate(cs)`, `cs = 0`** | idem | `flags &= ~2`, mesmo teste do cs |

- **Os veículos da fase não são regeneráveis:** `dbProcInteractive` cria com `regen = false`; o `initAfterDbLoad` do tanque e do
  caminhão ainda zera o bit 0. Morto, o cs volta ao `CsPool` (outro objeto pode pegá-lo) e o nó vai para `deadList`, que o jogo
  nunca mais visita. Sem o cs, o nó da malha (`cs->epNode`) se perde e o `Vehicle`/`HeliVehicle` aponta para um cs que não é mais dele.
- **Receita para reciclar** (recomendada): depois do `initAfterDbLoad`, ligar `bits |= 1` em cada veículo do pool e pôr
  `regenTimer` (+0x4C) = 0x7FFFFFFF. Assim o `kill` mantém o cs (malha e objetos intactos; só `drawMe`/`testCollision` = 0, fora da
  grade), o nó vai para `regenList` e o jogo nunca o regenera sozinho (o `regenUpdate` só devolve verdadeiro com o contador ≤ 0).
  O bit 0 não tem outro leitor (`isRegenable` não é chamado). Para acordar um morto:
  1. Tirar o nó da `regenList` (`wotm::sym::x_12LevelPickups_s_regenList`) e pôr na cabeça da `activeList` (nó = `{data, next, prev}`,
     como o `pickupsHook` faz).
  2. `regen` virtual (slot 0x3C). Tanque/caminhão: `Pickup::regen` (0x17F780): `regenTimer = 0xA8C`, `drawMe = 1`,
     `health = maxHealth`, cs na posição e rotação de casa, `hatCheck`. Helicóptero: `HeliPickup::regen` (0x13EDE0) = isso +
     `testCollision = 1`, `target = 0`, rajada/tempos zerados, `fx = -1`, `enterState(0)`.
  3. Repor o que o `regen` não repõe: `regenTimer = 0x7FFFFFFF`; `cs->testCollision = 1`; `flags |= 0xA` (tanque/caminhão, como o
     `initAfterDbLoad`) ou `|= 2` (helicóptero). Tanque/caminhão: `mode = 0`, `formation = 0`, `focus = 0`.
  4. Posicionar (seção 5) e `enterState(0)`.
- Alternativa sem o bit 0 (morto no `deadList`): exigiria `CsPool::csActivate` (0x1F75B8) + `hierSetCsEpNode` com o nó guardado antes
  + `Vehicle::setCs`/`HeliVehicle::setCs` + `hdReparentCsGrid`. Mais frágil; não recomendado.
- **Pego e solto por um monstro:** `MilitaryPickup::grab` põe estado 7 e o `HeliPickup::grab` estado 5; o `drop` devolve à
  `activeList` mas não sai desse estado: o veículo fica inerte para sempre. O mod precisa tratar estado 7/5 na `activeList` (acordar
  com `enterState(0)` ou contar como estacionado). Arremessado vira arma; no impacto o `killPickup(…, 1)` o manda à `regenList`
  (com o bit 0) ou à `deadList`.

## 4. Formações

- **Não precisa de líder.** Com `mode == 0` (o que o `initAfterDbLoad` deixa: `mode = 0`, `formation = 0`, `focus = 0`, estado 0) o
  `update` chama direto o `updateAttackBehavior`: o tanque sozinho vaga pela rede, mira e atira. Velocidade máxima 88 (construtor do
  `TankVehicle`).
- Líder (`mode` 1, `leadFormation`: vel. 88, `enterState(0)`): roda o mesmo `updateAttackBehavior`, inverte a formação no fim do
  caminho e chama `updatePositions` acima de meia velocidade. Membro (`mode` 2, `followFormation`: vel. 146.7, estado 2): vai ao
  `formationPos` e copia o alvo quando o líder entra em 3. `MilitaryFormation::remove` (0x14CB08): `resignFormation` do removido
  (`mode = 0`, `enterState(0)`) e promove o primeiro membro a líder.
- `initMilitaryForVegas` (0x1813E8) é a montagem da cutscene: duas `BoxFormation(2, 83, 150)` com até 4 tanques cada (os demais
  escondidos), 2 caminhões em cada, líderes postos em (167, -2030, 0.2) e (-89, -2030, 0.2) de Vegas, helicópteros desenhados no
  estado 0. Coordenadas fixas de Vegas e memória nova na `MemoryStack::global` a cada chamada: **não usar fora da aventura**.

## 5. Posicionamento

- `TankPickup::setTrans` / `MissileTruckPickup::setTrans` (0x1CFA30 / 0x14DBE0, slot 0x6C): `cs->trans = p`,
  `hdReparentCsGrid(cs)`, `VehicleNavigator::init` (0x1DE110), `enterState(state atual)`. Não atualiza o `orient.w` do `Vehicle`
  (altura de onde o `updateCollision` suaviza): chamar também `Vehicle::setPos(veículo, p)` (0x1DDC60) e zerar a velocidade
  (veículo +0x20), senão o tanque "desce" da altura antiga por alguns quadros.
- `VehicleNavigator::init`: `PathNet::getClosestPath(pos, frente, &i, tipo)` com **tipo 3** para os ids 0x1391..0x1392 (tanque e
  caminhão; os carros usam 0). `getClosestNode` começa do nó 0 e só aceita nós do tipo pedido: **só o `VEGAS.FOP` tem nós tipo 3**
  (8 de 64). Em qualquer outra fase o tanque se agarra ao caminho do nó 0 e atravessa o mapa até lá. Depois o `getRandPath` não
  filtra tipo. Contagem por fase em `militares_caos_modos.md`, seção 4.
- Helicóptero: sem `setTrans`. `HeliVehicle::setPos(hv, p)` (0x13F738: `cs->trans = p`, zera a velocidade do `PointMass`),
  `hdReparentCsGrid(cs)` (0x1FE318), `HeliVehicleNav::init` (0x13FD78: nó mais perto do **tipo 1**; termina com `disable`), e então
  `enterState(0)` para voltar a vagar.
- **Pôr num nó da `PathNet::fodder`** (`wotm::sym::x_7PathNet_fodder` 0x7CA4D0): `numNodes` u16 +0; nós em +0x10, passo 0x30: posição
  (x, y, z; Z para cima) +0, tipo u8 +0x10, `numPaths` u8 +0x11, ligações `{u16 id, u8, u8}` +0x12. Escolher um nó do tipo do veículo
  (3 tanque/caminhão, 1 helicóptero) longe dos monstros; para tanque e caminhão baixar a z ao chão como o `gameGetStartPoint`
  (`pos.z -= hdHatTest(&pos, &normal, 5, false, 4096)`, 0x1FE990). Para tanque fora de Vegas: enganchar o `getClosestNode`
  (0x17ED98) quando o tipo pedido é 3 e a fase não tem nenhum (cair no tipo 0 ou -1, "qualquer"), ou trazer `.FOP` de mod.

## 6. Dano e crédito do KO

- Tiro: `TankVehicle::updateControls` (0x1D0498, também do caminhão) chama `Weapons::CreateStaticWeapon(tipo, byId, targetId, …)`
  com `tipo` = veículo +0x60 (30 canhão, 26 míssil), **`byId` = id de `Interactives` do próprio veículo** (bits 7..17 do cabeçalho
  do `cs->epNode`, o do `addInteractive`) e `targetId` = veículo +0x5C (o `focus`). Helicóptero (`HeliVehicle::updateControls`):
  tipo 32, `byId` = o dele, `targetId` 0. A arma guarda `m_ownerId = byId` e o acerto vai por
  `Interactives::dispatchHit(dir, dano, m_ownerId, alvo)` → `Monster::takeHit(dir, dano, attackerId)`.
- `Monster::takeHit` só aceita origem quando `*getInteractive(attackerId) == 1` (monstro); o veículo tem `kind` 5 → `src = 0` →
  `takeDamage(dano, false, 0)` → `blowUpMonster(0)` → `StateDeath::setKiller(0)`: **`m_killer` (+0x846C = `StateDeath` +0x8450, +0x1C)
  fica 0**. Dano normal; nenhum outro monstro recebe crédito, vitória nem estamina.
- **Mas a vítima perde um ponto.** `StateDeath::transitionInto` (0x1529D8): com assassino, `m_slots[killer->m_monsterNum].m_winsThisGame++`
  (e o assassino vai à vitória); **sem assassino (0x15300C), `m_slots[vitima->m_monsterNum].m_winsThisGame--`**, em qualquer modo.
  É o mesmo castigo de cair na água ou morrer sozinho. No free-for-all (modos 2, 3, 11) o `gameResolveLifeAndDeath` trata `m_killer == 0`
  como essa morte sem dono (`rtReturnToShell(1, 0x1E)`; o `Shell::Evaluate*` respawna, sem vitória). No elimination de 2 jogadores (6)
  o `Shell` conta `m_wins` só com assassino e reescreve `m_winsThisGame` a partir dele, então ali não há −1 efetivo; a vida/continue
  é gasta normalmente.
- Se o dono quiser "neutro" de verdade: marcar no `takeHit` do monstro (entrada) quando `attackerId` é um veículo militar do pool e o
  golpe zera a vida, e na saída do `StateDeath::transitionInto` devolver o +1 se `m_killer == 0` e a marca estiver ligada.
- Modos que anulam: dodgeball (7: golpe sem origem vale 0) e crush (9: sem dano antes da morte súbita) no `takeDamage`.

## 7. Script de Vegas

- `TheGame::InitAfterDbLoad` chama `LevelPickups::initAfterDbLoad` e depois, só com `shell->m_mode == 1`, `vegasInitAfter`
  (0x1DD790): `whichAction = 0`, **`turnOffMilitary`** (tudo no 7/5, `drawMe = 0`). `vegasInitBefore` e `TheGame::InitBeforeDbLoad`
  zeram `vegasSpawnMilitary` (0x6F8558). `TheGame::Update` chama `vegasUpdate` (0x1DD7B0) também só com `m_mode == 1`.
- `vegasUpdate`, máquina em `whichAction` (0x6F8550) e contador `D_006F8D0C` (fields):
  - 0: quando `m_monsters[4]` (Togera, tipo 0x60) morre de vez (`m_unkF6`) e `vegasSpawnMilitary == 0`: esconde e desliga o Togera,
    `whichAction = 1`, 30 fields, `fadeOutAndIn(4)`.
  - 1: no fim dos 30: `whichAction = 2`, **`vegasSpawnMilitary = 1`**, contador 540; se `m_monsters[5]` é o Robo-47 (0x40), mostra,
    `aiInit`, `m_playerNum = 2`, roteiro 0x2262 (cinema) e música dele.
  - 2: conta os 540 (30 se o filme foi abortado); ao chegar a 0, **`initMilitaryForVegas`**.
  - Atalho de depuração (byte `m_monsters[0]` +0xFD): no 0 pula direto para o 2 (contador 600, mata o Togera com 1000, acorda o
    Robo-47); no 2 dá a vitória (`m_unkF7 = 1`, `rtReturnToShell(0, 0xF)`).
- `turnOnMilitary` e `initMilitaryForCentral` não são chamados por Vegas (o Central chama o `initMilitaryForCentral`; não há
  tanques no `CENTRAL.NGP`).
- `vegasSpawnMilitary` só é lido pelo próprio `vegasUpdate` (trava contra disparo duplo). `_12LevelPickups$s_militaryStillAlive`
  (0x6F8E98) é zerado a cada `LevelPickups::update` e ligado por tanque, caminhão e helicóptero vivos e não estacionados, mas
  **nenhum código o lê** (nem dados apontam para ele). Serve ao mod como "há exército vivo", de graça.
- Consequência para a etapa 1: fora da aventura ninguém chama `turnOffMilitary`; os veículos criados com o modo 1 fingido saem
  ativos e visíveis nas posições do `.NGP`. O mod chama `turnOffMilitary` (0x181008) na saída do `TheGame::InitAfterDbLoad`
  (0x136F58).

## 8. Som

O "0xAA do banco 5" de `militares_spawn.md` estava errado: 0xAA (170) é a distância mínima passada ao `calcDualMonoMixVolPan`.

| Som | Banco (`gSoundBanks`, 0x41A050) | Fora de Vegas |
|---|---|---|
| Motor do tanque (`TankSound::updateTankSound` 0x1B5560) | **[12] `BANK_LEVEL`, som 0** (em Vegas `VEGAS_S.BNK`, som 0 em laço) | **errado**: toca o som 0 do banco da fase (ex.: Metro, som não-laço); nas fases sem banco próprio fora da aventura (CAPITOL, DBALL, CRUSH) depende do que estiver no slot |
| Caminhão | sem som de motor | — |
| Rotor, metralhadora, queda do helicóptero (`updateHeliSound`) | [21] `BANK_AMBIENT`, 0x1A, 0x54 (só tipo 12, estado 2), 0x5C | ok (banco comum) |
| Disparo/voo/explosão das armas 26, 30, 32 (`WeaponSound`) | [21] `BANK_AMBIENT` (0x1C, 0x1E, 0x2A, 0x55; explosões 0 e 1) | ok |
| Destruição (`playDestructibleSound(0x65)`) | [21] `BANK_AMBIENT` | ok |

- `BANK_AMBIENT` é `GENAMB_S.BNK` com até 2 monstros (ou modos 4 e 6) e `GENAM_LS.BNK` (versão leve, mesmos 96 sons) com 3 ou mais.
- `TankSound::playTankFireSound` (AMBIENT 0x2A) não tem chamador.
- Curiosidade: a fase 12 (TRAINING) carrega `MILTRY_S.BNK` (2 sons, o 1 em laço), um banco "militar" sem uso visível.
- Saída para o motor: como o `panelShellBank` (`native/runtime/wotm_snd.inc`), carregar uma cópia de `SND/VEGAS_S.BNK` só no motor
  nativo e, num gancho do `updateTankSound` fora de Vegas, tocar o som 0 dela (ou simplesmente não tocar motor).

## 9. Leituras de modo/fase/jogador durante o update

- Os TUs `TankPickup`, `TankVehicle`, `MissileTruckPickup`, `MilitaryPickup`, `HeliPickup`, `HeliVehicle`, `Vehicle`,
  `VehicleNavigator`, `HeliVehicleNav` e `MilitaryFormation` **não leem** `m_gameMode`, `m_levelId`, `m_monsters` nem o `shell`.
- Leituras indiretas: `getClosestPlayer` (`m_monsters[0..m_numMonsters)`, só humanos; seção 2); `isTargetClear` (`m_numAIs`,
  `m_monsters[4..]`); `updateHeliSound` (`shell->m_numPlayers == 1` liga o doppler; inofensivo); `Pickup::grab` (texto do Central na
  aventura, só tipo 14; dodgeball). Os `s_padControl` (tanque 0x6F8330, caminhão 0x6F7CC8, helicóptero 0x6F7B44) são de depuração
  (pad 1 dirige o veículo) e ficam 0.
- O que quebra fora da aventura é só o que já foi dito: o portão do `createPickup`, o `turnOffMilitary` que não roda, alvo só
  humano, rede tipo 3 só em Vegas e o som do motor.

## Riscos

- Alvo: sem o gancho, numa luta contra CPUs o exército só mira o(s) humano(s), e mira até KO'd.
- Morte: sem o bit 0 o cs volta ao pool e o veículo não tem volta; com ele, `regenTimer` precisa ficar alto senão o jogo regenera
  sozinho na posição de casa depois de 2700 quadros, sem `testCollision` nem `flags` (tanque intocável e "fantasma").
- Pego e solto: fica no 7/5 na `activeList`.
- Placar: KO pelo exército tira 1 da vítima (decisão do dono).
- Rede: tanque/caminhão fora de Vegas sem nó tipo 3; helicóptero sem nó tipo 1 em 3-MILE, SANFRAN, MINISF, CANYON2, CAPITOL e nas
  fases sem `.FOP`.
- Helicóptero: `HeliPickup::takeHit` lê o `attacker` como `Monster*` (`m_index` para vibrar o pad) quando o golpe é do tipo 0 com
  origem 8; se um projétil de outro veículo chegar com essa origem, vibra um pad qualquer (baixo).
- O `LevelPickups::update` do port é o `pickupsHook` (`wotm_coop.inc`): a reciclagem deve conviver com ele (mesmas listas).
- Formação (`initMilitaryForVegas`) aloca na `MemoryStack::global` a cada chamada: evitar fora da carga.

## Como foi medido

Leitura do asm (`asm/nonmatchings/game/...` e `asm/game/{VehicleNavigator,HeliVehicleNav,MilitaryFormation}.s`), dos `.sdata`
(constantes) e das vtables; contagem dos tipos de nó dos `.FOP` (cabeçalho 0x12282000, pontos de 0x40 bytes, tipo no byte +0x32);
`tools/snd989.py bnk` em `VEGAS_S`, `MILTRY_S`, `METRO_S`, `GENAMB_S` e `GENAM_LS`. Nada rodado no jogo.
