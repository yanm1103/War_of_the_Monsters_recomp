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

## Não funciona (teste do dono pelo menu)
O laser novo (rajada fixa mirada pela câmera, `mechaLaser` em `wotm_custom.inc`) não deu dano e não interagiu com prédios. O disparo
usa o `StateUltraTazer` só como roteiro de anims e tempo: na fase 0xA1 o original fica de fora e o nosso código faz o raio
(`hdLosCSTest` da boca 0x80B até 4000 na direção da mira), `HitEvent` tipo 0 + `Interactives::dispatchHit` no monstro, empurrão,
`HurtHistory` próprio + `dispatchHit` de 5,01 em prédio, faíscas (`particleCreateFx`). Especificação do laser do chefe em
`mecha_laser.md` (mesma pasta).

Pistas para a próxima sessão, na ordem:
1. Confirmar se o `mechaLaser` roda: não há log nele. Pôr uma linha por disparo (ponto de impacto, `_hdResult` hit/ids, alvo) e ver
   se a fase 0xA1 chega (o `tazerOK` do jogador chama o original, que pode recusar por outro motivo).
2. Se roda e o raio não acerta: conferir o `hdLosCSTest` (argumentos, o pulo pelo `m_id` em a3, se a boca 0x80B está no lugar certo
   e não dentro do próprio casco) e a direção da câmera (`_cs` +0x60 pitch, +0x68 heading; 0 = +Y).
3. Se acerta e não dá dano: conferir o `getInteractive` (o `m_kind` em +0 do retorno) e o `dispatchHit` (a0 dir, f12 dano, a1 atacante,
   a2 alvo).
4. Torre: o log acusa `torre FALTANDO`. O `animationGetHandle` não acha as anims de giro e inclinação da instância principal nem
   por 11/12 nem por 290/291; ver no `mechamon.py` como as anims 11/12 do chefe ficaram (o `ANIM_MAP` as renomeia) e o que o
   4º argumento do `animationGetHandle` é de fato. Sem ela o feixe sai reto para a frente do Mecha e não dá para mirar (teste do dono).

## Memória usada no topo da RAM
`PS2_RAM_SIZE-0x1400..-0x1000`: 2 Mechas × 0x1C0 (6 `_animHandle`, 2 `AnimBlend`, 1 `HurtHistory`) e 0x80 de rascunho para as
chamadas (de, até, `_hdResult`, direção, ponto, faísca). Logo acima ficam os `kQuadDefs` do split4.

## Ainda por fazer depois do laser
- **Pular:** o Mecha não pula (o dono quer que pule). Ver o que trava o `StateJump` no Jelly (tipo 0x1C0) ou se faltam as anims de pulo no `ANIM_MAP` do `mechamon.py`.
- Equilíbrio (com o choque do Jelly ele venceu duas rodadas sobrando 164 de vida), som do laser (`Final_s.bnk` só no nível 11),
retícula, teste pela seleção (casa CUSTOM, `kFraming`, retrato) e docs (`MODS.md` seção Mecha, `MODELOS_3D.md`).
