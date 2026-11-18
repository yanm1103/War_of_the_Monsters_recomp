# Três bugs da fase MAUAC (base CENTRAL), 2026-11-18

Fase custom `mods/MauaClaude` (MAUAC sobre a CENTRAL, `"clear": "scenery"`, props AIR_B9ED/AIR_C0CE trazidos do AIRPORT,
CHAO/TERRENO do Blender e do New terrain). Relatos: pedestres como riscos brancos, hangar AIR_B9ED transparente com faces
internas magenta, janelas dos prédios em z-fighting só com a câmera andando. Os três eram de código (ferramentas e runtime), não
dos dados do mapa.

## 1. Pedestres como riscos brancos (17306d2, aac3efd)

**Causa.** O `"clear": "scenery"` tirava também os OBJECTs sem id pendurados em GROUPs com id. Os GROUPs 0x1B58..0x1C1F são as
texturas de partícula, e o `particleParseTexture` (chamado do `dbProcInteractive`) lê o OBJECT filho 0
desses grupos para montar `TEX0[]`/`texId[]`/`TextureInfo[]`. Com o slot apontando o nó NULL, a tabela ficava com lixo
(`WOTM_FXLIST`: texId 54908, TEX0 `0000000100000006` no lugar de tex289) e o desenho nativo das partículas, pedestres incluídos,
caía no blob branco.

**Medida.** Na CENTRAL o clear tirava 336 slots; 108 deles pendiam de nós que o jogo acha pelo id (texturas de partícula, armas
0xFA0.., power-ups 0x13EC.., HUD 0x1F44.., ponto 16000).

**Correção.** Uma só regra (fica o que pende de um nó achado pelo id) em `tools/lvl.py` (`found_by_id`/`clear_keeps`),
`tools/lvlglb.py` e `native/runtime/wotm_levelpatch.inc` (`foundById`/`clearKeeps`): o clear passa a tirar 228 slots, todos só
abaixo da raiz. `WOTM_FXLIST` mostra o texId e o TEX0 da textura de cada sistema. `tools/test_lvl_clear.py` passa nas nove bases;
no jogo os pedestres da MAUAC voltaram a ter textura. MODS.md descreve a regra (aac3efd).

## 2. Hangar transparente / magenta, e o CHAO/TERRENO (3d8e645)

**Causa.** `lvlprop` desenhava todo OBJECT com o `PRIM.ABE` do molde (CENTRAL 0xB08290, ABE 1) e alfa de vértice 0x80. As texturas
das fases têm alfa cru até 0xFF e o GS faz `As = At * Av / 0x80`; com ABE 1 e Av 0x80 sai As ~ 2, e a cor vira
`(Cs - Cd) * 2 + Cd`: a superfície escurece, clareia ou deixa ver o que está atrás. No AIRPORT o corpo (tex134) e o teto (tex132)
do hangar são desenhados com ABE 0; na prop saíam com ABE 1, e por isso o teto mostrava o interior e o xadrez magenta do chão.

O mesmo acontecia com os materiais `tex<id>` do Blender (CHAO, TERRENO), que é o pedido do coordenador no meio da tarefa: rua
preta, calçada oliva, terreno claro demais.

**Correção** (`tools/lvlprop.py`):
- units `game` (objeto de outra fase): material com `extras.gs.blend` nulo ganha ABE 0, como na origem.
- material `tex<id>` sem `extras.gs`: toma o ABE da maioria dos OBJECTs rígidos da fase com essa textura e, com ABE 1, o alfa de
  vértice deles como alfa cheio (`stage_tex_abe`). Na CENTRAL: tex229 (asfalto), tex160 (calçada) e tex270 (cascalho) ficam com
  ABE 1 e Av 0x42; tex122 (concreto) e tex255 com ABE 0; tex310 com ABE 1 e Av 0x0D (a fase o usa quase transparente, então ele
  sai bem apagado num prop).

**Conferido.** Props reconstruídas numa cópia do mod, fotos antes e depois na mesma pose: teto do hangar opaco como no AIRPORT,
asfalto cinza-escuro, calçada bege e terreno cinza. Testes `test_lvlprop_destr/hull`, `test_prop_matrix`,
`test_lvl_terrain/floor/extras` e `level_editor/test_collision` passam.

## 3. Janelas e placas em z-fighting com a câmera andando (207471a)

**Causa.** `level_object` (lvlprop) copiava os 0x70 bytes do cabeçalho do OBJECT molde, `+0x28` (zBufferFudge) junto: todo OBJECT
de prop saía com 1.01099, o valor do molde 0xB08290 da CENTRAL. A camada nativa aplica o fudge quando ele está entre 0.5 e 2 e é
diferente de 1, escalando o vértice ao longo do raio a partir do olho (`wotm_native.hpp`: `out = eye_ + (out - eye_) / fz`). Um
fudge uniforme não separa nada. Os fudges que separam vêm da fase de origem e são por OBJECT, mas o prop agrupa por textura e os
perdia. Valores medidos no AIRPORT:
- AIR_C0CE (handle 0xAF7540): as paredes (0xADB170, 0xADBD50) têm 1.0; 0xADB930 (tex169, tex131, tex154) e 0xADC060 (tex111 e
  tex110, as placas GATE/NOTICE) têm 1.00305.
- AIR_B9ED (0xB03F60): o vidro sem textura (0xAF7590) tem 1.00488.

No prop, placa e parede ficavam coplanares e o z-buffer escolhia uma ou outra pixel a pixel. Com a câmera parada o padrão é fixo
e parece desenho; com ela andando, cintila. A interpolação (`WOTM_INTERP`) não tem a ver: com o mesmo fudge em tudo, o que é
coplanar continua coplanar.

**Correção.**
- `tools/lvlglb.py`: um OBJECT com `+0x28` diferente de 1 grava `extras.zFudge` no material da cena.
- `tools/lvlprop.py`: o `zFudge` do material (extras do lvlglb, ou custom property do Blender entre 0.5 e 2) entra na chave do lote,
  o que separa os OBJECTs, e vai para o `+0x28` de cada OBJECT; sem ele o valor é 1.0. `prop.json` (stats) e o preview mostram o
  valor.
- `native/runtime/wotm_freecam.inc`: `WOTM_FREECAM=<s>@x,y,z,heading,pitch,vaivem` faz a câmera fixa ir e vir de lado (seno de
  4 s), para fotografar o que só aparece em movimento.

**Conferido.** O AIR_C0CE reconstruído tem as paredes com 1.0, os OBJECTs de tex169/131/154/111/110 com 1.003052 e o vidro do
AIR_B9ED com 1.004883. No jogo, MAUAC só com os dois hangares, câmera em `1100,1150,130,180,-5` com vaivém de 150 e seis fotos entre
24 e 25.5 s: com os props antigos (1.011) aparecem faixas magenta e cortes nas janelas da fachada do AIR_C0CE, que mudam de foto
para foto; com os novos a fachada fica estável e as placas GATE/NOTICE inteiras.

## O que fica para o dono

- Reiniciar o editor (o cache da cena é chaveado pelo carimbo do lvlglb e se regenera sozinho).
- Reimportar AIR_B9ED e AIR_C0CE do AIRPORT, e não só reconstruir: o `source.glb` antigo não tem `zFudge`, e reconstruído a partir
  dele tudo sai com 1.0 (sem o z-fighting do 1.011 uniforme, mas também sem a separação das placas).
- Reconstruir CHAO, TERRENO e os outros props. Eles saem com fudge 1.0 em vez de 1.011, então duas superfícies de props diferentes na
  mesma altura (TERRENO e CHAO, por exemplo) podem brigar. O remédio é um custom property `zFudge` (1.003 ou parecido) no material
  que deve ficar na frente.
- tex310 num prop fica quase transparente (Av 0x0D, como a CENTRAL o usa).
