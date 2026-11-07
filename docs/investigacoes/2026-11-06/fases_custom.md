# Fases custom: como uma fase nova entraria no jogo (editor de fase, etapa 3)

Tema: como o jogo lista, escolhe e carrega uma fase; o que é amarrado ao índice e o que é amarrado ao nome do arquivo; e uma
proposta de "fase custom por mod" (`stage.json`, no estilo do `monster.json`) que clona uma fase existente com um patch. No fim, uma
avaliação de fase do zero. Os relatórios irmãos desta pasta (`objetos_fase.md`, `colisao_fase.md`, `pts_fase.md`) cobrem o que o patch
mexe dentro da fase; aqui é só a "casca" (menu, índice, nomes, código por fase).

Marcação: **[certo]** = lido no código (C++ decompilado, asm conferido ou código de `native/`); **[hipótese]** = dedução não
verificada no jogo rodando. Nenhum código do repo mudou nesta etapa.

## Resumo
1. **Duas chaves separadas** [certo]. Tudo que é *arquivo* da fase sai de um **nome** (`whichLevel`, preenchido de
   `GameLevelNames_006EF748[m_levelNum]`): `.NGP/.TEX/.RTX/.PTR`, `PTS/<nome>.*`, `RST/<nome>.MRS`. Tudo que é *código* por fase
   (scripts, sons, música, água, regras de dano, IA) sai do **índice** (`Shell::m_levelNum`, copiado para `TheGame::m_levelId` no
   `Shell::InitRTState`). Trocar o nome sem trocar o índice dá "outra fase com o código da fase base". O próprio jogo já faz o
   inverso: Volcano (15) roda o código de Club Caldera (8), Tokyo Deathmatch (16) o som de Tsunopolis, Mini Baytown (14) e Lake
   Powell (13) os bancos de Baytown e do cânion.
2. **Há vagas livres no índice** [certo]: 17..24 (`dylan`, `david`, `lars`, `jenn`, `yeon`, `jacob`, `steve`, `pierre`) têm nome
   nas tabelas, nenhum arquivo e nenhum código; `levelUnlocked` as dá como abertas. A 17 já é a RANDOM do port. O switch da foto
   da seleção (`Shell+0x5D0`) tem 24 filhos (0..23), então **18..23 = 6 vagas** com foto; a lista estendida (`kList`) cabe 24
   entradas e hoje usa 17.
3. **Recomendação:** fase custom = vaga 18..23 no menu (nome e foto próprios); na confirmação, o port troca `m_levelNum` pelo índice
   da base e escreve o id da custom em `GameLevelNames_006EF748[base]`. O jogo então carrega `LVL/<ID>.*`, `PTS/<ID>.*`,
   `RST/<ID>.MRS` (o registro de mods serve os da pasta da fase e aponta os que faltam para os da base, como já faz com as skins) e
   o patch `LEVELS/<ID>.JSON` cai sozinho no gancho atual do `dbInitDb`, porque ele procura pelo nome do arquivo 0 do `fileStatus`.
4. Bases seguras para clonar: CENTRAL, METRO, VEGAS, CANYON2, CANYON1, MINISF, TOKYODM, UFO, CAPITOL (scripts só na aventura).
   Bases com script em todo modo (AIRPORT, 3-MILE, SANFRAN, ISLAND, VOLCANO, TOKYO) herdam o script, que procura objetos por id:
   mover pode servir; apagar esses objetos é risco.
5. Fase do zero: hoje não há escritor de `.NGP` de fase. O caminho viável é "base + geometria trocada" (malhas novas em cima do
   esqueleto de uma fase existente: raiz, céu, grade de colisão); um `.NGP` inteiro do zero é projeto grande.

## 1. Como o jogo lista e escolhe fases

### Tabelas de nome (todas com 28 entradas, índice = `m_levelNum`)
| Tabela | Endereço | Formato | Quem lê |
|---|---|---|---|
| `GameLevelNames_006EF748` | 0x6EF748 | `char[28][10]` | `ResolveCommandLineArguments`, `Shell::LoadLevelFiles`, `LoadLevelDB`, `LoadResTexture`, `LoadTexture` [certo] |
| `GameLevelNames_006E82A8` | 0x6E82A8 | `char[28][10]` | `AiReset`: pasta dos `.AIC` da aventura (`<FASE>\<MONSTRO>N.AIC`), só com `m_gameMode` 1 [certo] |
| `GameLevelNames_006F2208` | 0x6F2208 | `char[28][10]` | `userintMain`, só um `printf` [certo] |
| `GameLevelNames_006F2708` | 0x6F2708 | `char[28][10]` | `uidev` (menu de desenvolvimento) [certo] |
| `GameLevelLongNames_006EEEF8` | 0x6EEEF8 | `char[28][18]` | `screenLevelSelect` (texto da seleção, `charSelectStringOne`); o port já escreve "Random" na 17 [certo] |
| `GameLevelLongNames_006ED120` | 0x6ED120 | `char[28][18]` | `MonsterMc` (texto do save da aventura) [certo] |

Cada TU tem a sua cópia (a tabela é `static` num header), por isso quatro cópias de nomes curtos. Só a 0x6EF748 decide arquivo.
Índices e nomes: `GLOSSARY.md`, "Stages".

### Do índice ao arquivo [certo]
- Na entrada da sessão (`main`, laço `do { ... } while (m_inSession)`): `ResolveCommandLineArguments` faz
  `sprintf(whichLevel, "%s", GameLevelNames_006EF748[m_levelNum])` (com `argv[1]` no lugar quando há linha de comando:
  `UseCommandLineLevel`, e `argv[2]` vira o `m_levelNum`; é o caminho de desenvolvimento "arquivo X com o código da fase N", o mesmo
  desenho da proposta); `resetObj->setFileName(whichLevel, ...)` aponta o `.MRS` (`resetcom::getFileAndPathname`, tipo 4:
  `\RST\<nome>.MRS;1`).
- `Shell::LoadLevelFiles` / `LoadLevelDB` / `LoadResTexture` / `LoadTexture`: com `m_levelNum` 0 (ou linha de comando) usam
  `whichLevel`; com `m_levelNum < 0x1D`, `formatFilename(path, GameLevelNames_006EF748[m_levelNum], ext, SH_FILE_0)` =
  `\LVL\<nome>.<ext>`; o `LoadLevelDB` ainda faz `strcpy(whichLevel, GameLevelNames_006EF748[m_levelNum])`. Acima de 0x1C vai para
  `host0:monster.*`.
- Os pontos: `StartPointTool`, `PowerUpTool`, `AiPathTool`, `PathTool::loadPoints` montam `PTS/<GetLevelName()>.<ext>`
  (`GetLevelName` devolve `whichLevel`). Exceções por nome fixo: `QUAKE.AIP/.FOP/.PUP` (`sanFranInitAfter`) e `CRATER.PUP`
  (`threeMileInitAfter`).
- `fileStatus` (0x445390) guarda o nome de cada arquivo carregado em `+0xB1`, 9 bytes por arquivo (8 letras + 0). É dali que
  `wotm_levelpatch::stageName` tira o nome para achar `LEVELS/<NOME>.JSON`, e que `Scene::loadResources` lê o `.RTX` (paletas).
- Fora disso, nenhum código compara o nome da fase como texto: os únicos que chamam `GetLevelName` são os quatro `loadPoints` e o
  `LoadLevelFiles` [certo, busca nos `jal`].

### Menu de seleção (tela 8, `screenLevelSelect`) [certo]
- A lista vem de `g_levelList` (0x6F7B30): `LevelList {first, count, int *levels}`. Listas do retail: `PO_1PFreeForAll`,
  `PO_MPFreeForAll`, `PO_1PEndurance`, `PO_MPElimination` (0x2A1B70..0x2A1BA0, com os vetores `*_Levels` logo antes). A aventura
  não usa a tela 8: avança `m_levelNum++` de 1 a 11 (`EvaluateOnePlayerStoryStatus`, com `levelMonsters`/`levelMonsterModels`, 12
  fases, e `m_levelNumAIs[17]`).
- Por fase a tela mostra: `GameLevelLongNames_006EEEF8[nivel]` e o filho `nivel` do switch da foto (`shell->m_sw5D0`); trava pelo
  `levelUnlocked(nivel)` (10, 11, 14, 15 e 25..27 pela tabela `monsterSelectMode`; o resto, aberto) com o `m_lockedSw`.
  Confirmar grava `shell->m_levelNum = levels[cursor]` e `changeScreen(8, 99)`. Com 2+ CPUs no modo 2 ou CPU no modo 3, a quarta
  entrada é pulada.
- Prévia em filme: existe a tabela `movieFileNames` (0x2B31F8, `mov/<fase>.pss`, inclusive `dylan`..`steve`), mas a tela 8 do
  retail só chama `myMovie.Play(MainMovie)` ao voltar; não achei leitor da tabela [hipótese: sobra sem uso].
- Cartão de carga: `Shell::DisplayLoadBackground` escolhe o filho por `m_levelNum` (2→11, 3→3, ...) [certo para os casos lidos;
  o padrão para índices sem caso não conferido].
- **O port** (`native/runtime/wotm_levelsel.inc`, `NATIVE.md`, "Stage select"): na entrada da tela troca a lista do retail por uma
  cópia no topo da RAM (`kList`, até 24 fases) com as fases do retail, as que faltam de 1 a 16 e a RANDOM (17); desenha foto própria
  (`levelpics/<nome>.png`, `picName`/`findPic`, `g_pics[24]`) por cima dos filhos sem arte; no `changeScreen(8, 99)`
  (`wotm_padsel.inc`) resolve a RANDOM e chama o narrador (`wotm_narrator::announceLevel(m_levelNum)`). É exatamente onde uma fase
  custom entraria.

### Quantidade máxima
- Índices: 28 (0..27); `LoadLevelFiles` aceita até 0x1C [certo]. Livres: 17..24 [certo]; 17 é a RANDOM do port.
- Foto: 24 filhos no switch, então 18..23 têm filho [certo pelo comentário do `wotm_levelsel.inc`]; que objeto cada um desenha e se
  a foto PNG cobre bem, como na 17 [hipótese]. A 24 (`pierre`) não tem filho: `hierSetSwitch` com 24 sairia do switch.
- `kList`: 24 entradas; o retail estendido + RANDOM usa 17, sobram 7 (bastam para 18..23) [certo].
- Mais de 6 customs exigiria uma entrada só que pagina no lado nativo (fora do escopo desta etapa).

## 2. O que é amarrado ao índice (código por fase)

Fontes: `TheGame::InitBeforeDbLoad` (asm), `InitAfterDbLoad` e `Update` (C++), `SoundManager::loadRTSoundBanks`,
`LevelObjectSoundManager::init/update`, `StreamingSoundManager::initStreamingSoundManager` (asm) e uma varredura dos ~80 arquivos de
`asm/nonmatchings/game` que leem `m_levelId` (0x1203D0) ou `m_levelNum` (0x2BB0) [certo para a lista; os detalhes de cada um,
como marcados].

### Por fase (índice da base)
| # | Base | Init/Update da fase | Só na aventura? | Outros pontos por índice |
|---|---|---|---|---|
| 1 | CENTRAL | `centralInitBefore/After/Update` | sim | tutorial da aventura (`Grapple`, `MonsterStates`, `MonsterMeters`, `Pickup`, `PowerUps`: `m_gameMode 1 && m_levelId 1`); sons de Metro (1 e 4); água −5000 (= sem água) |
| 2 | VEGAS | `vegasInitBefore/After/Update` | sim (`vegasSpawnMilitary`) | banco `Vegas_s.bnk`, música `VEGASAMB/BAT`; o exército nativo pula VEGAS por nome (`wotm_army.inc`); água −5 |
| 3 | CANYON2 | `canyon2InitBefore/After/Update` | sim (Goliath, `canyon2LevelProgression`) | sufixo de IA no `formatFilename` (modo 0/1); `Ai::isMinion`; `PowerUps` (`assBossActive`); banco `Canyon_s.bnk` |
| 4 | METRO | nenhum | — | sons de Metro; água −4000 (padrão) |
| 5 | AIRPORT | `airportInitBefore/After/Update` | **não** | banco `airprt_s.bnk`; água −66 |
| 6 | 3-MILE | `threeMileInitBefore/After/Update`, `threeMileInitAi` (aventura) | **não** (chaminés, reator, banheira, contagem; chefe planta e cratera na aventura) | ids 0x2712, 0x2715..0x2718 só levam dano de jogador (`Destructible::takeHit`); `CRATER.PUP`; `HomingBug`, `Hud`, `Ai`, `StateDeath`, `gameGetStartPoint` com `craterSwitched`; `3Mile_s.bnk` (+`Count_s.bnk` na aventura); água −42.9 |
| 7 | SANFRAN | `sanFranInitBefore/After/Update` | **não** (terremoto) | `QUAKE.*`; id 0x2762 só com dano 100; `AiBrain::reset`, reflexos da IA (`m_levelId 7`); `SanFra_s.bnk`; água −37 |
| 8 | ISLAND | `islandInitAfter/Update` | **não** (vulcão) | tokens "Destructibles"; `Volcno_s.bnk`; água −24.8 |
| 9 | TOKYO | `tokyoInitBefore/After/Update` | **não** (onda, `UfoTokyo`, cena final) | `MonsterTriggers` (`m_levelId 9`), `StateDeath`; `Tokyo_s.bnk`, `TOKYOAMB/BAT` |
| 10 | UFO | `ufoInitBefore/After/Update` | sim | `Pickup::hatCheck`, `AiNavigator`, `globalUpdate` (10); `Space_s.bnk`; trava `levelUnlocked` |
| 11 | CAPITOL | `FinalBoss::initBefore/initAfter/update` | sim | `HomingBug`, `PowerUps`, `StateAirStrike`, `StateZapAttack` (11 + aventura); trava |
| 12 | TRAINING | nenhum | — | `Miltry_s.bnk`; sem música de fase (cai no padrão) [hipótese]; o modo treino do port usa o índice |
| 13 | CANYON1 | nenhum | — | `Canyon_s.bnk`, sons de cânion; o menu de pausa do port testa 13 |
| 14 | MINISF | nenhum | — | `SanFra_s.bnk`; trava |
| 15 | VOLCANO | `islandInitBefore` + `islandInitAfter/Update` | **não** | lava a +282; `Volcno_s.bnk`; trava |
| 16 | TOKYODM | nenhum | — | sons de Tokyo |
| 17..24 | (teste) | nenhum | — | sem banco de fase (caso padrão), sem música de fase (`initStreamingSoundManager` só tem casos até 16), água −4000, `m_levelNumAIs[17]` estouraria (só na aventura) |
| 25..27 | minijogos | `BigShotLevel`/`CrushLevel`/`DodgeBallLevel` pelo `m_mode` 7..9 | — | fora das listas da tela 8 |

Água: `setWaterLevel` por `m_levelId` no fim do `InitBeforeDbLoad` [certo; os valores, lidos dos imediatos].
Música: `initStreamingSoundManager` escolhe o par AMB/BAT por `m_levelId` (tabela de saltos com 17 casos) [certo]; o par de cada
fase [hipótese, pela ordem das strings].

### O que uma fase clonada herda (com o índice da base)
- **Tudo da tabela acima**: sons, música, água, cartão de carga, regras de dano por id, scripts que rodam em todo modo.
- **Os scripts dependem de ids de objeto da imagem** (as faixas 0x2710..0x3E7F e destrutíveis específicos registrados em
  `dbProcInteractive`): mover o objeto mantém o id e o script continua achando [hipótese]; apagar (`"delete"`) um objeto que o
  script usa pode deixar ponteiro nulo ou um evento que nunca termina [hipótese, não testado]. Bases sem script fora da aventura
  não têm esse risco.
- **Na aventura** a custom não entraria (a aventura avança o índice e não passa pela tela 8).

## 3. Proposta: fase custom por mod

### Formato
`mods/<mod>/stages/<pasta>/stage.json`:
```json
{ "id": "mycity", "name": "MY CITY", "base": "CENTRAL",
  "patch": "patch.json", "picture": "mycity.png", "narrator": "mycity" }
```
- `id`: até **8** letras/dígitos (o `fileStatus` guarda 8; a tabela de 10 bytes, 9). Vira o nome dos arquivos: `LVL/MYCITY.*`.
- `name`: até 17 letras (`GameLevelLongNames`, 18 bytes).
- `base`: nome de arquivo (`CENTRAL`) ou índice; dá o índice interno e os arquivos que a pasta não trouxer.
- `patch`: o mesmo formato de `levels/<FASE>.json` (com os `from` da base: o patch casa pelo handle e pela posição da imagem da base,
  que é a que carrega se a pasta não trouxer `.NGP`).
- `picture`: PNG 2:1 da foto da seleção (o caminho de `levelpics/` já existe). `narrator`: `narrator/<nome>.mp3` na confirmação.
- Arquivos próprios opcionais na pasta: `<ID>.NGP/.PTR/.TEX/.RTX`, `PTS/<ID>.SPT/.PUP/.FOP/.PED/.AIP`, `RST/<ID>.MRS`, PNGs da fase.
  O que faltar é o da base (alias).

### Registro (`native/wotm_mods.hpp`, junto de `scanSkins`/`scanCustoms`)
- `scanStages`: lê os `stage.json`, ignora a pasta `stages/` na sobreposição comum (como `skins/` e `monsters/`), dá a cada custom
  uma vaga 18..23 em ordem de mod/id (como as skins ganham os trajes 4+ a cada boot) e registra:
  - `LVL/<ID>.{NGP,PTR,TEX,RTX}`, `PTS/<ID>.{SPT,PUP,FOP,PED,AIP}`, `RST/<ID>.MRS`: o arquivo da pasta, ou alias para o da base
    (`m_alias`, o mesmo mapa que leva `MON/LIZARD4.*` a `MON/LIZARD1.*`). O jogo (`fileCdSearchFile`/`sceCdRead`) e o nativo
    (`disc::readFile`, que já consulta `aliasOf`) passam a achar os dois.
  - `LEVELS/<ID>.JSON` = o `patch` da custom (o `applyFor` atual o acha pelo nome do `fileStatus`, sem mudança).
- Log no estilo das skins: `[mods] fase mycity ("MY CITY", meu_mod) = vaga 18, base CENTRAL (1); proprios: NGP, SPT; patch 12 entradas`.

### Ganchos no runtime
1. **Lista** (`wotm_levelsel::extend`): acrescenta as vagas das customs antes da RANDOM e escreve o `name` em
   `GameLevelLongNames_006EEEF8[vaga]` (como faz com "Random"). Foto: `picName(vaga)` devolve o `picture` da custom. `unlocked()`
   já devolve aberta para 18..23. A RANDOM pode incluir as customs (opção).
2. **Confirmação** (`wotm_padsel.inc`, `changeScreen(8, 99)`, antes do `resolveRandom`/narrador; e o `WOTM_MATCH` em
   `wotm_overrides.cpp`, onde grava `m_levelNum`, aceitando o id da custom): primeiro restaura as entradas de
   `GameLevelNames_006EF748` que o port tiver mudado; se `m_levelNum` é vaga de custom, escreve o id (minúsculo) em
   `GameLevelNames_006EF748[base]` e põe `m_levelNum = base`. Daí em diante o jogo é o da base com outro nome: `ResolveCommandLineArguments`
   → `whichLevel` = `mycity`, `.MRS`, `PTS/*` e `LVL/*` pelo nome, e o código por índice da base. "Jogar de novo"/"refazer a fase"
   ficam na mesma sessão e relêem a tabela já trocada [certo pelo laço do `main`]. O narrador anuncia o `narrator` da custom.
3. **Checagens nativas por nome**: `wotm_army.inc` compara o nome do `fileStatus` com "VEGAS" para não pôr o exército duas vezes;
   com custom de base VEGAS precisa olhar a base. Os PNGs de textura de fase (`central_t0004.png`) seguem o nome do arquivo: a custom
   usa os seus (`mycity_t0004.png`) e não herda os da base (decidir se herda).
4. **Símbolos** (`wotm::sym::`/`wotm::fn::`): `GameLevelNames_006EF748`, `GameLevelLongNames_006EEEF8`, `g_levelList`, `shellInfo`
   (+0x2BB0 `m_levelNum`), `fileStatus`, `ResolveCommandLineArguments__FiPPc`, `LoadLevelFiles__5Shell`, `levelUnlocked__Fi`; os
   ganchos de `changeScreen` e `dbInitDb` já existem.

### Alternativas descartadas
- **Trocar uma fase existente** (`levels/CENTRAL.json`): já existe, mas a fase original some do menu.
- **Manter o índice 18..23 até o fim**: sem banco de som de fase, sem música, água padrão, sem scripts; serviria só para "fase
  neutra" e pede código nativo para cada lacuna. O índice da base dá tudo de graça.
- **Sobrepor `LVL/CENTRAL.*` só durante a sessão** (sem trocar o nome): o patch da custom e o de `levels/CENTRAL.json` disputariam o
  mesmo nome, e logs e PNGs ficariam ambíguos.

### Riscos
- Objeto usado por script apagado no patch (bases da tabela com "não" na coluna aventura) [hipótese].
- `QUAKE.*` e `CRATER.PUP` têm nome fixo: a custom de SANFRAN/3-MILE usa os da base (o terremoto e a cratera recriam pontos da
  fase original em cima da fase mudada). Contorno: alias por sessão desses dois nomes [hipótese].
- Restaurar a tabela de nomes a cada confirmação; se um caminho de entrada na sessão não passar pelo gancho (aventura, co-op,
  `WOTM_MATCH`), a base carregaria com o nome da última custom. A aventura e o co-op não oferecem a vaga, mas leem a tabela: restaurar
  também na entrada do `userintMain` fecha o caso.
- Fotos 18..23: o que os filhos do switch desenham não foi visto [hipótese].
- Carga: um `.NGP` próprio maior que o da base esbarra no teto 0x1F10000 (o `checkLoad` já avisa).

## 4. Fase nova do zero (sem template): avaliação
O que o jogo exige de um `.NGP` de fase (`FORMATS.md`, "Level object ids", "Stage collision"; relatórios irmãos):
raiz do mundo (id 1), céu (id 2, `viewSetSkyEntry`), grade de colisão (op 15, uma por fase) com caminhos e cascos coerentes com a
grade, a hierarquia de GROUP/LOD/TRANSLATE/OBJECT com os pacotes do VU1 por textura, prédios como `INTERACTIVE_STATE` →
`DESTRUCTIBLE` com estados e classes de vida, pickups com os modelos que o jogo registra, luzes, o `.PTR` com cada ponteiro, e
`.TEX`/`.RTX` com os texIds. Fora do `.NGP`: `.SPT` (pontos de partida; sem ele os monstros nascem na origem), `.AIP` (sem rede a IA
não navega), `.PUP`, `.FOP`/`.PED` opcionais, `.MRS` (copiar o da base).

O que já existe: leitura completa (`tools/ngp.py`, `tools/lvl.py`), escrita de malha com pele para monstros (`tools/ngpmesh.py`,
VU1), enxerto (`tools/ngpgraft.py`), texturas (`tools/tex.py --inject`), reconstrução de caixas e células de colisão (`lvl.py
patch`, regras do `colisao_fase.md`), escrita de pontos é simples (registro de 64 bytes).

Tamanho: **médio** para "base + geometria trocada" (malha estática nova num OBJECT, colisão em caixas derivadas da malha,
destrutíveis copiados da base com malha nova), reaproveitando raiz, céu, grade e `.MRS` de uma base; **grande** para um escritor de
`.NGP` de fase inteiro (hierarquia, `.PTR`, grade, LOD, luz assada nas cores de vértice). Recomendo o primeiro, como etapa depois do
`stage.json`, e o segundo só se o editor precisar de terreno próprio.
