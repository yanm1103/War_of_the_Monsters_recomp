# Mecha como monstro custom: o caminho de carga no port

Investigação de 2026-10-27 para o item 10 do `TODO.md` (Mecha jogável) e o item 8 (campo `files` do `monster.json`). Só
leitura: nada foi mudado nem rodado. Pergunta: como plugar arquivos próprios (`.NGP/.PTR/.TEX/.RTX`, talvez `.CHR`) gerados por
uma ferramenta para que o jogo carregue o Mecha como um `Monster` de um tipo base.

## Resumo
- O caminho já existe e funciona **para skins**: uma pasta `mods/<mod>/skins/<id>/` com `.NGP+.PTR(+.TEX/.RTX)` vira
  `MON/<PREFIXO><n>.*` (n = 4..99) e o jogo a carrega como mais um traje do tipo. Nada no jogo depende do traje além do nome
  do arquivo e de dois testes de som.
- Para o Mecha, o mais simples é **um custom de base X cujo traje padrão é uma skin com arquivos próprios**. Falta pouco no
  registro (o `costume` do custom aceitar o id de uma skin) e, com base jogável, falta distinguir o custom dos monstros normais
  do mesmo tipo nos ganchos da luta (`customByType`).
- O `.CHR` (golpes e câmera) é por **tipo**, lido de `RST/<longname>.CHR` pelo `fileCdSearchFile`; um mod pode trocá-lo, mas
  troca para todos os monstros daquele tipo. Um `.CHR` por custom pede um gancho no `charreset::setCharacter`.
- Tipos livres (6, 12, 16, 17) não valem a pena: 16 e 17 nem são carregados pelo `LoadMonstersDB` (`sel >> 5 < 0x10`), e o 6/12
  ficam sem banco de som, sem retrato 3D, sem casos nos switches de colisão e de corrida. Recomendado: tipo base existente.

## 1. Como uma skin com arquivos próprios entra hoje

### Registro (antes do boot)
`Registry::scanSkins` (`native/wotm_mods.hpp:281-398`):
- Cada `skins/<pasta>/skin.json` vira o traje `n` (4, 5, ... por tipo, na ordem mod/id; `:352-378`). O nome é
  `<PREFIXO><n>` e tem de caber em 8 letras (`:357`, `fileStatus.names[9]` em `src/common/file.cpp:14`, `fileAddName` `:385-393`).
- Arquivos da pasta pela extensão (qualquer nome): `.NGP`, `.PTR`, `.TEX`, `.RTX` (`:363-373`). `.NGP` e `.PTR` andam juntos ou
  a skin é ignorada (`:374-377`).
- O que a pasta traz entra em `m_files["MON/<PREFIXO><n>.<EXT>"]` (`:380-388`); o que falta vira alias para o traje base
  (`m_alias`, `:390`). Os LSN falsos são dados depois, em `scan` (`:160-166`), para todos os `m_files`, inclusive os das skins.

### Carga (no jogo)
- `Shell::LoadLevelFiles` (`src/game/Shell.cpp:1834`) → `LoadMonstersDB` (`:1396`) → `LoadResTexture` (`:1226`) →
  `LoadTexture` (`:1302`) → `dbsRelocateFileZero` → `dbInitDb`.
- O nome é montado por `formatFilename1(path, MonsterLongNames[sel >> 5], m_costume[k], ext, ...)` = `\MON\<nome><traje>.<ext>`
  (`Shell.cpp:1736-1742`). **É aqui que `MON/ROBOT4` aparece**: o traje 4 só existe no nome.
- A troca pelo arquivo do mod é no gancho do `fileCdSearchFile` (`native/runtime/wotm_mods.inc:22-80`, `searchFile`): a chave
  `MON/ROBOT4.NGP` está em `m_files`, então o `sceCdlFILE` sai com o LSN falso e o tamanho do mod; o `sceCdRead`
  (`cdRead`, `:84-102`) lê do arquivo do PC. Com alias (arquivo que a skin não trouxe), o nome é trocado no próprio texto do jogo
  durante a busca (`:30-50`).
- `.PTR`: `dbsRelocateViaPtrListFile` (`src/common/dbs.cpp:436-460`) lê `mon/%s.ptr` com o nome registrado pelo `fileAddName`
  (`ROBOT4`), e passa pelo mesmo gancho.
- `dbInitDb` (`src/game/db.cpp:110-155`) reloca cada `.NGP` e liga as raízes de animação do próprio arquivo;
  `dbProcInteractive` manda ids 0x20..0x400 para `TheGame::MonsterParse` (`src/game/TheGame.cpp:243`): tipo = `id - (id & 0x1F)`,
  raiz `id & 0x1F == 0` cria `AddMonster(cs, tipo, dup)`, os outros são anexos. **O tipo do monstro vem do id da raiz do `.NGP`**,
  não do nome do arquivo.
- `Monster::initAfterDbLoad` (`src/game/MonsterInit.cpp:293-315`) busca as 300 animações por `animationGetHandle(m_typeBits,
  m_dupId, i)` (`src/common/animation.cpp:367`): a instância de personagem do `.NGP` tem de ter `head.id1 == tipo` e as
  animações com os ids que o código de `Monster` usa (idle 0x5A etc.). Animação que falta deixa o handle vazio (o jogo segura a
  pose, como no Goliath).
- Na luta, `wotm_skins.inc` (`soundSkin`, `:27-39`) troca o traje da skin pelo `base` no `SetPlayerMonster`/`SetAIMonster`, para o
  `m_skinNum` (que só o som lê) cair num traje do disco.

### Tetos
| Teto | Onde | O que acontece ao passar |
|---|---|---|
| RAM dos monstros | `loadMonstersDBHigh` (`native/runtime/wotm_overrides.cpp:1088`): NGP dos monstros e todos os RTX/TEX a partir de 0x2000000, contíguos, até o fim dos 64 MB | nada confere; `wotmCheckLoadEnd` (`:1102-1132`) loga a folga e avisa abaixo de 0,5 MB |
| Com `WOTM_HIGHRAM=0` | tudo de 0xA00000 até 0x1F10000 (`kLoadCeiling`) | aviso perto do teto; passar corrompe |
| `.PTR` | buffer de 0x30D40 (200.000) bytes na `MemoryStack` (`dbs.cpp:450-460`); maior do disco: `ALIEN0.PTR`, 164.376 | só `printf("Pointer file too big.  Prepare for crash...")` |
| `texInfo[1000]` | os texIds somam fase + `.TEX`/`.RTX` de cada monstro (`texmRelocate`) | `printf` e escrita depois do array; o port loga `carga da fase: N de 1000 texIds` (`wotm_overrides.cpp:1110-1119`, aviso a partir de 900) |
| VRAM residente (`.RTX`) | `texmResInit`/`texmResRelocate` (`src/common/texm.cpp:180`, `:618`) empilham as `.RTX` | o jogo **sai** ("TRYING TO LOAD TOO MUCH TEXTURE INTO SPACKLE TEXTURE LOCATION", `:678-681`); o port não avisa antes |
| Cache de texturas (`.TEX`) | bins entre `startVramAddr` e `lastVramTexAddr` (`texm.cpp:156-300`) | só `printf("Texture bin allocation overflow")` |
| `fileStatus` | 13 arquivos por tipo (fase + 12 monstros) | — |
| Nome | 8 letras (`ROBOT4`..`ROBOT99` cabem; `ASSBOSS10` não) | `scanSkins` recusa |
| Arquivo de mod | 256 MB (`kMaxSize`, `wotm_mods.hpp:109`); prévia 3D: 64 MB (`wotm::disc::readFile`, `native/wotm_disc.hpp:129`) | ignorado / sem prévia |

**Log hoje para a skin:** `[mods] skin <monstro>/<id> ("NOME", mod) = traje N (PREFIXOn); base B; da pasta: NGP PTR ..., K png`
(`wotm_mods.hpp:393`) e, na primeira busca, `[mods] MON/ROBOT4.NGP <- <mod> (N bytes; arquivo novo, nao esta no disco)`
(`wotm_mods.inc:68-70`). Como o arquivo é novo, **não há comparação com um tamanho**; o único teto conferido na carga é o
`texInfo` e a folga de RAM. `.PTR` acima de 0x30D40 e VRAM residente não têm aviso (pendência já listada no item 8).

**Tamanho máximo de NGP que cabe:** com a RAM alta, o limite é a folga dos 32 MB de cima. Pior caso medido com 8 monstros no
Metro: ~21 MB de folga; numa luta de 2 monstros no Capitólio (TEX 0,8 MB, RTX 58 KB) a área de cima tem ~30 MB. Na prática, um
NGP de monstro de **até ~15-20 MB** cabe numa luta de 2 a 4; o maior do disco é `FINAL0.NGP` (2,5 MB). O que aperta antes é o
`.PTR` (200.000 bytes = ~50.000 ponteiros e texIds) e a VRAM do `.RTX` (tem de ficar perto do tamanho de uma `.RTX` de monstro,
7-20 KB; a `ASSBOSS1.RTX` tem 14,8 KB).

## 2. Custom com arquivos próprios (`files`)

### O que existe
- `Custom` (`wotm_mods.hpp:63-78`) guarda `type`, `prefix` e `costume` (0..3: `:446-448` corta o resto). Sem campo de arquivos.
- `scanCustoms` roda **antes** de `scanSkins` (`:157-158`), para que uma skin possa declarar `"monster": "<id do custom>"`
  (`:309-318`): ela vira um traje do tipo base (`k.monster = cu->type`, `k.prefix = cu->prefix`), listado só pelo custom
  (`customCostumes`, `:206-211`). A `base` dela, sem o campo, é o `costume` do custom (`:333`).
- Ou seja: **um custom já pode ter uma skin com NGP/PTR/TEX/RTX próprios** (ex.: `skins/mecha/skin.json` com
  `"monster": "mecha"` e os 4 arquivos). Mas o traje padrão do custom continua sendo um traje do disco (o `costume` 0..3), e a skin
  aparece como segunda opção na seleção.

### Forma mais simples: o costume do custom é uma skin
Mudanças, sem implementar:
1. **`wotm_mods.hpp`, `scanCustoms` (`:446-448`):** aceitar `"costume"` como string (id de skin) e guardá-lo num campo novo
   (`Custom::costumeSkin`), ou aceitar `"files": "<pasta>"` relativo à pasta do monstro. Para `"files"`, a forma mais limpa é
   o próprio `scanSkins` tratar a pasta do custom como uma skin implícita (id = id do custom, `custom = cu.id`).
2. **`wotm_mods.hpp`, `scan` (`:157-158`):** depois do `scanSkins`, um passo que resolve `costumeSkin` para o `index` da skin e
   grava em `c.costume` (que então pode ser 4..99). Sem isso, o `costume` precisa ser conhecido antes das skins, e o índice só
   existe depois.
3. **`Skin::base` da skin implícita:** hoje cai no `cu->costume` (`:333`); com o costume virando 4+, precisa de um `"base"`
   explícito (0..3, traje do disco que dá som e o que faltar), senão o `if (k.base < 0 || k.base > 3)` o zera.
4. **`customCostumes` (`:206`):** já devolve `c.costume` primeiro; com ele = índice da skin, a skin aparece duas vezes (como
   costume e como skin do custom). Filtrar `k.index != c.costume`.
5. **Retrato (`native/wotm_texmods.hpp:109-115`):** a chave é `prefix + costume` (`ROBOT4`), então já serve; `portraitTex` precisa
   vir no JSON (o padrão só existe para `ASSBOSS1`, `wotm_mods.hpp:462`).
6. **Consumidores que já aceitam traje 4..99:** `WOTM_MATCH` (`wotm_overrides.cpp:2477-2481` copia `cu->costume`; `:2501` aceita
   até 99), `selectFinish`/`pickSel` (`wotm_padsel.inc:670-715`, grava o traje como está), prévia 3D (abaixo), `soundSkin`
   (`wotm_skins.inc`, acha a skin pelo índice e devolve o `base`).

### O que quebra com base jogável (2 robot, 3 lizard...)
`Registry::customByType(type, costume = -1)` (`wotm_mods.hpp:227-231`) acha **o primeiro custom do tipo**, sem o traje, e é usado:
- `wotm_custom.inc:286` (`setPov`): a câmera emprestada (`camera.from`) iria para **todo Robo-47** da luta, inclusive o normal.
- `wotm_custom.inc:138` (`setShownName`): só chamado para 13/14/15; com base jogável o HUD continua dizendo `robo-47`
  (`MonsterNewNames` é por tipo; renomear afetaria os Robo-47 normais).
- `wotm_padsel.inc:73-76` (`customOfSel`) devolve `nullptr` para tipos jogáveis: o nome no log e o destaque da casa CUSTOM
  (`:1554`) não reconhecem o custom.
O traje na luta também não identifica: o `m_skinNum` vira o `base` (`soundSkin`). A forma segura de saber "este `Monster` é o
Mecha" é pelo arquivo: o nó raiz (`m_cs->epNode`) dentro de `fileStatus.ngpAddr[i]..[i+1]` e o nome `fileStatus.names[i]`
(`ROBOT4`), como o `monsterPortrait` já faz (`wotm_overrides.cpp:1994-2015`). Um `customOfMonster(m)` com essa regra substituiria o
`customByType` nos ganchos de câmera e nome.

Com base 13/14/15 (tipo que só o custom usa), nada disso aparece: o `customByType` acerta porque não há outro monstro do tipo.

## 3. O `.CHR`
- Lido por `charreset::setCharacter` (0x122AC0, em asm em `asm/game/charreset.s`): copia `MonsterLongNames_006E8BC8[m_typeBits >> 5]`
  e chama `resetcom::setFileName`; `MonsterTweaks::setActiveMonster(m, true)` (`src/game/MonsterTweaks.cpp:29-35`, chamado pelo
  `initAfterDbLoad`) força a leitura. `resetcom::getFileAndPathname` (`src/common/resetcom.cpp:276-310`) monta
  `\RST\<nome>.CHR;1` (creatorKey 5) e o `fileReadf` (`:260-265`) lê pelo `fileCdSearchFile`, ou seja, **passa pelo gancho de
  mods**. Buffer de 0xC000 bytes (`m_data`), o tamanho de todo `.CHR` do disco (49.152).
- O `.AIC` (parâmetros de IA) vem do `AiReset::setAi` (`asm/game/AiReset.s`, 0x10DC50) com `"%s%s%s%d.aic"`: `rst`, `/`, o nome do
  tipo (`MonsterLongNames_006E83C0`) e um número do `TheGame` (`+0x1203CC`); na aventura entra o nome da fase. Também por tipo.
- **Um mod pode trocar** `mods/<mod>/RST/ROBOT.CHR`, mas vale para todo Robo-47. Para um `.CHR` só do custom, é preciso um gancho
  no `setCharacter` (ou no `setFileName` chamado por ele) que, para o `Monster` do custom, troque o nome por um próprio
  (`MECHA` → `\RST\MECHA.CHR`, servido como "arquivo novo" pelo registro). Com base 13/14/15 basta trocar o `.CHR` do tipo
  (`RST/FINAL.CHR`), porque ninguém mais usa o tipo.
- **Câmera emprestada** (`wotm_custom.inc:185-296`): não usa o jogo. `camTable` lê `RST/<from>.CHR` no host (`Registry::hostPath`,
  senão o disco), extrai a chave 0xFC6 (2 telas × 9 POVs de 0xA0), escala as três distâncias e, na entrada de cada
  `Cameras::SetCameraPOV` de uma câmera que segue o custom, copia a tabela para `Monster+0x6CF0`. Um `"camera": {"file": ...}`
  com um `.CHR` do mod caberia aqui sem tocar no jogo (trocar o `rel` em `camTable`, `:244`).

## 4. Tipos livres (6, 12, 16, 17)
| Tipo | Nome | Arquivos | O que o jogo faz |
|---|---|---|---|
| 6 | spider | só `RST/SPIDER.CHR` (sem `MON/`, sem `.AIC`) | sem banco de som (`loadMonsterBank`, `src/game/Sound.cpp:341-356`: o 0xC0 não tem caso; os `MonsterSound::play*` não têm caso para 0xC0); `collisInitPoints` e `StateRun::init` não têm caso (pontos de colisão e corrida ficam no padrão); aparece em `initDynamics`, `updatePowerUpGlow`, `takeHit`, `StateBatSwipe`, `StateCrowdControl::transitionInto` |
| 12 | ogre | só `RST/OGRE.CHR`, com outras chaves (`docs/FORMATOS.md`) | sem banco; quase nenhum switch (`updatePowerUpGlow`, `updateMonsterSound`) |
| 16 | temp16 | nada | **não é carregado**: `LoadMonstersDB/LoadResTexture/LoadTexture` pulam `sel >> 5 >= 0x10` com um `printf` (`Shell.cpp:1262`, `:1280`, `:1426`, `:1453`) |
| 17 | — | — | fora das tabelas (`MonsterLongNames[17]`, `MonsterNewNames[17][12]`) |

No port: `WOTM_MATCH` recusa 6, 12 e >15 (`wotm_overrides.cpp:2524-2531`); `monsterFile` (`native/wotm_savepics.hpp:91-95`) não tem
prefixo para 6 e 12 (sem prévia 3D); o narrador não tem nome para 6 (`wotm_narrator.inc:32`); `kCustomBases` só aceita 13-15
(`wotm_mods.hpp:52`). Usar um tipo livre pediria: prefixo e nome nas tabelas do port, banco de som (gancho no `loadMonsterBank`
como o do Jelly), `.CHR` e `.AIC` próprios e conviver com os switches sem caso. **Vale mais um tipo base existente**, com
arquivos trocados pela skin.

**Banco de som por tipo:** `gSoundBanks[0..10]` dos jogáveis (`Ape_s`, `Robot_s`...; versões `_ls` com 3+ slots), `BANK_MECH` 22
(`Assemb_s.bnk`, Goliath), `BANK_FINAL` 24 (`Final_s.bnk`, Jelly, Cerebulon **e os sons do Mecha**: `FinalBossSound::
playMechaPowerDownSound`, `updateMechLaser`, passos em `updateFinalBossSound`, `Sound.cpp:3935-4012`). Não há banco por traje.

**Base 2 (robot) x 15 (final) para o Mecha:**
- **2 robot:** código de monstro comum completo (agarrar, item de especial, escalar, IA comum, `Robot_s.bnk`), com casos do Robo-47
  que combinam com um robô (`StateJump` toca o jato, `MovementStates.cpp:2024`; `AiReflex.cpp:2311`; `Sound.cpp:2657`, `:2870`). Exige
  o `customOfMonster` da seção 2 para não contaminar Robo-47 normais e mantém o nome `robo-47` no HUD sem gancho novo. Golpes e
  câmera: os do `ROBOT.CHR` (ou gancho no `setCharacter`).
- **15 final:** o tipo é só do custom (câmera, nome no HUD, `Final_s.bnk` já resolvidos pelo `wotm_custom.inc`), mas herda as
  restrições do tipo 0x1E0 no retail (o Cerebulon não pega o item de especial, `PowerUps.cpp`; os testes de `Grapple.cpp` por tipo) e os sons do
  Cerebulon nos `MonsterSound::play*`. Dois Cerebulons na mesma luta (Mecha + Cerebulon) dividiriam `.CHR`.
- Em ambos o `.NGP` gerado precisa ter a raiz com o id do tipo (0x40 ou 0x1E0), a instância de animação com `id1` = tipo, e as
  animações do Mecha regravadas com os **ids do `Monster`** (idle 0x5A, andar, socos que o `.CHR` aponta). O Mecha da fase usa
  animações do roteiro do `FinalBoss::updateMecha`, não ids de `Monster`.

## 5. Seleção, launcher, save, `WOTM_MATCH`
- **Seleção nativa** (`wotm_padsel.inc`): a casa CUSTOM lista `Registry::customs()` (`customAt`, `:601-606`); `fillCustom`
  (`:1506-1522`) usa `cu->type` e `customCostumes`; a escolha grava `cu->type << 5` e o traje (`pickSel`/`selectFinish`,
  `:670-715`). Com o costume = índice da skin, já sai `ROBOT4`.
- **Prévia 3D** (`selModel`, `wotm_padsel.inc:2428-2452`; lista de saves, `wotm_savepics.hpp:185-196`): `monsterFile(tipo)` +
  traje → `Model::load` (`native/wotm_model.hpp:325-330`) → `wotm::disc::readFile`, que **procura antes em `mods/`**
  (`Registry::hostPath`, `wotm_disc.hpp:113-117`). Como a skin registra `MON/ROBOT4.NGP` em `m_files`, a prévia **aceita o arquivo
  do mod** sem mudança. Precisa do tipo em `monsterFile` (2 e 15 estão; 6 e 12 não) e de animação 0x5A no NGP (senão pose de
  base). Limite de 64 MB do `readFile`.
- **Enquadramento** (`kFraming`, `wotm_savepics.hpp:351-364`): chave `tipo | traje do disco << 8` (o `base` da skin,
  `wotm_padsel.inc:2480`). O Mecha herdaria o do Robo-47/Cerebulon do traje base; um enquadramento próprio pede a chave por custom
  (ex.: `"framing": {"zoom", "shiftY", "fwd"}` no `monster.json`, lido no `renderSelModels`).
- **Retrato do HUD:** `"portrait"` + `"portraitTex"` (texId na `.RTX` do Mecha) no `monster.json`; o `monsterPortrait` acha o
  retrato pelo arquivo do nó raiz, então uma `.RTX` própria com o retrato na última entrada também funciona sem PNG.
- **Narrador:** `"narrator": "<nome>"` → `narrator/<nome>.mp3` (`wotm_narrator.inc:135-143`).
- **Launcher** (`tools/launcher/WotmLauncher.cs:267-300`): lista os `monster.json` com `"base"` e manda `WOTM_MATCH=<id>`; não lê
  `costume` nem skins. Nada a mudar.
- **`WOTM_MATCH`:** `<id do custom>` vira tipo + `cu->costume` (`wotm_overrides.cpp:2470-2486`); traje até 99 aceito (`:2501`).
- **Save:** a aventura não guarda custom (a aventura não oferece a casa CUSTOM; `MODS.md`).

## Recomendação
1. A ferramenta gera `MECHA.NGP/.PTR/.TEX/.RTX` com a raiz e a instância de animação no id do tipo base e as animações nos ids do
   `Monster`; `.PTR` abaixo de 200.000 bytes, `.RTX` do tamanho de uma de monstro.
2. Os arquivos vão numa pasta de skin do custom (`mods/Mecha/skins/mecha/` com `skin.json` `{"monster": "mecha", "base": 0}`) ou
   direto em `mods/Mecha/monsters/mecha/` com `"files"`; o `monster.json` aponta o costume para essa skin (mudanças 1-4 da seção 2).
3. Base: **15 (final)** para o primeiro teste (nada a mexer em câmera/nome/banco, o tipo é exclusivo; o `Final_s.bnk` já tem os
   sons do Mecha) ou **2 (robot)** para o código de monstro comum completo, com o `customOfMonster` antes.
4. `.CHR`: trocar `RST/FINAL.CHR` no mod (base 15) ou gancho no `charreset::setCharacter` (base 2).
5. Seleção, prévia 3D, `WOTM_MATCH` e launcher já funcionam com o traje 4+; faltam só enquadramento por custom e `portraitTex`.
