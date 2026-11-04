# Exército de Vegas como entidades soltas: o que há no disco e o custo de um mod de spawn (2026-10-25)

## Pergunta do dono
Na fase 2 da aventura (Vegas), depois de derrotar o Togera, uma cutscene traz o exército (tanques, helicópteros, "antiaéreos")
junto com o Robo-47. Ele só aparece ali. Pergunta: esses veículos pertencem à fase? E quanto custaria um mod que os solta em qualquer
fase por timer ou por um fator de caos (prédios, carros e pedestres destruídos)? Decisão do dono: anotar, não fazer agora.

## O que está no disco
Varredura da árvore de nós dos 19 `.NGP` de `disc/LVL/` (mesma descida do `dbsTraverse`/`dbProcInteractive`, parando no nó de
pickup), procurando os ids que o `LevelPickups::createPickup` (`src/game/Pickup.cpp`) transforma em veículo militar:

| Veículo | Id do nó (tipo) | Fases | Em Vegas |
|---|---|---|---|
| Tanque (`TankPickup`) | 0x1392 (10) | só VEGAS | 8 |
| Caminhão de míssil (`MissileTruckPickup`), o "antiaéreo" | 0x1391 (9) | só VEGAS | 4 |
| Helicóptero militar (`HeliPickup`, militar) | 0x1394 (12) | VEGAS, METRO (12), TOKYO (6) | 5 |
| Torre (`TurretPickup`) | 0x13A0 | CANYON2 (2), UFO (6) | 0 |
| Disco voador (`UfoPickup`) | 0x13A1 | ISLAND (7), UFO (5) | 0 |

- Cada veículo tem malha própria dentro do `.NGP` da fase: tanque 20 nós e 3 objetos (~20 KB de pacote VIF), caminhão 18 nós e
  3 objetos (~7 KB), helicóptero 40 nós e 11 objetos (~19 KB). Texturas do `VEGAS.TEX`: 79 e 87 (tanque e caminhão), 77, 78 e 88
  (helicóptero). Todos os tanques de Vegas são o mesmo modelo, e o mesmo vale para os caminhões.
- O helicóptero militar é idêntico byte a byte (md5 dos pacotes) em VEGAS, METRO e TOKYO; cada fase leva sua cópia. Quatro das
  malhas são as mesmas do helicóptero civil do CENTRAL (0x1393). No Metro o dono os viu numa cutscene; em Tóquio não se lembra.
- O `createPickup` só constrói tanque, caminhão, helicóptero militar, torre e disco voador com `TheGame::m_gameMode == 1`
  (aventura). Fora dela os nós ficam no arquivo e o jogo não cria nada (por isso não aparecem no free-for-all nem pelo `WOTM_MATCH`,
  modo 2).

## Como Vegas os usa
- Na carga da aventura são criados e ficam escondidos na lista de pickups ativos (`LevelPickups::activeList`).
- `vegasUpdate` (script da fase) chama `LevelPickups::initMilitaryForVegas`: duas `BoxFormation(2, 83, 150)` com 4 tanques cada
  (líder + membros), 2 caminhões em cada, helicópteros no estado 0, `drawMe` 1. Também chama `turnOffMilitary`.
- `turnOffMilitary` / `turnOnMilitary`: tipos 5 e 10 (tanque), 9 (caminhão), 11, 12 e 23 (helicópteros) vão ao estado parado
  (7; 5 no helicóptero) ou ao estado 0, com `drawMe` 0 ou 1. É um pool pronto: esconder e acordar.
- `_12LevelPickups$s_militaryStillAlive` é ligado pelo `HeliPickup::update` enquanto há militar vivo (o script de Vegas lê).

## Dependências de cada veículo
- **Navegação:** `TankPickup` e `MissileTruckPickup` montam o `VehicleNavigator` sobre `PathNet::fodder` (a rede dos carros e
  pedestres; `lui %hi(_7PathNet$fodder)` no construtor). O helicóptero também: o `HeliPickup` passa `&PathNet::fodder` ao
  `HeliVehicleNav(HeliVehicle&, PathNet*)`, que tem `followPath`, `getRandPath`, `getSeekPath` e `getFleePath` sobre ela (não é voo
  livre). Os carros (`CarPickup`) e o disco voador (`UfoPickup`) usam a mesma rede.
- **De onde vem a rede:** `TheGame::InitBeforeDbLoad` inicia o `PathTool::instance` (nome `fop`, pasta `pts`), que lê
  `disc/PTS/<FASE>.FOP` e o `exportPoints` copia para `PathNet::fodder` (até 256 nós). Há `.FOP` em 13 fases: 3-MILE, AIRPORT,
  CANYON2, CAPITOL, CENTRAL, ISLAND, METRO, MINISF, SANFRAN, TOKYO, TOKYODM, UFO e VEGAS. Sem `.FOP`: BIGSHOT, CANYON1, CRUSH,
  DBALL, TRAINING e VOLCANO. Com zero pontos o `exportPoints` monta uma rede de reserva de 2 nós, (-100, 0, 250) e (100, 0, 250),
  ligados entre si (não conferido se o `PointToolKit::loadPoints` deixa zero pontos quando o arquivo falta). Nessas 6 fases um
  veículo solto andaria só nesse segmento; o mod precisaria trazer um `.FOP` próprio (o formato é o do `PathTool::exportPoints`).
- **Tiro:** `TankVehicle::updateControls` dispara com `Weapons::CreateStaticWeapon` (tipo no campo +0x60 do veículo). Nenhuma arma
  (ids 0xFA0..0x11F7) é exclusiva de Vegas: os modelos de projétil estão em quase todas as fases.
- **Som:** `TankSound::updateTankSound` toca o som 0xAA do banco de índice 5 (`gSoundBanks`). Não conferido se o 5 é o banco da
  fase (`VEGAS_S`) ou um comum; se for o da fase, fora de Vegas sai mudo ou errado. Precedente para resolver: o painel da pausa
  carrega uma cópia do `SHELL_S.BNK` (`wotm_snd_hooks::panelShellBank`).
- **Memória e slots:** os construtores alocam na `MemoryStack::global` e o `initAfterDbLoad` pega slot em `Interactives`
  (`addInteractive`, o índice vai para o nó de cima). Por isso o pool deve ser criado na carga, não no meio da luta, e cada
  instância precisa de uma cópia própria do nó de cima (as malhas podem ser compartilhadas).

## Plano proposto (em etapas)
1. **Vegas e helicópteros de Metro/Tóquio fora da aventura (barato, ~1 dia).** Gancho no `createPickup` que finge o modo 1 só
   para os ids militares; depois da carga, tudo como o `turnOffMilitary` deixa. Um timer do mod acorda N deles (`setTrans` num nó
   da `PathNet::fodder` + `enterState(0)` + `drawMe` 1, como o `turnOnMilitary`). Sem mexer em arquivo; serve para provar que a
   IA, o tiro e o som funcionam fora da aventura antes do resto.
2. **Fator de caos (barato).** Ganchos em prédio destruído (`Destructibles`), carro destruído (`CarPickup::kill`) e pedestre
   morto somam pontos; cada faixa solta uma leva.
3. **Tanque e caminhão em qualquer fase (médio a difícil, vários dias).** Ferramenta offline (`tools/ngp.py`, `tools/tex.py`) tira
   de VEGAS os nós dos três veículos e as texturas para um arquivo de mod; na carga, um gancho copia para a RAM alta (a de
   `WOTM_HIGHRAM`), rebaseia os ponteiros (a imagem da fase é pré-ligada em 0xA00000), sobe as texturas e cria o pool. O ponto
   arriscado é a textura: espaço livre na VRAM e o `TBP0` do `TEX0` de cada objeto. Relaciona-se com os "arquivos próprios" dos
   monstros custom (`TODO.md`, item 8), que também precisam carregar modelo extra.

## Decisões do dono (2026-11-04)
- Escopo: as três etapas, nessa ordem (a 3 só depois da 1 e da 2 testadas).
- Liga/desliga: opção no menu nativo ("Recompiled options"), desligada por padrão.
- Gatilho escolhível no menu: timer, caos ou os dois.
- Modos em que vale: configurável no menu (os versus; a aventura fica como é).
- Alvo: cada veículo ataca o monstro mais próximo.
- Dano normal; KO pelo exército não dá ponto a nenhum jogador.
- Pool reciclado: veículo destruído volta escondido ao pool e pode sair na próxima leva.
- Etapa 3, nas seis fases sem `.FOP`: só helicópteros.
- Testes ao vivo no segundo monitor e sem som (`WOTM_MONITOR=1`, `WOTM_MUTE=1`, a criar).

## Como foi medido
Scripts de rascunho (fora do repo) sobre `tools/ngp.py`: descida genérica pelos filhos de cada tipo de nó (GROUP, LOD, SWITCH,
TRANSLATE, ROTATE, SCALE, CONTROL, ACTION_DATA, CHAR_INSTANCE, ANIM_XFORM, ANIM_SCALE, INTERACTIVE_STATE, DESTRUCTIBLE), contagem
dos ids 0x12C0..0x13EB e 0xFA0..0x11F7 por fase, md5 do pacote VIF de cada objeto (`polyPkt`, `QWC + 1` quadwords) para comparar
modelos entre fases. Chamadas e alvos lidos no asm de `TankPickup`, `TankVehicle`, `MissileTruckPickup`, `vegas` e `Sound`.
