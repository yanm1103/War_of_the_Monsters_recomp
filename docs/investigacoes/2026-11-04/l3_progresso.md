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

## Pendente (próximos passos, na ordem)

1. **Diferenças no monview**: na anim 22 a garra que cresce (escala animada do `sc_`) não aparece, porque as peças rígidas
   perdem a escala animada. Opção: escala por canal não existe no ANIM_XFORM; avaliar um OBJECT rígido de verdade sob a junta
   (ANIM_SCALE + rígido, como o jogo faz) ou aceitar. O enquadramento do monview também sai diferente (o boneco aparece mais
   alto e à direita); conferir se é a esfera do GROUP/LOD (o monview enquadra por ela?) ou o deslocamento da raiz.
2. **Abrir no jogo** como monstro custom (`monster.json` com `files`): ps2EntryRunner com `WOTM_VU1CHECK`, depois uma luta
   `WOTM_MATCH` + `WOTM_AIVSAI`. Ler o log ([custom], [mods] png, contatos/passos). Testes ao vivo com `WOTM_MONITOR=1`,
   `WOTM_MUTE=1`, `WOTM_FULLSCREEN=0`, `WOTM_RES=224`.
3. **Esqueleto de outro monstro**: Robo-47 ou Agamo (`--edit`) sobre a base Togera. Exercita texturas novas (PNG do .glb) e
   localizadores com outra orientação. Congar não serve de base, mas serve de rig.
4. **Rig do Blender do zero**: script do Blender 5.1 (`C:/Program Files/Blender Foundation/Blender 5.1`) que monte um bípede
   simples virado para -Y em metros, com `loc_*`, ações a090/a120; mais a volta Blender de um export do kit (eixos dos ossos
   mudam na importação: conferir que o ngprig não depende deles).
5. Tamanho: o NGP sai com ~3 MB (o corpo antigo fica morto no arquivo). Conferir o limite do carregador de monstro (MODS.md,
   "Mod files bigger than the disc's"); se apertar, reaproveitar a faixa do corpo antigo.
6. Kit: `modkit.py monster rig <mod> <id> <rig.glb>` e botão "Import rig..." na GUI; `pose.py`/`wotm_model.hpp` com mais de
   64 saídas, se precisar.
7. Docs (MODS.md, MODELS_3D.md seção "New skeleton", TOOLS.md, FORMATS.md se faltar campo) e tirar o L3 do `TODO.md`.

## Relato do sub-agente (o que do jogo depende do esqueleto)

- Só o Congar (tipo 0x20): `initDynamics` monta 8 BoneSpring em atMat fixos (4→0x816, 8→0x817, 20→0x818, 28→0x81A,
  22→0x819, 26→0x81B, 11→0x81C, 13→0x81D).
- O resto vai por id: canais (`animationGetChannelIdx`: AnimPappy 1..7, AnimContact), localizadores 0x802..0x832, anexos por
  id - tipo (+6 AILOD: `clearListFlags` zera +0x24 do último nível; manter o layout de LOD) e animações por id (< 300).
- O nativo não indexa osso por constante.
- Limites: animPktPtr 0..63, bloco ≤ 32 matrizes, código do osso no vértice 0..31, saídas < 64 no viewer, skelStack 5,
  skelMatStack 160, pilha de animação de 16 KB (avisar acima de ~600 canais), anim 191 sobrescrita pela retícula.
