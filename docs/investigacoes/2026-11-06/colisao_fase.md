# Colisão da fase: o que o editor de fase precisa mexer (etapa 1)

Investigação para o editor de fase (mover, girar, apagar e duplicar prédios/objetos, com patch JSON aplicado na carga). Tema deste
relato: **a colisão estática da fase**. PTS/* e o registro dos objetos (dbInitDb, Destructibles) estão em outros relatos.

Fontes: `include/hieri_types.h`, `include/hd.h`, `src/common/hd.cpp`, `hdTraverse.cpp`, `HdIterator.cpp`, `hdPrim.cpp`, `hdPoly.cpp`,
`hdHat.cpp`, `td.cpp`, `ColGrid.cpp`, `dbs.cpp`, `src/game/db.cpp` (o C++ equivalente; onde ele ainda é rascunho, o asm do retail foi
lido localmente). Medições: os 19 `.NGP` de `disc/LVL`, lidos pelos scripts de prova (seção "Scripts").

Legenda: **[certo]** = lido no código e conferido nas 19 fases; **[medido]** = número tirado das fases; **[hipótese]** = falta conferir.

## Resumo para quem tem pressa

- A colisão estática não percorre a árvore da fase. Ela usa uma **grade 2D (XY, Z para cima) de células de 200 unidades** (um nó op 15
  por fase, raiz da imagem) cujas células apontam listas de **paths pré-calculados no disco**. Cada path é a cadeia de nós da raiz até
  uma folha de colisão (op 42 polígonos, op 10 casco convexo ou op 11 gatilho), mais uma **AABB em mundo**.
- **As matrizes são lidas ao vivo**: a cada consulta, `hdTraversePath` refaz a transformação mundo→folha a partir dos nós do path
  (TRANSLATE, ROTATE, ANIM_XFORM) e confere SWITCH/DESTRUCTIBLE/NULL. **O que é fixo é a AABB do path e a inscrição dele nas células.**
- Logo: mover/girar = mexer no nó de transformação **e** refazer a AABB dos paths que passam por ele **e** reinscrevê-los nas células.
  Apagar = tirar os paths das células (ou anular a AABB, ou trocar o nó por NULL). Duplicar = criar paths novos (pode reaproveitar as
  folhas) e nós de lista novos. Tudo é viável em C++ na carga; a memória nova pode ir para os 32 MB de cima do runtime.

## 1. Layout dos nós de colisão

Todos os ponteiros são endereços absolutos do EE (imagem pré-ligada em 0xA00000; a fase não tem relocação de ponteiros, só de TEX0, então
**a imagem na RAM é igual à do disco** fora os campos que o jogo escreve em tempo de execução) [certo].

### Grade, op 15 (`_HierCollisionGrid`, 0x20 + 4·n bytes) [certo]
| off | campo | |
|---|---|---|
| 0x0 | head | op 15 |
| 0x4 | s16 nCols, 0x6 s16 nRows | colunas em X, linhas em Y |
| 0x8 | float cellSize | **200 nas 19 fases** [medido] |
| 0xC..0x18 | float minX, minY, maxX, maxY | retângulo da grade; `nCols·cellSize` passa um pouco de `maxX - minX` |
| 0x1C | `ColGridCsList *csListArr` | `nCols·nRows` cabeças de lista de **cs** (objetos dinâmicos), **zeradas no disco** (19/19) |
| 0x20 | `HierListNode *cells[nCols·nRows]` | célula `row·nCols + col`; col = floor((x - minX)/cellSize), row = idem em Y |

- Um único nó por fase, sempre **raiz da imagem** (está na lista de raízes do cabeçalho, sem pai) [medido 19/19]. `dbsTraverse` (dentro
  do `dbInitDb`) acha o op 15 e chama `hdSaveColGrid`, que guarda o ponteiro no global `colGrid` (o primeiro vence; `dbInitDb` zera antes).
- Só `common/hd` e `common/HdIterator` leem `colGrid` (busca por `colGrid` no asm: 14 arquivos, todos desses dois TUs e dados). Nada
  preenche `cells` em tempo de execução.
- Dimensões medidas: de 7x7 (CRUSH) a 102x40 (BIGSHOT); METRO 27x28, ISLAND 55x60, SANFRAN 42x45.

### Nó de lista (`_HierListNode`, 8 bytes) [certo]
`{next, HierCollisionPath *val}`. Lista simples por célula. **No disco, cada path vem seguido dos seus próprios nós de lista, um por
célula coberta** (4348/4348 [medido]); o `next` encadeia nós de paths diferentes da mesma célula. 77470 referências no total.

### Path (`_HierCollisionPath`, 0x24 + 4·length bytes) [certo]
| off | campo | |
|---|---|---|
| 0x0 | FVECTOR3 boundBoxMin, 0xC boundBoxMax | **AABB em mundo** |
| 0x18 | traceId | carimbo da busca (`HdItCell::next` escreve; 0 no disco) |
| 0x1C | s32 length | nós em `path[]` |
| 0x20 | flags bit 0 `triggerVol` | 1 = gatilho (só `tdTest` pega); 0 = sólido |
| 0x24 | `HierHead *path[length]` | da raiz até a folha |

- `path[0]` é sempre um GROUP raiz da imagem (4348/4348); `path[i+1]` é filho de `path[i]` (4308/4348; os 40 restantes passam por nós
  **NULL (op 9) já no disco**, cujos filhos o leitor não sabe seguir, e 1 por SKEL_BONE) [medido]. O último é a folha.
- **A AABB é a caixa local da folha (min/max dos vértices usados) com os 8 cantos levados ao mundo** pela matriz do path, não a caixa
  justa dos vértices: bate com erro < 0,5 em 3521 de 3556 paths estáticos conferidos, e os outros 35 (METRO, TOKYODM) com erro <= 1,0
  [medido]. A caixa justa só bate em 3377 (difere nos girados). Paths com CHAR_INSTANCE/ANIM_XFORM (377, animados) não foram conferidos:
  a caixa deles depende da pose da animação [hipótese: pose de repouso ou varredura da animação].
- A inscrição nas células é **exatamente** o retângulo de células coberto pela AABB em XY (floor nas duas pontas, preso à grade):
  4348/4348 [medido]. Nenhum path fora da grade.
- Comprimentos de 2 a 18; folhas: **3901 POLY_COLL (op 42), 12 COLL_VOL (op 10), 435 COLL_TRIG (op 11)** nas 19 fases [medido]. Cada
  folha tem um path só (exceto 1 em VEGAS, via LOD com dois pais); 60 folhas não têm path (sem colisão estática, provavelmente usadas por cs).
- Tamanho do bloco de colisão: de 4,8 KB (TRAINING) a 152 KB (ISLAND), 0,4% a 2,8% da imagem (12% em BIGSHOT pela grade grande). Ordem na
  imagem, sempre no fim: [paths intercalados com os nós de lista] [csListArr] [grade] e ~150..520 bytes até o fim da imagem [medido].
  **Não há folga** entre os paths: crescer um path ou ganhar células exige memória nova.

### Polígonos, op 42 (`_hierpolycollisiongroup` + `_hierpolycollisionobject`) [certo]
- Grupo: `{head, u32 numObjs (+4), objs[numObjs] (+8)}`.
- Objeto (0x18 + 14·numBBoxes + 8·numPolys bytes): `vertexPool` (+0, FVECTOR3 de 12 bytes, **coordenadas locais da folha**), u16 numPolys
  (+4), u16 numBBoxes (+6), `bboxOrigin` (+8) e `bboxScale` (+0x14), árvore de caixas `_hdpolybboxnode[numBBoxes]` (+0x18; s16 min/max em
  `(p - origem)·escala`, `isLeaf`/`numChildren`/`firstChild` de 9 bits; filhos internos são dois consecutivos, folha lista `numChildren`
  polígonos a partir de `firstChild`), depois os polígonos `_hiercollisionpoly` (8 bytes: `idx[4]` u8, só os 3 primeiros lidos, então
  **triângulos**, até 256 vértices por objeto; `matId` u16 = superfície; 2 bits por eixo dizendo qual vértice é o mín/máx). Sem árvore
  (`numBBoxes` 0) testa todos. Leitor: `hdSphereVsPolyCollis`.
- Somente leitura em tempo de execução: pode ser compartilhado por vários paths.

### Casco convexo, op 10 (`_hiercollisionvolume`, 0x20 + 16·numPlanes) [certo]
`numPlanes` s16 (+4), `useNormal` (+6), `matId` (+8, vira `hdResult.surface`), `plExclude` (+0xA), `actTree` (+0xC), esfera local
(+0x10/+0x1C), planos `{n, d}` (+0x20) em **coordenadas locais**; dentro quando `n·p < d` (`hdSphereVsHull`). Só 12 paths nas fases
(AIRPORT 2, CAPITOL 10), de 4 a 12 planos.

### Gatilho, op 11 (`HierCollisionTrigger`) [certo]
`{head, _hierbsp *bsp (+4)}`; BSP de nós `_hierbsp` de 0x20 (`front`, `back`, `coplanar`, matIds, plano). `tdTest` (câmeras, `Cameras.cpp`)
pega só paths com `triggerVol` = 1 e testa ponto dentro com `bspPointTest`. 435 paths, 3510 nós de BSP; só 19 estão sob INTERACTIVE
(3-MILE 12, SANFRAN 7) [medido].

### Outros
- TERRAIN (13): `dbsTraverse` imprime "TERRAIN NODES NO LONGER SUPPORTED"; não aparece nas fases. BSP (18): `hdTraverseHier` o devolve
  (cs), mas `hdTestNode` só trata 10 e 42; não aparece em paths [certo/medido].
- Nenhum path passa por SCALE (op 30) [medido], e `hdTraversePath` não trata SCALE.

## 2. Onde ficam na hierarquia

- A grade é um nó só, raiz, fora da árvore do mundo [certo].
- A folha de colisão fica **na mesma cadeia de transformação da malha**: o padrão dominante é
  `GROUP raiz … T|R → INTERACTIVE_STATE → DESTRUCTIBLE → GROUP do estado (flags 0x3, visual+colisão) → GROUP só de colisão (flags 0x102)
  → POLY_COLL` (ex.: METRO 358 de 576 paths `G-T-I-D-G-P`) [medido]. 3239 das 3901 folhas de polígono estão sob um INTERACTIVE; em
  **1512 de 1513 INTERACTIVEs com colisão** a malha (OBJECT) e a colisão estão na mesma subárvore (a exceção é de CAPITOL) [medido].
- O nó logo acima do INTERACTIVE é um TRANSLATE ou ROTATE de um filho só em 1276 de 1513 casos (84%); nos outros é um GROUP ou um T/R com vários
  filhos (TOKYODM 39, UFO 23, VEGAS 16, CAPITOL 15). Dentro do INTERACTIVE quase não há transformação (nenhuma em 3145 de 3269 paths, 96%) [medido].
  INTERACTIVEs nunca têm dois pais; há INTERACTIVE dentro de INTERACTIVE (prédio com partes destrutíveis).
- O que fica **fora** de INTERACTIVE: chão, terreno e muros (662 folhas de polígono), quase sempre `G-P` (sem transformação) ou com um
  ROTATE/TRANSLATE acima.
- Alguns paths passam por CHAR_INSTANCE + ANIM_XFORM (377; SANFRAN 80, TOKYO 61, 3-MILE 153): colisão animada (ponte, guindaste etc.).
- `path[]` = cadeia de nós desde a raiz (veja acima); `boundBoxMin/Max` = mundo (veja acima).

## 3. Pré-calculado ou montado? Como as matrizes entram

- **Grade, listas e paths são do disco**: nenhum código os monta; `csListArr` vem zerado e só recebe cs (monstros, veículos, pickups,
  mecha etc.) via `hdReparentCsGrid`, com nós de um pool global de 1024 (`InitColGridCsListArray`, `allocateColGridCsListNode`;
  "Too many CS's in CS lists" quando acaba) [certo].
- `dbInitDb` só toca a colisão por: `hdSaveColGrid(0)` e depois o op 15 achado pelo `dbsTraverse`; e pelo modo de jogo
  (`g_dbsDisableNodeMask` 0x80/0x10/0x20): GROUPs com esse bit nos flags viram NULL (op 9), o que **corta os paths que passam por eles**
  (há GROUPs 0x32/0x33/0x23/0x83 em paths) [certo].
- **As consultas** (`hdHatTest`, `hdSphere*`, `hdCsTest` via `HdItGridBox`; `hdLosTest` via `HdItGridLine`; `tdTest`) andam nas células
  da caixa/segmento, testam a AABB do path (`hdAABBVsAABB` / `hdSphereVsAABB`) e, se passar, chamam `hdTraversePath`, que **refaz do zero**
  a matriz mundo→folha com os nós do path: TRANSLATE subtrai `trans`; ROTATE aplica a inversa (transposta) da 3x3 em torno do pivô da
  linha 3 da matriz; ANIM_XFORM pega a matriz da animação na hora (`updateAnimTransMatrix`); GROUP com `flags & (option << 8)` corta
  (o bit 0x100 dos grupos 0x102 fica fora quando a consulta passa `option` 1); SWITCH e DESTRUCTIBLE cortam se o filho ativo não é
  `path[i+1]`; NULL corta. Os ids do resultado (`hatId1/2`) vêm do último id1/id2 não nulo da cadeia [certo].
- Consequência: **a colisão acompanha a matriz do nó em tempo real**; a AABB e as células não. Mover um nó sem refazer a AABB faz a
  colisão sumir no lugar novo (o teste de caixa falha) e o lugar velho ficar sem nada sólido (a caixa passa mas a geometria já saiu).
- A ROTATE é tratada como ortonormal (inversa = transposta): todas as ROTATE em paths conferidas são ortonormais [medido em TOKYO 167/167
  e no casamento das AABBs]. **Escala não é suportada pela colisão** (nem SCALE nem ROTATE com escala).
- Cs (dinâmicos) usam outro percurso, `hdTraverseHier`, que anda a árvore de verdade a partir do `epNode` do cs (só GROUPs com o bit de
  colisão 0x2), na matriz do cs.

## 4. Destrutíveis e colisão

- Prédios destrutíveis **não são cs**: a colisão deles é só por paths estáticos (o TU `game/Destructible` não chama nada de `hd*` nem
  de cs) [certo].
- Cada estado do DESTRUCTIBLE tem a sua subárvore, e **os cascos/polígonos de cada estado têm paths próprios**; `hdTraversePath` só aceita
  o path se `child[INTERACTIVE.whichChild].child == path[i+1]`, então a troca de estado (`SwitchState`/`ChangeState`, que escrevem
  `whichChild`) troca a colisão na mesma hora em que troca o desenho [certo].
- Medido nas 19 fases: o estado 0 tem colisão em 100% dos INTERACTIVEs com colisão; o estado 1 em ~35% (ex.: METRO 131/206, CENTRAL 35/63,
  CANYON1 8/79, UFO 20/122); estados 2+ quase sempre têm. Ou seja: **na maioria dos prédios o estado quebrado (1) não colide**; nos que
  colidem, é o casco do escombro [medido]. Há SWITCH dentro de estados (3-MILE, CAPITOL, VEGAS) e NULL no disco dentro de estados
  (CAPITOL 9, VEGAS 12, VOLCANO 8, ISLAND 8, AIRPORT 2 paths): esses paths estão mortos até algo trocar o opcode
  [hipótese: script/ação liga o nó; não achei quem escreve].

## 5. Conclusão prática para o editor

Notação: N = nó editado (o TRANSLATE/ROTATE logo acima do INTERACTIVE, ou o da peça estática); P(N) = paths que contêm N
(construa um índice nó → paths uma vez na carga: ~4348 paths, ~77 mil refs, instantâneo).

**Mover (só translação)**
1. Escrever o novo `trans` (TRANSLATE +0x10) ou a linha 3 da ROTATE (+0x40). A colisão já usa isso.
2. Para cada path em P(N): AABB nova. Exato para estáticos: caixa local da folha (vértices usados do POLY_COLL / vértices do casco) com
   os 8 cantos transformados pela matriz do path (a regra do disco). Atalho que serve também para animados: somar o deslocamento em
   mundo à AABB velha (exato para translação pura).
3. Reinscrever: tirar o path das células velhas (desligar o nó de lista da célula: lista simples, precisa achar o anterior) e pôr nas novas
   (empurrar na frente de `cells[i]`). Se a quantidade de células crescer, faltam nós de lista: alocar novos (8 bytes cada).
4. Se a AABB nova sair do retângulo da grade, o `hdLosTest` (que recorta o segmento na grade) deixa de ver a peça; recusar o movimento
   ou refazer a grade (abaixo).

**Girar**
- Se N já é ROTATE: escrever a 3x3 (ortonormal, sem escala) e o pivô; depois os passos 2 e 3. Para animados, os 8 cantos da AABB velha
  pela rotação delta dão uma caixa conservadora.
- Se N é TRANSLATE: a colisão não sabe girar um TRANSLATE. Criar uma ROTATE nova (0x60 + 4·numKids, alinhada em 16; a linha 3 faz a
  translação) com os mesmos filhos, trocar o ponteiro no pai **e em `path[i]` de cada path de P(N)** (o comprimento do path não muda).
  O desenho e o `dbsTraverse` já tratam ROTATE.

**Apagar** (três jeitos, do mais localizado ao mais completo)
- Tirar os paths de P(N) de todas as células: a peça deixa de colidir; o desenho fica (combinar com a outra frente).
- Anular a AABB (min > max): `hdAABBVsAABB`/`hdSphereVsAABB` recusam e nada mais muda [hipótese leve: conferir o `hdSphereVsAABB`
  com caixa invertida]. Não usar `triggerVol` = 1 para isso: o `tdTest` passaria a ler uma folha de polígono como gatilho.
- Trocar o opcode de N por NULL (9): corta os paths (NULL no meio do path para o `hdTraversePath`), some do desenho e, se feito antes do
  `dbInitDb`, o `dbsTraverse` não desce nele (o destrutível nem é registrado). É o mesmo truque que o `dbInitDb` usa para os modos.

**Duplicar**
- A colisão estática **não lê os filhos dos nós, só os paths**: o duplicado precisa de paths novos, não de estar pendurado na árvore
  (pendurar é assunto do desenho/registro).
- Para cada path em P(N): path novo = `path_velho[:i]` (os ancestrais, para manter flags de modo/NULL) + cópia de N + cópias do
  INTERACTIVE/DESTRUCTIBLE logo abaixo (estado próprio: `whichChild` não pode ser compartilhado, e o DESTRUCTIBLE copiado aponta para os
  mesmos GROUPs de estado) + o resto do path velho (GROUPs, folha **compartilhados**: somente leitura). AABB pela regra do disco e nós de
  lista nas células cobertas.
- Custo por prédio: (0x24 + 4·len) por path + 8 por célula coberta + os nós copiados. Nas fases, 1 a 10 paths por INTERACTIVE na
  grande maioria e ~18 células por path em média (o chão puxa a média para cima) [medido]: poucos KB por prédio.
- Alternativa: duplicar como cs (epNode na cópia, `hdReparentCsGrid`). Funciona pelo `hdTraverseHier`, mas gasta o pool de 1024 nós de
  cs (um por célula coberta) e exige o cs no desenho; não recomendo para prédios.

**Memória nova**: não há folga na imagem da fase. O runtime tem 64 MB e o jogo vê 32 (`NATIVE.md`, "64 MB RAM"); os 32 MB de cima só
recebem o que os hooks mandam (os monstros já carregam em 0x2000000). Uma área reservada lá para nós/paths/listas do editor serve; os
ponteiros do EE passam pela máscara normalmente.

**Refazer a grade inteira** (para crescer o retângulo ou simplificar): alocar grade + `csListArr` (zerado) novos, reinscrever todos os
paths, trocar o ponteiro da raiz op 15 no cabeçalho da imagem **antes do `dbInitDb`** (o `hdSaveColGrid` pega o novo) e antes de qualquer
cs se registrar (os cs guardam índices de célula). Viável: são milhares de paths, nada pesado.

**Quando aplicar**: para a colisão, qualquer momento depois da imagem estar na RAM (`Shell::LoadLevelFiles`) e antes da primeira
consulta. Antes do `dbInitDb` é o mais seguro (troca de grade, NULL que evita o registro do destrutível; o `Destructible` copia posição e
matriz no `AddDestruct`, ver o relato do registro). As edições de AABB/células também podem vir depois, mas não há motivo.

**Viável em C++ na carga: sim.** Tudo é leitura/escrita de structs de layout conhecido na RAM do EE; dá para fazer offline em Python
sobre o disco (a RAM é a imagem do disco) e conferir antes de rodar.

**Funções do jogo reaproveitáveis** (endereços em `native/wotm_syms.h`):
- `hdTraversePath` (0x201F98): matriz mundo→folha de um path e se ele está vivo (para validar a AABB recalculada e para o editor saber
  o que a colisão vê).
- `hdHatTest`, `hdLosTest`: sondar o chão e escolher o prédio sob o cursor da câmera livre (o `hdResult` traz os ids da cadeia; para
  destrutíveis o id2 do INTERACTIVE é o índice em `Interactives`, ver FORMATS.md).
- `hdSaveColGrid` (0x1FE048): registrar uma grade nova. `hdReparentCsGrid` / `hdRemoveCsFromGrid`: se algum duplicado virar cs.
- `mathfMulMatrix`, `mathfMulVec`, `mathfUnitMatrix`: mesma convenção de matriz do jogo.
- A regra da AABB e a da inscrição estão reimplementadas em `colisao.py` (abaixo) e casaram com o disco.

## Em aberto

- AABB dos paths animados (CHAR_INSTANCE/ANIM_XFORM, 377 paths): de que pose sai. Para mover, somar o deslocamento à caixa velha resolve.
- Os 35 paths com até 1 unidade de folga (METRO 31, TOKYODM 4): arredondamento da ferramenta [hipótese].
- Quem liga os nós NULL que já vêm no disco dentro de estados de destrutíveis (39 paths) [hipótese: ações de script].
- Conferir no jogo (runtime) um movimento real: mover um prédio de CANYON1 na carga, refazer AABB/células e testar `hdHatTest` em cima
  dele no lugar novo e no velho.
- Gatilhos de câmera (op 11) sob INTERACTIVE (19 paths): se o editor mover esses prédios, os gatilhos seguem pelas mesmas regras.

## Scripts de prova (fora do repo, pasta de rascunho da sessão)

- `C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\colisao\colisao.py`:
  `python colisao.py <repo> [FASE ...] [--verbose]`. Grade, células, paths, folhas, cadeia pai→filho, regra da AABB, inscrição nas
  células, disposição na imagem. Importa `tools/ngp.py`. Saída das 19 fases em `todas.txt` na mesma pasta.
- `predios.py` (mesma pasta): padrões de path, INTERACTIVE com malha e colisão, colisão por estado, nó acima do INTERACTIVE. Saída em
  `predios.txt`.
- `gatilhos.py`, `diag.py`, `diag2.py`, `diag3.py` (mesma pasta): gatilhos/cascos sob INTERACTIVE, NULL em paths, convenção da ROTATE
  e a descoberta da regra dos 8 cantos.
