# Arquivos por fase fora do .NGP: PTS/*.SPT, .PUP, .FOP, .PED, .AIP, RST/*.MRS e os .AIC das pastas das fases

Etapa 1 do editor de fase (o que cada arquivo é, quem lê, para onde vai, limites e em que momento um patch pode entrar).
Fontes: `src/common/PointToolKit.cpp`, `src/game/{StartPointTool,StartPoints,PowerUpTool,PowerUps,PathTool,AiPathTool,AiPathNet,TheGame}.cpp`,
`src/common/resetcom.cpp`, `src/game/reset.cpp` e o asm local (`InitBeforeDbLoad__7TheGame`, `exportPoints__10AiPathTool`,
`isIn__9AiPathNetR8_fvectorR6AiPath`, `__8PedGroupUcR8_fvector`, `initPath__8PedGroup...`, `sanFranInitAfter/sanFranUpdate`,
`threeMileInitAfter`, os inicializadores estáticos de `PathTool`, `AiPathTool` e `TheGame`).

Script de prova: `C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\pts\pts.py`
(fora do repo; `python pts.py disc` dá o resumo por fase e as estatísticas, `--dump CENTRAL` lista todos os pontos e o .MRS de uma
fase, `--mrs` lista as chaves de todos os .MRS).

Legenda: **[certo]** lido no código (decompilado ou asm) e conferido nos 78 arquivos de `disc/PTS`; **[hipótese]** não confirmado.

## 1. Os cinco arquivos de pontos têm um formato só (PointToolKit)

**[certo]** Todos são lidos por `PointToolKit::loadPoints(nome, notificar)` (0x21DD80), base comum de `StartPointTool`,
`PowerUpTool`, `PathTool` (duas instâncias) e `AiPathTool`. O caminho sai de `Shell::formatFilename(buf, "pts", <nome da fase>, <ext>)`.

- Lê o arquivo inteiro num buffer de **0x4000** tirado do topo da `MemoryStack::global` (`allocHigh16`), confere a versão e copia
  `contagem × 64` bytes para `PointToolKit::points[256]` (dentro da instância da ferramenta, 0x4000 bytes). Sem arquivo: 0 pontos.
- Com `notificar` = 1 chama o slot 3 da vtable, que é o `exportPoints` da ferramenta: é ali que os pontos viram a estrutura que o
  jogo usa. A conversão acontece **uma vez, na carga**; depois disso o jogo só lê a estrutura convertida.

Arquivo:

| Offset | Tipo | Campo |
|---|---|---|
| 0x00 | u32 | versão `0x12282000` (outra versão: 0 pontos, mensagem "PointToolKit file is of version") |
| 0x04 | u32 | contagem N |
| 0x08 | 64 × N | registros |

Registro de 64 bytes (comum aos cinco):

| Offset | Tipo | Campo |
|---|---|---|
| 0x00 | f32 × 4 | x, y, **z (para cima)**, w |
| 0x10 | s32 | número de links |
| 0x14 | s32 × 6 | links: índice de outro ponto **do mesmo arquivo** (só FOP/PED/AIP) |
| 0x2C.. | bytes | dependem do formato (abaixo) |

Eixo: **[certo]** Z é a vertical. `AiPathNet::isIn` mede o corredor em XY e a altura em Z; `PowerUp::testHidden` olha para cima
somando 1500 em z; `gameGetStartPoint` assenta somando/subtraindo em z.

Limite: **[certo]** o buffer de 0x4000 cabe 8 + 255 × 64 = 0x3FC8, logo **255 pontos por arquivo** (o `points[256]` também). Um
arquivo maior estoura o buffer no retail; no port o `cdRead` de `wotm_mods.inc` já corta a contagem para o que cabe (`fixedCap`).
Nos 78 arquivos do disco o tamanho é sempre exatamente `8 + N·64`; o maior é ISLAND.AIP com 224 pontos.

Campos que nenhum `exportPoints` lê (**[certo]** que não são lidos; significado **[hipótese]**, sobra do editor da Pandemic):
- `w`: vale 0, 40, 45, 50 ou 55 (provável altura do marcador no editor). FOP/PED e SPT copiam o vetor inteiro (o w vai junto), AIP
  troca w por z, PUP troca o w do `_cs` por z. Nada encontrado que leia esse w. Num patch: preservar o que vier; ponto novo com 0.
- byte 0x3C: 0 ou 1 (em geral 1 a 2 pontos por arquivo; todos os pontos em ISLAND.AIP, UFO.AIP, QUAKE.AIP, MINISF.SPT, VEGAS.PED).
  Provável "selecionado" do editor.
- links além do número de links: há valores ≠ 0 (links apagados no editor). Ignorados.

### Quando cada arquivo é carregado ([certo])

| Momento | Arquivo | Ferramenta (instância) | Destino |
|---|---|---|---|
| `TheGame::InitBeforeDbLoad` (antes do .NGP) | `<FASE>.AIP` | `AiPathTool::instance` (0x25C4F0) | `AiPathNet::monster` (0x7C3CB0, 0x5B80) |
|  | `<FASE>.FOP` | `PathTool::instance` (0x2AAC00), título "Fodder Path Tool" | `PathNet::fodder` (0x7CA4D0, 0x3010) |
|  | `<FASE>.PED` | segunda `PathTool` (0x705BF0), título "Ped Path Tool" | `PathNet::ped` (0x7D0000, 0x3010) |
| `TheGame::InitAfterDbLoad` (depois do .NGP), se `m_gameMode != 8` | `<FASE>.PUP` | `PowerUpTool::instance` (0x2AF0C0) | `PowerUps::instance` (via `CreatePowerUp`) |
|  | `<FASE>.SPT` | `StartPointTool::instance` (0x41A540) | `StartPoints` (0x7D3010, 0x560) |
| `threeMileInitAfter` (Atomic Island) | `CRATER.PUP`, **sem** exportar | `PowerUpTool` | exportado quando a cratera troca (`threeMileUpdateCountDown`) |
| `sanFranInitAfter` (Baytown, qualquer modo) | `QUAKE.AIP`, `QUAKE.FOP` (não existe: 0 pontos), `QUAKE.PUP`, sem exportar | as três | exportados no terremoto: `sanFranUpdate`, quando o token "Destructibles" de um monstro chega a 3800 |

Consequências para o editor:
- O PUP precisa vir depois do .NGP porque `CreatePowerUp` usa o modelo do tipo que o `.NGP` registrou (hat id 0x13EC + tipo). Um
  power-up de um tipo cujo modelo a fase não tem é recusado (`CreatePowerUp` devolve -1).
- Os `points[]` das ferramentas **nem sempre** guardam o arquivo da fase: em Baytown, depois do `InitAfterDbLoad`, as três
  ferramentas guardam o QUAKE; em Atomic Island o `PowerUpTool` guarda o CRATER. Reexportar a ferramenta para "aplicar" uma edição
  ao vivo em Baytown poria a rede de veículos no fallback de 2 nós. O editor deve guardar a própria cópia dos pontos, por nome de
  arquivo (`SANFRAN.FOP`, `QUAKE.FOP`, `CRATER.PUP`), e não depender da instância.
- Em Baytown o terremoto troca a rede: `QUAKE.FOP` não existe no disco, e `PathTool::exportPoints` com 0 pontos monta os 2 nós
  padrão (-100, 0, 250) e (100, 0, 250). **[certo]** pela leitura; o efeito em jogo (veículos depois do terremoto) não foi visto.

## 2. `.SPT`: pontos de largada (StartPointTool → StartPoints)

Bytes próprios: **0x2C** `u8 lista` (0..3), **0x2D** `u8 ângulo`, **0x2E** `u8 assentar no chão`. **[certo]**

- `exportPoints` limpa `StartPoints` e chama `addPoint(lista, &pos, graus, chao != 0)` para cada ponto.
- Ângulo: `graus = b / (255/360)` (o `quantizer` vale 0,70833 = 255/360); acima de 180 subtrai 360. Gravar: `b = (graus < 0 ? graus + 360 : graus) · 255/360`.
  É a guinada em volta de Z (`Monster::setRot(graus · π/180, 0, 0)`). Resolução de 1,41°.
- `StartPoints::Point` (0x20 bytes): `pos` (16 bytes, com w), `f10` graus (float), `f14` assentar (bool).
- Listas e capacidades (tabela `StartPoints::s_pointTypes`, 0x41A4F8): **lista 0: 1 ponto, lista 1: 2, lista 2: 20, lista 3: 20**
  (43 × 0x20 = 0x560). Ponto além da capacidade **sobrescreve o último** da lista (`addPoint` usa `max - 1`).
- Quem usa (`TheGame::gameGetStartPoint`): lista 0 = primeira vida do jogador no modo história; 1 = primeira vida do jogador nos modos
  2..6 (e 7/10); 2 = primeira vida da IA (e o padrão); 3 = renascer (sorteio, depois rodízio, procurando um sem nada a 120 unidades;
  lista 3 vazia cai na 2). Com `chao` = 1, `pos.z -= hdHatTest(pos, normal, 5, false, 4096)`: o ponto desce até o chão (ou telhado)
  embaixo dele. Sem ponto nenhum: (0, 0, 500). Atomic Island com `craterSwitched` usa um ponto fixo, fora do arquivo.
- Medido: 192 pontos, listas 0:13, 1:34 (sempre 2 por fase), 2:66, 3:79; `chao` 1 em 155. CENTRAL tem o ponto 0 da lista 1 em
  z 613,8 com `chao` 1.
- Patch: **no arquivo** (antes do `InitAfterDbLoad`) ou **na RAM** (`StartPoints::m_instance` + `s_pointTypes`): o jogo lê os pontos
  a cada nascimento, então mudar a RAM vale no próximo nascimento. Reexportar ao vivo (limpar e `addPoint`) é seguro.

## 3. `.PUP`: power-ups (PowerUpTool → PowerUps)

Bytes próprios: **0x2C** `u8 tipo`, **0x2D** `u8 flags`. **[certo]**

- `exportPoints` chama `PowerUps::KillPowerUps()` e `CreatePowerUp(tipo, flags, &pos)` para cada ponto.
- Tipos (`PowerUp::Type`, `include/game/power_ups.h`): 0..2 vida (25/50/100), 3..5 stamina, 6 vida 10, 7 stamina 10 (não regenera),
  8 arma especial, 9 cloak, 10..19 itens do modo história (só com `m_gameMode` 1). Na fase 11 do modo história o cloak não entra e a
  arma especial acima de z 500 também não.
- Flags (= `PowerUp::m_flags` inicial): bit 0 tem sombra, bit 1 o `update` reassenta a sombra, bit 2 o assentamento também baixa a
  altura de repouso; bit 3 aparece (flags 15 em 2 pontos) e não tem leitor conhecido **[hipótese: sem efeito]**. Medido: 7 em 439 de
  528, 3 em 78, 0 em 8, 2 em 1, 15 em 2.
- A posição é a altura de repouso (`trans.w = z`); a rotação é sorteada (`rand() & 0x1FF` graus). Não há ângulo no arquivo.
- Limite: **100 power-ups ativos** (`PowerUps::m_powerUps[100]`); o 101º é recusado. O maior arquivo tem 55 (METRO).
- Medido: tipos 0:209, 1:26, 2:13, 3:178, 4:22, 5:11, 7:1, 8:54, 9:13, 10:1 (TRAINING, item de história que fora do modo 1 não aparece).
- Patch: **no arquivo**, ou **ao vivo** reexportando: o próprio `exportPoints` mata todos e recria, então o editor pode montar os
  `points[]` do `PowerUpTool` com a lista editada e chamar `exportPoints__11PowerUpTool` (0x1881D0). Atenção ao caso CRATER/QUAKE acima.

## 4. `.FOP` e `.PED`: redes de caminho (PathTool → PathNet)

**[certo]** O mesmo código (`PathTool::exportPoints`, 0x17F180) serve aos dois; muda só a rede de destino. O formato em RAM está em
`docs/FORMATS.md` ("Path nets"): `u16 numNodes`, nós de 0x30 a partir de +0x10, até 256.

Bytes próprios do registro: **0x2C..0x31** `u8 largura[6]` (por link), **0x32** `u8 tipo do nó`, **0x33..0x38** `u8 queda[6]` (por link).
Links: só a metade baixa (u16) de cada `s32` do 0x14 é usada; no máximo 6 links por nó.

- FOP (`PathNet::fodder`): veículos (tipo 0 carro, 1 helicóptero, 2 disco voador, 3 tanque/caminhão). Medido: 902 nós, tipos
  0:269, 1:158, 2:467, 3:8 (só VEGAS); links por nó 0..6; 2566 de 2567 links são de mão dupla (cada lado grava o seu). Largura e
  queda × 10 (`HeliVehicleNav`, já no FORMATS.md).
- PED (`PathNet::ped`): pedestres. Medido: todos os nós com tipo 0 e **1 link**, e os links formam pares (A↔B): cada caminho de
  pedestre é um segmento de dois nós; queda sempre 0; largura 1..18. Quem lê: o construtor de `PedGroup` pega o nó de `ped` mais
  perto da posição do grupo (que vem do `.NGP`, nó 0xBB8, assunto do outro agente) com tipo -1 (qualquer) e segue o link 0;
  `PedGroup::initPath` usa `largura · 10` como meia-largura da faixa onde os pedestres andam. Em CENTRAL os nós de PED estão em z 0.
- Sem arquivo (TRAINING, CANYON1, VOLCANO, DBALL, CRUSH e CRATER não têm FOP; CANYON1, SANFRAN, QUAKE, TRAINING, DBALL, VOLCANO não
  têm PED): a rede fica com os 2 nós padrão.
- Patch: **no arquivo** (a carga é no `InitBeforeDbLoad`). Ao vivo é arriscado: `PedGroup` guarda **ponteiros** para nós de `ped`
  (`PathDat` +0x0/+0x4/+0x8) e os veículos guardam o nó em que estão; reexportar muda o que esses ponteiros/índices significam.
  Recomendo editar FOP/PED só com recarga da fase.

## 5. `.AIP`: rede de navegação dos monstros da IA (AiPathTool → AiPathNet::monster)

**[certo]** `AiPathTool::exportPoints` (0x107598) **converte** os pontos em nós e segmentos ("paths"); não é cópia.

Bytes próprios do registro: **0x2C..0x31** `u8 largura[6]` (por link), **0x32** `u8 tipo do nó`. O resto do 0x33 em diante não é lido.

`AiPathNet` em RAM (0x5B80 bytes):

| Offset | Campo |
|---|---|
| +0x00 | u16 numNodes (até **300**, mas o arquivo só traz 255) |
| +0x02 | u16 numPaths (até **450**; além disso "Exceeded AiPathNet::MAX_PATHS; ignoring path") |
| +0x10 | nós de 0x30: `pos` (w = z), +0x10 `u16 paths[12]` (índices de path), +0x28 `u8 numPaths` (até 12) |
| +0x3850 | paths de 0x14: +0 `u16 nó A`, +2 `u16 nó B`, +4 e +8 `s32 1`, +0xC `u8 máscara = 1 << tipo do nó A`, +0xD `u8 largura` do link, +0xE `u8 altura = 20`, +0x10 `u16 comprimento 3D × 4`, +0x12 `u16 comprimento em XY × 4` |

- Um link de mão dupla vira **um** path (criado pelo nó de menor índice; o outro reaproveita o índice); um link de mão única vira o
  seu próprio path. Os links até 12 são lidos do 0x14 em diante, mas acima de 6 eles cairiam sobre os bytes de largura; no disco o
  máximo é 6, então na prática são 6.
- `AiPathNet::isIn(pos, path)` (o corredor do segmento): em z, de `min(zA, zB) - 50` até `max(zA, zB) + altura · 10` (200); em XY,
  projeção dentro do segmento e distância lateral até `largura · 10 + 100`. `getClosestPath(pos, máscara)`: `Monster` usa 0xFF;
  o `AiNavigator` usa a máscara do path em que o monstro está, então **o tipo do nó separa sub-redes** (CANYON2 tem 4 tipos com
  4 nós cada; AIRPORT tipos 0 e 1).
- Medido: 891 nós; links por nó 0..6; 3048 de mão dupla e 3 de mão única; paths por fase: ISLAND **418/450**, TOKYO/TOKYODM 304,
  VEGAS 187, os outros < 70. ISLAND está perto do teto: acrescentar ligações lá pode estourar os 450.
- Patch: **no arquivo** (`InitBeforeDbLoad`). Ao vivo dá para reexportar (a rede é recriada no mesmo lugar), mas cada `Monster`
  guarda um ponteiro para o path em que está (`m_closestPath`) e o navegador guarda o caminho planejado; depois de reexportar o
  ponteiro continua válido (mesmo array), só o significado muda até o próximo `getClosestPath`. **[hipótese]** que isso basta.

## 6. `RST/<FASE>.MRS`: arquivo "reset" de luz, neblina e céu da fase (não são 48 KB de floats)

**[certo]** É um arquivo do `resetcom` (classe `reset`, objeto `resetObj` criado com CREATOR_KEY 4, que dá a extensão `.MRS` em
`resetcom::getFileAndPathname`). O nome vem de `whichLevel` (`setFileName` no `main`). É lido em `TheGame::InitBeforeDbLoad` por
`resetcom::forceRead` (depois de `globalResetObj`, o `MONSTERS.GBL`), inteiro em `resetcom::m_data[0xC000]`, e as chaves são
aplicadas na hora.

Formato: cabeçalho `f32 magic` (1263096448.0, 'KIRR' como float), `f32 versão` (2.0), `f32 criador` (4.0); depois chaves
`{s32 tipo, s32 tamanho (com os 8 do cabeçalho), dados}` até a chave 1 (fim). O editor do retail grava o buffer inteiro de 0xC000;
só os primeiros **2944 bytes** contam e o resto é lixo do buffer (em 18 dos 19 arquivos não é zero).

| Tipo | Tamanho | Conteúdo | Aplicado por |
|---|---|---|---|
| 2 | 0x18 | cor de fundo `s32 r, g, b, a` | `viewSetBgColor` |
| 3 | 0x18 | ambiente `f32 r, g, b` | `viewSetAmbient` |
| 4 | 0x40 | neblina `f32 minR, maxR, maxV, farClip; s32 r, g, b` | `fogSetFogParms` |
| 5 | 0x84 | 3 luzes paralelas: `f32 r[3], g[3], b[3], h[3], p[3]` (cor 0..255, direção em graus) | `plightSetParaLight` |
| 7 | 0xA14 | hora do dia: `f32 velocidade, s32 tod, f32 tempo`, 0xA00 de tabela | `viewSetTOD` |
| 0xBBA | (lido 8 bytes depois) | `f32` altura padrão do chão | `hdSetDefaultHeight`, `TheGame::m_defaultHeight` |
| 0xBC2 | 0x28 | `dustLighting` | cópia |
| 0xBC3 | 0x18 | `trailColoring` | cópia |
| 0xBC4 | 0x18 | `fragColoring` | cópia |

Medido: os 19 `.MRS` têm a mesma sequência de chaves (2 3 4 5 7 BBA BC2 BC3 BC4 1). Ex.: CENTRAL fundo (37, 20, 13), neblina de 1200
a 16380, luzes (190,140,110 h 120 p 45), (170,170,225 h -120 p 45), (225,170,160 h 0 p 25), altura padrão -100.
Não tem posição de nada; para o editor serve só se ele for mexer em luz/neblina. Patch: no arquivo (mesmo tamanho, trocar valores das
chaves) ou chamar os setters depois do `InitBeforeDbLoad`. Fica para a referência (`FORMATS.md`) corrigir a linha "48 KB of floats".

## 7. `.AIC` das pastas das fases

**[certo]** Não são de fase: são parâmetros da IA de cada monstro (`<FASE>/<MONSTRO><0..2>.AIC`, um por dificuldade; formato
`"%s%s%s%d.aic"` em `AiReset::setAi`), arquivos `resetcom` (mesmo cabeçalho e chaves, 0xC000) com chaves lidas por `AiReset::readKey`
(medidores de vida e stamina etc.). Sobrepõem os de `RST/` para o monstro daquela fase (ex.: `3-MILE/ENERGY0.AIC` difere de
`RST/ENERGY0.AIC`). Nenhuma posição; fora do escopo do editor.

## 8. Resumo para o patch JSON

| Formato | Momento da carga | Limite | Onde aplicar |
|---|---|---|---|
| SPT | `InitAfterDbLoad` | 255 no arquivo; listas 1/2/20/20 | arquivo, ou RAM (`StartPoints`), vale no próximo nascimento |
| PUP | `InitAfterDbLoad`; CRATER/QUAKE em eventos | 255 no arquivo; 100 ativos; tipo precisa de modelo no .NGP | arquivo, ou ao vivo com `exportPoints__11PowerUpTool` |
| FOP | `InitBeforeDbLoad`; QUAKE no terremoto | 255 nós (rede 256), 6 links | arquivo; ao vivo arriscado |
| PED | `InitBeforeDbLoad` | 255 nós, 6 links (na prática 1) | arquivo; ao vivo arriscado (ponteiros do PedGroup) |
| AIP | `InitBeforeDbLoad`; QUAKE no terremoto | 255 nós (rede 300), 6 links, 450 paths | arquivo; ao vivo possível |
| MRS | `InitBeforeDbLoad` | 0xC000 | arquivo ou setters |

Caminho mais simples e que cobre todos os casos (inclusive CRATER e QUAKE, porque é por nome de arquivo): aplicar o patch **na leitura
do arquivo** de pontos, montando o arquivo editado (mesmo formato; guardar `w`, 0x3C e os bytes não lidos de cada ponto que já existia).
O port já intercepta esses arquivos no `cdRead` de `wotm_mods.inc` (é onde está o corte de 255), e um mod já pode trocar
`PTS/<FASE>.<EXT>` inteiro hoje. Alternativa equivalente: gancho em `PointToolKit::loadPoints` (0x21DD80) chamando o original
com `notificar` = 0, aplicando o patch em `points[]`/`numPoints` pelo nome recebido e chamando o `exportPoints` da ferramenta.
Para pré-visualizar no editor sem recarregar: PUP e SPT reexportam com segurança; FOP/PED/AIP pedem recarga da fase.

## Em aberto

- O que `w` e o byte 0x3C significavam no editor (hipótese: altura do marcador e "selecionado"); nenhum leitor no jogo encontrado.
- Bit 3 das flags de PUP (2 pontos com 15): sem leitor conhecido.
- Efeito em jogo de `QUAKE.FOP` faltar (rede de veículos vira os 2 nós padrão no terremoto de Baytown): não visto rodando.
- Se mudar a rede AIP ao vivo deixa a IA estável (ponteiros `m_closestPath` e o caminho do `AiNavigator`).
