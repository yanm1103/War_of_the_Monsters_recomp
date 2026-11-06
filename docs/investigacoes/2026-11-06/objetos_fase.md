# Objetos da fase no .NGP: o que o editor de fase precisa saber (etapa 1)

Tema deste relatório: como prédios e objetos aparecem na hierarquia do `.NGP` de fase, o que a carga (`dbInitDb`) registra
deles e onde um patch (mover, girar, apagar, duplicar, copiar de outra fase) tem de entrar. A colisão está em
`colisao_fase.md` (mesma pasta) e os `PTS/*` em `pts_fase.md`; aqui a colisão só aparece onde toca o objeto.

Marcação: **[certo]** = lido no código (C++ decompilado ou asm conferido); **[medido]** = contado nos 19 `.NGP` de `disc/LVL`
pelos scripts abaixo; **[hipótese]** = dedução não verificada no jogo rodando.

## Scripts (fora do repo)
Pasta `C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\objetos\`
(importam `tools/ngp.py` e `tools/tex.py`; só leem o disco do usuário):
- `objetos.py`: a prova pedida. `python objetos.py` dá o resumo das 19 fases; `python objetos.py CENTRAL -v` lista objeto por
  objeto (tipo, nó, alça, id, posição no mundo, rumo, estados, classe de vida, nós, OBJECTs, emissores, animados, colisão,
  bytes próprios em faixas, dados compartilhados, referências de fora, palavras da grade que apontam para ele, ações);
  `--csv arquivo.csv` grava tudo.
- Auxiliares: `arvore.py` (árvore a partir de um nó), `pais.py` (cadeia até a raiz), `ids.py` (ids por raiz e nós com vários
  pais), `acoes.py`/`acoes2.py` (ações dos estados e se os ponteiros delas estão no `.PTR`), `listados.py` (campos de ponteiro
  fora da lista 0), `grade.py`/`grade2.py` (ponteiros da área da grade), `luz.py` (bits de luz e cor de vértice), `modo.py`
  (GROUPs por modo, emissores, LIGHT), `texs.py` (texIds e listas do `.PTR`), `conta.py` (contagens e referências externas).

## Resumo
1. Prédio = `TRANSLATE` (ou `ROTATE`) → `INTERACTIVE_STATE` → `DESTRUCTIBLE` (2 ou 3 estados) → um GROUP por estado. Cada
   prédio é cópia própria (nenhum `INTERACTIVE_STATE` tem dois pais), mas dentro dele há malhas instanciadas, inclusive entre
   prédios diferentes. A posição é o `trans` do TRANSLATE acima do `INTERACTIVE_STATE`. CENTRAL: 67 destrutíveis (7 aninhados em
   outros), 115 pickups, ~149 "props".
2. A carga copia posição e matriz para tabelas próprias (Destructible, Pickup e seu `_cs`, emissores autoStart, grupos de
   pedestres) e escreve na imagem (id2 do slot, GROUP→NULL por modo, `numKids` do pickup). Limites: 1500 Destructibles (com
   checagem), 1024 Interactives (**sem checagem**), 1024 `_cs`, 100 sistemas de partícula. Logo, o patch estrutural tem de ser
   aplicado **antes do `dbInitDb`**.
3. A luz base é a cor de vértice, que anda junto com o objeto; luz paralela (poucos objetos) e pontual (muitos) são calculadas
   ao vivo pela matriz. Nenhum objeto da fase projeta sombra (todos `dontCastShadow`); a sombra projetada vem só dos `_cs`
   bloqueadores (monstros). Mover é aceitável; girar inverte o lado "aceso" do prédio.
4. Duplicar em RAM dá: o fim da imagem até 0x1F10000 está livre na luta com `WOTM_HIGHRAM` (6,4 MB no METRO, 13,4 MB no CENTRAL).
   Mas a lista 0 do `.PTR` **não tem todos os ponteiros**: falta `INTERACTIVE_STATE.child`, as ações dos estados e os caminhos da
   grade. A cópia precisa de um percurso tipado (o de `objetos.py`). Para acrescentar um filho, cria-se um GROUP novo e se
   reaponta um ponteiro de filho já existente. Copiar de outra fase exige juntar `.TEX` e `.RTX` (ids e paletas).
5. `DrawItem` só guarda o `OBJECT` e o `_cs`; como os OBJECTs são compartilhados entre prédios, o dono tem de ser anotado na
   travessia (`Scene::walk`): o último TRANSLATE/ROTATE acima do `INTERACTIVE_STATE`.
6. Hook na entrada do `dbInitDb` chamado pelo `Shell::LoadLevelFiles` (depois do `dbsRelocateFileZero`), com a área reservada
   logo depois da imagem. Edição ao vivo: só mexer no nó para ver, e recarregar a fase para valer.

## 1. Como um prédio aparece na hierarquia

### Raízes da imagem (CENTRAL) [medido]
25 raízes. As que importam: **4** = GROUP id 1, o mundo (`viewSetWorldEpNode`; 7823 nós); **5** = GROUP dos pickups (116
TRANSLATE/ROTATE com um GROUP de id 0x1389..0x13A2 cada, fora do mundo: o jogo desenha cada um pelo `_cs` do Pickup);
**1** = GROUP com armas, power-ups, texturas de partícula e HUD; **3** = céu (id 2); **6..21** = grupos de pedestres (TRANSLATE
id 0xBB8); **22** = GROUP id 5 com 18 `COLL_TRIG` (id 0x157D); **23** = `COLL_GRID`; **24** = `ANIM_MGR` (55 CHAR_INSTANCE).

### Cadeia de um prédio [medido, CENTRAL, prédio em (697.7, 972.2)]
```
GROUP#1 (mundo, r 6460)
 └ GROUP a00510 (10 quarteirões)
    └ GROUP a00330 → GROUP b8a430 (quarteirão, esfera r 804, 4 prédios)
       └ TRANSLATE b18310  t = (697.7, 972.2, 0.5)          ← alça: posição do prédio
          └ INTERACTIVE_STATE 1196e20 (whichChild +4, child +0xC)   ← na cauda da imagem, longe do resto
             └ DESTRUCTIBLE b182e0  hitPointClass 0, hp 100, 3 estados
                ├ estado 0 (intacto): GROUP b08260 fl 3
                │   ├ CONTROL b08120: child1 = GROUP → LOD (5000, 1500)
                │   │     ├ LOD 0: TRANSLATE (-697.7, -972.2, -0.5) → OBJECT em coordenadas de mundo (24 vértices)
                │   │     └ LOD 1: GROUP → OBJECTs (118, 68 vértices) + LOD interno
                │   │   child2 = GROUP com a versão "fantasma" (CONTROL escolhe pela visão do monstro)
                │   └ GROUP fl 0x102 (colisão) → POLY_COLL
                ├ estado 1 (danificado): GROUP fl 3 → GROUP/CONTROL/LOD, POLY_COLL, NULLs, GROUPs de PART_EMITTER (fumaça)
                └ estado 2 (ruína):     idem
```
- O LOD mais grosseiro vem em coordenadas de mundo e é desfeito por um TRANSLATE inverso logo abaixo do LOD. Mudar só o TRANSLATE
  de cima continua coerente: o objeto vai para `local + nova posição` [certo pela conta].
- As malhas e nós de colisão de todos os estados ficam em coordenadas locais do TRANSLATE. A colisão (`POLY_COLL`) é irmã do
  CONTROL visual dentro do GROUP do estado e anda com o TRANSLATE na hierarquia. A grade, porém, guarda caminhos com a AABB fixa
  (ver `colisao_fase.md`) [medido].
- **Posição e orientação** [certo]: TRANSLATE `trans` em +0x10; ROTATE matriz 4x4 em +0x10, com a translação na linha 3
  (+0x40); SCALE `trans` +0x10 e `scale` +0x20. Convenção do `tools/ngp.py`: vetores-linha, Z para cima.
- Alça dos destrutíveis nas 19 fases [medido]: 997 TRANSLATE, 398 ROTATE, 187 sem transformação (o INTERACTIVE_STATE pendurado
  direto num GROUP, com a malha em coordenadas de mundo). No CAPITOL, 15 destrutíveis dividem o TRANSLATE com irmãos (o
  Capitólio e as colunas). Para mover um destes sozinho é preciso **inserir** um TRANSLATE próprio.
- **Aninhados** [medido]: 303 destrutíveis estão dentro do estado de outro (placas e caixas d'água no topo dos prédios: ROTATE
  → INTERACTIVE_STATE dentro do LOD do estado 0). Movê-los é relativo ao prédio, e o prédio carrega os seus.
- No disco, todo nó das fases tem `id1` 0 no INTERACTIVE_STATE, menos os especiais: 3-MILE 0x2712, 0x2715..0x2718, 0x2265;
  SANFRAN 0x275D..0x2762; CANYON2 0x2775..0x2777; CAPITOL 0x27AE..0x27B3 [medido].

### Instâncias [medido]
- Nenhum `INTERACTIVE_STATE` nem `DESTRUCTIBLE` tem mais de um pai nas 19 fases: **cada prédio é cópia própria**.
- Dentro deles há instanciamento: no CENTRAL, 45 nós têm vários pais (LOD, GROUP, OBJECT, CONTROL, CHAR_INSTANCE), por exemplo
  um LOD (janela/caixa de ar) pendurado em 10 TRANSLATEs, metade num prédio e metade em outro. Eles moram numa área comum
  (0x1100000..0x115E000 no CENTRAL). Os destroços (`ActDebris`) de todos os prédios usam a mesma malha comum (0x10F0000...).
- Os pickups também compartilham a parte visual: TRANSLATE → GROUP (id do pickup) → SWITCH → GROUP → **CONTROL comum**.
- Os blocos de contexto do GS (`ctx` em +4 de GROUP/TRANSLATE; ~150 B) são compartilhados: ~26 por prédio no CENTRAL.

### Contagem por fase [medido, `objetos.py`]
| fase | imagem (B) | folga até 0x1F10000 | destr | aninhados | sem alça | pickups | props | estados | Interactives (destr+pickups) |
|---|---|---|---|---|---|---|---|---|---|
| 3-MILE | 8633040 | 12,8 MB | 193 | 7 | 10 | 34 | 59 | 399 | 227 |
| AIRPORT | 3594192 | 17,6 MB | 29 | 7 | 6 | 47 | 36 | 64 | 76 |
| BIGSHOT | 536368 | 20,6 MB | 0 | 0 | 0 | 0 | 1 | 0 | 0 |
| CANYON1 | 2276464 | 18,9 MB | 79 | 0 | 0 | 6 | 6 | 158 | 85 |
| CANYON2 | 2734592 | 18,5 MB | 16 | 0 | 0 | 18 | 15 | 47 | 34 |
| CAPITOL | 9882624 | 11,6 MB | 66 | 20 | 22 | 42 | 66 | 174 | 108 |
| CENTRAL | 8043456 | 13,4 MB | 67 | 7 | 1 | 115 | 149 | 169 | 182 |
| CRUSH | 1872256 | 19,3 MB | 22 | 2 | 1 | 0 | 1 | 52 | 22 |
| DBALL | 1038320 | 20,1 MB | 0 | 0 | 0 | 1 | 17 | 0 | 1 |
| ISLAND | 7436416 | 14,0 MB | 170 | 39 | 30 | 95 | 213 | 353 | 265 |
| METRO | 15423744 | 6,4 MB | 206 | 43 | 27 | 201 | 313 | 483 | 407 |
| MINISF | 5713408 | 15,6 MB | 49 | 16 | 6 | 25 | 33 | 132 | 74 |
| SANFRAN | 8958240 | 12,5 MB | 142 | 60 | 10 | 50 | 73 | 286 | 192 |
| TOKYO | 5423344 | 15,9 MB | 105 | 47 | 6 | 45 | 116 | 227 | 150 |
| TOKYODM | 3606848 | 17,6 MB | 122 | 11 | 1 | 52 | 86 | 254 | 174 |
| TRAINING | 369680 | 20,7 MB | 0 | 0 | 0 | 0 | 1 | 0 | 0 |
| UFO | 5333728 | 16,0 MB | 122 | 0 | 23 | 11 | 1 | 244 | 133 |
| VEGAS | 8081392 | 13,4 MB | 125 | 44 | 44 | 117 | 73 | 278 | 242 |
| VOLCANO | 2571168 | 18,6 MB | 69 | 0 | 0 | 41 | 3 | 138 | 110 |

Total: 1582 pares INTERACTIVE_STATE + DESTRUCTIBLE alcançados das raízes (o `FORMATS.md` fala em 1566; a contagem daqui segue
todos os estados e os filhos dos CONTROL), 900 pickups e 1262 props. "Props" é heurística [hipótese]: TRANSLATE/ROTATE/SCALE mais
alto, filho de GROUP, dentro do mundo, fora de destrutível e pickup, com OBJECT embaixo. Entram aí pedaços de terreno com
TRANSLATE nulo. Tamanho de um destrutível sem os compartilhados: CENTRAL média 80 KB, máximo 210 KB; o maior de todos fica no
SANFRAN, com 1,4 MB [medido].

## 2. O que a carga registra e guarda

Fluxo [certo]: `Shell::LoadLevelFiles` → `LoadLevelDB` (inflate do `.NGP` em 0xA00000) → `LoadMonstersDB` → `LoadResTexture` →
`LoadTexture` → `dbsRelocateFileZero` (das três listas do `.PTR`, numeradas aqui 0, 1 e 2, só aplica a 2: `TBP0 += vram/64`) → `dbInitDb`. O `dbInitDb` chama
`dbsTraverse(raiz, dbProcInteractive)` para cada raiz que não é `ANIM_MGR`. O `dbsTraverse` acumula translação e rotação e chama o
callback com **posição e matriz do mundo** para cada nó com id, e sempre para INTERACTIVE_STATE e DESTRUCTIBLE. Ele visita
**todos** os estados do DESTRUCTIBLE e os dois filhos do CONTROL. Nota lateral: o rascunho `NON_MATCHING` do `dbsTraverse` em
`src/common/dbs.cpp` não soma o `trans` no caso TRANSLATE; o asm soma (3 `add.s` sobre a translação acumulada).

| quem | guarda | limite | consequência para o patch |
|---|---|---|---|
| `Destructibles::AddDestruct` | ponteiros para o DESTRUCTIBLE (+0x24) e o INTERACTIVE_STATE (+0x28); **cópia** da posição (+0x40) e da matriz transposta (+0x50); vida pela classe | 1500 (checa `m_count`) [certo, asm] | posição/matriz congeladas na carga: ações (destroços, câmera, `shedMonsters`) usam a cópia |
| `Interactives::addInteractive` | ponteiro do objeto em `s_dbInteractive[1024]`; o índice vai para o `id2` (bits 7..17) do INTERACTIVE_STATE | **1024, sem checagem** [certo, asm]; id2 tem 11 bits | duplicata além de ~1024 − (destr + pickups + monstros + outros) corrompe memória. O METRO já usa 407 |
| ids especiais em `AddDestruct` | `sanFranSaveInteractive` (0x275D..0x2762), colunas do CANYON2 (0x2775..0x2777, vetor de 4) e do CAPITOL (0x27AE..0x27B3, vetor de 8), **contador `s_numPillars` comum aos dois** | 4 / 8 | duplicar um destes sem zerar o `id1` estoura os vetores |
| `LevelPickups::createPickup` + `Pickup::init` | objeto na `MemoryStack::global` (0x170..0x4C0 B), um `_cs` do `CsPool` (1024); **cópia** de pos (+0x20), rot (+0x30), `cs->trans/rot/mat`; escreve `whichChild = 0` no SWITCH e **`numKids = 1` no GROUP** (some com o grupo de locators) | `_cs` 1024; pilha 512 KB | mover o TRANSLATE depois da carga não muda nada; a cópia tem de nascer antes do `dbInitDb` |
| `Pickup::initAfterDbLoad` | `addInteractive` + id2 no epNode; `hdReparentCsGrid` (inscreve o `_cs` na grade pela posição) | | idem |
| `PedGroup` (0xBB8) | posição copiada; 0x190 B na `gMemStackDb` | | — |
| emissores com `isAutoStart` (bit 3 de +0x5C) | `particleCreateModeledFx` com a **posição do mundo** já na carga; marca `isActivated` | `fx[100]` | 3 no CENTRAL, até 12 numa fase [medido]. Os outros 99 % são ativados na travessia de desenho (`hierParticleNode`), na primeira vez que o estado aparece, com a matriz daquele quadro. Depois o fx fica onde nasceu, exceto `isMoving` |
| LIGHT com lens flare | nada na carga: `hierLightNode` lê a cada quadro (até 74 por quadro) | | segue a hierarquia. Há 7 LIGHT em todas as fases |
| `ANIM_MGR` (raiz) | lista de ponteiros para os CHAR_INSTANCE (`animationSetGlobal`) | | 18 destrutíveis/props animados no total; a cópia compartilha a instância ou entra num `ANIM_MGR` novo (como faz `tools/ngpgraft.py`) |
| `hdSaveColGrid` (COLL_GRID) | ponteiro para a grade | | caminhos com ponteiros crus para TRANSLATE, INTERACTIVE_STATE, DESTRUCTIBLE, POLY_COLL... (ver 4) |
| `dbsTraverse` em si | escreve na imagem: bit 11 de +0xC do OBJECT (UV animado); **GROUP com flag 0x10/0x20/0x80 vira NULL (op 9) conforme o modo**; isActivated dos autoStart | | 1 a 13 GROUPs por fase dependem do modo [medido]; o patch não deve depender deles |
| scripts 0x2134.. e por fase 0x2710.. | `ActionDispatch::addAction` com a matriz/trans do próprio SCRIPT; `threeMileAddEpNode`... guardam ponteiros | | — |

**Conclusão [certo]:** apagar, duplicar e mover de verdade (colisão, ações, pickups) tem de mexer na imagem **antes** do `dbInitDb`.
Depois dele, mexer no TRANSLATE só move o desenho (e a colisão a cada consulta, mas sem a AABB e as células: `colisao_fase.md`).

## 3. Luz e sombra
- **Cor de vértice** [certo + medido]: o VU1 multiplica a cor do vértice; nas amostras, 55..70 % das malhas dos prédios têm cor
  variada (luz pré-calculada). Ela anda com o objeto. Ao **mover**, o prédio leva a própria luz (aceitável). Ao **girar**, a
  face pré-iluminada gira junto e pode ficar contra o sol do céu [hipótese visual]. Se o chão tiver sombra ou oclusão "assada"
  na cor de vértice embaixo do prédio, ela fica no lugar antigo [hipótese: não conferido].
- **Luz paralela** (`dirLight`, 0x100): 95 de 1227 OBJECTs de prédio no CENTRAL (52/1461 no TOKYO). É calculada pela normal girada
  pela matriz do objeto: gira certo [certo, `Scene` e programa 0x1010 do VU1].
- **Luz pontual** (`pointLight`, 0x80): 769/1227 OBJECTs de prédio no CENTRAL. É ao vivo (explosões, fogo), pela matriz: certo.
- **Sombras projetadas** [certo + medido]: os bloqueadores são só `_cs` (`psBlkLst`, até 10: monstros, objetos na mão). **Todos** os
  OBJECTs das fases têm `dontCastShadow`, e ~88 % dos de prédio têm `dontShadowMe` (não recebem). Mover prédios não muda nada no
  sistema de sombras; o nativo testa o receptor pela esfera do objeto, que vem da hierarquia.

## 4. Duplicar

### Onde pôr os bytes
- Com `WOTM_HIGHRAM` (padrão), `loadMonstersDBHigh` pula a próxima carga para 0x2000000. Embaixo de 32 MB fica só o `.NGP` da fase,
  e o trecho do fim da imagem até 0x1F10000 fica livre na luta [certo pelo código do runtime; não conferido num dump]. Folga:
  de 6,4 MB (METRO) a 20,7 MB (tabela acima).
- Sem `WOTM_HIGHRAM`, os NGP dos monstros vêm logo depois da imagem. Aí é preciso reservar a área: no fim do `LoadLevelDB`, antes
  do `LoadMonstersDB`, somar o tamanho do patch a `fileStatus.ngpAddr[1]`, que é o próximo endereço de carga.
- Alinhamento: copiar com `(destino − origem) % 0x80 == 0` (os pacotes começam em 0x80, como no `ngpgraft.py`); ROTATE é
  alinhado em 16.

### O que copiar (faixa fechada) [medido]
- Um prédio são ~2 faixas próprias: o bloco contíguo dos nós, malhas e ações, mais o INTERACTIVE_STATE na cauda. Fora delas ficam
  os nós instanciados e os contextos do GS, que a cópia **pode continuar apontando** se for na mesma fase.
- **A lista 0 do `.PTR` de fase não cobre tudo.** A imagem já vem ligada em 0xA00000 e `dbsRelocateFileZero` pula a lista 0, então
  nada no jogo depende de ela estar completa:
  - `INTERACTIVE_STATE.child` (+0xC): fora da lista em 100 % dos casos;
  - ações dos estados (`DESTRUCTIBLE` +0x10 + 12·i), `ActGroup.acts[]` e `ActTransform.act`: fora (761 + 507 no CENTRAL); só o
    `ActDebrisChunk.hier` (nó do destroço) está listado; `ActDomino.target` (16 no TOKYO, que derruba outro destrutível): fora;
  - os caminhos da grade: 2336 palavras no CENTRAL apontam para nós da árvore sem estar no `.PTR`.

  Os INTERACTIVE_STATE e os `ActGroup` de topo moram na cauda da imagem, junto dos caminhos. Hipótese: um pós-processo os
  acrescentou depois de gerar o `.PTR`.
- Por isso a cópia precisa de um **percurso tipado**: filhos por opcode (o do `dbsTraverse`, mais os estados do DESTRUCTIBLE e os
  dois filhos do CONTROL), ações (GROUP 0, TRANSFORM 1, DEBRIS 3, DOMINO 8, RUNSCRIPT 12; `src/game/ActionDispatch.cpp` e
  `Debris.cpp`) e, para os dados, os ponteiros da lista 0. `objetos.py` (`Level.footprint`) faz isso e confirma: fora o `ANIM_MGR`
  (CHAR_INSTANCE animados), nenhum ponteiro listado de fora entra num prédio.
- **Cópia rasa ou profunda** [certo + hipótese]:
  - Rasa: TRANSLATE + INTERACTIVE_STATE + DESTRUCTIBLE novos (~100 B), com os estados apontando para as mesmas subárvores. Desenha
    certo, porque a matriz vem do caminho e o estado sai do `whichChild` de cada INTERACTIVE_STATE. Mas os emissores compartilhados
    só soltam fumaça uma vez (`isActivated` fica no nó), e o `ChangeState(..., reset)` escreve no SWITCH comum.
  - Profunda: as faixas próprias (~80 KB por prédio no CENTRAL), compartilhando só instâncias e contextos.

  Recomendação: profunda para destrutíveis; rasa basta para props e para o visual de pickups (que já é compartilhado).

### Acrescentar um filho
- GROUP tem `numKids` u16 em +8 e os filhos em +0x20, sem folga [certo]. Para acrescentar: escrever no arena um GROUP novo (os
  0x20 bytes do original + n+1 ponteiros) e trocar **um ponteiro de filho já existente** por ele. Por exemplo, um dos 5 filhos do
  GROUP id 1: `GROUP{n=2: filho antigo, GROUP do editor}`. Assim não se mexe no cabeçalho da imagem nem no id 1 (o `viewSetWorldEpNode`
  pega o nó pelo id).
- Esferas: com `g_doFOVGroups` o hier corta GROUPs pela esfera (+0x10/+0x1C). Um prédio movido para longe do quarteirão some
  quando a esfera do quarteirão sai da vista. O GROUP do editor deve ter `noFOV` (bit 2 de +0xA) e `visual|collision` (3). Para o
  que for movido em lugar, aumentar as esferas dos GROUPs ancestrais ou reparentar o objeto no GROUP do editor. Mas a colisão lê
  a cadeia do caminho: o TRANSLATE original tem de continuar sendo o mesmo nó.
- Apagar: trocar o ponteiro do filho por um NULL (op 9) ou tirar da lista (`numKids − 1`, em lugar). Antes do `dbInitDb` o objeto
  nem é registrado. Os caminhos da grade continuam até ele ser tirado das células (`colisao_fase.md`).
- Girar um objeto cuja alça é TRANSLATE: inserir um ROTATE entre o TRANSLATE e o INTERACTIVE_STATE. A colisão refaz a matriz pelos
  nós do **caminho**, então o caminho também precisa do nó novo (caminho maior = memória nova).

### Texturas e `.PTR` (copiar de outra fase)
- Lista 1 (`texId` u16) e lista 2 (`TEX0`) do `.PTR` de fase têm o mesmo tamanho (pares por objeto texturizado) [medido]. Na
  fase só a lista 2 é aplicada (`TBP0 += vram/64`) [certo]. Os texIds da fase são globais (0..~330; o `texInfo` tem 1000 entradas
  somando fase e monstros, ~18 por monstro) [medido].
- **Na mesma fase:** copiar depois do `dbsRelocateFileZero` leva o TEX0 já relocado: não precisa fazer nada. Copiar antes exige
  somar `vram/64` no `TBP0` das cópias, ou pôr os deslocamentos delas na lista 2.
- **De outra fase:** os texIds e o CBP (paleta no `.RTX`) da origem colidem com os da fase. É preciso acrescentar os pacotes ao
  `.TEX` da fase com ids novos depois do maior, as paletas ao `.RTX`, e remapear `texId` (+0x50 do OBJECT) e o CBP do TEX0. É o
  que `tools/ngpgraft.py` + `mechamon.build_tex/build_rtx` já fazem para monstros. Também é preciso copiar os contextos do GS e
  as instâncias que a fase de destino não tem. Como o `.TEX`/`.RTX` são lidos antes do `dbInitDb` como arquivos, o caminho
  natural é um **passo offline** que gera `mods/<mod>/LVL/<FASE>.TEX/.RTX` (e a imagem estendida ou um blob), em vez de juntar
  texturas no runtime [hipótese de projeto].

## 5. Picking no editor
- `Scene::collect` gera `DrawItem{m, node = OBJECT, cs, ...}` [certo]. Não guarda ancestrais. Como os OBJECTs são compartilhados
  entre prédios (instâncias acima), não dá para mapear OBJECT → objeto offline: o dono tem de ser anotado na travessia.
- Proposta: `Scene::walk` mantém `xform_` (último TRANSLATE/ROTATE/SCALE visitado) e `owner_`:
  - no caso 39 (INTERACTIVE_STATE): `owner_ = xform_` se o pai imediato for transformação, senão o próprio INTERACTIVE_STATE;
    restaurar na volta;
  - nos `_cs` (pickups e veículos, `drawCs`): `owner_ = epNode` do `_cs`;
  - props: a transformação mais alta abaixo de um GROUP do mundo.

  Gravar `DrawItem::owner`. Pick: raio da câmera livre contra as esferas dos itens (+0x10/+0x1C pela `m`), depois contra os
  triângulos dos candidatos (`forEachTriPos`, já usado nas sombras); o mais perto dá o `owner`, e se destacam todos os itens com o
  mesmo `owner`.
- **Alça certa**: o TRANSLATE/ROTATE logo acima do INTERACTIVE_STATE (aninhados: o ROTATE do interno, relativo ao prédio). Para
  pickups, o TRANSLATE/ROTATE acima do GROUP do pickup, mas a posição viva está no `Pickup` (+0x20/+0x30) e no `_cs`. Sem alça
  (187) ou com alça dividida (15): inserir TRANSLATE.

## 6. Onde aplicar o patch e riscos
**Recomendado:**
1. Gancho na entrada do `dbInitDb` quando quem chama é o `Shell::LoadLevelFiles` (o `loadLevelFilesTapped` já marca a carga;
   o `dbInitDb` também roda para o menu e o preshell). Nesse ponto a imagem está inflada e com o TEX0 relocado, e nada foi
   registrado. Aplicar: apagar, mover/girar (em lugar), inserir alças, duplicar no arena, GROUP do editor. Também a parte da
   colisão: AABBs, células e caminhos (`colisao_fase.md`).
2. Reservar o arena: com `WOTM_HIGHRAM`, depois do fim da imagem (conferir contra 0x1F10000, como o `checkLoad`); sem ele, somar ao
   `fileStatus.ngpAddr[1]` no fim do `LoadLevelDB`.
3. Identificar objetos no JSON pelo deslocamento da alça na imagem do disco (a imagem é pré-ligada em 0xA00000 e determinística),
   com um hash do `.NGP` de base: um mod que troque o `LVL/<FASE>.NGP` muda os endereços.

**Edição ao vivo:** escrever no TRANSLATE/ROTATE em RAM mostra o resultado na hora (o nativo lê a matriz a cada quadro). Ficam
velhos: `Destructible` +0x40/+0x50, que dá para corrigir pelo `id2` → `Interactives::s_dbInteractive`; `Pickup` e `_cs`; fx autoStart
e fumaça já ativada; AABB e células da colisão. Para valer, gravar o JSON e recarregar a fase.

**Riscos:**
- `levelSignature` (hash do cabeçalho N + N ponteiros e do `tempVramTexAddr`): o patch não precisa mexer no cabeçalho. Se mexer,
  só invalida a cena uma vez por carga.
- O cache de malhas do nativo é por endereço do OBJECT (`cache_`). Reescrever o arena com outro conteúdo no mesmo endereço exige
  `scene.invalidate()`.
- Reinício dentro da sessão: `TheGame::ResetLevel` (C++ equivalente, não afinado) só reinicia HUD, pads e câmera; não toca na
  imagem nem nos destrutíveis. Carga completa: o `.NGP` é inflado de novo do disco e o patch tem de ser reaplicado (o gancho faz
  isso sozinho). Checkpoints do modo história: não investigados.
- Limites: Interactives 1024 sem checagem; `_cs` 1024; `MemoryStack` dos pickups; `fx[100]` (prédios duplicados = mais fumaça
  simultânea); `texInfo` 1000; VRAM residente do `.RTX` em cópias de outra fase.
- Ids especiais (colunas, SANFRAN, 3-MILE) e `ActDomino.target` (TOKYO): zerar `id1` das cópias e decidir se o dominó da cópia
  aponta para o original ou para a cópia.
- GROUPs que viram NULL pelo modo: um patch que referencie nós dentro deles não pode supor que estejam ativos.
- `CHAR_INSTANCE` animados dentro de prédios (18 casos): compartilhar a instância ou criar `ANIM_MGR` novo.
