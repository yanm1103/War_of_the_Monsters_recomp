# Golpes do Godzilla STE: plano (2026-11-21)

Pedido do dono: os golpes do Godzilla 2000 do STE (mod `ste_teste`, branch `ste-monstros`) ficam ruins encaixados nos slots do Togera. As velocidades saem esquisitas e os golpes escolhidos são ruins. Alguns golpes vão precisar ser acelerados, mas não todos, então um fator único não serve.

Controles do STE, segundo o dono: □ = chutes e investida (no WotM a investida é o toque duplo para correr mais o ataque, numa coisa só), ✕ = golpes de mão, ○ = golpes de cauda.

Legenda: **[C]** confirmado (dados ou asm); **[I]** inferência que ainda falta validar.

## 1. Por que fica ruim hoje

- **[C]** O `ste_clips.py` (opção `strike`) estica cada golpe do STE ao comprimento do slot do Togera e deforma o tempo para o pico cair a 30% da janela do `LIZARD.CHR`. A aceleração varia por golpe:

  | slot | golpe | STE | slot | fator |
  |---|---|---|---|---|
  | 17 | HMMR_FIN | 1,17 s | 0,40 s | 2,9x |
  | 22 | UPPERCUT | 1,67 s | 0,67 s | 2,5x |
  | 18 | COMBO_BITE | 0,90 s | 0,40 s | 2,25x |
  | 19 | COMBO_BITE | 0,90 s | 0,40 s | 2,25x |
  | 15 | HEADBUTT | 2,00 s | 1,33 s | 1,5x |

- **[C]** A escolha seguiu a parte do corpo que o slot do Togera testa (boca, cabeça, cauda), não o que o Godzilla faz no STE. Por isso a mesma mordida aparece três vezes, e os chutes ficaram de fora (o Togera não tem slot de pé).
- **[C]** No STE o acerto vem cedo e a recuperação é longa (o jab acerta em 7-41% da animação; a do Togera acerta em 55-99%). Espremer isso numa janela tardia distorce o golpe.

## 2. O que o STE guarda: `MONSTER_DATA`

`tools/ste/ste_moves.py <iso> Godzilla2k.cmp` lista os golpes. O formato está no docstring da ferramenta.

- **[C]** 215 estados de 0x84 bytes, cada um com o nome do estado, a animação e uma lista de eventos com instantes em fração da animação.
- **[C]** O evento de tipo 0 liga e desliga o ataque, com o dano. O dano é o mesmo valor do campo +0x54 do estado.
- **[I]** O evento de tipo 15 liga e desliga conjuntos de esferas por parte do corpo. Pelos nomes dos golpes: 1-4 = cauda, 5-7 = braço direito, 8-10 = braço esquerdo, 12-14 = perna direita, 15-17 = perna esquerda. Isso bate com os botões que o dono descreveu: ✕ usa os braços, □ as pernas, ○ a cauda.
- **[I]** O campo +0x50 (0,2 a 1) é a força: 1,0 nos golpes que derrubam (Overhand, Tail_Full_Spin, Tail_uppercut, Double Kick).

Golpes do Godzilla 2000 (duração da animação, janela do ataque, dano):

| botão no STE | golpe | duração | ataque | dano |
|---|---|---|---|---|
| ✕ | Jab_right | 0,57 s | 7-41% | 5 |
| ✕ | Jab_left | 0,57 s | 7-34% | 5 |
| ✕ | Hammer | 1,17 s | 33-49% | 15 |
| ✕ | Uppercut | 1,67 s | 29-39% | 15 |
| ✕ | Overhand | 2,53 s | 31-50% | 25 |
| ✕ | Double Swipe | 2,20 s | 3 acertos: 13-21%, 28-36%, 61-72% | 10, 10, 30 |
| ✕ | Punch_left / Punch_right (RUN_PUNCH_R) | 0,90 s | 14-41% | 10 |
| ✕ | Jump Punch | 1,07 s | 18-36% | 15 |
| □ | Kick (KICK3) | 0,93 s | 13-30% | 6 |
| □ | Kick2 (KICK) | 0,70 s | 14-31% | 6 |
| □ | Double Kick | 2,17 s | 39-48% | 25 |
| □ | Jump Kick | 1,13 s | 6-37% | 20 |
| □ | Headbutt (investida) | 2,00 s | 15-54% | 15 |
| ○ | Tail_snap | 0,73 s | 12-25% | 6 |
| ○ | Tail_reverse | 1,17 s | 17-34% | 10 |
| ○ | Tail_Full_Spin | 1,33 s | 34-62% | 25 |
| ○ | Tail_sweep | 3,33 s | 28-46% | 10 |
| ○ | Tail_slam / Tail_slam_2 | 2,00 s | 27-37% / 56-65% | 10 / 15 |
| ○ | Tail_uppercut | 1,67 s | 26-37% | 25 |
| ○ | Jump tailwhip | 1,17 s | 30-40% | 10 |
| ? | Combo_Bite | 0,90 s | 30-43% | 15 |
| ? | Stomp | 1,73 s | 36-46% | 8 |

Os botões da tabela vêm da parte do corpo (tipo 15) e do que o dono disse, não dos dados.

**Falta decifrar:** a ligação de um golpe ao seguinte (qual botão, dentro de qual janela). Não está nos eventos dos estados. Deve ficar no grafo que vem depois deles (nós de 0x4C com ponteiros, de ~0xFA18 ao fim do `MONSTER_DATA`). O campo +0x04 do estado (1,33 no jab, 0,4 no Headbutt) também não está explicado.

## 3. O que o WotM permite

- **[C]** O mod pode ter `.CHR` próprio (`monster stats`, pasta `files/`).
- **[C] Velocidade por golpe:** o `.CHR` tem, em cada slot, uma velocidade de animação: campo +0x24, que o `charreset::readKey` copia para a `PunchConfig` +0x04, e o `StatePunch::startChain` passa ao `animationSetSpeed`. Nos monstros do jogo ela vai de 0,5 a 1,4 (o jab do Togera usa 0,85). É exatamente o "acelerar só alguns": a animação sai no ritmo do STE e o slot decide a velocidade.
- **[C]** A janela do acerto é uma fração da animação (CHR +0x2C/+0x30), então segue o golpe do STE sem conversão. O localizador testado é por slot (mãos 0x802/0x803, pés 0x804/0x805, e os do rig). Dano, tranco, atordoamento e pausa também são por slot.
- **[C]** Os golpes ganham ids de animação novos (os que o Togera não usa) e não precisam ocupar os ids 10-22 do Togera. O rig próprio (`Import rig`) aceita ação com id novo.
- **[C]** A investida do WotM é o `StateRamAttack` (266 = carga, 267 = impacto). O ataque no ar é o `StateButtSlam` (261/262).

## 4. Plano

1. **Terminar a leitura do STE** (`ste_moves.py`):
   - decifrar a ligação dos combos, para saber o que o STE encadeia depois de cada golpe e em que instante dá para cortar a recuperação;
   - conferir o papel dos conjuntos de esferas (o que o dono descreveu já casa).
2. **Exportar os golpes no ritmo do STE.** Novo modo do `ste_clips.py`: animação inteira, sem `strike` nem deformação, só sem a deriva da raiz e terminando de frente (opção `face`, que já existe). Corte opcional da recuperação a partir do instante em que o STE permite encadear, para golpes que demoram demais para voltar à guarda.
3. **`.CHR` próprio, gerado a partir de um JSON do mod.** Uma ferramenta grava a tabela de socos inteira: animação, velocidade (+0x24), janela, dano, localizador, tranco, atordoamento e janela de combo por posição e tipo. O `chr_punch.py` hoje só troca a animação.
   - Velocidade 1,0 em todos de início; acelerar só os que ficarem lentos no teste.
   - Candidatos: Tail_sweep (3,33 s), Overhand (2,53 s), Double Swipe (2,20 s), Double Kick (2,17 s), Tail_slam (2,0 s).
4. **Mapa de botões.** A lista de golpes do Godzilla 2000 na Tohoku Kingdom (https://www.tohokingdom.com/vg/gste/characters.htm; o GameFAQs recusa acesso automático) não tem cadeias de combo. Cada golpe é um botão mais uma direção ou uma condição. Comandos do STE: ✕ = soco/alto, □ = chute/baixo, ○ = golpe feroz (cauda), △ = bloqueio.

   | golpe do STE (lista da Tohoku Kingdom) | comando no STE | estado no MONSTER_DATA |
   |---|---|---|
   | Tail Snap | ○ | Tail_snap |
   | Tail Uppercut | na direção do oponente + ○ | Tail_uppercut |
   | Spinning Tail | cima + ○ | Tail_Full_Spin |
   | Tail Sweep | baixo + ○ | Tail_sweep |
   | Tail Slam | ○ de costas para o oponente | Tail_slam |
   | Swipe | ✕ | Jab_right / Jab_left (?) |
   | Strong Bite | cima + ✕ | Combo_Bite (?) |
   | Overhand | cima + ✕ | Overhand |
   | Uppercut | baixo + ✕ | Uppercut |
   | Hammerfist | na direção do oponente + ✕ | Hammer |
   | Ferocious Swipes | para longe do oponente + ✕ | Double Swipe (?) |
   | Running Punch | ✕ correndo | Punch_left / Punch_right |
   | Double Kick | para longe do oponente + □ | Double Kick |
   | Jump Kick | R2, □ | Jump Kick |
   | Nuclear Uppercut | □ segurando | ? |

   A lista não traz o □ sozinho (Kick/Kick2) nem o Headbutt, que estão no MONSTER_DATA.

   O WotM tem 6 entradas (□, △, frente/trás + cada um), contra cerca de 15 do STE (3 botões × direções), mas cada entrada muda com a posição no combo (10 posições). Proposta:
   - □ = Jab_right → Jab_left → Kick → Kick2 → Hammer: soco e chute na mesma cadeia, como a do Ultra-V;
   - frente+□ = Hammer → Overhand;
   - trás+□ = Double Swipe → Double Kick;
   - △ = Tail_snap → Tail_reverse → Tail_Full_Spin → Tail_slam;
   - frente+△ = Tail_uppercut → Uppercut;
   - trás+△ = Tail_sweep;
   - Combo_Bite no meio da cadeia de △, como a mordida do Togera;
   - investida (toque duplo + ataque, 266/267) = Headbutt;
   - ataque no ar (261/262) = Jump Kick.
5. **Dano e força para a escala do WotM.** Proposta: o dano do Togera vai de 4 a 9, o do STE de 5 a 25, então dano WotM ≈ 3 + STE × 0,25. Força 1,0 = derruba (knockback ≠ 0); força 0,6 = atordoa um pouco.
6. **Teste:** o dono no modo treino, que mostra a tabela de socos, a janela e o dano de cada golpe.

## 5. Decisões do dono

- O mapa de botões da seção 4: em aberto.
- A recuperação longa do STE fica como está (dono, 2026-11-21: "depois eu testo e vejo"). O corte da recuperação no passo 2 fica de fora.
