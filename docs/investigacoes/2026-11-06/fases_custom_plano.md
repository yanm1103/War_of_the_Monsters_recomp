# Fases custom a partir de template: plano final (item 4 do "Editor de fase" no TODO.md)

Plano de implementação. Ele parte de `fases_custom.md` (mesma pasta), do primeiro rascunho do plano e de duas críticas, todas
conferidas no código. Nada foi mudado no repo nem compilado, e o jogo e o editor não foram abertos.
Marcas: [certo] = li no código; [hipótese] = falta medir; [dono] = decisão do dono.

## Resumo

- A ideia central continua: a custom carrega os arquivos pelo nome dela, e o código por fase vem do índice da base.
  - Os arquivos saem de `GameLevelNames_006EF748[m_levelNum]`: `ResolveCommandLineArguments` → `whichLevel` → `.MRS`, `PTS/*`, `LVL/*`.
  - O código por fase sai de `m_levelId`, que o `InitRTState` copia do `m_levelNum`.
  - Basta escrever o id da custom em `GameLevelNames_006EF748[base]` e pôr `m_levelNum = base`.
- O que mudou em relação ao rascunho:
  1. **A tradução vaga → base vai para a confirmação**, `changeScreen(8, 99)`, e para o `WOTM_MATCH`, onde ele grava o `m_levelNum`.
     No `ResolveCommandLineArguments` seria tarde, porque o `DisplayLoadBackground` da tela 99 já escolheu o cartão pela vaga.
     O gancho no `ResolveCommandLineArguments`, o filtro por `m_mode` e o ramo "manter" saem do plano.
  2. **A tabela é restaurada** em dois pontos: no começo da confirmação e na entrada do `userintMain`.
  3. **O patch da custom é `levels/<ID>.json`**, achado pelo `findPatch` atual sem código novo. O campo `patch` e o passo do `findPatch` saem.
  4. **O alias de nome mais longo é trocado no lugar**, só para `LVL/`, `PTS/` e `RST/` e com até 8 letras, como hoje. A vaga nova de RAM sai.
  5. **Na v1 a custom não traz `LVL/*` próprio**: geometria, `.PTR`, `.TEX` e `.RTX` são sempre os da base, lidos do disco.
     Ela pode trazer `PTS/*`, o `.MRS` e PNGs de textura.
  6. **A custom herda as restrições da base por índice.** METRO fica fora com CPUs quando não há `.AIP` próprio. A trava da base vale para a custom [dono].
  7. **O `WOTM_MATCH` ganha uma sintaxe sem ambiguidade**: `@<id>` no último campo.
  8. **Foto e narrador seguem as convenções que já existem**: `levelpics/<id>.png` e `narrator/<nome>.mp3`. Sem PNG, o switch mostra o filho da base.
  9. **O cursor da tela 8 não é lembrado**, como no retail. A escrita de `m_levelNum` na tela 8 sai.
  10. **O editor vai em duas etapas**: primeiro listar, abrir, salvar e testar; depois o "New stage…".
- A primeira entrega que se pode testar junta os passos 1 a 3: um `stage.json` escrito à mão, com `WOTM_MATCH=3,@mycity`.

## O que existe (conferido)

- **Nome → arquivos** [certo]
  - `src/game/Shell.cpp:1155-1175`: `ResolveCommandLineArguments` faz `sprintf(whichLevel, GameLevelNames_006EF748[m_levelNum])`.
  - `:1373-1388`: `LoadLevelDB` e `fileAddName` gravam em `fileStatus+0xB1` o nome **pedido**.
  - `:1834-1850`: `LoadLevelFiles` busca `\LVL\<nome>.NGP/.TEX`.
  - `:272-331`: o laço da sessão roda `ResolveCommandLineArguments` → `setFileName(whichLevel)` → `InitRTState` → `InitBeforeDbLoad` → `LoadLevelFiles`.
    Ele repete sem passar pelo `userintMain`: RESTART, F10 e "jogar de novo".
- **Tela 99** (`src/game/ui.cpp:352-358`) [certo]
  - `DisplayLoadBackground(false)` escolhe o cartão por `m_levelNum` (Shell+0x2BB0, cadeia de comparações por índice no asm).
  - Ela roda antes de o `userintMain` voltar. O `printf` "Starting level" usa `GameLevelNames_006F2208`, outra tabela, e só serve ao log.
- **Confirmação** (`native/runtime/wotm_padsel.inc:2401-2406`) [certo]
  - O gancho do `changeScreen(8, 99)` roda **antes** da original: `resolveRandom`, depois `announceLevel(m_levelNum)`, depois `extraCpus`.
  - O `resolveRandom` pode sortear qualquer entrada da `kList`, inclusive as vagas 18..23.
- **`WOTM_MATCH`** (`native/runtime/wotm_overrides.cpp`) [certo]
  - `:317`: o `userintMainDirect` grava `m_levelNum = g_matchLevel`.
  - `:2885-2933`: o parser lê todo campo que começa com letra como id de monstro custom (`Registry::custom`). Sem achar, deixa
    `v = 0` com `end == p`, e a partida é recusada.
  - `:2967`: `g_matchLevel = v[n-1]`.
  - `:2971`: com o `WOTM_MATCH`, o `userintMain` é trocado pelo `userintMainDirect`. Nenhum outro código nativo engancha o `userintMain`.
- **Cursor da tela 8** (`src/game/screen.cpp:3132-3143`) [certo]
  - Na primeira passada, `currentSelection[8] = LL->first`, e `beginning2925 = LL->first` é o limite da volta. O `m_levelNum` não é lido.
  - Com CPUs (modo 2 com 2 ou mais, modo 3 com 1 ou mais), a entrada de índice 3 é pulada (`:3160-3204`).
- **Trava** (`screen.cpp:4889-4917`) [certo]
  - `levelUnlocked`: 10, 11, 14 e 15 pela tabela de destrave (mais 25..27); o resto, 1.
  - `wotm_levelsel.inc:41-53` (`unlocked`) faz o mesmo. As vagas 18..23 saem abertas.
- **Pontos no disco** [certo, `ls disc/PTS`]
  - METRO não tem `.AIP`: é a quarta entrada, a que o retail pula com CPUs.
  - CANYON1 e TRAINING não têm `.FOP`/`.PED`.
  - Todas as 9 bases têm `.SPT` e `.PUP`.
- **Seleção** (`native/runtime/wotm_levelsel.inc`) [certo]
  - `kRandom = 17`. A `kList` fica em `PS2_RAM_SIZE-0x300`, com até 24 entradas.
  - `extend` põe a RANDOM por último. `resolveRandom` exclui 12, 16, a 4ª entrada com CPUs e as trancadas.
  - `kOwn[] = {12, 13, 16, kRandom}`. `picObject` acha o OBJECT do filho do switch, que tem 24 filhos (NATIVE.md, "RANDOM").
  - As fotos vêm de `levelpics/<nome>.png` (em `mods/<mod>/levelpics/`, ao lado do ELF e na raiz).
- **Fotos do SHELL.TEX** (`native/wotm_savepics.hpp:42-61`) [certo]: `kLevelPics` não tem 12, 13 nem 16.
- **Alias** (`native/runtime/wotm_mods.inc:44-73`) [certo]
  - O `searchFile` troca o nome no próprio texto do jogo e restaura depois, só com `to.size() <= from.size()`. Com `MYCITY` → `CENTRAL`, falha.
  - `m = find(aliasOf(rel))`: se outro mod sobrepõe o arquivo da base, o alias serve o arquivo desse mod.
  - O log diz "skin: arquivo do traje base".
- **`fileCdSearchFile`** (`src/common/file.cpp:557-612`) [certo]
  - Põe o caminho em maiúsculas no próprio buffer, copia nome, setor e tamanho do TOC e não guarda o ponteiro.
  - Os `fileRead*` passam pelo `gFileName` (0x20 bytes, `wotm_syms.h:251`) e pelo cache `D_00735730`, que compara o nome pedido.
- **Patch** (`wotm_levelpatch.inc:1555-1583`) [certo]
  - `findPatch` varre `mods/*/levels/<NOME>.json` a cada carga, sem olhar maiúsculas, e o nome vem do `fileStatus`.
  - Com o id no `fileStatus`, `levels/MYCITY.json` já é achado.
- **Registro** (`native/wotm_mods.hpp`) [certo]
  - `:271-272`: a cópia comum pula `SKINS/` e `MONSTERS/`.
  - `:282-283`: `scanCustoms` e depois `scanSkins`.
  - `:295`: `assignLsns`.
- **Exército** (`wotm_army.inc:196-201`) [certo]: compara o nome do `fileStatus` com "VEGAS". O `loadResHook` roda depois do `InitRTState`.
- **Narrador** (`wotm_narrator.inc:9-10, 85, 151`) [certo]
  - `narrator/<nome>.mp3` em `mods/<mod>/narrator/`, ao lado do ELF e na raiz.
  - Para 17..24, o `levelName` devolve nulo.
- **Editor** (`tools/level_editor/editor.py`) [certo]
  - `:56`: `LEVEL_NUM`.
  - `:445`: `WOTM_MATCH=3,%d` com `LEVEL_NUM.get(name, 1)`. Uma fase desconhecida abriria CENTRAL sem avisar.
  - `:448`: `WOTM_TRAINING`.

## Desenho

### A. Formato e regras
`mods/<mod>/stages/<pasta>/stage.json`:
```json
{ "id": "mycity", "name": "MY CITY", "base": "CENTRAL", "narrator": "mycity", "random": true }
```
- **`id`**: `[A-Za-z][A-Za-z0-9]{0,7}`, isto é, começa com letra e tem até 8 caracteres (o `fileStatus` guarda 8).
  - Recusado, sem diferença de maiúsculas, quando repete:
    - os 28 nomes de `GameLevelNames` e `monster`;
    - `SHELL`, `SHELL2`, `PRESHELL`, `LOAD`, `UI` e `ARMY`;
    - os stems de `MON/` do disco;
    - os ids e os nomes de arquivo das skins e dos monstros custom (os PNGs casam só pelo stem, em `wotm_texmods.hpp:81-90`);
    - outra custom.
  - Cada recusa vira uma linha no log.
- **`name`**: até 17 caracteres (`GameLevelLongNames`, 18 bytes).
  - Só ASCII em maiúsculas, dígitos, espaço e a pontuação dos nomes do retail [hipótese: a fonte do `screenWriteInstructions` não
    tem acentos nem minúsculas]. O que estiver fora disso vira maiúscula ou é tirado, com log.
- **`base`**: nome do arquivo ou índice. Só 9 bases são aceitas (lista do TODO): CENTRAL 1, VEGAS 2, CANYON2 3, METRO 4, UFO 10,
  CAPITOL 11, CANYON1 13, MINISF 14 e TOKYODM 16.
  - São as 16 menos as 6 que têm script em todo modo (AIRPORT, 3-MILE, SANFRAN, ISLAND, TOKYO, VOLCANO) e menos TRAINING (12).
  - Fica fora a TRAINING: ela é a fase do modo treino do port, que usa o índice, e não tem `.FOP`/`.PED` nem música de fase.
  - As de script só na aventura (1, 2, 3, 10, 11) são seguras porque a custom não entra na aventura.
- **`narrator`**: nome em `narrator/<nome>.mp3`, buscado como nas skins. Sem ele, o narrador fica calado.
- **Foto**: `levelpics/<id>.png`, buscada como as outras fotos. Não há campo para ela.
- **`random`**: padrão `true`.
- **Patch**: `levels/<ID>.json` do mesmo jeito de hoje, em qualquer mod. Os `from` são os da base.
- **Arquivos próprios na pasta, v1**
  - Aceitos:
    - `<id>.SPT/.PUP/.FOP/.PED/.AIP`, registrados como `PTS/<ID>.*`;
    - `<id>.MRS`, registrado como `RST/<ID>.MRS`;
    - PNGs `tNNNN.png` e `rtx_tNNNN.png`, numa lista `stagePngs()` parecida com a `skinPngs()`.
  - Recusados, com log: `<id>.NGP/.PTR/.TEX/.RTX`.
    - O `.PTR` é a tabela de ponteiros do `.NGP`, e o `.TEX`/`.RTX` levam os texIds que o `.NGP` usa: não se misturam com os da base.
    - Os `from` do patch e o editor valem para a geometria da base.
    - A geometria própria fica para a v2, em grupo de tudo ou nada.

### B. Registro (`native/wotm_mods.hpp`, `scanStages`)
- Roda depois de `scanSkins` e **antes de `assignLsns()`** (`:295`). `stages/` e `stage.json` são achados sem olhar maiúsculas (`childNoCase`), por causa do Linux.
- A cópia comum pula `STAGES/`.
- `struct Stage {id, name, base, slot, mod, dir, narrator, random, ownPts}`. As vagas 18..23 são dadas na ordem mod/id. A partir da sétima custom, as que sobram ficam de fora, com log.
- Aliases: `LVL/<ID>.{NGP,PTR,TEX,RTX}`, mais os `PTS/<ID>.*` e o `RST/<ID>.MRS` que faltarem, apontam para o arquivo da base.
  - Ficam marcados como **só do disco** (`m_discAlias`): o `searchFile` e o `disc::readFile` não servem para eles a sobreposição de outro mod.
  - Assim a custom carrega o mesmo que o editor e o `check_patch` leem.
  - Um `levels/CENTRAL.json` de outro mod também não vale na custom, porque o nome dela é outro.
- Quando a base não tem um arquivo (por exemplo `.AIP` da METRO, `.FOP`/`.PED` da CANYON1), o alias não é criado.
  - O jogo não acha o arquivo, igual na base.
  - A linha do registro diz "a base nao tem", para que um "NAO ACHADO" depois não pareça erro do mod.
- Acessores: `stages()`, `stage(id)` e `stageBySlot(n)`.
- Log: `[mods] fase mycity ("MY CITY", meu_mod) = vaga 18, base CENTRAL (1); proprios: SPT, PED; disco: NGP PTR TEX RTX PUP FOP AIP MRS`.

### C. Alias de nome mais longo (`native/runtime/wotm_mods.inc`, `searchFile`)
- A condição `to.size() <= from.size()` é relaxada só para `LVL/`, `PTS/` e `RST/` com `to` de até 8 letras.
  - Todo buffer por onde passam os caminhos de fase já cabe um nome de 8 letras, porque o disco tem `TRAINING.*`.
  - Exemplos: `gFileName` tem 0x20 bytes; o caminho do `LoadLevelFiles` está num buffer de 0x80 [certo para os dois; os buffers do
    `.MRS` e do `PointToolKit` são hipótese, testados por extensão no risco 1].
- O `saved` passa a guardar `max(len, len novo) + 1` bytes, e a restauração devolve tudo. Hoje ela restaura só `len + 1` e deixaria lixo depois do NUL.
- O log diz "fase custom: arquivo da base" quando o alias é de fase.
- O cache `D_00735730` compara o nome pedido (`MYCITY`), então não confunde a custom com a base [certo, `file.cpp:175`].

### D. Ativação e restauração (`wotm_padsel.inc` e `wotm_overrides.cpp`; um `wotm_stages.inc` novo com `activate`/`restore`)
- Em `restore(rdram)`:
  - se alguma entrada de `GameLevelNames_006EF748` foi trocada, devolve o original e zera `g_active`;
  - o original é copiado **na primeira escrita**, não na instalação dos ganchos.
- Em `activate(rdram)`:
  - se `m_levelNum` é a vaga de uma custom, guarda o original de `[base]`, escreve o id em minúsculas (10 bytes, com NUL), faz `m_levelNum = base` e `g_active = stage`;
  - log: `[fases] custom mycity: vaga 18 -> base CENTRAL (1)`.
- No gancho do `changeScreen(8, 99)`, a ordem é:
  1. `restore`;
  2. `resolveRandom`, que pode sortear uma custom;
  3. o narrador, com a vaga: `announceLevel` toca o `narrator` da custom;
  4. `activate`;
  5. `extraCpus`.
- No `userintMainDirect` (`WOTM_MATCH`): `restore` e depois `activate`, logo depois de gravar `m_levelNum`.
- Um gancho novo envolve o `userintMain__Fv` quando não há `WOTM_MATCH`: chama `restore` e depois a original.
  - Toda volta aos menus passa por ali: aventura, co-op, minijogos, demonstração e "voltar ao menu".
  - RESTART, F10 e "jogar de novo" não passam, e a custom continua. O gancho, o filtro por modo e o ramo "manter" do rascunho somem.
- Com isso, o `DisplayLoadBackground`, o `printf` da tela 99 e todo leitor nativo de `m_levelNum` já veem a base.
  - Esses leitores são `wotm_pausemenu.inc:438` (13), `wotm_coop`, `adventureAis` e o save. A custom de base 13 ganha o menu de pausa da 13.
  - O cartão de carga é o da base [certo: ele é escolhido por índice].

### E. Lista, trava e foto (`native/runtime/wotm_levelsel.inc`)
- **Lista**
  - `extend` acrescenta as vagas antes da RANDOM, com a mesma regra de `WOTM_HIGHRAM=0` das 1..16, e grava o `name` em `GameLevelLongNames_006EEEF8[vaga]`.
  - Custom de base METRO sem `.AIP` próprio fica **fora da lista** quando a tela 8 abre com CPUs (a mesma condição do `skip3`).
    - O retail pula a METRO pelo mesmo motivo; numa vaga 18..23 ela escaparia do pulo.
- **RANDOM**: o `resolveRandom` usa `unlocked(base)`, pula a custom de base METRO sem `.AIP` quando `skip3` vale e respeita `random: false`.
  - A exclusão de 12 e 16 não é herdada, porque ela é por serem fases de teste da equipe (ver "Críticas rejeitadas").
- **Trava [dono]**. Proposta: a custom herda a trava da base.
  - Um gancho em `levelUnlocked__Fi` devolve `orig(base)` para a vaga. A tela 8 mostra o cadeado da base (`m_lockedSw`) e não confirma.
  - Alternativa: não listar a custom de base trancada.
- **Foto**
  - Depois da original, quando a entrada atual é uma custom, o gancho põe o switch `m_sw5D0` no filho da base, cuja estrutura é conhecida.
  - Com `levelpics/<id>.png`, a custom entra em `kOwn`, e `g_obj[vaga] = picObject(sw, base)` desenha o PNG por cima.
    - A busca do `nodeTexture` tem de preferir a vaga quando os dois apontam para o mesmo nó.
  - Sem PNG, fica a foto da base, inclusive a nativa da 13 e da 16.
  - Isso dispensa os filhos 18..23 do switch.
- **Cursor**: nada muda. O retail volta para `LL->first`.

### F. Exército (`wotm_army.inc:200`)
- O teste passa a ser `m_levelId != 2` (`TheGame+0x1203D0`) em vez do nome "VEGAS".
- Como a v1 não aceita `.NGP` próprio, uma custom de base VEGAS sempre traz os veículos do NGP de VEGAS.
- Na v2, com geometria própria, a regra muda para "base VEGAS e NGP da base".

### G. `WOTM_MATCH`
- O último campo pode ser `@<id>`: `WOTM_MATCH=3,@mycity`.
- O parser trata o `@` antes do `strtol`: `Registry::stage(id)` → `v = vaga`. Com outro campo depois, ou com o id não achado, a partida é recusada com log.
- Não há ambiguidade com os monstros custom, e a regra do id (A) também proíbe os mesmos nomes.

### H. Texturas PNG (`wotm_texmods.hpp`)
- O stem da custom (`MYCITY`) procura primeiro em `stagePngs()` da pasta.
- Depois cai para o stem da base (`CENTRAL_tNNNN.png` de qualquer mod), então um mod de HUD ou de textura feito para a base continua valendo.
- Os texIds são os mesmos, porque o `.TEX` é o da base.

### I. Editor (`tools/level_editor/`, por último, porque está em edição agora)
- Cada fase tem duas chaves:
  - **`base`**: objetos, cena, facts, cache, nomes (`lvlnames`) e `check_patch`;
  - **`id`**: patch (`levels/<ID>.json` pelo `patch_path`/`patch_winner` atuais, só com outro nome), Test (`WOTM_MATCH=3,@<id>`), Reload e PNGs.
- Partes:
  - **7a**: `Stages.list` inclui as customs, validadas com as mesmas regras do A, com o motivo de cada recusa na lista. Abrir, salvar, testar e Reload.
    - O `launch_game` deixa de usar `LEVEL_NUM.get(name, 1)` e passa a usar o nível da base ou `@<id>`.
    - A opção "every" do painel HUD inclui as customs.
  - **7b**: com `PTS` próprios na pasta, o `stage_root` da custom sobrepõe os `PTS/<id>.*` aos da base. O `pedsapi`/`lvl.Stage` leem pelo nome da base.
    - Até isso existir, a custom com PTS próprios abre com um aviso, e os painéis de pontos e pedestres ficam só para leitura.
  - **7c**: "New stage…" (id, nome, base entre as 9, mod e "começar com as mudanças atuais"). Grava `stages/<pasta>/stage.json` e copia o patch atual para `levels/<ID>.json`.
    - Como a base é a mesma, não há conversão.
    - A custom nova só entra no jogo depois de reiniciá-lo, porque o registro é lido no boot. O Test já reinicia o jogo.

## Riscos e provas

1. **Alias mais longo por extensão** [certo que hoje falha].
   - Montagem: uma custom `mc` de base CENTRAL e uma `longnam8` de base UFO (essa encurta), com `WOTM_MATCH=3,@mc`.
   - O log `[mods]` tem de mostrar achado, com o tamanho da base, para `LVL/*.NGP/.PTR/.TEX/.RTX`, `PTS/*.SPT/.PUP/.FOP/.PED/.AIP` e `RST/*.MRS`.
   - A sequência de `.PUP` e `.MRS` também precisa estar certa, porque esses caminhos vêm de outros buffers. Antes do C, "NAO ACHADO".
2. **Restauração**: a sequência abaixo, com o log de `[fases]` e `[fase] <NOME>` a cada carga.
   - Custom pelo menu → menu principal → CENTRAL normal (`CENTRAL`) → aventura (`CENTRAL`, `m_mode 1`) → co-op → minijogo → eliminação na custom.
   - Também a demonstração do menu ocioso (o ramo do `EvaluateGameStatus` que sorteia `m_levelNum` 1..11): nenhuma carga com o id errado.
   - [hipótese] O `userintMain` não roda de novo entre a confirmação e a sessão. Prova: nenhuma linha de `restore` entre `activate` e `[fase] MYCITY`.
3. **Replay na sessão**: RESTART, "jogar de novo" e F10 continuam em `MYCITY`, e o patch editado vale no F10.
4. **Código e cartão da base**: o cartão de carga da base na **primeira** carga, numa foto da tela 99, além de `m_levelId == base` no log.
   - O banco e a música da base (`[snd]`) e a água da base.
5. **O patch casa com a base**.
   - Editor: o `check_patch` da custom e o da base, com o mesmo JSON, dão o mesmo resultado.
   - Jogo: `[fase] MYCITY: ... aplicado`, com as mesmas contagens da base.
6. **Tela 8 com até 23 entradas** (testada com 17).
   - Seis customs, percorridas nos dois sentidos, no modo 2 com 0 e com 2 CPUs, no modo 3 com CPU e na eliminação.
   - Uma foto de cada uma (PNG e fallback da base, inclusive as bases 13 e 16) em `docs/screenshots/2026-11-06/`.
   - A custom de base METRO some com CPUs.
7. **Trava** [dono]: com o save sem a UFO, a custom de base UFO mostra o cadeado e não confirma. A RANDOM não a sorteia.
8. **Exército com base VEGAS**: uma só leva de veículos (linha `[exercito]`).
9. **Nome**: uma foto da tela 8 com o `name` mais longo (17) e com pontuação.
10. **Linux**: só `std::filesystem` e código do registro. O build Linux tem de passar (`LINUX_MAINTENANCE.md`).

## Passos em ordem

1. **Registro** (M): `native/wotm_mods.hpp` (`scanStages`, `Stage`, `m_discAlias`, regras do id e da base, `stagePngs`), mais o
   `native/wotm_disc.hpp`, que precisa respeitar o `m_discAlias`.
   - Conferir: as linhas `[mods] fase ...` e cada recusa (id inválido ou reservado, base fora das 9, `.NGP` próprio, sétima custom), com `mods/_tmp/` local.
2. **Alias mais longo** (P): `native/runtime/wotm_mods.inc` (condição, restauração maior, texto do log, `m_discAlias` no `find`).
   - Conferir: risco 1.
3. **Ativação e restauração, mais o `WOTM_MATCH` com `@id`** (M): `wotm_stages.inc` novo, ganchos em `wotm_padsel.inc`
   (`changeScreen(8, 99)`) e em `wotm_overrides.cpp` (`userintMainDirect`, wrapper do `userintMain__Fv` e parser). Os símbolos já existem em `wotm_syms.h`.
   - Conferir: `WOTM_MATCH=3,@mycity` com um `levels/MYCITY.json` à mão; riscos 3, 4 e 5. **Primeira entrega usável.**
4. **Menu** (M): `wotm_levelsel.inc` (`extend`, nomes, METRO, RANDOM, foto pela base, gancho de `levelUnlocked`) e `wotm_narrator.inc` (narrador da custom).
   - Conferir: riscos 2, 6, 7 e 9, pelo menu. Mandar o passo a passo ao dono, sem roteiro de controle.
5. **Exército e PNGs** (P): `wotm_army.inc` e `wotm_texmods.hpp` (fallback do stem para a base).
   - Conferir: risco 8, e um PNG `CENTRAL_tNNNN.png` de outro mod aparecendo na custom.
6. **Editor 7a** (M), depois **7b** (M) e **7c** (P): `editor.py`, `web/app.js`, `web/index.html`, `pedsapi`/`hudapi`/`lvlnames`.
   - Só quando os outros agentes tiverem soltado esses arquivos.
   - Conferir: o `check_patch` da custom igual ao da base, o Test in game na custom (`[fase] MYCITY` na aba Game), o Reload, a recusa
     mostrada na lista e o painel HUD gravando para a custom.
7. **Docs** (P):
   - `MODS.md`: a seção "Custom stages", mais "Stage select pictures", "Level editor" e "Under the hood".
   - `NATIVE.md`: "Stage select" e `WOTM_MATCH` com `@id`.
   - `GLOSSARY.md`: vagas 18..23.
   - `TODO.md`: o item 4 sai, e entram o que ficou para a v2 e a decisão da trava, se ela ainda estiver aberta.
   - Rodar `python tools/doclint.py`. O commit vai sem gate (só `native/`, `tools/` e `docs/`).

## Críticas rejeitadas, ou aceitas em parte, e por quê

- **"Usar uma vaga de 0x80 bytes no topo da RAM para o alias longo, com o mapa completo do topo"** (crítica 1): rejeitada.
  - A troca no lugar basta, porque todo buffer de caminho de fase já cabe 8 letras (TRAINING no disco).
  - Sem vaga nova, não há mapa a manter nem risco de colisão.
  - O ponto da crítica sobre o `fileCdSearchFile` (põe em maiúsculas no lugar e não guarda o ponteiro) é aceito como fato e vale para o C.
- **"A lista das 9 bases não é dada"** (crítica 2): rejeitada em parte.
  - A lista está no `TODO.md` (item 4) e fica explícita aqui, na seção A, com o motivo de cada exclusão.
  - Aceito o resto: a herança do pulo da METRO e da trava.
- **"A custom de base 12/16 fica fora do RANDOM"** (crítica 2): rejeitada.
  - A RANDOM exclui TRAINING e TOKYODM por serem fases de teste da equipe, não por algum defeito técnico (NATIVE.md, "RANDOM").
  - A custom é uma fase do modder: o `random` dela decide. A 12 nem é base aceita.
- **"Como a lista estendida é a mesma, o cursor já volta para o índice da custom"** (crítica 2): rejeitada.
  - O `screenLevelSelect` põe `currentSelection[8] = LL->first` em toda primeira passada (`screen.cpp:3132-3143`).
  - Aceita a parte comum às duas críticas: tirar a escrita de `m_levelNum` na tela 8.
- **"Guardar a origem de cada arquivo herdado no log e decidir se o alias segue outros mods"** (críticas 1 e 2): aceita, com a decisão tomada.
  - Os arquivos herdados vêm sempre do disco, para casar com o editor e com o `check_patch`.
  - A exceção são os PNGs (H), que são cosméticos e usam os mesmos texIds.
- **"Registrar NGP+PTR e TEX+RTX em grupos"** (crítica 1): adiada para a v2. Na v1 o `LVL/*` próprio é recusado, que é mais simples e coerente com o patch e o editor.
- **"Exército: testar `m_levelId == 2` e o NGP ser o da base"** (crítica 1): desnecessário na v1, que não tem NGP próprio. Anotado para a v2.
- **"Provar que o gancho em `ResolveCommandLineArguments__FiPPc` dispara"**: perdeu o objeto, porque esse gancho saiu do plano.

## Fora do escopo

- Aventura e campanha co-op: elas não passam pela tela 8, e a restauração no `userintMain` garante a base.
- As 6 bases com script em todo modo, junto com o alias por sessão de `QUAKE.*` e `CRATER.PUP`. A TRAINING como base.
- `.NGP/.PTR/.TEX/.RTX` próprios (v2, em grupo de tudo ou nada, com o editor abrindo o `.NGP` da pasta). Fase do zero.
- Mais de 6 customs (exigiria paginação ou mais vagas).
- Cartão de carga, música, banco de som e água próprios: tudo vem da base.
- Lembrar o cursor da tela 8 ou a última custom no save.
- Acentos e minúsculas no `name`, se a fonte não tiver.
