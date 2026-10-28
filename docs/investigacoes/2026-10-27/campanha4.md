# Campanha com até 4 jogadores (TODO 13)

Junta as quatro frentes do levantamento (fluxo da aventura, cinemas e vistas, chefes e fases, menu e save; os textos completos estão em
`23b8a40:docs/investigacoes/2026-10-27/campanha4_0{1..4}_*.md`) com o andamento. O que já está no código está descrito em
`docs/passos/NATIVO.md`, "Campanha com até 4 jogadores": entrada, tela dividida, tela com quem sobra, largada, vidas, vitória,
tokens, sem tutorial. Aqui fica o resto: o que falta, com os endereços (`native/wotm_syms.h`), e os testes.

## 1. Andamento e testes

| Parte | Estado |
|---|---|
| Entrada pela tela 5, seleção de 2 (retail) e de 3 e 4 (telão), direto à fase 1 | testada pelo dono com 2 jogadores |
| Tela dividida (metades com 2, quadrantes com 3 e 4) | testada com 2 e 3 |
| Largada lado a lado (chão conferido pelo `hdHatTest`) | testada com 2 |
| Vidas por jogador, renascer sem esmaecer, sair sem vidas, game over com todos fora | testado com 2 e 3 |
| Sem tutorial na fase 1 | testado |
| Tokens do jogador 1 × 1,2/1,3/1,4 | escrito, não conferido no placar |
| Tela com quem sobra (vistas remontadas, pick-up verde da vista, HUD) | escrito, falta o teste do dono |
| Menu de debug: vida baixa (F1, 7 e 8) | pronto |

**Do último teste do dono** (3 jogadores), corrigido no código e esperando o teste seguinte:
- Pick-up verde não aparecia para o P2 depois de ele ficar com a tela inteira: o `LevelPickups::enableHighlights(v)` usa
  `m_monsters[v]` e apaga o realce do `m_monsters[1 - v]`.
- Com 3, quando sobravam 2 a tela não virava metades (o quadrante de quem saiu passava a seguir outro jogador).
- HUD dos inimigos bugado na tela do P3 sozinho. Hipótese: o `Hud::addHealthIndicators` (0x144948) desenha um ícone para cada
  `m_monsters[0 .. m_numMonsters)` com `m_playerNum != 0` que não é o dono do HUD, e quem saiu continuava na lista. Sem captura
  do bug; se continuar, pedir uma.

**Aberto:** usar a vida baixa no P1 bagunça o HUD do P3 com o P1 ainda vivo (3 jogadores). Suspeita: o HUD 2 é cópia do HUD 0 e as
animações da coluna são compartilhadas (`hudUse`/`hudAnimSave` do `wotm_split4.inc`); o ícone de vida baixa do P1 no HUD do P3 pode
ser só o ícone de parceiro do retail. Precisa de captura.

## 2. Chefes: alvo sorteado entre os jogadores vivos

Decisão do dono: cada ataque mira um alvo sorteado entre os vivos, sem vida extra para o chefe. Hoje todos miram o P1
(`m_monsters[0]`).

| Fase | Função | Endereço | O que assume |
|---|---|---|---|
| 3 | `Monster::takeHit` (Goliath) | 0x15F048 | golpe comum no 0x1A0 só conta vindo do P1 (`m_id` do `m_monsters[0]`) |
| 3 | `Ai::updateAssBoss` | 0x103C30 | esquiva quando o P1 está a 250 ou há arma a 300 |
| 3 | `StateTopSpin::update` / `acceptHit` | 0x1CC4A8 / 0x1CCAB0 | o giro só acerta e empurra o P1 |
| 3 | `StateCannonHands::update` | 0x1CCED8 | mísseis teleguiados no P1 |
| 3 | `canyon2BossIntro` | 0x122320 | `cleanUpForMovie` só no P1 |
| 6 | `threeMileInitAi` | 0x1D1368 | só o P1 vai à torre central no começo |
| 6 | `AiScript3Mile::update` / `enterState` | 0x10F030 / 0x10F2A8 | estado 3 sai quando o P1 chega a 200 de uma IA |
| 6 | `threeMileUpdateCountDown` | 0x1D1CE8 | ao abrir a cratera só o P1 é levado ao ponto fixo; a água desce e os outros ficam onde estavam (podem cair no buraco). O mais grave: prende jogador |
| 6 | `PlantBoss::update` | 0x183560 | P1 morto reinicia o ciclo e mexe na câmera 0 |
| 6 | `PlantBossHead::update` | 0x184560 | olha, morde e cospe só o P1; `Cameras::InitPlantBossCamera` (0x11CEC8) só na câmera 0 (usar a do `m_cameraView` do mordido) |
| 6 | `PlantBossRoot::wakeUp` / `update` | 0x185A38 / 0x185DE0 | raízes nascem em volta do P1 e só o ferem |
| 6 | `PlantBossBase::update` | 0x1863D0 | o tapa só pega o P1 |
| 9 | `tokyoUpdate` | 0x1D6B88 | fim exige o P1 vivo (passa a ser "algum jogador vivo"); no cinema só o P1 é abduzido |
| 11 | `FinalBoss::updateMecha` | 0x1320D8 | `m_monster7C` = P1, gravado uma vez no estado 1; só ele leva o laser; P1 morto encerra o disparo |
| 11 | `FinalBoss::updateJelly`, `StateUltraTazer::update` | 0x133538, 0x1CD7A0 | perseguição e choque no `m_monster7C` / `m_monsters[0]` |

Como trocar o alvo:
- **`m_monsters[0]` trocado durante a chamada** (molde: o `StateTopSpin` do `wotm_custom.inc`): serve para `PlantBoss*`,
  `updateAssBoss`, `StateTopSpin`, `StateCannonHands`, `StateUltraTazer`, `AiScript3Mile`. Na mesma função o P1 também serve para a
  câmera 0, o `cleanUpForMovie` e o byte de teste `+0xFD`; com a troca eles passam ao alvo.
- **Campo próprio:** no Capitólio basta regravar `finalBoss.m_monster7C` a cada ciclo (estado 8 → 2 do Mecha, volta à poça 4 → 1
  da jelly, ou na morte do alvo).
- `FinalBoss` e `PlantBoss` já estão em C++ equivalente: o laser e a mordida podem virar código legível com o alvo como parâmetro.
- 0x1A0, 0x1C0 e 0x1E0 são códigos de monstro (Goliath, jelly, final), não slots. O Mecha e a planta não são monstros.

## 3. Cinemas

- Os roteiros animam só `m_monsters[0]` (`ActAnim` com `ACTANIM_PLAYER`, `doSetPlayerPos` 0x100D40); os outros jogadores ficam
  parados onde estavam, visíveis na cena. Falta listar os `obj` que os roteiros da aventura usam.
- `cleanUpForMovie` (0x162E90) só no P1: chamar para todos em vegas, ufo, tokyo, canyon2, 3-Mile, planta e final.
- Pular: o `updateCinema` lê só os controles 0 e 1 (`inputGetInput(8, 0)` e `(8, 1)`); decisão: qualquer controle.
- `endCinema` (0x100B20) refaz só as câmeras 0 e 1 (`+0xD98`/`+0xD90` = 1); as 2 e 3 também precisam.
- Barra do chefe: o `drawBossHealthBar` (0x1452A0) põe a barra na frente da câmera 0; nas outras vistas ela fica fora do lugar.
  Gancho: trocar o `_cs` da câmera 0 pelo da vista em curso durante a chamada.
- Letterbox: um `ActCinema` com o bit 1 deixa o letterbox ligado depois do fim (em quadrantes, só na vista 0). Conferir se algum
  roteiro da aventura usa.
- O cinema em tela cheia e a volta à divisão já existem (`cinemaViews` do split4; com 2 jogadores, o toggle do retail); a tela com
  quem sobra se refaz no quadro seguinte (`applyLayout`).

## 4. Final

`Shell::GenesisMovie` (0x1AB4A0) toca o filme pelo `m_monsterSel[0]` e grava o desbloqueio daquele herói; o `uiMain` (0x1D93D8) o
toca fora do `rtMain`. Decisão: um filme atrás do outro, na ordem dos jogadores (e o desbloqueio de cada um).

## 5. Save

- O `setSaveData` (0x1679A8) força `m_numPlayers = 1` e `m_mode = 1`; o `updateStoryModeLoad` vai do save direto à carga
  (`changeScreen(9, 0x63)`).
- Formato decidido: chave `coop` no slot do `save.json` com os jogadores 2..N por nome; os campos de cima continuam sendo o jogador
  1 (o que o binário guarda). O binário fica igual (um PCSX2 vê uma aventura de 1 jogador).
  ```json
  { "level": "baytown", "monster": "congar", "variant": 0, "lives": 3, "score": 1200, "costume": "congar/0",
    "coop": [ { "monster": "togera", "variant": 0, "costume": "togera/1" } ] }
  ```
- Mudar no `wotm_save.inc`: `Row`, `rowFromRam` (lê `m_numPlayers`, `m_monsterSel[1..3]`, `m_costume[1]` e os trajes 3 e 4 do
  split4), `saveRow`, `buildRows`, `loadRow` (depois do `setSaveData`: `m_numPlayers`, monstros e trajes dos outros) e o
  `adventureToJson`, que troca o slot inteiro quando o binário difere e precisa preservar `coop`.
- Carregar um save co-op: a escolha de controles (fase `Config` do `wotm_padsel.inc`, exigindo N jogadores) e dali à carga.
- Lista de saves: logos dos parceiros e "2P/3P/4P" no quadro.

## 6. Carga e memória

- Pior caso: 8 monstros (fases 3 e 6: 4 jogadores + 4 IAs); no Capitólio, 4 + 2 com o `FINAL0.NGP` (2,5 MB) e o Mecha do
  `CAPITOL.NGP`. Com `WOTM_HIGHRAM` (padrão), 8 monstros cabem no Metro com folga; o Capitólio não foi medido.
- Medir no log da carga: `texInfo[1000]` (Canyon com 4 jogadores + 3 `MANTIS1` + `ASSBOSS1`, que traz 836 KB de `.TEX`) e a VRAM
  residente das `.RTX` (o jogo sai se passar).
- CUSTOM de tipo chefe (Goliath jogável) na seleção da aventura co-op: os chefes 0x1C0/0x1E0 do Capitólio e o 0x1A0 do Canyon são da
  fase; barrar os custom de tipo chefe na casa CUSTOM da seleção.

## 7. Depois

- Entrar e sair do grupo entre fases (o "salvar?" do `wonLevelOverlay` é o ponto natural).
- Dificuldade por número de jogadores (não decidida): vida do Mecha `100 + 75·mm` (`FinalBoss::initBefore` 0x131918), cabeça da
  planta 20 fixo, IAs a mais só nas fases genéricas (1, 4, 5, 7, 8), limite de renascimento do 3-Mile (10).
- Quem sai sem vidas fica escondido mas continua no mundo; se as IAs o atacarem, marcar o monstro como fora para elas.
