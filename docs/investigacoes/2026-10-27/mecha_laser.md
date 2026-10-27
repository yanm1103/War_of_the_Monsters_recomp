# Laser do Mecha (FinalBoss::updateMecha) e FireBreath::TestCollis: o que o gancho do Mecha jogável precisa

Pedido: especificar o laser do chefe Mecha (fase Capitol, nível 11) e o sopro de fogo para reproduzir, num gancho nativo
C++, um laser em rajada mirado pela câmera saindo da boca de um Monster custom. Fontes: `src/game/FinalBoss.cpp`
(updateMecha em C equivalente, 183/1303, "mesmos blocos e chamadas"), conferido contra o asm do retail no caso 4 inteiro
(constantes, offsets, registradores); `FireBreath::TestCollis` e `Activate` lidos direto do asm; `Interactives.cpp`,
`Monster.cpp` (takeHit/takeDamage/knockBack), `hdLos.cpp`, `mathf.cpp`, `Sound.cpp`.

Grau de certeza: **[C]** conferido no asm do retail; **[D]** vem da decomp equivalente (não conferi linha a linha);
**[I]** inferência.

## 1. FinalBoss::updateMecha, estado 4 (disparando)

Objeto: o global `finalBoss` (0x00288F20, 0x490 bytes). Campos usados:

| off | campo | uso |
|---|---|---|
| 0x20 | m_state | 4 = disparando |
| 0x28 | m_timer | 240 ao entrar no estado 4; `--m_timer` por update (não por field) |
| 0x34 | m_laser | comprimento do feixe, 0..1 de 4000 |
| 0x40 | m_sound (FinalBossSound, 0x10 bytes; +4 = handle do loop do laser) | |
| 0x70 | m_mechaCs | cs do Mecha |
| 0x7C | m_monster7C | o alvo (jogador 1) |
| 0x1B0 | m_anim3 (_animHandle, 16 bytes) | hier 0x27A9 anim 0: o feixe |
| 0x1C0 / 0x228 | m_blend1 / m_blend2 (AnimBlend) | anim 11 (giro da torre) / anim 12 (pitch) do Mecha |
| 0x2E0 | m_hurt (HurtHistory, 0x84 bytes) | dano acumulado em objetos |
| 0x380 | m_loc27AAPos | localizador 0x27AA = emissor do laser (gravado por setLocator) |

Estáticos: `inc.2490` = 5 (empurrão por update), `D_006F8BEC` = dano por update, `smoothFactor.2488`, `degreeOffset.2489`.

### Duração e mira
- Estado 3 termina quando a anim 4 acaba: anim 6, `m_state=4`, `m_timer=240`, `m_laser=0`. [C/D]
- Fora do switch, todo update (exceto estados 0, 6, 7, 9): `rot = mathfRotationFromVector(target - mechaCs->trans)`
  (target = localizador 0x809 do jogador, Monster+0x3E30); no estado 4, enquanto `m_timer >= 176` o heading recebe
  −10° (0.17453292 rad), o que dá a varredura inicial; em 175 `smoothFactor = 0.015`. `m_heading` segue `rot.z` por
  `smoothEasyInAngleTC`, e `m_blend1.setPercent(heading/2π + 0.5)`. [D]
- Rajada: 240 updates (4 s a 60 Hz). Dano no jogador: 0.5 / 0.575 / 0.65 por update conforme `game+0x1203CC`
  (matchMode 0/1/2), ou seja 120 / 138 / 156 se o raio pegar o tempo todo. [C]

### Pseudocódigo do caso 4 (ordem real das chamadas) [C]
```
spark = target                       // target = loc 0x809 do jogador (copiado antes do switch)
if (target.z < 180) target.z -= 50
dir = target - m_loc27AAPos
p = mathfPitchFromVector(&dir)
degreeOffset = smoothEasyInTC(degreeOffset, 0, 0.01, 0.001)       // nunca lido
m_blend2.setPercent(smoothEasyInTC(m_blend2.getPercent(), 0.5 - p/π, smoothFactor, 0.001))
h = (m_blend1.getPercent() - 0.5) * 360 * (π/180)                 // heading de verdade do feixe
rph.x = mathfPitchFromPointToPoint(&m_loc27AAPos, &player->loc809)  // pitch
rph.y = 0                                                           // roll
rph.z = h                                                           // heading
mathfRotMatrixRPH(mat, &rph)          // mat local na pilha
dir = (0, 4000, 0) aplicado às linhas de mat  => dir = mat[1] * 4000  (linha 1 = frente)
to = m_loc27AAPos + dir
hdLosCSTest(&m_loc27AAPos, &to, &res, 0x27A6)     // retorno ignorado
if (res.hit) {
    if ((res.ids & 0x7FF) == player->m_id) {          // id2 == Monster+0x20
        dist = |m_loc27AAPos - player->loc809| + 20;  to = player->loc809
    } else {
        dist = |m_loc27AAPos - res.point|;  to = res.point;  to.z += 50
    }
} else dist = 4000
if (dist < 4000) {
    d = dist / 4000
    m_laser = (m_laser < d) ? smoothEasyIn(m_laser, d, 0.25, 0.001) : d   // cresce suave, encolhe na hora
    3 vezes, cada uma com chance mathfRand(0,7)==0 (1/8), fx 0x58, depois 0x21, depois 0x1F:
        spark = to + (mathfRandf(-35,35) em x, y, z)
        particleCreateFx(&spark, fx, mathfRandf(1,5), NULL, 0.0)
} else
    m_laser = smoothEasyIn(m_laser, 1.0, 0.25, 0.001)
animationSetToPercent(m_anim3 /*cópia*/, m_laser, 1)
if (res.hit) {
    id1 = (res.ids >> 11) & 0x3FFF;  id2 = res.ids & 0x7FF
    if (id1 == player->m_typeBits) {                  // Monster+0x14
        dmg = {0.5, 0.575, 0.65}[game->matchMode]
        push = normalize(dir) * inc(5)
        player->m_cs->trans.x += push.x;  player->m_cs->trans.y += push.y   // z não
        player->takeDamage(dmg, false, NULL)
        inputSetActuator(0, cópia de 32 bytes de game+0x1227E0)              // pad 0 fixo
        *(int*)(player + 0x1A84) = 1                  // MonsterSound(+0x1A7C).f8: "laser pegou neste quadro"
        if (player->m_dead) m_timer = 1               // encerra a rajada no decremento abaixo
    } else if (id2 != 0) {
        amount = (id1 - 0x27AE < 6) ? 1.1 : 0.16      // 0x27AE..0x27B3: pilares do Capitólio
        if (m_hurt.creditHit(id2, amount) > 5.0) {
            m_hurt.creditHit(id2, -5.0)
            HitEvent::create()->pos = &to              // só pos; o resto fica zerado (type 0)
            hitDir = to - m_mechaCs->trans
            Interactives::dispatchHit(&hitDir, 5.01, 0 /*attacker*/, id2)
        }
    }
}
if (--m_timer == 0) {
    animationTransitionInto(m_anims[5], 500, 1, 1); m_state = 5
    m_blend2.rampOut(200); animationSetToPercent(m_anim3, 0.0, 1)
}
m_sound.updateMechLaser(&m_mechaCs->trans)         // this = finalBoss+0x40
```
Estado 5 chama `m_sound.terminateMechLaser()` todo update. [C]

### Observações
- **O monstro atingido pelo laser do Mecha não recebe HitEvent nem knockback de estado**: só `takeDamage` + empurrão
  direto de 5 unidades por update no plano xy + vibração. Sem recoil. [C]
- **Mira do feixe**: no retail a peça 0x27A9 é um ep node da hierarquia do próprio Mecha (addEpNode), não uma instância
  separada; o feixe aponta porque os blends 11/12 giram o esqueleto. `m_loc27AAMat` (0x390) só é escrito (setLocator),
  ninguém lê. Nenhuma matriz do feixe é gravada à mão em updateMecha; `mat` do RPH é local, só para calcular `dir`. [C]
- `hdLosCSTest(..., 0x27A6)`: o 4º argumento vira `skipId2` do iterador, comparado com `cs->epNode->id2` (o slot do
  interactive, < 1024). 0x27A6 é um id1, então no retail **não pula nada** (provável descuido; o FireBreath passa o
  `m_id` do dono). No gancho, passe o `m_id` (Monster+0x20) do atirador. [C]
- `m_hurt` nunca é zerado no Mecha: `creditHit` devolve 0 quando a tabela de 16 ids enche. O FireBreath zera a contagem
  (`+0x88`) a cada `Activate`. [C]

## 2. FireBreath::TestCollis [C]

`FireBreath` mora em Monster+0x68C0. `Activate(f12..f18)` grava: +0x98 timeLeft (duração em fields), +0x9C alcance,
+0xA0 dano em monstro, +0xA4 dano em objeto, +0xA8 e +0xAC (argumentos de lightOnFire), +0xB0 (valor gravado em
Monster+0x1B4 do alvo); zera a contagem do HurtHistory em +0x88 (o HurtHistory está em +8). Update desconta
`timerGetFieldsLastFrame()` de timeLeft e chama TestCollis enquanto > 0.

```
o = owner (this+4)
from = o->loc[0x80B] (Monster+0x3E50) - 20 * o->axis (Monster+0x33D0)
range = (game+0x1203C8 == 1 && game+0x1203D0 == 6 && craterSwitched) ? 1000 : this->range(+0x9C)
to = from + o->axis * range
hdLosCSTest(&from, &to, &res, o->m_id)            // pula o próprio cs
if (!res.hit || id1 == 0 || id2 == 0) return
h = HitEvent::create()
h->pos = &res.point; h->result = &res; h->type = 4; h->source = 5
h->subtype = (o->m_typeBits == 0x120) ? 0x10 : 0xF
if (id1 - 0x20 < 0x3E1) {                          // id1 em [0x20, 0x400]: é monstro
    t = Interactives::getInteractive(id2)
    dmg = this->dmgMonster(+0xA0) / (timerGetFieldsLastFrame() * 60)
    if (o->m_typeBits == 0x120) t->lightOnFire(this+0xA8, this+0xAC, o->m_id)
    if (t->m_playerNum == 1) inputSetActuator(t->m_index, cópia de game+0x1227C0)
    *(float*)(t + 0x1B4) = this+0xB0
} else {
    t = getInteractive(id2)
    if (t->m_kind == 3) {                          // destrutível: acumula até 5
        if (hurt.creditHit(id2, this->dmgObj(+0xA4) / timerGetUpdateRate()) < 5) return
        hurt.creditHit(id2, -5); dmg = 5.0
    } else dmg = this->dmgObj / timerGetUpdateRate()
}
mathfRotationFromPointToPoint(&rot, &from, &to)     // é ESTE vetor (pitch, 0, heading) que vai como "dir"
Interactives::dispatchHit(&rot, dmg, o->m_id, id2)
```
No `Monster::takeHit`, type 4 cai no `default` do switch: só dano (sem recoil nem knockback), depois de passar pelo
`acceptHit` do estado e pelo bloqueio.

**Qual é mais simples de reproduzir:** a estrutura do FireBreath (um LOS, um HitEvent, um dispatchHit para todo alvo,
o atacante creditado) é a melhor base. O do Mecha é específico demais (só o jogador 1 recebe dano, pad 0 fixo,
`attackerId` 0). Chamar o próprio `TestCollis` não serve: ele usa o eixo Monster+0x33D0, não a câmera.

## 3. HitEvent e como o hit chega ao alvo [C]

- `HitEvent::create()` (0x001472A0): `memset(&HitEvent::s_takeHitInfo, 0, 0x40)` e devolve esse ponteiro
  (0x007CD528). É um bloco global único: preencha os campos **depois** do create e **antes** do dispatchHit.
- Layout (include/game/hit_event.h): +0x00 pos (`_fvector*`), +0x04 result (`_hdResult*`), +0x0C type, +0x10 source,
  +0x14 subtype, +0x18 sourceKind, +0x20 sourceArg, +0x24 (int copiado no recoil), +0x28 (com source 0xB: 0x10 usa
  0x34/0x38), +0x34 strength34 = **speed** do knockBack, +0x38 strength38 = **lift**, +0x3C (float do recoil).
- `Interactives::dispatchHit(dir, dmg, attackerId, targetId)`: se `0 < targetId < s_numInteractives`, chama a virtual
  `takeHit(dir, dmg, attackerId)` do interactive (vptr em +0x10; delta em vt+8, função em vt+0xC). Para um Monster isso
  é `Monster::takeHit`; para destrutíveis/carros/etc., o takeHit da classe deles.
- `Monster::takeHit(dir, dmg, attackerId)`: normaliza `dir` **no lugar** (w = comprimento; passe um vetor descartável);
  se `m_unk49` e o `acceptHit` do estado aceitar, segue; atacante = `getInteractive(attackerId)` se `m_kind == 1` (aplica
  multiplicadores de fúria em +0x48C/+0x49C); bloqueio pode escalar/anular; por tipo: 1 recoil, 2 bloqueável, 3 hit
  grande, 5 choque, 7 atordoar, **8 = só knockBack(dir, lift=+0x38, speed=+0x34)**, 10 queimar, 0/4/outros = só dano;
  vibra o pad do alvo se `m_playerNum == 1`; termina em `takeDamage(dmg, false, atacante)`.
- `Monster::takeDamage(dmg, force, src)`: ignorado se `!force && !m_unk49`, god mode, filme, etc.; dano de CPU
  (`src->m_playerNum == 2`) é escalado pela dificuldade.
- `Monster::knockBack(dir&, lift, speed)`: 0x1A0 BigTakeHit, 0x1C0/0x1E0 recoil, os outros StateThrowBack com
  velocidade = dir (z pela metade) normalizado × speed + lift em z, limitada a s_maxVel. Entra num estado: chamar todo
  quadro reinicia o estado.

## Recomendação para o gancho
Por quadro da rajada: `from` = Monster+0x3E50; direção da câmera; `to = from + dir*4000`;
`hdLosCSTest(from, to, &res /*0x30 bytes*/, self->m_id)`. Comprimento como no Mecha (dist/4000, `smoothEasyIn` 0.25 ao
crescer); faíscas 0x58/0x21/0x1F com 1/8 de chance cada, ±35, escala 1..5. Monstro atingido (id1 em [0x20,0x400] e
`getInteractive(id2)->m_kind == 1`): todo quadro `HitEvent::create()` com pos/result, type 0, e
`dispatchHit(&tmpDir, ~0.5, self->m_id, id2)` (passa pelo bloqueio e credita o atacante); a cada N quadros (ou por
dano acumulado), type 8 com +0x34 speed / +0x38 lift para o knockback. Se quiser só o comportamento do Mecha: empurrão
de 5/quadro em xy + `takeDamage`. Objetos: HurtHistory próprio (buffer de 0x84 bytes zerado a cada rajada) +
`creditHit(id2, 0.16 ou 1.1)`, e ao passar de 5: `creditHit(id2,-5)`, `HitEvent::create()->pos = &ponto`,
`dispatchHit(&(ponto - self->trans), 5.01, self->m_id, id2)`.
Peça do feixe: `cs->trans = from`; `mathfRotationFromPointToPoint(&cs->rot, &from, &to)` (x = pitch, y = 0,
z = heading com 0 em +y); `mathfRotMatrixRPH(cs->mat, &cs->rot)` faz a linha 1 da matriz apontar de from para to
(e escreve a linha 3 = (0,0,0,1)); `animationSetToPercent(handle, len, 1)`. Que o eixo local do modelo do feixe é +y
é inferência [I] (é o eixo que o Mecha usa para o raio).
Som: `FinalBossSound::updateMechLaser`/`terminateMechLaser` tocam a amostra 5 de `gSoundBanks[24]` (Final_s.bnk),
que só é carregado no nível 11 em game mode 1; fora dali o banco é nulo. `terminateMechLaser` ainda mexe no som de
impacto de `game->m_monsters[0]`. Para o gancho, use um FinalBossSound próprio (0x10 bytes zerados) só se o banco
estiver carregado, ou outro som; para parar, `gLoopManager.terminateLoop(&handle)`.
