# Cópia de prédio com desabamento animado (2026-11-06)

Relato do dono: "duplicar prédio que tem animação de cair -> as cópias até conseguem ser destruídas, mas só caem quando o original
cai". Contexto anterior: `copia_profunda.md` (mesma data).

## Causa
- CENTRAL 0b65a70: DESTRUCTIBLE 0b65a40, estado 1 -> GROUP 0b57570 -> CHAR_INSTANCE 0b4bef0 (dentro da alça) com 2 ANIM_XFORM
  (0b4b700 gira e anda, canais 0..5; 0b4bd10 só aparece pelo canal de visível 7). O ACT_ANIM 0b65984 do estado 1 (obj 0, id2 0,
  anim 0x385) acha essa instância pelo `animationGetHandle` (primeira com id1/id2 e uma animação de id 0x385).
- A cópia profunda parava no CHAR_INSTANCE (ficava em comum) e tirava o ACT_ANIM da cópia. A cópia quebrada mostrava a mesma instância
  parada no quadro 0; quando o original quebrava, o ACT_ANIM dele tocava a instância comum, e a cópia caía junto.
- Varredura das 19 fases: cada CHAR_INSTANCE tem `val`/`dirty`/`atMat`/`animCtx`/`prevKey` próprios (nenhum em comum), o control node
  aponta para a animação do personagem, os campos de execução (animOutput, next/prev) são 0 e `activeTree` é 0 no disco;
  `animPktPtr` é igual a `val` em todas; nenhum SKEL_BONE debaixo de instância de objeto copiável; nenhum `matrixIdx` passa de
  `numAnimTran`. O ANIM_XFORM guarda estado da instância no próprio nó (visível, ângulos, bits de sujo); as matrizes ficam em
  `atMat[matrixIdx]` da instância.
- O gancho roda na entrada do `dbInitDb`, antes do `animationSetGlobal`: uma instância nova registrada no ANIM_MGR ganha o AnimPlayer e
  os control nodes iniciados pelo próprio jogo.

## O que mudou (`wotm_levelpatch.inc` e `lvl.py`, mesmas funções e contadores)
- `spine`: desce nas partes animadas (para só no SKEL_BONE). Primeira passada: a instância entra quando um ACT_ANIM de um destrutível
  da cópia a anima (`animTargets`, regra do `animationGetHandle`) ou há nó mutável abaixo dela. Segunda passada: os ANIM_XFORM/
  ANIM_SCALE dessas instâncias (a instância mais perto acima) entram também.
- `cloneAnimState`: o clone ganha `val`, `dirty`, `atMat`, `animCtx` e o `prevKey` de cada control node, copiados do disco;
  `animPktPtr` no `val` novo, `activeTree` 0, id2 novo (de 0x7FF para baixo, livre para aquele id1; o dupId dos monstros é pequeno).
- `registerInstances`: no fim de cada entrada com cópia, um ANIM_MGR novo na arena (lista velha + clones) no lugar da raiz.
- `rewriteAct`: ACT_ANIM cuja instância foi clonada vai para o clone (id2 em `ids.anim`); os outros continuam saindo.
- Destrutível dentro de parte animada deixa de ser recusado (só dentro de esqueleto, que não existe nas fases); os emissores de lá são
  clonados ("emitters left shared" some).
- Custo conferido antes: blocos do estado de animação, ACT_ANIM refeitos, o ANIM_MGR novo.
- Contadores: `anim instances cloned`, `anim actions redirected`. `lvl.py patch` ganhou duas checagens: ACT_ANIM das cópias que animam
  a parte do original, e CHAR_INSTANCEs das cópias fora do ANIM_MGR.
- `WOTM_LEVELPATCH_BREAK` aceita `:<estado>` (o `ChangeState` só roda as ações do estado de destino, e no CENTRAL o desabamento é o
  estado 1; levar direto ao final pula a animação) e a linha de +4 s conta as instâncias só de um lado e quantas tocam (AnimPlayer com
  árvore principal) agora e na quebra.

## Testes
- `lvl.py patch`, uma cópia de cada objeto com ACT_ANIM (patch gerado, fora do repo), antigo (HEAD) x novo, rc 0 nos dois:
  - CENTRAL 5 objetos: `anim instances cloned 4, anim actions redirected 5`, `global actions dropped` 12 -> 7 (0dfe970 mira obj 0x38c,
    que não existe na fase: sai como antes), `emitters left shared 3` some (+3 emissores clonados).
  - ISLAND 5: instâncias 5, refeitos 5, dropped 6 -> 1. METRO 4: 4, 4, dropped 4 -> 0.
  - TOKYO 7: antes 2 pulados (0cb1720 e 0d8b0e0, destrutível dentro de CHAR_INSTANCE), agora os 7 entram; instâncias 6, refeitos 6.
  - Regressão: `mods/LevelTest` (CENTRAL) idêntico; `mods/Teste` CENTRAL (anim 4, refeitos 6) e 3-MILE (as 2 instâncias com emissores
    dentro agora clonadas, `emitters left shared 6` some); aninhada (0b376a0 e 0b65a70): rc 0. Fora os endereços da arena, o resto do
    log igual; as 7 checagens em 0 em todos.
- No jogo (`WOTM_LEVELPATCH=1`, IA x IA, `WOTM_CHEATS=inv`), log de contagens igual ao do `lvl.py`:
  - CENTRAL, cópia de 0b65a70 em (-638,-424,0) heading 90. `BREAK=15:copies:1`: cópias 1 instância própria, tocando 1 (0 na quebra);
    originais tocando 0; fora 0. `BREAK=15:originals:1`: o contrário (originais tocando 1, cópias 0).
  - TOKYO (`WOTM_MATCH=3,1,9`), cópia de 0d8b0e0 (antes recusada) em (1841,1275,0): ACT_ANIM no estado final. `15:copies`: cópias
    tocando 1, originais 0; `15:originals`: originais 1, cópias 0, e os dois vizinhos do dominó do original (0f19c60, 0f19e30) mudam.
    Na quebra das cópias mudaram 0f12780 e 0f19c40, que não são alvo de dominó da 0d8b0b0 e ficam longe da cópia (a luta).
  - CENTRAL com os 5 prédios animados copiados, `15:copies:1,25:copies,35:originals:1,45:originals`, 100 s: 6 de 6 em cada passo,
    instâncias 4/4 tocando só do lado quebrado até os originais quebrarem, fora 0, sem CRASH nem CONGELOU.
- Na rodada de 40 s a linha de +4 s das cópias não saiu: o processo foi fechado antes (carga ~15 s + 15 s + 4 s); com 60 s saiu.

## O que fica
- Parte animada que o próprio jogo anima pelo id (scripts de fase, chefes: `animationGetHandle` com id2 0 no código) só mexe no
  original; a cópia fica parada.
- Cada instância clonada toma 0x18 bytes do `MemoryStack::global` (AnimPlayer) na carga.
- O teste visual (a cópia caindo na tela) fica para o dono.
