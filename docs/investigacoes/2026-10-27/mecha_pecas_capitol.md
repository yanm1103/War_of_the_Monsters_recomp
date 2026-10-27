# Peças do Mecha em `LVL/CAPITOL.NGP` (inventário para um monstro custom)

Objetivo: saber o que recortar de `CAPITOL.NGP/.PTR/.TEX/.RTX` para gravar o Mecha (robô da fase 1 do chefe final) no formato de
monstro (`MON/xxx.NGP/.PTR/.TEX/.RTX`). Endereços são os da imagem pré-ligada em 0xA00000 (offset no arquivo descompactado =
endereço − 0xA00000). Lido do disco com `tools/ngp.py`, `tools/pose.py`, `tools/animcurve.py`, `tools/tex.py` e a lista 0 do `.PTR`
(todas as posições de ponteiro da imagem).

Scripts (fora do repo, em `build/mecha_inv/`, rodar da raiz): `mecha_tree.py` (base: ponteiros do `.PTR`, filhos por opcode),
`mecha_dump.py --tree` (árvore), `mecha_refs.py` (fecho de ponteiros, o que sai/entra), `mecha_tex.py` (texturas),
`mecha_size.py` (caixas), `mecha_anim.py` (animações e canais), `mecha_bytes.py` (malhas, blocos da instância).

## 1. Árvore

Caminho: raiz 4 da fase `a002e0` (GROUP id 0, flags 3, 1 filho) → `126a620` (GROUP id 0, flags 7, 2 filhos; o ponteiro está em
`a00300`). Os dois filhos são os eps 0x27A8 e 0x27A6. Os outros ids não são raízes próprias: 0x27A7, 0x27A9 e 0x27AA estão
**dentro** da subárvore do 0x27A6. 0x27AB e 0x27AD não existem em `CAPITOL.NGP` (o `finalBoss.addEpNode` os recebe de outra fase;
o 0x27AD é o SWITCH usado no `updateJelly`). 0x27AC não existe.

```
126a620 GROUP id0 flags 7
├─ 12335e0 GROUP 27A8 (flags 1)            ep m_ep[2]
│   └─ 12335b0 CHAR_INSTANCE 27A8 (1 anim, 2 canais)
│       └─ 1233530 GROUP (ctx GS 12334e0) └─ 1232e80 OBJECT 72 vért., tex 214, UV animado
└─ 126a5f0 GROUP 27A6 (flags 7)            ep m_ep[0] (o que o hierSetCsEpNode liga ao cs)
    └─ 126a5c0 CHAR_INSTANCE 27A6 (14 anims, 219 canais)
        └─ 1266a20 GROUP (3 filhos)
            ├─ 1234b00 SKEL_BONE raiz (out 12) → 1234a00 ANIM_XFORM "quadril" (t z 142)
            │    ├─ 1233610 ANIM_XFORM (sem filhos; só a 194 mexe)
            │    ├─ 4 pernas, 3 ossos cada (SKEL_BONE+ANIM_XFORM), na ponta TRANSLATE z −66,5 → ATTACHPT
            │    │    0x805 (out 2,1,0), 0x816 (5,4,3), 0x817 (8,7,6), 0x804 (11,10,9)
            │    └─ 6 ANIM_XFORM sem filhos (1234520..1234930, canais 50..91)
            ├─ 123a7e0 SKEL_BONE raiz (out 42) → 123a710 ANIM_XFORM (t z 142)
            │    → 123a5f0 (tz) → 123a4d0 (tz) → 123a380 ANIM_XFORM "torso" (rz = canal 103 = giro da torre)
            │        ├─ 1234ee0 cabeça + 3 peças (1234b50/c70/d90), TRANSLATE → ATTACHPT 0x80E
            │        ├─ braço esq. 1235c40/1235b20 → ROTATE 1235a70 → 3 dedos (2 ossos + ponta ANIM_XFORM com vis
            │        │    → ATTACHPT id 0)
            │        ├─ braço dir. 1237f60/1237e40 → ROTATE 1237d90 → 3 dedos idem
            │        │    └─ ROTATE 1237d30 ├─ ATTACHPT 0x27AA (ponta do laser)
            │        │                     └─ ROTATE 27A9 1237cd0 (ep m_ep[3]) → CHAR_INSTANCE 27A9 (laser)
            │        ├─ COLL_VOL 1238080
            │        ├─ TRANSLATE z −45,1 → ATTACHPT 0x809 ; TRANSLATE z +64,3 → ATTACHPT 0x80C
            │        ├─ ROTATE 12381d0 → LIGHT 1238190
            │        ├─ TRANSLATE z −280,8 → OBJECT 1238230 (cabine, rígido, 77 vért., tex 211)
            │        ├─ TRANSLATE z −280,8 → CHAR_INSTANCE 27A7 123a290 (ep m_ep[1]) → GROUP (ctx 123a090)
            │        │     ├─ OBJECT 1238960 (146 vért., tex 207; canais a/u/v)
            │        │     └─ ANIM_SCALE 123a020 (sx/sy/sz) → OBJECT 12394a0 (146 vért., tex 211)
            │        └─ ROTATE 123a320 → LIGHT 123a2e0
            └─ 12669f0 GROUP flags 5 (2 filhos): OBJECT 123a830 skinned (3300 vért., pk 0)
                                                  OBJECT 124d5d0 skinned (4732 vért., pk 1)
```

Contagem (162 nós): GROUP 9, CHAR_INSTANCE 5, OBJECT 10, SKEL_BONE 43, ANIM_XFORM 52, TRANSLATE 14, ATTACHPT 16, ROTATE 6,
ANIM_SCALE 2, SCALE 2, COLL_VOL 1, LIGHT 2. **Sem LOD, sem SWITCH, sem INTERACTIVE/DESTRUCTIBLE, sem PART_EMITTER.**

- **Skinned de verdade**: o corpo são 2 OBJECTs skinned (`123a830` metade de baixo, z 0..154; `124d5d0` de cima, z 112..344) com
  43 saídas de osso (`animMatrixPtr` com 43 matrizes em `1266a90..1267590`; `animPktPtr[0]` e `[1]` em `1267680`/`1267880`, cadeias
  com tag 0x3 apontando os blocos de paleta). Rígidos: cabine `1238230`, as duas cascas do 0x27A7 e as peças do laser.
- **CHAR_INSTANCEs** (5; entradas 42..46 das 98 do ANIM_MGR da fase `136ca60`): 0x27A6 `126a5c0` (219 canais), 0x27A7 `123a290` (6),
  0x27A8 `12335b0` (2), 0x27A9 `1237ca0` (5) e uma sem id `1237730` (2 canais, aninhada no laser).
- **Locators (ATTACHPT, op 40)**: 0x804, 0x805, 0x816, 0x817 (pés), 0x80E (cabeça; o `setLocator` grava em `m_pinTrans`), 0x809
  (torso, z −45), 0x80C (torso, z +64), 0x27AA (ponta do laser) e 10 de id 0 (6 pontas de dedo, 2 do laser, 2 sob 1237b40). Faltam,
  comparado a um monstro, 0x802, 0x803, 0x806, 0x807, 0x808, 0x80A, 0x80B (e 0x818/0x819, 0x852.. do ROBOT0). Obs.: o FinalBoss lê
  o 0x809 do **monstro do jogador** (`m_monster7C`), não o do Mecha; do Mecha só usa 0x804/0x805/0x816/0x817 (poeira dos pés),
  0x80E e 0x27AA.
- **Colisão**: só o COLL_VOL `1238080` (sob o torso). Nenhum COLL_GRID próprio (o da fase é a raiz `136c4c4`).

## 2. Como torre, 0x27A8 e laser se ligam
- **"Torre" não é o 0x27A7.** O giro da torre é o osso do torso `123a380` (canal 103 = rz), movido pela animação **11** (1 curva,
  rz de −π a +π em 10 quadros) como blend aditivo `m_blend1` em porcentagem fixa (`heading/2π + 0,5`). A mira (inclinação) é a
  animação **12** (6 curvas: rx/ry/rz dos antebraços `1235b20` e `1237e40`) via `m_blend2`. Ambas são do próprio 0x27A6.
- **0x27A7 = escudo** em volta da cabine (z 278..387, raio ~142), filho do torso (sob TRANSLATE z −280,8), com instância própria:
  anim 1 (sobe: escala z e alfa 0,1→1, 30 q), 2 (laço de brilho: UV 1→2, 60 q), 0 (baixa, 30 q), 194 (cinema). Bate com o
  FinalBoss: `initAfter` liga a 1, o estado 7 (vulnerável) passa para a 0, os sons são "shield power up/down".
- **0x27A9 = laser**, filho da garra direita (sob `ROTATE 1237d30`, irmão do ATTACHPT 0x27AA). Anim 0 (400 q): canal 0 = sy do
  ANIM_SCALE `1237770` (1→2015, comprimento do feixe), 1/2/4 = visibilidades, 3 = ty do ANIM_XFORM `1237a60` (0→4000, o ponto de
  impacto, sprite `12378d0` tex 205). O `animationSetToPercent(m_anim3, m_laser)` escolhe o comprimento. A instância sem id
  `1237730` (ativa e em laço no arquivo) rola o V das 3 malhas do feixe (tex 204).
- **0x27A8 = irmão do 0x27A6**, não filho: disco de ~146 de lado no plano XZ (y −20..0) com V rolando (anim 0, 15 q; `loop` 0 no
  arquivo). Fica fora do cs do Mecha (o cs é ligado ao `m_ep[0]` = `126a5f0`); o FinalBoss só guarda o ponteiro.
  Para o monstro custom pode ficar de fora.

Tudo o que o Mecha precisa para andar/mirar/atirar está, portanto, numa árvore só (`126a5f0` e abaixo); o 0x27A8 é opcional.

## 3. Ponteiros para fora
Fecho de ponteiros a partir de `126a620` (nós + todos os blocos de dados alcançados pelas posições da lista 0 do `.PTR`):
- **Nenhum ponteiro de dentro aponta para fora.** Tudo cai numa faixa contígua `1232e80..12a9a00` (**486 272 bytes**), sem nenhum
  nó da fase misturado. Os 2877 ponteiros da faixa têm o nibble alto 0 e todos têm alvo dentro da imagem.
- De fora para dentro só há 6 ponteiros: o filho de `a002e0` (em `a00300`) e as 5 entradas do ANIM_MGR da fase (`136cb10..136cb20`).
- Dentro da faixa ficam: ctx do GS dos GROUPs (`12334e0`, `1236cc0`, `12379e0`, `123a090`), pacotes das malhas, `val`/`dirty`/`atMat`/
  `animCtx`/`animMatrixPtr`/`animPktPtr` de cada instância, os `_animCharacter` (0x27A6 em `12a9330`, 0x27A7 em `12a99a0`, 0x27A8 em
  `126a710`, 0x27A9 em `126a7f0`, a sem id em `126a6b0`), as `_animation` e as curvas (~250 KB, `126a7f0..12a9374`, depois das malhas).
  O `AnimChannelIdMap` do 0x27A6 está em `12a92e0`; `numAnimTran` 50; `angularChannelBits` = 0xFFD8 (offset, >16 canais).
- **ANIM_MGR da fase**: só lista as instâncias (`numCharInstances` + ponteiros); o `animationSetInstance` só escreve o id2 delas. O
  monstro precisa do próprio ANIM_MGR como raiz, listando as 4 (ou 5) instâncias. `activeTree` é 0 no arquivo (montado em execução).
- Fora da imagem, a dependência é só de texturas: 14 posições de texId (lista 1 do `.PTR`) e 14 de TEX0 (lista 2) na faixa.
  Malhas: 191 600 bytes (77 008 + 103 216 dos skinned).

## 4. Texturas
Todas do `CAPITOL.TEX` (215 pacotes, ids 0..216); paletas no `CAPITOL.RTX`. Os texIds/CBP do `.NGP` já estão nos números do
arquivo (deslocamento 0); o `TBP0` do TEX0 está 0 no `.NGP` (a lista 2 do `.PTR` soma a base da VRAM na carga).

| texId | quem usa | formato | pacote .TEX | bytes | paleta (.RTX, CBP) |
|---|---|---|---|---|---|
| 209 | corpo skinned (slot 0 dos dois) | T8 256x256 | 0x121c00 | 65 664 | 0x1a7c0, 483 |
| 210 | corpo skinned (slot 2) | T8 256x256 | 0x131c80 | 65 664 | 0x1abd0, 487 |
| 211 | corpo (slot 1), cabine 1238230, escudo 12394a0 | T8 256x256 | 0x141d00 | 65 664 | 0x1afe0, 491 |
| 207 | escudo 1238960 | T8 128x128 | 0x10db00 | 16 512 | 0x19fa0, 475 |
| 204 | feixe do laser (3 malhas) | T4 128x128 | 0x107580 | 8 320 | 0x19730 (8x2), 466 |
| 205 | impacto do laser (sprite) | T8 32x32 | 0x109600 | 1 152 | 0x19780, 467 |
| 214 | 0x27A8 (disco) | T8 64x64 | 0x171e80 | 4 224 | 0x1bc10, 503 |

Total: **227 200 bytes** de pixels + 7 paletas (6 320 bytes); sem mipmaps. Um monstro ocupa ~125–141 KB de `.TEX` (FINAL0 125 568,
ROBOT0 141 568), então o Mecha pesa ~1,7x na VRAM (sem o 0x27A8: 222 976). Colisão de ids: os monstros numeram as próprias texturas
a partir de 1 (ROBOT0 1..18, FINAL0 1..12) e a lista 1 do `.PTR` de monstro soma o maior id do arquivo anterior; os ids 204..214 do
Mecha precisam ser renumerados (1..7) e o CBP/TBP reescritos para o esquema do monstro (lista 2: TBP0 += fim das texturas
anteriores; CBP pelas marcas 0x1B/0x2C/0x24, ver `FORMATOS.md`, "Correções").

## 5. Tamanho
- Caixa (pose da anim 2, quadro 0): x −242..252, y −142..142, z 0..385 → **~385 de altura**, ~494 de largura. Em repouso (vértices
  crus, braços abertos) x −405..405, z 0..344. Anim 0 (andando): z −4..388; anim 10: z 0..402.
- Monstros (idle): ROBOT0 ~107 de altura (x −51..56), LIZARD0 ~100, FINAL0 ~191. **O Mecha tem ~3,6x a altura de um monstro comum**
  e ~2x o FINAL0.
- Bytes: subárvore 486 KB (malhas 192 KB, animação ~250 KB, o resto nós/instâncias); ROBOT0.NGP inteiro tem 1,2 MB, FINAL0 2,5 MB.
- O FinalBoss liga uma sombra de bloqueio de raio `mechaShadowRadius` e o `AnimPappy` com offset z 141,964 (= o z 142 dos ossos raiz).
- Curiosidade: o 0x27A6 tem 219 canais, o mesmo número do FINAL0 (108 animações).

## 6. Animações do 0x27A6 (`character` `12a9330`, 14 animações, 60 quadros/s)
Os ids 7 e 8 não existem: `initAfter` pede 0..12, então `m_anims[7]` e `[8]` ficam sem animação (o `update` faz transição para
`m_anims[8]`). 65, 194 e 195 não são pedidos pelo FinalBoss (194 deve ser do cinema 0x226A; o `m_timer` inicial 0x762 = 1890 fields,
~ os 1860 quadros da 194).

| id (k) | quadros | laço no arquivo | uso (FinalBoss) | o que muda (canais que variam) |
|---|---|---|---|---|
| 65 (0) | 71 | não | — | nada varia: pose parada (212 curvas constantes) |
| 0 (1) | 200 | sim | anda +y | pernas, quadril, tronco, braços, garras |
| 1 (2) | 200 | sim | anda −y | idem |
| 2 (3) | 300 | não | parar/esperar | idem + cabeça (13 canais) |
| 4 (4) | 190 | não | preparar laser (velocidade 2x) | idem + garras (14 por mão) |
| 5 (5) | 100 | não | fim do disparo | corpo inteiro |
| 12 (6) | 20 | não | blend aditivo de mira (m_blend2) | só rx/ry/rz dos antebraços (135..137, 180..182) |
| 11 (7) | 10 | não | blend aditivo de giro (m_blend1) | só rz do torso (103), −π..π |
| 6 (8) | 40 | sim | disparando | só as 4 pernas (6 canais cada) |
| 194 (9) | 1860 | não | cinema (31 s) | tudo, inclusive `1233610` |
| 195 (10) | 30 | não | — (mesmos canais que a 3) | braços, cabeça, torso, pernas |
| 10 (11) | 100 | não | desligar / baixar | corpo inteiro |
| 9 (12) | 100 | não | religar | corpo inteiro menos a cabeça |
| 3 (13) | 30 | não | levar golpe (`takeHit`, 50 ms) | braços, cabeça (17), torso, pernas |

Canais do 0x27A6 por parte: 0..6 quadril (vis, rx..tz), 7..13 `1233610`, 14..49 pernas (9 por perna: 3 ossos x rx/ry/rz), 50..91
seis ANIM_XFORM sem filhos, 92..98 `123a710`, 99..100 tz de `123a5f0`/`123a4d0`, 101..104 torso (rx, ry, rz, tz), 105..128 cabeça e
3 peças, 129..140 braço esquerdo, 141..173 garra esquerda, 174..185 braço direito, 186..218 garra direita (os vis das pontas em 150,
163, 173, 195, 208, 218). Nenhum canal sobra sem nó. As animações grandes têm 212 curvas (194/195/10/9/3 têm 194..201).
Instâncias menores: 0x27A7 (seção 2), 0x27A9 (seção 2), 0x27A8 (anim 0, 15 q: V de −0,5 a 0) e a sem id (anim 0, 60 q, V 0→2).
