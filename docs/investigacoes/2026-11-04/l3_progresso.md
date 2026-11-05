# L3 (esqueleto novo): progresso até a pausa de 2026-11-04

Item 8 do `TODO.md`, último marcador. Contexto do formato em `monstro_novo.md` (mesma pasta). WIP no branch `wip/l3-ngprig`.

## O que existe

- `tools/ngprig.py <base.NGP> <rig.glb> <saida> [--name N] [--front +y|-y] [--scale S | --height H] [--no-fill]`. Usa só a
  biblioteca padrão e reaproveita `ngpmesh` (malha e pacotes), `ngpanim` (curvas, solver PRH) e `vifpkt`. A docstring do
  arquivo descreve as regras.
  - Base: tudo dela fica (código do tipo, sombra, retícula, raiz 1, COLL_GRID, sons). A raiz 0 e o ANIM_MGR passam a apontar
    para um CHAR_INSTANCE novo no fim do arquivo; o antigo fica morto no arquivo.
  - Por junta mantida: SKEL_BONE (restPoseInv = inverso do repouso do .glb) seguido de ANIM_XFORM (bits=1, pré-rotação =
    rotação local de repouso, ângulos de repouso 0, canais rX rY rZ tX tY tZ consecutivos, máscara 0x3F). A raiz principal
    tem hasColl (bit 4).
  - Paleta em blocos de até 32 matrizes (o Jelly usa 32), um OBJECT por bloco e até 5 texturas. Junta usada em dois blocos
    ganha uma saída a mais (SKEL_BONE + TRANSLATE(0)).
  - Canais extras: 6 de contato (0x802..0x805, 0x816, 0x817), gerados pela altura dos localizadores; 7 do AnimPappy (ids 1..7),
    copiados da animação da base de mesmo id e esticados para o novo comprimento. Os sons vêm do cabeçalho da animação da base.
  - Id da base que falta no .glb: entra com a pose de repouso e o comprimento da base (`--no-fill` deixa de fora).
  - Localizadores: empties ou juntas `loc_<hex>` ou por apelido (`hand_l`...). Os que faltam vão para onde a base os tem, na
    junta mais próxima, com aviso. Colisão: caixas `coll*` ou uma caixa automática sob um ROTATE que desfaz o repouso da raiz.
  - Texturas: `tex<id>` da base é mantida; imagem nova recebe texId novo (cópia do pacote do corpo no .TEX) mais
    `<nome>_t<id>.png`; material sem imagem vira um PNG da cor dele.
  - Eixos: com a raiz "WotM" do kit, só o mundo é convertido (os eixos locais já são os do jogo). Numa cena do Blender, os eixos
    locais giram junto, o modelo é virado para +Y e posto na altura da base.
  - Malha rígida entra como aparece no 1º quadro do idle, presa à junta sem escala mais próxima acima dela.
- `tools/monglb.py --edit` exporta os localizadores como empties `loc_<id>` filhos da transformação de cima. O ngpmesh e o
  ngpanim ignoram esses nós.
- Base Congar recusada: BoneSpring mexe nos atMat 4, 8, 11, 13, 20, 22, 26 e 28 (relato do sub-agente, resumido abaixo).

## Como foi medido

Teste de identidade: o `LIZARD0` exportado com `monglb --edit` (com localizadores) e reimportado pelo `ngprig` sobre a base
`LIZARD0` (`build/l3/`, fora do repo).
- 42 juntas, 35 saídas, 2 blocos (32 + 3), 2 OBJECTs, 151 animações, 265 canais. Altura 119,2 (a base tem 119,3).
- Os 16 localizadores caem na mesma posição da base.
- Comparação junta a junta do mundo no modelo novo e na base (`ngpanim.Game.anim_pose`, animações 90, 0, 22, 60, 120 e 150):
  rotação < 0,001 e posição < 0,07 unidade; o resto é a tolerância de chaves do export.
- `build/monview.exe` abre e anima o resultado (2716 triângulos). Na anim 22, quadro 20, a pose bate com a da base.

Erros encontrados e corrigidos no caminho:
- Eixos locais convertidos duas vezes no .glb do kit (cada junta saía girada 90°; a pele não sentia, os localizadores sim).
- A pose de ligação do kit tira a escala dos nós de escala, e as garras do Togera saíam fora do lugar. Agora a malha rígida
  vem do idle.

## 2026-11-05: peças rígidas vivas e primeira luta

- Peça rígida que escala ou se move em relação à junta em alguma ação (a garra que cresce do Togera) agora vai como o jogo
  faz: ANIM_XFORM próprio (sem SKEL_BONE; o jogo tem vários assim, o Robo-47 tem 18) → ANIM_SCALE (só escala, pivô 0) →
  GROUP → OBJECT rígido por textura (cabeçalho do corpo com `+0x68 = -1`, `polyPkt` no pacote com a tag DMA). A escala de
  osso não chega à pele (o esqueleto do jogo não tem escala); o ngprig avisa quando uma ação escala junta da pele.
- Comparação da garra (vértices no mundo, `clawcmp.py` de rascunho) com a base: igual até 0,1 nas anims 22 e 90; monview
  igual à base, e o enquadramento também (a diferença de antes sumiu com as garras fora da paleta).
- No jogo: mod `teste_l3` no runtime (`discroot/mods/teste_l3/monsters/rigliz`, base togera, `files`), `WOTM_MATCH=rigliz,3,1`
  + `WOTM_AIVSAI` + `WOTM_VU1CHECK=1`, 110 s: carrega (LIZARD5, 3 MB em 0x2000000, 28 MB até o teto), luta sem queda nem
  congelamento, checagem média 99,78% casados (a mesma luta sem mod, Togera x Togera: 99,87%).
- Bug achado pela checagem e corrigido: na refatoração do ANIM_XFORM o laço dos canais reaproveitava a variável do índice e
  todo ANIM_XFORM saía com `matrixIdx` 2. A pele não sentia (cada osso é calculado na hora), as garras saíam com a matriz
  errada só no VU1 (o monview e o ngpanim reconstroem pelos canais).
- Achado à parte (não é do L3): nas lutas com o Togera do disco, as garras rígidas dele divergem (o nativo desenha uns 16
  triângulos que o VU1 não desenha, flags 0x311, sob o LOD de 4 níveis). As garras do rig novo, num GROUP sem LOD, casam.
- Esqueleto do Robo-47 (`monglb --edit` do ROBOT0) sobre a base Togera (`build/l3/t2`, mod `teste_l3/robrig`): 46 juntas,
  20 saídas, 19 peças rígidas vivas (as que o Robo-47 esconde e mostra pelo canal de visibilidade, que o kit exporta como
  escala 0; antes elas ficariam presas na junta e sempre visíveis), 16 localizadores do próprio .glb, 162 animações, 460
  canais. Textura nova: o PNG do .glb vira o texId 13 (`rigrob_t0013.png`) e um material sem imagem vira o 14. O ngprig
  quebrava aqui (o texId das peças vivas não era montado); corrigido. Monview: pose igual à do ROBOT0 (a textura é a do
  Togera, porque o monview não lê os PNGs). No jogo (`WOTM_MATCH=robrig,3,1`, 110 s): `LIZARD6.TEX t13/t14 <- png`, sem
  queda, 99,83% casados em média (mínimo 98,9%), nenhuma divergência dentro do arquivo novo.
- Rig do Blender do zero (`build/l3/biped.py`, Blender 5.1 em fundo: `blender -b --factory-startup --python biped.py --
  saida.glb`): bípede de caixas, 16 ossos, Z para cima, virado para -Y, ~2 m, pesos 1 por osso (meio a meio nas juntas),
  9 empties `hand_l`... presos a ossos, ações `a090` e `a120` (cena a 30 fps). O ngprig gera em menos de 1 s: escala 58,3
  para a altura da base, 9 localizadores do .glb e 7 postos onde a base tem, 151 animações (149 em repouso). Os ossos
  `head` e `chest` têm nome de apelido de localizador: agora os empties têm preferência sobre ossos de mesmo nome. Monview e
  `ngpanim` conferem a pose (simétrica; a pose de zumbi do idle é do script de teste). No jogo (`biped`, 110 s): PNG da cor
  aplicado, sem queda, 99,93% casados.
- Volta pelo Blender de um export do kit (`build/l3/roundtrip.py`: importa o `LIZARD0_edit.glb` e exporta de novo): os
  eixos dos ossos mudam, mas o ngprig não depende deles. Com a cena a 60 fps antes da importação, os 16 localizadores ficam
  a no máximo 0,064 unidade dos do Togera nas anims 0, 22, 30, 60, 90, 120 e 150 (igual ao arquivo direto). Com a cena
  no padrão de 24 fps, perdem até 14 unidades nos golpes rápidos (reamostragem); o MODS.md já manda pôr 60 fps, e o ngprig
  agora avisa quando um .glb do kit chega com chaves mais espaçadas que 1/60 s (o export direto não dispara o aviso).
- Os scripts de conferência ficam em `build/l3/` (fora do repo): `loccmp.py` (localizadores no mundo, duas NGP) e
  `clawcmp.py` (vértices das peças rígidas no mundo).

- Kit: `modkit.py monster rig <mod> <id> <rig.glb> [--front] [--scale | --height]` e o botão "Import rig..." no painel do
  monstro custom (status "own skeleton: <glb>"). Conferido na linha de comando num mod de rascunho com o `biped.glb`: os 4
  arquivos e o PNG da cor na pasta de trabalho, `files` ligado, o pack leva tudo, mesh/anim import e mesh export recusam com
  a razão certa. O botão da GUI não foi aberto na tela. Os rigs de teste têm de 16 a 35 saídas, abaixo das 64 do viewer: o
  `pose.py`/`wotm_model.hpp` ficam como estão.
- Docs: MODS.md ("Own skeleton"), MODELS_3D.md ("New skeleton" e a linha do `ngprig.py`), FORMATS.md (`+0x68` do OBJECT
  rígido); L3 fora do `TODO.md` (era o último marcador do item 8).

## Pendente

1. O dono testa o "Import rig..." na GUI e decide o merge na `master`. No merge, esta pasta sai (a história fica no git).

## Relato do sub-agente (o que do jogo depende do esqueleto)

- Só o Congar (tipo 0x20): `initDynamics` monta 8 BoneSpring em atMat fixos (4→0x816, 8→0x817, 20→0x818, 28→0x81A,
  22→0x819, 26→0x81B, 11→0x81C, 13→0x81D).
- O resto vai por id: canais (`animationGetChannelIdx`: AnimPappy 1..7, AnimContact), localizadores 0x802..0x832, anexos por
  id - tipo (+6 AILOD: `clearListFlags` zera +0x24 do último nível; manter o layout de LOD) e animações por id (< 300).
- O nativo não indexa osso por constante.
- Limites: animPktPtr 0..63, bloco ≤ 32 matrizes, código do osso no vértice 0..31, saídas < 64 no viewer, skelStack 5,
  skelMatStack 160, pilha de animação de 16 KB (avisar acima de ~600 canais), anim 191 sobrescrita pela retícula.
