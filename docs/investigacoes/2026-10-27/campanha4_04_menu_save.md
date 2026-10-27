# Campanha com até 4 jogadores: entrada, menu, save e progressão

Frente 04 do item 13 do `TODO.md`. Só leitura (código, asm, docs); nada foi rodado. Endereços de `native/wotm_syms.h`.
Outras frentes (vistas, cinemas, chefes, morte em jogo) aparecem aqui só onde tocam no menu ou no save.

## 1. Como a aventura começa hoje

- **Tela 1 → 2:** `screenMain` (0x18DC88) item 1 põe `m_numPlayers = 1` e vai à 2. O item 2 (multiplayer) exige
  `inputIsCtlAvailable(1)` e vai à 5.
- **Tela 2 (`screenGameModes1P`, 0x18F040):** item 1 (ADVENTURE) faz `m_mode = 1`, `m_levelNum = 1` (salvo `UseCommandLineLevel`),
  `m_numAIs = m_levelNumAIs[1]` e `changeScreen(2, 3)`. No port a lista é texto (`wotm_mainmenu.inc`, `kScreens`); o TRAINING é uma
  linha nossa (`kTrainingRow`, item < 0) que arma uma flag e segue como item do jogo. É esse o molde de "item extra" que já existe.
- **Tela 3 (`screenCharSelect1P`, 0x18F628):** na 1a passada com `m_mode == 1` copia as IAs da fase (`levelMonsters`/
  `levelMonsterModels` → `m_monsterSel[4+i]`, `m_costume[2+i]`) e zera `Shell+0x2B2C` (pontos acumulados) e `Shell+0x2C18` (flag
  "veio de um save"). X grava `m_monsterSel[0]`/`m_costume[0]` e, no modo 1, vai **direto à carga** (`changeScreen(3, 0x63)`), sem
  seleção de fase. No port a tela 3 é o procurador nativo de 1 quadro (`wotm_padsel.inc`, `soloInput`/`soloSync`, com RANDOM e CUSTOM).
- **Carregar (tela 9, `screenLoadSave` 0x19BD00):** `MonsterMc::updateStoryModeLoad` (0x168958) → modal nativo de saves
  (`wotm_save.inc`, `loadRow` → `MonsterMc::setSaveData` 0x1679A8). O `setSaveData` **força `m_numPlayers = 1` e `m_mode = 1`**, põe
  fase, `m_monsterSel[0]`, `m_lives[0]`, `m_costume[0]`, os pontos em `+0x2B2C` e as IAs da fase; o `updateStoryModeLoad` liga
  `+0x2C18 = 1` e faz `changeScreen(9, 0x63)`: do save à carga, sem passar por escolha de controles.

Multijogador hoje (`wotm_padsel.inc`): tela 5 abre a **escolha de controles** (fase `Config`, Start com ≥ 2 jogadores, mapa manual
`wotm_pad::g_manualDevs`, `g_menuPlayers`), depois a lista FREE-FOR-ALL / ELIMINATION / MINIGAMES. Com 2 jogadores a seleção é a
tela 6 do retail sob o procurador; com 3 e 4 (`g_multi`) é a seleção nativa no telão (`selectFinish`), que grava `m_monsterSel[0..N)`,
`m_costume[0..1]`, `wotmMultiSetCostume` (3 e 4) e sai por `changeScreen(6, 8)`. O mapa manual só cai no `screenMain` e no
`screenWaitForStart` (`mainHook`/`waitStartHook`): **sobrevive às trocas de fase da campanha**, o que é bom.

## 2. Por onde entra "aventura com N jogadores"

### Proposta A (recomendada): linha ADVENTURE na lista da tela 5
- `kScreens[5]` ganha `{kAdventureRow, "ADVENTURE"}` (hoje `hasExtra` só vale para 1 e 2; estender para 5). X nela, no gancho do
  `screenGetInput` (0x1A58A0) da tela 5: `m_mode = 1`, `m_levelNum = 1` (respeitando `UseCommandLineLevel`), `m_numAIs =
  m_levelNumAIs[1]`, zera `+0x2B2C` e `+0x2C18`, arma `g_advCoop = N` e manda à tela 6 (como o retail faz com o item 1 → 13).
- Seleção: com 2, a tela 6 do retail (o `screenCharSelect2P` com `m_mode == 1` cai no ramo "não 2, não 6", o dos dois humanos; a 1a
  passada zera `m_costume[0..1]`); com 3 e 4, a nativa com `g_selCpus = 0` (zerar `g_want`; sem quadros de CPU).
- `changeScreenHook` (0x1A5718): com `g_advCoop`, `6 → 8` vira `6 → 0x63` (a aventura não tem seleção de fase) e, antes, as IAs da
  fase são recopiadas em `m_monsterSel[4..]`/`m_costume[2..]` (o `selectFinish` zera `m_monsterSel[g_multi..12)`, ou seja, apaga as
  IAs: tem de vir depois dele). A volta da 6 vai à lista da 5.
- Prós: reaproveita a escolha de controles e as duas seleções sem tocar no fluxo de 1 jogador; o mapa manual vale a campanha toda.
  Contras: a aventura aparece em dois lugares (SINGLE PLAYER e MULTIPLAYER); com 2 jogadores a tela 6 do retail precisa do desvio
  para 0x63 e o "taken" dos trajes (`WOTM_SEL_REPEAT`) vale como no free for all.
- Teto: 4 jogadores + IAs da fase cabem nos 8 slots (`m_levelNumAIs` vai até 4: fases 3 e 6, com chefe). `m_monsterSel[0..3]` são
  jogadores, `[4..7]` IAs, como no free for all.

### Proposta B: escolha de controles depois de SINGLE PLAYER → ADVENTURE
- O X em ADVENTURE (tela 2) abre a fase `Config` do `wotm_padsel` no telão; Start com 1 só segue o retail (tela 3), com 2+ vai à 6.
- Prós: a aventura fica num lugar só. Contras: um passo a mais para quem joga sozinho (dá para pular quando só há 1 dispositivo);
  a fase `Config` hoje é presa à tela 5 (`g_phase`, `g_reveal`, o "voltar" injeta o código 5 do `screenGameModes2P`).

### Proposta C: entrar na hora ("drop-in") na tela 3
- Na seleção de 1 jogador da aventura, outro dispositivo aperta X e ganha um quadro (até 4). Prós: zero menu novo, estilo fliperama.
  Contras: a tela 3 é procuradora de 1 quadro; o mapa de dispositivos teria de nascer ali (hoje só a fase `Config` o cria).

## 3. O save

**Retail (`savegame.dat`, `FORMATOS.md`):** slot de 0x3C, usa 0x14: fase, `m_monsterSel[0]`, `m_lives[0]`, pontos (`+0x2B28 +
+0x2B2C`), `m_costume[0]`. Sem campo para jogadores 2..4. As vidas gravadas não valem nada: todo começo de sessão roda
`InitRTState` → `InitPlayerLives` (`NUM_LIVES[1] = 3` para os 4 jogadores).

**Nativo (`wotm_save.inc`):** a gravação da aventura na fase não passa pelo binário: o modal do `McChooseSlot` (update 0x169BD0)
faz `rowFromRam` → `saveRow` direto no `save.json`; o carregamento monta um slot 0 e chama o `setSaveData`. O binário do cartão é
refeito do JSON no `stdInit` (`jsonToAdventure`, só os campos do retail) e lido de volta no `stdExit` (`adventureToJson`).

**Extensão proposta** (sem mexer no binário nem nos campos de hoje):
```json
{ "level": "baytown", "monster": "congar", "variant": 0, "lives": 3, "score": 1200, "costume": "congar/0",
  "coop": [ { "monster": "togera", "variant": 0, "costume": "togera/1" },
            { "monster": "magmo",  "variant": 0, "costume": "magmo/2" } ] }
```
- Os campos de cima continuam sendo o jogador 1 (o que o binário guarda); `coop` são os jogadores 2..N, por nome (skin de mod
  ausente → traje 0, como o `resolveCostume`). Sem `coop` = 1 jogador. Opcional: `"scores": [...]` por jogador, só para a lista.
- O cartão (e o `mcsave.py`, e um PCSX2) vê uma aventura de 1 jogador: degradação aceitável.
- Mudar: `Row` (+ jogadores extras), `rowFromRam` (lê `m_numPlayers`, `m_monsterSel[1..3]`, `m_costume[1]` e os trajes 3/4 do
  `wotm_split4`), `saveRow`, `buildRows`, `loadRow` (depois do `setSaveData`: `m_numPlayers = N`, `m_monsterSel[1..N)`,
  `m_costume[1]`, `wotmMultiSetCostume`), e o `adventureToJson`, que hoje troca o slot inteiro por `Value::object()` quando o binário
  difere: tem de **preservar `coop`** (e no caso "mesmo slot" já preserva).
- Carregar um save de N jogadores pelo menu principal cai na carga sem escolha de controles. Opções: (1) o `changeScreenHook` desvia
  `9 → 0x63` para a fase `Config` (exigindo N jogadores) e daí à carga; (2) mapa padrão (controles na ordem + teclado) se houver
  dispositivos bastantes, senão a escolha. (1) é mais previsível.
- Lista de saves: o quadro da esquerda mostra o monstro 3D do P1; acrescentar os logos (ou retratos) dos parceiros e "2P/3P/4P".

**Tokens e destraves:** os destraves (`monsterSelectMode[].state`) e o `battleTokens` (`Shell+0x2B24`) são **um só perfil** no retail;
não há por jogador. O jogo já tem um `TokenManager` por jogador (`game+0x11C370 + i*0x1004`, creditado pelo `Monster+0x24` do monstro
do jogador: predios, pedestres, armas), mas o `EvaluateOnePlayerStoryStatus` só soma o `grandTotal` do [0]: em co-op os tokens dos
outros se perdem. Gancho barato: no `TokenManager::grandTotal` (0x1D67E8) com `$ra` dentro do `EvaluateOnePlayerStoryStatus`
(0x1A7C08..0x1A7FD4), devolver a soma dos N (o `wotm_modes4.inc` já chama o `grandTotal` por jogador). Assim `+0x2B28` (pontos da
fase), o `save.json` e os `battleTokens` saem certos sem reescrever a função.

**Vidas e continues:** `m_lives[4]` já existe e o `InitPlayerLives` enche os 4. O `EvaluateOnePlayerStoryStatus` só olha `m_lives[0]`,
revive o slot `m_viewSlot[0]` (`game+0x1203E8`) e manda as mensagens "2/1 vidas" só ao HUD 0. Esgotou: `displayDialog(1)` →
`lostLevelOverlay` (0x1ADF40): GAME OVER, REPLAY / QUIT (continues infinitos: REPLAY recomeça a fase). Para N jogadores é preciso
decidir quando o `rtMain` devolve 1 (frente de jogo) e reescrever esse trecho: vidas por jogador (morto sem vidas assiste até o fim da
fase e volta na próxima) ou um pote comum; GAME OVER só com todos fora. Nada disso precisa ir ao save (as vidas recomeçam a cada fase).

## 4. Telas entre fases

Não há mapa nem loja entre as fases: a compra é na tela 21 (`screenUnlocker`), pelo menu principal.
- Fase vencida: `EvaluateOnePlayerStoryStatus` (r 0) soma o bônus, avança `m_levelNum`, copia as IAs da próxima, credita pontos e
  abre `displayDialog(2)` → `wonLevelOverlay` (0x1AEDC0) → `updateStoryModeSave` (0x168708: "salvar?" e o modal nativo) →
  `DisplayLoadBackground` (0x1AADD0: só lê `m_levelNum`, não depende de jogadores) → próxima sessão (`InitRTState`, `InitPlayers`).
- Fim: `displayDialog(3)` → `wonStoryOverlay` (0x1ADEC0) → `updateEndOfStorySave` (0x168280, já automático no JSON) e
  `GenesisMovie` (0x1AB4A0), que escolhe o filme do final pelo `m_monsterSel[0]` (só o P1).
- Assumem 1 jogador: o `setSaveData` (`m_numPlayers = 1`), o `EvaluateOnePlayerStoryStatus` (vidas, revive, HUD 0, tokens do [0]),
  o `GenesisMovie`, a tela 3 (o modo 1 pula a seleção de fase), o `InitPlayers` (`m_costume[i != 0]`, já contornado pelo
  `wotm_split4` para 3 e 4). As caixas de diálogo (`displayDialog`) leem o pad do controle 1; o `wotm_pausemenu.inc` já trata pausa e
  diálogos, conferir quem confirma o "salvar?" com o mapa manual.

## 5. Pontos de gancho

| Função (endereço) | O que mudar | Risco |
|---|---|---|
| `screenGetInput` (0x1A58A0), `wotm_mainmenu` `kScreens`/`hasExtra` | linha ADVENTURE na tela 5 (proposta A) ou fase `Config` na 2 (B) | baixo; o molde é o TRAINING (`wotm_mainmenu.inc` está com mudança não commitada do dono) |
| `changeScreen` (0x1A5718), `wotm_padsel` `changeScreenHook` | 5 → 6 com modo 1; 6 → 8 vira 0x63 com IAs da fase; volta 6 → 5; 9 → 0x63 com save co-op → `Config` | médio: a ordem com o `selectFinish`, que zera as IAs |
| `screenCharSelect2P` (0x1964D0) | 2 jogadores no modo 1 (ramo dos dois humanos); 3 e 4 pelo `selectFinish` sem CPUs | médio: o modo 1 nunca passou pela tela 6 |
| `MonsterMc::setSaveData` (0x1679A8) via `loadRow` | depois dele, `m_numPlayers` e jogadores 2..N | baixo |
| `McChooseSlot::update` (0x169BD0) / `McLoadGameBoxes::update` (0x16A940) | `rowFromRam`, `saveRow`, `buildRows`, desenho dos parceiros | baixo |
| `MonsterMc::stdInit` (0x167AD8) / `stdExit` (0x167B90) | `adventureToJson` preserva `coop` | baixo, mas perde dados se esquecido |
| `TokenManager::grandTotal` (0x1D67E8), `$ra` no `EvaluateOnePlayerStoryStatus` | soma dos N | baixo |
| `Shell::EvaluateOnePlayerStoryStatus` (0x1A7C08) | vidas por jogador, revive do slot certo, mensagens ao HUD do morto; GAME OVER com todos fora | alto (junto com a frente de jogo: quando o `rtMain` devolve 1) |
| `Shell::GenesisMovie` (0x1AB4A0) | filme do final: P1, sorteio entre os jogadores, ou o que fez mais pontos | baixo |
| `TheGame::gameReestablishViews` (0x139530), `wotm_split4` `splitPlayers` | hoje só modos 3, 6 e 7 dividem a tela: incluir o 1 | frente de vistas |
| `screenMain` (0x18DC88) | o `mainHook` já zera o mapa manual: zerar também `g_advCoop` | baixo |

Fora desta frente, mas que trava a proposta: no Capitólio os chefes 0x1C0/0x1E0 usam slots de jogador (`m_monsters[0..3]`,
`wotm_custom.inc`), e o Goliath (0x1A0) é o chefe de canyon2: monstros custom de tipo chefe como jogador na aventura em co-op devem
ficar de fora da seleção (casa CUSTOM só com os jogáveis, ou sem CUSTOM).

## Perguntas para o dono

1. Onde fica a entrada: ADVENTURE na lista do MULTIPLAYER (A), escolha de controles depois de SINGLE PLAYER → ADVENTURE (B), ou entrar
   apertando X na seleção (C)?
2. Tokens da campanha em co-op: somar os de todos no `battleTokens` (um perfil só, como hoje), ou só os do P1?
3. Vidas: 3 por jogador (quem acaba assiste até o fim da fase) ou um pote comum? GAME OVER só quando todos caem?
4. Carregar um save de N jogadores pelo LOAD GAME: passar pela escolha de controles sempre, ou usar os controles na ordem quando houver
   dispositivos bastantes?
5. Pode entrar ou sair gente entre as fases (o "salvar?" seria o ponto natural), ou o número de jogadores fica fixo no save?
6. Final: filme do monstro do P1, de quem fez mais pontos, ou sorteado?
7. CUSTOM na aventura em co-op: liberar os monstros custom jogáveis e barrar os de tipo chefe, ou deixar sem CUSTOM?
