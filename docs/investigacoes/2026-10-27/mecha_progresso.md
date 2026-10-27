# Mecha jogável: onde parou (2026-10-27)

Branch `wip/mecha`. O Mecha do Cerebulon entra como monstro custom com arquivos próprios (`MON/MECHA*` montado do `CAPITOL.NGP` por
`tools/mechamon.py`, base Jelly, `mods/Mecha/monsters/mecha/monster.json` com `"files": true` e `"kind": "mecha"`).

## Funciona (testado em luta IA x IA, `WOTM_MATCH=mecha,1,1 WOTM_AIVSAI=1`)
- Carga: `[mods] skin mecha/mecha ... = traje 4 (JELLY4)`, 340 de 1000 texIds; renderiza gigante, anda, apanha, a rodada termina.
- Câmera `"scale": 350`: enquadra bem (foto do dono aprovou o afastamento).
- Escudo: começa ligado (instância 0x1D8, anim 1 e depois 2 em laço), dano ×0,25 (gancho do `Monster::takeDamage`); depois de cada
  disparo baixa (anim 0) por 240 quadros contados no `Monster::update` do próprio Mecha (o `gFieldCounter` não anda no port) e religa.
  Sem escudo o laser fica bloqueado no `transitionOK`.
- Especial sempre disponível (`m_specialWeapon` = 1 antes do `transitionOK`); jogador dispara com Triângulo+Círculo.

## Laser (segunda sessão do dia): consertado na luta IA x IA
Causa do laser sem efeito e da torre "FALTANDO": a RAM do runtime tem 64 MB e o `mem()`/`cRd32` mascaravam o endereço com
`0x01FFFFFF` (32 MB). A área do Mecha (`PS2_RAM_SIZE-0x1400`) caía 32 MB abaixo, no meio dos dados carregados: o jogo gravava os
handles e o `_hdResult` no lugar certo e nós líamos zero (e escrevíamos a origem e o destino do raio, e o `memset` da vaga, em cima
de dados da fase). O `mem()` agora mascara com `PS2_RAM_SIZE - 1`. Os seis handles são achados (escudo 0/1/2, laser, torre 290/291).

Outras correções: a inclinação da torre usava o comprimento horizontal sem normalizar (ficava sempre 0,5); o acerto em monstro deixou o
`getInteractive` + `dispatchHit` (não conferidos) e faz como o chefe: o slot com `m_id == id2` toma `takeDamage(0,4, false, Mecha)`
direto, mais o empurrão.

Medido (`WOTM_MATCH=mecha,1,1 WOTM_AIVSAI=1`, log por rajada no `escudo baixou`): rajada inteira no Congar = 180 quadros, −72 de vida;
rajadas que passam por prédio somam 80 a 100 quadros em objetos. Nas fotos (`WOTM_SHOTS`) o feixe sai da torre girada até o alvo,
com explosão no ponto. O Mecha venceu a rodada com 166 de vida.

### Câmera da torre (pedido do dono: "muito ruim de mirar")
Com o Mecha de um jogador no `StateUltraTazer` (preparar, disparar e desligar), `laserCam` (depois do `Cameras::Update` e da órbita,
`wotm_overrides.cpp`) põe a vista dele 220 acima da ponta do canhão (eixo do corpo + raio da boca, 348, na direção da mira),
olhando para a mira: o corpo fica atrás (atrás do eixo, o próprio Mecha tapava a frente, no teste do dono), as garras aparecem
embaixo e o feixe sai fino até a retícula (colada na boca, o feixe enchia a tela). A torre segue a mira a 0,6 por quadro. Entra e sai em 12
quadros, misturada com a pose do jogo. `laserInput` (entrada do `interpretInputs`, antes da órbita) soma os dois analógicos e o
mouse na mira (sensibilidade e eixo invertido da aba CAMERA) e os zera para o jogo e a órbita. A mira começa para onde a câmera
olhava, 6 graus mais baixa; inclinação de −63 a +20 graus. Retícula no centro (`g_wotmMechaReticle`, desenhada no `wotm_live.inc`,
só sem tela dividida). Como o `mechaAim` já mira pelo centro da câmera, o feixe vai para a retícula. Teste sem controle:
`WOTM_MECHATEST=1` (dispara sempre, a mira varre); nas fotos o feixe sai debaixo da cúpula até a retícula.

### Sons (o dono confirmou jogando)
Os do chefe, pelo `FinalBossSound` (banco `Final_s.bnk`, já carregado para o Mecha): preparar para disparar (som 4) ao entrar no
laser, o laço do laser (som 5, `updateMechLaser` a cada quadro da rajada, na boca), escudo descendo (2) e subindo (1 e 7). O `this`
do `FinalBossSound` fica nos 12 bytes livres depois do `HurtHistory` da vaga (só o +4, o handle do laço, é lido). O monstro atingido
toca sozinho o laço do impacto (som 0x1B) com `Monster+0x1A84 = 1` no quadro, como o chefe faz; o laço do laser e o do impacto são
desligados à mão no fim da rajada e ao sair do laser (o desligamento por tempo do jogo usa o contador de fields, parado no port).

Falta conferir pelo dono, jogando: a mira pela câmera (o jogador; a IA mira o peito do inimigo mais perto) e se prédios caem.
Equilíbrio: 72 por rajada é muito (a vida é 200); o chefe faz 0,5 por quadro mas a rajada dele pega menos tempo.

## Memória usada no topo da RAM
`PS2_RAM_SIZE-0x1400..-0x1000`: 2 Mechas × 0x1C0 (6 `_animHandle`, 2 `AnimBlend`, 1 `HurtHistory`) e 0x80 de rascunho para as
chamadas (de, até, `_hdResult`, direção, ponto, faísca). Logo acima ficam os `kQuadDefs` do split4.

## Ainda por fazer depois do laser
- **Pular:** o Mecha não pula (o dono quer que pule). Ver o que trava o `StateJump` no Jelly (tipo 0x1C0) ou se faltam as anims de pulo no `ANIM_MAP` do `mechamon.py`.
- Equilíbrio (com o choque do Jelly ele venceu duas rodadas sobrando 164 de vida), som do laser (`Final_s.bnk` só no nível 11),
retícula, teste pela seleção (casa CUSTOM, `kFraming`, retrato) e docs (`MODS.md` seção Mecha, `MODELOS_3D.md`).
