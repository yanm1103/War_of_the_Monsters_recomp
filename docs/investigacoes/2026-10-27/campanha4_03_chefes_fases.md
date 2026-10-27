# Campanha com até 4 jogadores: fases, chefes e o que assume um jogador só (2026-10-27)

Frente 3 do item 13 do `TODO.md`. Só leitura: C++ de `src/game/` (equivalentes `NON_MATCHING`), tabelas de dados do retail e rascunhos
do m2c das funções ainda em asm (`vegasUpdate`, `ufoUpdate`, `tokyoUpdate`, `sanFranUpdate`, `canyon2*`, `threeMile*`,
`AiScript3Mile`, `Ai::updateAssBoss`, `StateTopSpin`, `StateCannonHands`, `StateUltraTazer`). Nada rodado. Endereços de
`native/wotm_syms.h`.

## 0. Correção do TODO: 0x1A0, 0x1C0 e 0x1E0 não são slots
São **códigos de monstro** (`tipo << 5`, o `m_typeBits`/`m_monsterSel`): 0x1A0 = 13 `assboss` (Goliath Prime), 0x1C0 = 14 `jelly`,
0x1E0 = 15 `final`. Entram na luta como **IAs comuns** pela tabela `levelMonsters` e ocupam um `m_slots[]` cada, como qualquer
monstro. O que têm de especial está no `TheGame::SetAIMonster` (0x137B10):
- 0x1A0: desligado na carga (`disableMonster`: `m_playerNum` 0, nada desenhado) até o `canyon2BossIntro` dar `aiInit`.
- 0x1C0 e 0x1E0: desligados e entregues ao `finalBoss` (`+0x74` jelly, `+0x78` final); contam como IA no `m_numAIs` mesmo
  desligados (o `GetNumAIsAlive` só zera quando os dois morrem).
- O Mecha (fase 0 do chefe final) **não é monstro nem slot**: é o próprio objeto `finalBoss` com a árvore 0x27A6 do `CAPITOL.NGP`.
- A planta (3-Mile) também não: `plantBoss` (3 cabeças, 10 raízes, base) são `DbInteractive` da fase.

## 1. As 11 fases da aventura (`m_mode` 1)
`m_levelNum` vai de 1 a 11 (`EvaluateOnePlayerStoryStatus` soma 1 a cada vitória; `InitRTState` copia para `TheGame::m_levelId`, então
os dois índices são o mesmo). IAs: `Shell::m_levelNumAIs[]` (preenchido no construtor do Shell) e `levelMonsters[12][12]` /
`levelMonsterModels[12][4]` (dados do `TheGame`). Nomes em `GLOSSARIO.md`.

| # | Fase | IAs (traje) | Script / chefe | Fim da fase |
|---|---|---|---|---|
| 1 | Midtown Park (`central`) | Congar (0) | `centralInitAfter`: 3 power-ups de dica (tipos 10–12, texto da aventura) + militares do centro | genérico (todas as IAs fora) |
| 2 | Gamblers Gulch (`vegas`) | Togera (0), Robo-47 (0) | Robo-47 desligado na carga; `vegasUpdate`: morte do Togera (`ais[0]`) → cinema 0x2262, Robo-47 `aiInit`, exército | genérico |
| 3 | Rosdale Canyon (`canyon2`) | 3× Preytor (1) = formigas, Goliath (0x1A0, 1) | `canyon2Update`, `canyon2LevelProgression` 0→2 (intro 0x226F)→3→6; formigas renascem do formigueiro (`updateRespawn`); IA do chefe `Ai::updateAssBoss*` | pelo script (fase 3 fora do genérico) |
| 4 | Metro City (`metro`) | Preytor (0), Congar (2) | — | genérico |
| 5 | Century Airfield (`airport`) | Raptros (0), Raptros (1) | aviões (`airportUpdate`), sem referência a jogador | genérico |
| 6 | Atomic Island (`3-Mile`) | 4× Kineticlops (0) | `AiScript3Mile` (enxame/power-up/fogo), chaminés + contagem, até 10 renascimentos; cratera (`craterSwitched`) → `PlantBoss` | pelo `PlantBoss` (cinema 0x2264) |
| 7 | Baytown (`sanfran`) | Robo-47 (1), Robo-47 (2) | terremoto quando os tokens "Destructibles" de **qualquer** jogador passam de 0xED8 (já em laço sobre `m_numMonsters`) | genérico |
| 8 | Club Caldera (`island`) | Agamo (0), Magmo (0) | vulcão atira lava em todos os slots | genérico |
| 9 | Tsunopolis (`tokyo`) | Ultra V (1), Ultra V (2) | `AiScriptTokyo`, onda, `UfoTokyo` | `ais[0]` e `ais[1]` fora **e jogador 1 vivo** → cinema 0x2266 (o disco leva o jogador 1) |
| 10 | UFO (`ufo`) | 3× Zorgulon (0) | — | `ais[0..2]` fora → cinema 0x2267 |
| 11 | Capitol (`capitol`) | jelly (0x1C0), final (0x1E0) | `FinalBoss`: Mecha (fase 0) → jelly (1) → final (2) → encerramento (3, cinema 0x226D, `GenesisMovie`) | pelo `FinalBoss` (fim da campanha) |

"Genérico" = `TheGame::gameResolveLifeAndDeath` (0x138068), caso 1: com todas as IAs fora (`m_unkF6`), põe **o jogador 1** na pose de
vitória e, quando ela acaba (`m_unkF7`), `rtReturnToShell(0)`. As fases 3, 6, 9, 10 e 11 ficam fora desse teste e acabam pelo script.

## 2. Slots, memória e carga
- **Slots:** cada monstro carregado é um slot (`AddMonster`), jogadores primeiro (`InitPlayers`), depois IAs. Com N jogadores:
  N + IAs da fase; o pior caso é **8** (fases 3 e 6: 4 + 4), no Capitólio 4 + 2 = 6. Cabe nos 16 `m_slots` e nas 4 vagas de IA de
  `m_monsters[4..7]` (nenhuma fase passa de 4 IAs). As IAs continuam em `ais[0..]` com 4 jogadores, então os scripts que leem
  `ais[i]` por índice fixo não mudam de alvo.
- **Carga:** jogadores 3 e 4 já passam pelo caminho das IAs na carga (`wotm_split4.inc`, `toLoadLayout`); a aventura troca
  `m_monsterSel[4..]`/`m_costume[2..]` entre fases no `EvaluateOnePlayerStoryStatus`, **depois** do `toPlayLayout`, então a ordem
  bate. Conferir só a exceção do `m_mode` 6 no gancho (a aventura é modo 1, entra no caminho normal).
- **RAM:** com `WOTM_HIGHRAM` (padrão) 8 monstros cabem (medido no Metro, 21 MB de folga). Capitólio: `FINAL0.NGP` 2,5 MB, o maior
  do disco, mais o `CAPITOL.NGP` com o Mecha; deve caber, não medido. Com `WOTM_HIGHRAM=0` as fases 3 e 6 com 8 monstros estão perto
  do teto do PS2 (Vegas/SF ficam com ~2 MB; Metro passa).
- **Tetos a conferir no log da carga** (não medidos na aventura): `texInfo[1000]` (o `ASSBOSS1` traz 836 KB de `.TEX`; Canyon com 4
  jogadores + 3 `MANTIS1` + `ASSBOSS1`), VRAM residente das `.RTX` (o jogo **sai** se passar) e a cache de `.TEX` (só `printf`).
  Com 3 e 4 jogadores o `getVramAddr` usa o layout do "modo 30 Hz" (`Use30HzMode`), que o port mantém: é nele que os tetos valem.
- **Som:** com mais de 3 slots o `loadRTSoundBanks` (0x1B1FF0) carrega os bancos "lo-res" dos monstros; o banco do chefe é pedido à
  parte (`loadMechBossSoundBank` no Canyon, `Final_s.bnk` no Capitólio). Sem mudança necessária.

## 3. O que mira só o jogador 1
"P1" = `m_monsters[0]` (`players[0]`). Coluna "alvo" = jogador escolhido pela regra a decidir (perguntas).

### 3.1 Vitória, derrota e vidas (vale para todas as fases)
| Função | Endereço | O que assume | O que mudar | Risco |
|---|---|---|---|---|
| `TheGame::gameResolveLifeAndDeath` (caso 1) | 0x138068 | qualquer jogador morto → `rtReturnToShell(1)`; vitória só do P1 (se o P1 estiver morto, ninguém faz a pose e a fase não acaba) | morto com vidas: renasce sozinho sem voltar ao Shell; vitória quando todas as IAs saem, pose de quem estiver vivo | médio (precedente: `wotm_modes4.inc` já embrulha este caso para o modo 6) |
| `Shell::EvaluateOnePlayerStoryStatus` | 0x1A7C08 | só `m_lives[0]`; renasce o slot `m_viewSlot[0]` (o P1) mesmo quando quem morreu foi outro → o morto continua morto e cada quadro gasta uma vida do P1 até o fim de jogo; "game over" quando o P1 acaba as vidas | vidas por jogador (`m_lives[0..3]` já existem, o `InitPlayerLives` enche as 4 com 3); renascer o slot do morto; fim de jogo quando todos estiverem sem vidas; mensagens 0x25/0x26 (vidas) no HUD do morto | médio |
| idem, pontuação | idem | bônus e total pelo `m_tokens[0].grandTotal()` (só os tokens do P1) | somar os 4 `TokenManager` ou mostrar por jogador | baixo |
| `TheGame::gameGetStartPoint` | 0x138D68 | jogador na 1ª vida vai à lista 0 (no geral um ponto só): os 4 nascem no mesmo lugar; 3-Mile depois da cratera: ponto fixo para todos | deslocar os jogadores 2–4 (lista 2/3, ou um anel em volta do ponto); o `wotm_split4` já faz isso para renascimento com mais de 4 | baixo |
| `Monster::cleanUpForMovie` chamado só no P1 | 0x162E90 | durante cinemas só o P1 perde capa, fogo, retículas | chamar para todos os jogadores (vegas, ufo, tokyo, canyon2, 3-Mile, plant, final) | baixo, cosmético |
| `ActionDispatch::doSetPlayerPos` | 0x100D40 | os roteiros de cinema posicionam o jogador pelo índice (o P1); os outros ficam onde estavam e aparecem/somem errado na cena | depois do cinema (e no começo), trazer os jogadores 2–4 para perto do P1 ou para pontos de início | médio, cosmético |
| `inputSetActuator(0, …)` (Mecha, mordida da planta) | — | vibração só no controle 0 | vibrar o controle do atingido | baixo |
| `Hud::drawBossHealthBar` / `addHealthIndicators` / `updateTextBoxMessage` | 0x1452A0 / 0x144948 / 0x146030 | barra do chefe montada na câmera 0 (ou na unificada); caixas de texto da aventura (power-ups 10–19) só lidas para o P1 | conferir no HUD 2/3 do `wotm_split4`; texto de dica para quem pegou | baixo |

### 3.2 Por fase
| Fase | Função | Endereço | O que assume | O que mudar | Risco |
|---|---|---|---|---|---|
| 2 | `vegasUpdate` | 0x1DD7B0 | gatilho pela morte de `ais[0]`; cinema limpa só o P1 | nada além do `cleanUpForMovie` | baixo |
| 3 | `Monster::takeHit` (Goliath) | 0x15F048 | golpe comum no 0x1A0 só conta se o atacante for o P1 (`m_id` do `m_monsters[0]`) | aceitar qualquer jogador (`m_playerNum` 1); o gancho do Goliath jogável (`wotm_custom.inc`) já passa por esse teste | baixo |
| 3 | `Ai::updateAssBoss` | 0x103C30 | esquiva quando o **P1** está perto (250) ou há arma em 300 | usar o jogador mais perto | baixo |
| 3 | `StateTopSpin::update` / `acceptHit` | 0x1CC4A8 / 0x1CCAB0 | o giro só acerta e empurra o P1; perto do P1 (250) muda o estado do chefe | todos os jogadores no alcance; precedente: o mesmo gancho troca o alvo para o Goliath jogador | baixo |
| 3 | `StateCannonHands::update` | 0x1CCED8 | os mísseis teleguiados miram o P1 (cabeça e alvo do `CreateStaticWeapon`) | alvo por salva (perto ou rodízio) | baixo |
| 3 | `updateRespawn` / `canyon2RespawnAi` | 0x121D28 | exatamente 3 formigas em `ais[0..2]` (timers `[3]` fixos); renascidas olham o P1 | nada se as IAs da fase continuarem 3+1; **não** dá para pôr mais formigas sem reescrever | médio se mexer no número |
| 3 | `canyon2BossIntro` | 0x122320 | desliga `ais[0..2]`, liga o chefe, limpa o P1 | `cleanUpForMovie` de todos | baixo |
| 6 | `threeMileInitAi` | 0x1D1368 | põe só o P1 na torre central no começo | posicionar os outros em volta | baixo |
| 6 | `AiScript3Mile::update` / `enterState` | 0x10F030 / 0x10F2A8 | estado 3 sai quando o P1 chega a 200 de uma IA; laço sobre as 4 IAs | qualquer jogador | baixo |
| 6 | `threeMileUpdateRespawn` | 0x1D2788 | exatamente 4 IAs (`ais[0..3]`, `stackStart[4]`); limite de 10 renascimentos | escalar o limite com N; não mudar o número de IAs | médio |
| 6 | `threeMileUpdateCountDown` (fim das chaminés) | 0x1D1CE8 | ao abrir a cratera só o **P1** é levado ao ponto fixo; a água desce (−2000), os outros ficam onde estavam (podem cair no buraco) | levar todos, com deslocamento | **alto** (fica preso/fora do mapa) |
| 6 | `PlantBoss::update` | 0x183560 | P1 morto reinicia o ciclo de ataque e mexe na câmera 0; cinemas limpam o P1 | só reiniciar com todos mortos; câmera do morto | médio |
| 6 | `PlantBossHead::update` | 0x184560 | olha, morde, mastiga e cospe **só o P1**; os outros nunca são mordidos (só batem) | alvo por cabeça (mais perto do pino) a cada subida (estado 1); `Cameras::InitPlantBossCamera` só na câmera 0 | médio |
| 6 | `PlantBossRoot::wakeUp` / `update` | 0x185A38 / 0x185DE0 | raízes nascem em volta do P1 e só ferem o P1 | sortear o jogador por raiz; dano a quem estiver na ponta | baixo |
| 6 | `PlantBossBase::update` | 0x1863D0 | o tapa que arremessa pega só o P1 | jogador mais perto | baixo |
| 9 | `tokyoUpdate` | 0x1D6B88 | fim exige o P1 vivo; no cinema só o P1 é abduzido (sobe e some entre os quadros 260 e 630) | "algum jogador vivo"; levantar todos os jogadores vivos (ou esconder os outros) | médio, cosmético |
| 10 | `ufoUpdate` | 0x1D74F8 | fim por `ais[0..2]`; limpa só o P1 | `cleanUpForMovie` de todos | baixo |
| 11 | `FinalBoss::updateMecha` | 0x1320D8 | `m_monster7C` = P1, gravado **uma vez** no estado 1; torre gira, anda (y do P1), mira o locator 0x809 do P1; só o P1 leva o dano do laser (0,5/0,575/0,65 por field, pela dificuldade) e o empurrão; outro jogador no feixe vira "destrutível" (`creditHit` até 5, depois `dispatchHit` 5,01) | trocar `m_monster7C` a cada ciclo (estado 8 → 2, ou ao morrer o alvo); dano do feixe a qualquer jogador; P1 morto encerra o disparo (`m_timer = 1`) → o mesmo para o alvo | médio |
| 11 | `FinalBoss::updateJelly` | 0x133538 | a jelly persegue e dá choque no `m_monster7C`; o `StateUltraTazer::update` (0x1CD7A0) também lê `m_monsters[0]` (provável: alvo do choque) | alvo = `m_monster7C` trocado a cada volta à poça (estado 4 → 1) | médio |
| 11 | `FinalBoss::update` (fase 2/3) | 0x131CC0 | o final (0x1E0) é IA comum: no modo 1 as IAs atacam qualquer não-IA (`s_attackOtherAi` 0, `Ai::globalInit`); o encerramento limpa só o `m_monster7C` | só o `cleanUpForMovie` | baixo |
| 11 | `FinalBoss::takeHit` | 0x133FB8 | só no estado 7 (escudo baixo, 240 fields) e só projétil/arremesso; aceita de qualquer um | nada; 4 jogadores derrubam o Mecha bem mais rápido (escala de vida) | — |

Comum a vários scripts: o "atalho de teste" do byte `+0xFD` do P1 (mata a fase) em `vegas`/`ufo`/`tokyo`/`sanfran`/`3-Mile`/
`canyon2`/`FinalBoss`. Inofensivo.

### 3.3 Como trocar o alvo
- **Troca de `m_monsters[0]` durante a chamada** (gancho de entrada/saída), como o `wotm_custom.inc` faz no `StateTopSpin::update`:
  serve para `PlantBoss*`, `updateAssBoss`, `StateTopSpin`, `StateCannonHands`, `StateUltraTazer`, `AiScript3Mile`. Cuidado: na
  mesma função o P1 também é usado para câmera 0, `cleanUpForMovie` e o byte de teste; com a troca esses passam ao alvo.
- **Campo próprio:** no Capitólio basta regravar `finalBoss.m_monster7C` (já é um ponteiro, lido em todo o Mecha e a jelly).
- **C++ por gancho:** `FinalBoss` e `PlantBoss` já estão em C++ equivalente; o laser e a mordida podem virar código legível com
  "alvo" como parâmetro, que também serve de base para mod.
- Não usar o `m_gameMode` para separar a campanha de N jogadores: tudo acima (militares no `createPickup`, `Ai::isMinion`,
  `s_attackOtherAi`, `AiScript3Mile`, `centralInitAfter`, `canyon2InitAfter`, `threeMileInitAi`) testa `m_gameMode == 1` /
  `m_mode == 1`. O discriminante é `m_numPlayers > 1` com `m_mode` 1. (Existe um modo 5 "co-op" no `EvaluateGameStatus`, mas o
  `EvaluateTwoPlayerCoopStatus` do retail é vazio e nenhum script conhece o 5: resto de um modo cortado, não serve de base.)
  Tela dividida no modo 1 é da frente do `m_gameMode` (o `gameReestablishViews` só divide nos modos 3, 6 e 7).

## 4. Dificuldade com mais jogadores
O que o jogo já escala por `TheGame::m_matchMode` (0/1/2, fácil/médio/difícil) e seria o lugar natural de escalar por N:
| Ponto | Hoje | Sugestão |
|---|---|---|
| Vida do Mecha | `100 + 75·mm` (`FinalBoss::initBefore` 0x131918 e `InitPlayers` ao reiniciar) | × (1 + 0,5·(N−1)) |
| Vida de cada cabeça da planta | 20 fixo (`PlantBossHead::initBefore`) | idem |
| Vida do Goliath, jelly, final e IAs | vida máxima do monstro (`+0x448`); limiares do Goliath em fração (`assBossTier1/2Health`, não mudam) | multiplicar o máximo no `aiInit` dos chefes |
| Dano do chefe | laser 0,5–0,65/field; raiz `1 + 0,5·mm`; mordida até 25 | manter (cada jogador apanha sozinho) |
| Velocidade da cabeça da planta | anim 1 a 1,75/2,5/3,0 | manter |
| IAs da fase | `m_levelNumAIs` (1–4) | **+1 IA por jogador extra só nas fases genéricas (1, 4, 5, 7, 8)**, anexada depois das do roteiro e até 4 em `ais[]`; nas fases 2, 3, 6, 9, 10, 11 os scripts contam índices fixos (`ais[0]`, `[0..2]`, `[0..3]`) e a IA extra não entraria no fim da fase |
| Renascimentos | formigas sem limite; Kineticlops até 10 | limite × N no 3-Mile |
| Ajustes de IA | `AiScript3Mile` no difícil põe 20 em três pesos | — |

## 5. Ordem sugerida (do mais barato ao mais caro)
1. Vidas e renascimento por jogador + vitória com o P1 morto (`gameResolveLifeAndDeath` caso 1, `EvaluateOnePlayerStoryStatus`). Sem
   isso nenhuma fase funciona com 2+.
2. Pontos de início e `cleanUpForMovie`/posição de todos nos cinemas.
3. 3-Mile: levar todos na abertura da cratera (o único caso que prende jogador).
4. Alvo dos chefes: Capitólio (`m_monster7C`), planta, Goliath.
5. Escala de dificuldade.

## Perguntas para o dono
1. **Vidas:** cada jogador com 3 vidas próprias, ou um estoque comum? Jogador sem vidas fica de fora até a próxima fase (e volta nela)
   ou a equipe perde quando um acaba?
2. **Fim de jogo:** só quando todos estiverem sem vidas? E o "continue" (diálogo 1) recomeça a fase para todos?
3. **Alvo dos chefes:** mais perto, aleatório por ataque, rodízio entre os vivos, ou "quem bateu por último"? (Vale para o laser do
   Mecha, a mordida da planta, os mísseis e o giro do Goliath.)
4. **Escala:** vida dos chefes × (1 + 0,5 por jogador extra) está bom? Quer IAs a mais nas fases sem roteiro (1, 4, 5, 7, 8)?
5. **Cinemas:** os jogadores 2–4 aparecem nas cenas (ao lado do 1) ou somem durante elas? Em Tóquio o disco leva todos?
6. **Pontuação e save:** pontos somados da equipe ou por jogador? O save da aventura guarda só o monstro do jogador 1 (e o progresso),
   ou os de todos?
