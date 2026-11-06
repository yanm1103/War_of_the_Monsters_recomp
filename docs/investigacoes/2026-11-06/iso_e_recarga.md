# Editor de fase: abrir direto da ISO (A) e recarregar a fase no jogo sem sair da partida (B)

Marcação: **[certo]** = lido no código/asm ou medido com script; **[hipótese]** = dedução ainda não testada no jogo.

## Resumo
- **A, ISO.** Dá para fazer sem dependência nova: `tools/iso9660.py` já existe (o kit de mods do release usa) e o `modkit.py` já tem a
  camada "ISO -> cache em pasta" (`disc_iso`/`disc_cache`/`disc_file`). Uma fase precisa de 10 arquivos, de 0,3 MB a 5,9 MB; as 19
  fases somam 47,6 MB. Com a fase copiada da ISO para o cache, `lvl.Stage`, `stage_points`, `read_mrs` e o `.glb` do `lvlglb` saem
  **idênticos** aos de `disc/` (SHA1 do `.glb` igual no CENTRAL e no TRAINING) [certo]. Não há pacote com arquivos dentro: cada
  arquivo da fase é um arquivo do ISO9660 (o `.NGP`/`.TEX`/`.RTX` têm um zip de um membro só, que o `ngp.py`/`tex.py` já abrem).
  `lvl.py`, `lvlglb.py`, `tex.py` e `ngp.py` não precisam mudar: a mudança fica toda no `editor.py` (e, se quiser, num `tools/discfs.py`
  tirado do `modkit.py`).
- **B, recarga.** **Corrige o `editor_pc.md` §4:** o RESTART do menu de pausa **recarrega a fase do disco** [certo, asm]. O
  `pauseLevelOverlay` (item 1) grava `m_restart = 1`, `m_inSession = 0` e `m_inMenus = 1`; o "sim" do `areYouSure` deixa isso como está.
  Então o laço da sessão do `main` termina, e o laço de fora (que continua enquanto `m_inMenus != 0`) volta para `InitBeforeDbLoad` +
  `LoadLevelFiles` sem passar pelos menus. O `TheGame::ResetLevel` só roda quando `m_restart != 0` **com a sessão ainda aberta**, e só
  zera HUD, pads e câmera; não é esse o caminho do RESTART. O `LoadLevelFiles` chama de novo o `dbInitDb`, onde está o gancho do patch,
  e o `applyFor` reabre o JSON a cada carga. **Hoje já é assim:** gravar no editor, depois PAUSA -> RESTART (no treino, RESET POSITIONS)
  aplica a versão nova, desde que o JSON já existisse quando o jogo deu boot [hipótese forte, falta ver no log: duas linhas
  `[fase] <FASE> <- ...`].
- **Reaplicar o patch sem recarregar não é viável.** O patch reescreve a imagem antes do `dbInitDb`, e a travessia logo depois copia
  posições e ponteiros para Destructibles, Interactives, Pickups, emissores, a grade de colisão, os veículos e os grupos de pedestres. As
  redes de carros e pedestres saem do PTS no `InitBeforeDbLoad`. Desfazer tudo isso é refazer a carga. O caminho seguro é o do próprio
  jogo, a recarga do RESTART, disparada pelo port (tecla e arquivo-sinal) em vez de pela pausa.
- **Mudanças no nativo para B** (pequenas): (1) o `applyFor` acha o JSON da fase no disco a cada carga, e não só no `Registry` montado
  no boot (hoje um JSON criado depois do boot não vale, e sem nenhum JSON no boot o gancho nem é instalado); (2) um gatilho "recarregar
  fase" (tecla F10 e/ou `WOTM_LEVELRELOAD=<arquivo-sinal>`) que faz o que PAUSA -> RESTART -> SIM faz, sem desenhar a pausa.

## A. Abrir o editor direto da ISO

### Onde está a ISO [certo]
`ISO/SCUS_971.97.War of the Monsters.iso`, 1 258 815 488 bytes (1,17 GiB = 614 656 setores de 2048, imagem "cooked", sem os 2352 do
modo raw). O descritor primário está no setor 16. O `iso9660.Iso` abre em 9 ms. Uma imagem `.bin` raw (2352) cai no
`ValueError("... is not an ISO9660 image")` do construtor; vale avisar isso na interface. O editor já procura `ISO/*.iso` para o
"Test" do runtime do clone (`editor.py`, `find_game`, `isos = sorted(glob(...ISO/*.iso))`), e o kit de mods do release lê o `iso=` do
`wotm.ini` (`modkit.ini_iso`).

### O que o editor lê do disco e por qual caminho [certo]
Todos os acessos são `open(<disc>/<pasta>/<NOME>.<EXT>)` montados com `os.path.join(disc, ...)`:

| Quem | Arquivo | Onde |
|---|---|---|
| `lvl.Stage.__init__` | `LVL/<F>.NGP` (via `ngp.Image.load` -> `inflate_ngp`), `LVL/<F>.PTR` (`read_ptr`) | `tools/lvl.py` ~linha 204 |
| `lvl.stage_points` | `PTS/<F>.SPT/.PUP/.FOP/.PED/.AIP` (com `os.path.exists`; falta em várias fases) | `tools/lvl.py` ~1028 |
| `lvl.cmd_mrs` / `read_mrs` | `RST/<F>.MRS` | `tools/lvl.py` ~1001, ~1181 |
| `lvlglb.export` | `LVL/<F>.TEX`, `.RTX`, `.NGP` (`tex.Library` -> `tex.read_file`, `ngp_texrefs` -> `ngp.inflate_ngp`) | `tools/lvlglb.py` ~38 |
| `editor.Stages.list` | `os.listdir(<disc>/LVL)` | `tools/level_editor/editor.py` ~98 |
| `editor.Stages.glb` | `os.path.getsize/getmtime(<disc>/LVL/<F>.NGP)` para o carimbo do cache do `.glb` | `editor.py` ~115 |
| `editor.check_patch` | `lvl.Stage(name, disc)` + `stage_points` | `editor.py` ~148 |

Nenhum deles lê outra coisa do disco (o `LEVELS.TXT`/`WORLDS.TXT` não são usados; os títulos vêm das tabelas `STAGE_TITLE`/`LEVEL_NUM` do
editor).

### Conferência contra a ISO [certo]
Script `C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\iso_check.py`:
abre a ISO com `tools/iso9660.py`, lista `LVL/` (19 fases) e, para cada uma, lê da ISO os 10 arquivos acima e compara o SHA1 com o de
`disc/`: **0 divergências**, nenhum arquivo só num dos lados; ler todas as 19 fases leva 0,15 s com o cache do SO quente (47 568 333 bytes).

| Fase | Bytes | PTS que falta |
|---|---:|---|
| 3-MILE | 3 979 489 | - |
| AIRPORT | 1 804 955 | - |
| BIGSHOT | 509 898 | SPT, PUP, FOP, PED, AIP |
| CANYON1 | 1 382 954 | FOP, PED |
| CANYON2 | 1 598 580 | - |
| CAPITOL | 4 771 511 | - |
| CENTRAL | 3 581 203 | - |
| CRUSH | 1 087 056 | FOP, AIP |
| DBALL | 551 460 | PUP, FOP, PED, AIP |
| ISLAND | 3 586 261 | - |
| METRO | 5 903 001 | AIP |
| MINISF | 2 637 410 | - |
| SANFRAN | 3 935 464 | PED |
| TOKYO | 2 530 030 | - |
| TOKYODM | 1 828 035 | - |
| TRAINING | 297 401 | FOP, PED |
| UFO | 2 696 052 | - |
| VEGAS | 3 645 428 | - |
| VOLCANO | 1 242 145 | FOP, PED, AIP |

Toda fase tem `.NGP/.PTR/.TEX/.RTX` e `.MRS`. "Pacotes": a árvore inteira tem só arquivos soltos. O que pesa é PSS (52 filmes, 483 MB),
VPK (51, 189 MB), NGP (68, 84 MB) e `ZZZDUMMY.BIN` (30 MB de enchimento); nada disso é lido pelo editor. As pastas por fase (`CENTRAL/`,
`TOKYO/`...) só têm `.AIC`.

Protótipo `...\scratchpad\iso_proto.py`: copia os arquivos da fase da ISO para `scratchpad\isocache\` (`iso.extract`) e roda
`lvl.Stage`, `stage_points`, `read_mrs` e `lvlglb.export` nas duas origens:
- CENTRAL: cópia em 0,02 s; 331 objetos, PTS (AIP 27, FOP 60, PED 32, PUP 35, SPT 11), 9 chaves do MRS nos dois; `.glb` com o mesmo
  SHA1 (`ab82f14dafec…`), 4,1 a 4,3 s.
- TRAINING: 1 objeto, AIP 2, PUP 26, SPT 3; `.glb` igual (`12371d52b992…`).

### Proposta: camada de arquivo do disco (pasta ou ISO)
**Recomendado: materializar a fase no cache, sem mexer nos leitores.** É o que o `modkit.py` já faz no release (`disc_file`: cada
arquivo pedido é copiado uma vez para `%LOCALAPPDATA%\WotM\modkit_disc\<chave>\`; a chave é o SHA1 de caminho|tamanho|mtime da ISO), e o
`MODS.md` já documenta isso para o kit. Para o editor:

1. Tirar do `modkit.py` para um `tools/discfs.py` (só a biblioteca padrão + `iso9660`) as funções `disc_iso`, `disc_ok`, `disc_cache`,
   `disc_has`, `disc_listdir`, `disc_file` (com `_iso_file`); o `modkit.py` passa a importá-las (sem mudar o comportamento). Se não quiser
   mexer no modkit agora, o editor pode importar `modkit` direto, mas ele puxa `tex`, `ngp` e outros no import (é mais pesado, mas
   funciona).
2. Acrescentar `discfs.stage_root(disc, name)`: com pasta, devolve a própria pasta; com ISO, copia para o cache (se faltar ou o tamanho
   diferir) `LVL/<F>.{NGP,PTR,TEX,RTX}`, `PTS/<F>.{SPT,PUP,FOP,PED,AIP}` (os que existirem) e `RST/<F>.MRS`, e devolve a raiz do cache.
   Esboço (os nomes da ISO são maiúsculos, então `lvl.Stage(name.upper(), root)` acha os arquivos também no Linux):

   ```python
   STAGE_FILES = (("LVL", ("NGP", "PTR", "TEX", "RTX")), ("PTS", ("SPT", "PUP", "FOP", "PED", "AIP")), ("RST", ("MRS",)))

   def stage_root(disc, name):
       iso = disc_iso(disc)
       if not iso:
           return disc
       root = disc_cache(disc)
       with iso.lock:
           for d, exts in STAGE_FILES:
               for e in exts:
                   rel = "%s/%s.%s" % (d, name.upper(), e)
                   f = iso.find(rel)
                   q = os.path.join(root, d, "%s.%s" % (name.upper(), e))
                   if f and not f[2] and not (os.path.isfile(q) and os.path.getsize(q) == f[1]):
                       iso.extract(rel, q)
       return root

   def stage_names(disc):
       names = disc_listdir(disc, "LVL")
       return sorted({n.rsplit(".", 1)[0].upper() for n in names if n.upper().endswith(".NGP")})
   ```
3. No `editor.py` (é o agente principal quem edita):
   - `cfg["disc"]` passa a aceitar pasta **ou** `.iso`. O padrão é `disc/` do repo se existir, senão o primeiro `ISO/*.iso`, senão o `iso=` do
     `wotm.ini` (`modkit.ini_iso`; no release não há `disc/`). Unificar com `cfg["iso"]`: se o disco é uma ISO, o "Test" do runtime do clone
     usa a mesma como `PS2X_CD_IMAGE`.
   - `Stages.list`: `discfs.stage_names(disc)` no lugar do `os.listdir(<disc>/LVL)`.
   - `Stages.objects`, `check_patch`: `lvl.Stage(name, discfs.stage_root(disc, name))` e `lvl.stage_points(root, name)`.
   - `Stages.glb`: `lvlglb.export(st, objs, root, ...)` e carimbo pelo `.NGP` em `root`. A cópia só é refeita se o tamanho mudar, então o
     mtime fica estável e o `.glb` em cache continua valendo. A chave do cache do `.glb` (hoje `CACHE/<F>.glb`) deveria incluir a origem
     (pasta ou ISO), para não misturar dois discos.
   - Settings: o campo "disc" aceita um arquivo (seletor de `.iso`), e a validação usa `discfs.disc_ok` (pasta existe, ou a ISO abre).
4. `lvl.py`/`lvlglb.py` na linha de comando: opcional, `--disc` aceitar `.iso` pelo mesmo `stage_root` (3 linhas no `main` de cada um).

**Alternativa (não recomendada agora): arquivos virtuais sem cópia.** Trocar os `open(path, "rb").read()` de `ngp.inflate_ngp`,
`tex.read_file`, `lvl.read_ptr`, `lvl.read_points`, `lvl.read_mrs` e o `os.path.exists` de `stage_points` por um `discfs.read(path)`
que entenda `iso:<imagem>!LVL/CENTRAL.NGP`. São uns 6 lugares em 3 arquivos que outros agentes também usam, e o ganho é só não gravar
uns MB no cache. A cópia é testada (o release a usa) e não toca em nada compartilhado.

## B. Recarregar a fase no jogo

### Quando o patch entra [certo]
`native/runtime/wotm_levelpatch.inc`:
- `wotmLevelPatchInstall`: só instala o gancho no `dbInitDb` se o `Registry` (montado **uma vez, no boot**: `wotm_mods.hpp`, `scan`)
  tiver algum `LEVELS/*.JSON`. Com `WOTM_LEVELPATCH=0`, não instala.
- `dbInitDbHook`: age só quando o `ra` cai dentro do `Shell::LoadLevelFiles` (o `dbInitDb` do menu, no `main`, passa direto).
- `applyFor`: lê o nome da fase em `fileStatus + 0xB1`, faz `Registry::get().find("LEVELS/<F>.JSON")` e **reabre `f->host` a cada
  carga** (`std::ifstream`). O conteúdo novo vale, mas um arquivo criado depois do boot não está no `Registry` e não vale. Se o editor
  apagar o JSON (patch sem objetos: `editor.py` faz `os.remove`), a próxima carga loga "JSON invalido, patch ignorado": o efeito está certo
  (fase do disco), mas a mensagem engana.
- A arena (1 MB, `kLevelArena`) é refeita a cada carga. As redes de carros e pedestres (`cutNets`) são cortadas na RAM; elas vêm do PTS
  carregado no `InitBeforeDbLoad`, que o `main` chama a cada sessão (o asm dele chama `PathTool::loadPoints` duas vezes,
  `AiPathTool::loadPoints`, `resetcom::forceRead` duas vezes etc.) [certo]. Então uma recarga começa do disco limpo e não corta duas vezes.
- Vencedor entre mods: com o mesmo `LEVELS/<F>.JSON` em dois mods, vence o último em ordem alfabética. Isso já aconteceu: o
  `%LOCALAPPDATA%\WotM\level_editor\game.log` tem "`[mods] LEVELS/CENTRAL.JSON: Teste vence LevelTest`", com `mods/LevelTest/` e
  `mods/Teste/` (não versionados) cada um com o seu `levels/CENTRAL.json`. Quem edita no mod `LevelTest` não vê a mudança no jogo.

### O fluxo do jogo [certo, asm de `game/Shell/main`, `game/ShellFinished` e C de `src/game/Shell.cpp`]
- `main`: laço de fora `do { m_inSession = 1; ResolveCommandLineArguments; resetcom::setFileName; InitRTState; BootInitGame;
  TheGame::InitBeforeDbLoad; Shell::LoadLevelFiles; ...; InitPlayers; ...; do { r = rtMain(first); EvaluateGameStatus(r); if (m_inSession)
  { m_restart ? TheGame::ResetLevel() : UnpauseLevel(); } } while (m_inSession); resetShellSoundFlags; } while (m_inMenus)`. Com
  `m_inMenus != 0` o salto volta direto para a carga da sessão, sem `userintMain`. O nome engana: "inMenus" aqui quer dizer "continua
  nas sessões". O `wotm_overrides.cpp` (`dialogAdventure`) já usa isso: `m_inSession = 0, m_inMenus = 1` "carrega a fase seguinte".
- Pausa: START -> `rtPauseRT` -> `rtReturnToShell(2, ...)` -> `rtMain` devolve 2 -> `EvaluateGameStatus(2)` -> `displayDialog(0)` (o
  `pauseLevelOverlay`, modal).
- `pauseLevelOverlay`, item 1 (RESTART): põe o próximo diálogo em 0x10 (o `areYouSure`), restaura os continues (`+0x2B40`/`+0x2B44` <-
  `+0x2B48`), zera `+0x2B4C`/`+0x2B50`, e grava `m_restart (+0x2BA8) = 1`, `m_inMenus (+0x2BD0) = 1`, `m_inSession (+0x2BCC) = 0`.
- `areYouSure`: "sim" zera `+0x2BA4`, restaura os continues, toca o som 0x4F (e `snd_StopAllSounds` se `game+0x1204C0+0x109C == 0`) e
  devolve 1, **sem tocar** em `m_inSession`/`m_inMenus`/`m_restart`. "Não" desfaz (`m_inSession = 1`, `m_inMenus = 0`, `m_restart = 0`).
- Volta no `EvaluateGameStatus`: `m_restart == 1` -> `DisplayLoadBackground(false)`, `m_restart = 0`. No `main`, `m_inSession == 0`
  fecha o laço de dentro (o `ResetLevel` não roda), e `m_inMenus == 1` faz a sessão de novo: `LoadLevelFiles` -> `dbInitDb` (gancho) ->
  patch relido.
- `TheGame::ResetLevel` (`src/game/TheGame.cpp`): `Hud::initForReplay` x4, pads, flags dos jogadores, `BigShotLevel::initForReplay`,
  câmera. Não toca na fase. Ele só roda se algum avaliador deixar `m_restart != 0` com `m_inSession != 0`.
- Treino: o RESET POSITIONS (`wotm_pausemenu.inc`) é o mesmo item 1 do overlay, com o "sim" do `areYouSure` refeito pelo `sureHook`
  (`g_autoYes`). Então também recarrega a fase. O comentário do topo do `wotm_pausemenu.inc` ("o TheGame::ResetLevel e o rtMain(0) poem
  monstros e fase como no comeco, sem recarregar") e o `editor_pc.md` §4 estão errados por essa leitura. Isso precisa ser confirmado no
  jogo (teste 1 abaixo) antes de corrigir o comentário.
- A camada nativa já espera várias cargas por execução: `loadLevelFilesTapped` conta `g_loadGen`, usado para recomeçar o que é guardado
  por arquivo carregado (`wotm_overrides.cpp` ~1361); `wotmLoadFreeze` congela a tela durante a carga.

### Reaplicar sem recarregar: não [certo pelo código, ver `objetos_fase.md` e `colisao_fase.md`]
O que teria que ser refeito à mão: (1) desfazer o patch anterior, o que exige a imagem de antes do `dbInitDb` (os nós antigos foram
reescritos, e as cópias e os `ROTATE` novos estão na arena); (2) refazer o registro da travessia (`Destructibles`, `Interactives` e a
tabela de ids do `dbProcInteractive`, `LevelPickups` e os `_cs` deles, emissores, `PedGroup`, veículos já nascidos com ponteiro para a
rede); (3) refazer a grade de colisão (células e caixas já copiadas); (4) refazer as redes do PTS (já cortadas e com veículos andando
nelas); (5) o estado da luta (prédios já destruídos, destroços no `TaskManager`). Isso é a carga inteira. A única coisa barata ao vivo é o
"fantasma" (só o desenho), que o `editor_pc.md` já descreve.

### Proposta: "recarregar fase" pelo caminho do RESTART
**Fase 0, sem mudar o nativo:** gravar no editor e, no jogo, PAUSA -> RESTART (no treino: RESET POSITIONS). Condição: o JSON da fase
tem que existir (no mod que vence) quando o jogo deu boot. O "Test" do editor já grava antes de abrir o jogo. O editor pode dizer isso na
barra de status depois do Save, quando houver um jogo aberto por ele.

**Fase 1, nativo (`native/runtime/wotm_levelpatch.inc`, ou um `wotm_levelreload.inc` incluído logo depois dele no `wotm_overrides.cpp`):**
1. **JSON achado na hora.** No `applyFor`, em vez de só o `Registry::find`: percorrer as pastas de `Registry::get().dir()` de novo
   (`directory_iterator`, sem `_`/`.` no começo, ordem por `Registry::key`, o último vence, como no `scan`) e usar
   `<mod>/LEVELS/<F>.JSON` sem diferenciar maiúsculas. Isso cobre mod novo, arquivo novo e arquivo apagado ("sem patch para <F>" no log,
   em vez de "JSON invalido"). Instalar o gancho do `dbInitDb` sempre que `WOTM_LEVELPATCH` não for 0, e não só quando há patch no boot:
   o custo é uma comparação de `pc`/`ra` por chamada do `dbInitDb` (uma vez por carga). Ler o arquivo inteiro com uma nova tentativa se o
   JSON não fechar, para o caso de o editor estar gravando naquele instante. E o editor deve gravar em `.tmp` + `os.replace` (hoje o
   `editor.py` abre o destino com `"w"` direto).
2. **Gatilho.** Uma flag atômica `g_reloadWanted`, ligada por:
   - tecla **F10** no teclado. Hoje o nativo usa F1, F3, F5, F6, F7, F8, F9, F11 e F12, então F10 parece livre; conferir a tabela de teclas
     do `NATIVE.md` e o painel de remapeamento ("F1 to F12 and Esc are not accepted"). Ligar onde as outras teclas do runtime são lidas
     (thread de desenho) e passar para o EE pela flag;
   - `WOTM_LEVELRELOAD=<arquivo>`: uma thread do host olha o mtime a cada 250 ms; mudou, liga a flag. O arquivo pode trazer o nome da
     fase, para ignorar um pedido de outra fase.
3. **Execução na thread do EE, num ponto seguro.** Gancho na entrada do `rtTimeToReturnToShell` (`wotm::fn::rtTimeToReturnToShell__Fv`,
   chamado uma vez por quadro na condição do laço do `rtMain`). Se `g_reloadWanted`, `m_inSession == 1`, `gRtReturn.active == 0`,
   `moviePlaying == 0`, nenhum diálogo aberto, fora da carga e (com pedido do arquivo) a fase atual (`stageName`) for a pedida: chamar
   `rtReturnToShell(2, 0)` (`wotm::fn::rtReturnToShell__Fii`, como `wotm_coop.inc`/`wotm_modes4.inc` já fazem com `call(...)`) e ligar
   `g_reloadPending`.
4. **No `displayDialog(0)` que vem em seguida** (`EvaluateGameStatus(2)`; o `wotm_pausemenu.inc` já tem `dialogHook` nele, e o novo
   teste entra antes): com `g_reloadPending`, não abrir a pausa. Fazer as escritas do item 1 do overlay mais o "sim" do `areYouSure`, as
   mesmas que o `sureHook` já repete: `+0x2BA4 = 0`; `+0x2B40`/`+0x2B44` <- `+0x2B48`; `+0x2B4C = 0`, `+0x2B50 = 0`; `m_restart = 1`,
   `m_inMenus = 1`, `m_inSession = 0`; `snd_StopAllSounds` se `game+0x1204C0+0x109C == 0`; `gExitDialog = 0`;
   `wotm_training::g_inFight = false`. Depois voltar ao `ra`. O resto é do jogo: `EvaluateGameStatus` mostra o fundo de carga e zera
   `m_restart`, e o `main` recarrega a fase. Logar `[fase] recarga pedida (F10 | editor)`; a carga loga a linha `[fase] <F> <- ...` de
   sempre.
5. Variável nova `WOTM_LEVELRELOAD` na tabela do `NATIVE.md` (`python tools/wotm_env.py` confere), e um parágrafo em `MODS.md`, "Level
   editor"/"Level patches": RESTART/F10 relê o patch.

**Fase 2, editor (`tools/level_editor/`, quem edita é o agente principal):**
- `launch_game`: passar `WOTM_LEVELRELOAD=<CACHE>/reload.txt` no ambiente (e `WOTM_LEVELPATCH=2`, se quiser as linhas dos caminhos).
- Botão "Reload in game" (habilitado se o jogo aberto pelo editor ainda roda: guardar o `Popen` e olhar `poll()`): `check_patch` + gravar
  (`.tmp` + `os.replace`) + escrever `reload.txt` com `<FASE> <contador>`. Depois ler o log do jogo (`game_stage_lines` já existe) e
  mostrar só as linhas `[fase]` **depois da última** `[fase] <FASE> <-` (agora o log tem várias cargas). Aviso se nenhuma linha nova
  chegar em ~20 s ("pause the game? the fight must be running").
- Aviso de mod que vence: se outro mod (não `_`/`.`, depois do atual em ordem alfabética) tem `levels/<FASE>.json`, dizer que é ele que
  vai valer no jogo (o caso `Teste` x `LevelTest`).
- Plano B sem nativo: "Test" fecha o jogo aberto pelo editor e abre outro (o `WOTM_MATCH` pula menus e abertura). Custa o boot inteiro, mas
  vale também para arquivos do disco sobrepostos (abaixo).

### Riscos e limites
- **Arquivos sobrepostos do disco editados depois do boot** (`mods/<mod>/PTS/*.SPT`, `.MRS`, `.NGP`, se o editor passar a gravá-los;
  `pts_fase.md`): o `Registry` fixa no boot o tamanho e a faixa de setores falsos de cada arquivo (`assignLsns`: um setor de folga entre
  arquivos). O conteúdo é relido na hora (`Registry::read` abre o `host`), mas um arquivo que **cresce** passa a ser lido com o tamanho
  velho, e um arquivo novo não existe para o jogo. A recarga só é segura para eles com um `Registry::refresh()` na entrada do
  `LoadLevelFiles`: re-stat; quem cabe na faixa continua; quem cresce ou é novo ganha uma faixa nova depois da última (sem renumerar os
  outros, porque um BNK/VPK/PSS de mod pode ter o LSN guardado num stream em curso). Até lá, para eles o editor usa o plano B (reabrir o
  jogo).
- O RESTART restaura continues e limpa a sessão como o do retail. Na aventura (modo 1) isso também vale para as vidas; o editor abre em
  `WOTM_MATCH=3,<fase>` e, por padrão, no treino. Outros modos (minigames 7/8/9, eliminação com 3 e 4 jogadores do `wotm_modes4.inc`)
  passam pelo mesmo caminho do RESTART do retail. Não testado aqui.
- O tempo de uma recarga é o da tela LOAD da fase (não medido aqui).
- Pedido no meio de agarrão, cinema ou diálogo: as condições do passo 3 adiam até o quadro seguinte que puder, como o START faria.
- Este relatório não rodou o jogo (as teclas precisam do foco da janela do dono).

### Testes para confirmar (no jogo, com o dono)
1. **RESTART recarrega?** Boot pelo editor ("Test", treino ligado, o JSON da fase já gravado). No jogo: PAUSA -> RESET POSITIONS. No
   `%LOCALAPPDATA%\WotM\level_editor\game.log` devem aparecer **duas** linhas `[fase] CENTRAL <- ...`. Depois, sem fechar o jogo, mudar
   um objeto no editor, Save, PAUSA -> RESET POSITIONS: a terceira linha `[fase]` traz o número novo de `moved`, e o objeto aparece no
   lugar novo. Repetir fora do treino com RESTART -> YES.
2. Com a Fase 1: F10 no meio da luta deve dar o mesmo resultado sem abrir a pausa; mudar `reload.txt` (botão do editor) também; um pedido
   durante o `ARE YOU SURE?` ou durante um filme espera.
3. JSON criado depois do boot (mod novo) e JSON apagado: a carga seguinte aplica ou loga "sem patch".
