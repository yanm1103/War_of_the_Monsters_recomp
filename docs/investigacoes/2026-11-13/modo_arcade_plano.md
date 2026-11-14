# Modo Arcade: plano (especificação de 2026-11-13)

Especificação do dono:
- 5 lutas seguidas. A 5ª é o chefe, sorteado entre Goliath Prime e Cerebulon.
- 1 a 4 jogadores, cooperando contra as CPUs.
- O número máximo de CPUs é configurável e depende dos jogadores: o total de monstros é no máximo 8.
- O modo dá battle tokens.

O plano anterior está em `facf8f2:docs/investigacoes/2026-11-11/modo_arcade.md`. Ele previa 7 lutas, só 1 jogador, Goliath na 4ª e Cerebulon na 7ª, com os chefes como CPU no versus.

O trabalho do Antigravity fica só como referência, na branch `wip/arcade-antigravity` (757d911): não linka, e os offsets estão errados.

Legenda: **[C]** confirmado lendo o código ou o asm; **[I]** inferência que ainda falta validar.

## 1. O que muda em relação ao plano de 11-11: o chefe luta na fase dele

O levantamento dos chefes (relatório do sub-agente, 2026-11-13) mostra o seguinte:

- **[C] Goliath como CPU no versus não funciona hoje.**
  - O `SetAIMonster` o esconde (`playerNum` 0).
  - O `Ai::updateInputs` troca o `AiBrain` pelo `updateAssBoss`, que depende de dados que só a canyon2 tem: `retreatPts`, `canyon2GetCherryBomb` e `assBossActive`.
  - O `takeHit` dele só aceita golpe do `m_monsters[0]`.
  - Para ele lutar no versus seria preciso uma IA nova (G1/G2 no plano de 11-11).
- **[C] O Cerebulon final como CPU no versus funciona.** Mas o disco não tem `RST/FINAL*.AIC`, então ele luta com a calibragem genérica, sem Mecha, sem Jelly e sem cinema.
- **[C] Na fase de verdade, em modo 1, os dois chefes já funcionam inteiros, inclusive em co-op de 2 a 4 jogadores.**
  - Goliath: Rosdale Canyon, nível 3.
  - Cerebulon: Capitol, nível 11, com Mecha, depois Jelly, depois a forma final.
  - O `wotm_coop.inc` já cuida disso: troca o alvo do Goliath e do `FinalBoss` entre os jogadores vivos, faz o revive e trata os filmes.

**Decisão proposta:** as lutas 1 a 4 são versus (modo 2 com 1 jogador, modo 3 com 2 a 4) e a luta 5 é a fase do chefe da aventura (modo 1, nível 3 ou 11).

Consequência: a luta do Goliath começa com as 3 Mutant Ants e a apresentação, como na história, e a do Cerebulon passa pelo Mecha e pelo Jelly. É mais longa, mas é a luta de chefe como ela é.

## 2. Configuração

- **MAX CPUS** na aba FEATURES (`wotm_settings.hpp` e `wotm_mainmenu.inc`), de 1 a 7. Padrão 3.
- Variável `WOTM_ARCADE_CPUS`, que entra na tabela do `NATIVE.md`.
- O valor usado na corrida é `min(MAX CPUS, 8 - jogadores)`, a mesma conta do `wotm_menucpus::maxFor`.
- CPUs por luta, crescendo: `cpus(n) = max(1, ceil(n * máx / 4))`, para n de 1 a 4.
  - Com máximo 4, as lutas têm 1, 2, 3 e 4 CPUs.
  - Com máximo 1, todas têm 1.
  - Com máximo 7, têm 2, 4, 6 e 7.

## 3. Entrada

- **Tela 2** (1 jogador): linha ARCADE depois de TRAINING (`kArcadeRow`).
  - X arma a flag, no molde do `wotm_training::g_menuArmed`.
  - No `changeScreenHook`, a transição 2 → 12 vira 2 → 6 (seleção).
- **Tela 5** (multijogador): linha ARCADE depois de ADVENTURE.
  - Passa pela escolha de controles (`g_menuPlayers`) e depois vai à seleção, como o `adventureStart`.
  - Bots do menu ficam desligados no arcade.
- **Na seleção**, cada jogador escolhe o próprio monstro (RANDOM vale). Na saída 6 → 8, o condutor:
  - sorteia a corrida inteira (semente guardada);
  - grava a luta 1 no Shell;
  - vai direto à carga (`r5 = 0x63`, como o co-op).
- **A flag desarma** no `mainHook`, no `menuDisarm` e quando a corrida acaba.

## 4. Sorteio

- **Fases 1 a 4:** do pool do RANDOM do versus (`wotm_levelsel`: destravadas e custom com `random`), sem repetir na corrida.
  - **[I]** Metro sem `.AIP` sai do pool quando a luta tem 2 CPUs ou mais, como faz o retail.
- **CPUs:** monstros destravados (`wotm_padsel::columnOpen`) e custom com `"ai": true`, com traje destravado sorteado.
  - Sem repetir dentro de uma luta enquanto houver monstro disponível.
  - Fora os tipos 13 a 15 (chefes).
- **Luta 5:** Goliath (nível 3) ou Cerebulon (nível 11), 50/50.
- **Teste:** `WOTM_ARCADE=<semente>[,<luta inicial>]` parte direto da carga, para testar a luta 5 sem jogar as outras.

## 5. Lutas 1 a 4: co-op contra as CPUs no versus

- **Campos do Shell [C]:**
  - `m_mode` 2 ou 3;
  - `m_levelNum`;
  - `m_numAIs`;
  - `m_monsterSel[4 + i]` e `m_costume[2 + i]` (o mesmo índice do `extraCpus`);
  - `m_killTarget` 0, porque o fim da luta é nosso.
- **Alvos:** `Ai::s_attackOtherAi = 0` durante a corrida, para as CPUs só mirarem nos humanos. No modo 1 o retail já faz isso.
- **Sem dano entre aliados:** gancho no `Monster::takeDamage` (a0 vítima, a2 fonte). Ele recusa humano em humano e CPU em CPU.
  - Fica na cadeia já existente, depois das trapaças e antes do treino.
  - **[I]** Conferir se agarrar e arremessar um aliado passa por outro caminho (`takeHit`).
- **Vida e morte:** gancho no fim de rodada (`EvaluateGameStatus`, modos 2 e 3), com o arcade ligado.
  - Humano morto com vida sobrando: gasta o único respawn da luta (`m_lives`, +0x2BBC) e volta (`playerInit`, com o aviso do HUD como no co-op).
  - Humano morto sem vida: fica fora.
  - CPU morta: fica fora.
  - Todas as CPUs fora: **vitória**.
  - Todos os humanos fora: **derrota**.
- **Próxima luta:** são as escritas do `levelreload::dialogHook`: `m_restart`, `m_inMenus = 1`, `m_inSession = 0`, a pausa em 0, `snd_StopAllSounds` e `gExitDialog = 0`.
  - O `Shell::main` recarrega tudo.
  - As fases custom passam pelo activate/restore do `wotm_stages`.

## 6. Luta 5: a fase do chefe em modo 1

- O condutor grava:
  - `m_mode = 1`;
  - `m_levelNum` 3 ou 11;
  - as IAs da tabela (`adventureAis`);
  - pontuação e "veio do save" zerados, como no `adventureStart`.
- Com 2 a 4 jogadores, o co-op do `wotm_coop.inc` assume. Isso pede o `wotm_split4::g_players` montado, que é o que a entrada multi já prepara.
- **Interceptar o `EvaluateOnePlayerStoryStatus`.** É preciso encadear com o `g_evalStory` do co-op, não registrar um segundo gancho.
  - **r = 0, chefe vencido:** fim da corrida (seção 8). Não deixa o original avançar de fase nem mostrar o diálogo 2.
    - No Capitol, o `FinalBoss` já toca o cinema final antes do `rtReturnToShell(0, 0)`.
  - **Game over** (o `displayDialog(1)` do original ou o `tailDialog(1)` do co-op): o mesmo CONTINUE/QUIT das lutas 1 a 4.
- **[I] Riscos:**
  - estado de modo 1 sobrando ao voltar ao menu: `canyon2LevelProgression`, `assBoss`, a fase do `finalBoss`;
  - na tentativa repetida pelo CONTINUE, o nível 3 recomeça pelas formigas.

## 7. Derrota

- O diálogo de fim de jogo da história (`displayDialog(1)`, CONTINUE ou sair) serve no modo 1.
- Nas lutas 1 a 4 (modos 2/3) é preciso conferir se ele serve fora da história. Se não servir, o diálogo é nosso, na fonte do jogo.
- **CONTINUE:** repete a mesma luta, com as mesmas CPUs e a mesma fase, sem limite. Os tokens da luta perdida não contam.
- **QUIT:** volta ao menu e descarta a corrida. Os tokens já ganhos ficam.

## 8. Tokens e final

- **Por luta vencida:** `grandTotal(P1) + dif × bônus[n]`, somado ao +0x2B24.
  - `dif` é 10, 15 ou 20, pela dificuldade das opções.
  - `bônus = {200, 400, 600, 800}` nas lutas 1 a 4 e 1500 no chefe.
  - Com mais jogadores, vale o multiplicador do co-op (`wotm_coop.inc` 1607-1617).
  - O `wotm_save` grava sozinho.
- **Final:** mais `dif × 3000`, depois os filmes de final de cada herói e os créditos.
  - Usa o `GenesisMovie` e o `genesisHook`/`endingMovie` do co-op, que já resolvem o filme pelo tipo e enfileiram um por jogador.
  - Depois volta ao menu principal.

## 9. Etapas

1. **Esqueleto:**
   - configuração MAX CPUS, linhas ARCADE e flag;
   - sorteio e `WOTM_ARCADE`;
   - lutas 1 a 4 encadeadas, com fim nosso, sem dano entre aliados e CPUs mirando só nos humanos;
   - tokens, CONTINUE e QUIT.
2. **Chefe:** luta 5 em modo 1 nos níveis 3 e 11, com 1 jogador e com co-op, interceptando a vitória e o game over, e o final com os filmes.
3. **Acabamento:**
   - "FIGHT N / 5" na tela de carga;
   - placar de tokens com a contagem;
   - doc de referência no `NATIVE.md` e item no `TODO.md` com o que sobrar.

## 10. Padrões adotados (o dono troca se quiser)

Confirmados pelo dono em 2026-11-13:
- O jogador escolhe o monstro.
- Cada jogador tem 1 respawn por luta, ou seja, 2 vidas. Sem vidas, CONTINUE infinito.
- A dificuldade é a das opções, fixa nas 5 lutas.
- O chefe luta na fase dele (Rosdale Canyon com as formigas; Capitol com Mecha e Jelly).
- No final entra um bônus de 3000, no lugar dos 10000 da história, além de `dif × 3000`.

## Achado à parte

`wotm_modes4.inc`, `takeDamageHook` (queimada): lê a fonte do dano em `GPR_U32(ctx, 5)`. Pela assinatura `takeDamage(float, bool, Monster*)`, esse registrador (a1) é o `bool force`; a fonte vem em a2 (`GPR_U32(ctx, 6)`), como já fazem o `callTakeDamage` do `wotm_overrides.cpp` e o `wotm_training.inc`. **[I]** Sem conferir na queimada.
