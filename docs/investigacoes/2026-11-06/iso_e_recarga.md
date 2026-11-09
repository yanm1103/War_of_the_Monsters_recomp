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

### Reaplicar sem recarregar: não [certo pelo código, ver `726b99b:docs/investigacoes/2026-11-06/objetos_fase.md` e `colisao_fase.md`]
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

## B, implementação (Fase 1)

### O que mudou
- `native/runtime/wotm_levelpatch.inc`: `findPatch` percorre as pastas de `Registry::get().dir()` a cada carga (sem `_`/`.`, ordem
  por `Registry::key`, o último vence, `levels/` e `<FASE>.json` sem diferenciar maiúsculas; loga `[fase] LEVELS/<F>.JSON: B vence A`).
  Sem arquivo: `[fase] <F>: sem patch`. JSON que não fecha é relido uma vez 200 ms depois. O gancho do `dbInitDb` fica instalado
  sempre que `WOTM_LEVELPATCH > 0`. Conta as cargas (`g_loads`) e zera o F10 pendente a cada carga.
- `native/runtime/wotm_levelreload.inc` (novo, incluído depois do `wotm_training.inc`; `wotmLevelReloadInstall` depois do
  `dialogAdventure`, para o gancho do `displayDialog` ficar por fora do da pausa):
  - pedido: F10 (`wotm_live.inc`, junto do F3, liga `g_wotmLevelReloadKey`) ou `WOTM_LEVELRELOAD=<arquivo>` (thread do host, mtime a
    cada 250 ms; conteúdo novo `"<FASE> <n>"`; o conteúdo do boot não conta; conteúdo vazio, gravação pela metade, é relido);
  - gancho na entrada do `rtTimeToReturnToShell`: com as condições do START no `rtPauseRT` (conferidas no asm: `shell+0x2BA4 == 0`,
    `moviePlaying == 0`, `game+0x120458 == 0`, `game+0x120454 == 0`) mais `m_inSession == 1` e `gRtReturn` livre, faz o que o START
    faz: `shell+0x2BA4 = 1` e `rtReturnToShell(2, 0)`. O próprio `rtTimeToReturnToShell` que roda em seguida já devolve 1 (contador 0 ->
    -1), então o `rtMain` sai no fim deste quadro;
  - gancho no `displayDialog`: o `displayDialog(0)` chamado de dentro do `EvaluateGameStatus` (ra na faixa dele) com a recarga pendente
    não roda; faz as escritas do item 1 do `pauseLevelOverlay` (conferidas no asm: `+0x2B40`/`+0x2B44` <- `+0x2B48`, `+0x2B4C`/`+0x2B50`
    = 0, `m_restart` 1, `m_inMenus` 1, `m_inSession` 0, `snd_StopAllSounds` se `StreamingSoundManager+0x109C == 0`) e do "sim" do
    `areYouSure` (`+0x2BA4 = 0`, `gExitDialog = 0`, `g_inFight = false`) e volta. Pular o `displayDialog` inteiro também pula o que ele
    faz na entrada e na saída para a pausa (`gHudEnable = 0`, `initPause` do som, `inputSetInputMode`, fade de saída, `+0x120460`):
    a sessão nova refaz tudo isso (`Hud::initBefore`, `InitAfterDbLoad`). O som 0x4F de confirmação não toca;
  - pedido que chegou antes de uma carga da fase (contador `g_loads`) cai: a carga já leu o JSON. Com a pausa aberta o `rtMain` não
    roda e o pedido espera a volta à luta. Pedido de arquivo para outra fase: `[fase] recarga pedida (arquivo) para VEGAS, mas a fase e
    CENTRAL: ignorada`. Pedido visto pelo `rtMain` sem quadro de luta por 10 s cai com uma linha.
- F10 estava livre: o nativo usa F1, F3, F5, F6, F7, F8, F9, F11 (o F12 só no `viewer.cpp`), nada no runtime do PS2Recomp, e o painel
  CONTROLS não aceita F1..F12. O GLFW não deixa o F10 abrir o menu da janela (`SC_KEYMENU`).
- Docs: `NATIVE.md` (tabela: `WOTM_LEVELRELOAD`; `WOTM_LEVELPATCH=0` também desliga a recarga), `RUNTIME.md` (F10 na lista de
  teclas), `MODS.md` ("Level patches": relido a cada carga, RESTART/RESET POSITIONS/F10/arquivo; "Under the hood": o mecanismo).
  Comentário do topo do `wotm_pausemenu.inc` corrigido: o RESET POSITIONS recarrega a fase.

### Testes no jogo (build_dev, `WOTM_MATCH=3,1 WOTM_TRAINING=1`, mods de teste na pasta de rascunho, teclas por `PostMessage`)
Patch A = `0x101E340` de (688.2, 119.6, 0) para (688.2, 329.6, 0); B = para (688.2, 529.6, 0). `lvl.py patch`: A `moved 1, paths
redone 1` e 4 links de veículo cortados; B `moved 1, paths redone 1, spheres grown 1` e o link 10-11.
1. Boot com A: `[fase] CENTRAL <- A (LEVELS/CENTRAL.JSON): moved 1, paths redone 1` + 4 links. Na luta, JSON trocado por B e
   `CENTRAL 1` no arquivo: `[fase] recarga pedida (arquivo): CENTRAL`, `[partida] rodada 1 terminou (rtMain devolveu 2)`, `LOAD`,
   `[fase] CENTRAL <- A ...: moved 1, paths redone 1, spheres grown 1` + `link 10-11 cortado` (as contas do `lvl.py` para B), e a luta
   volta (`[treino] intro pulada`).
2. JSON apagado + `CENTRAL 2`: `[fase] CENTRAL: sem patch`.
3. Mod novo `Zeta/Levels/central.JSON` (A) criado depois do boot, A de volta com B, `central 3`: `[fase] LEVELS/CENTRAL.JSON: Zeta
   vence A`, `[fase] CENTRAL <- Zeta ...: moved 1, paths redone 1`.
4. `VEGAS 4`: `[fase] recarga pedida (arquivo) para VEGAS, mas a fase e CENTRAL: ignorada`.
5. F10 (`WM_KEYDOWN` VK_F10, scancode 0x44): `[fase] recarga pedida (F10): CENTRAL` e a carga. Com a janela minimizada o desenho
   nativo não roda e o F10 não chega (o dono a tinha minimizado; restaurada sem ativar para o teste).
6. PAUSA -> RESET POSITIONS -> X: `[treino] reset positions ...` e logo `[fase] CENTRAL <- Zeta ...`: o RESET POSITIONS recarrega a
   fase (o teste 1 da lista acima).
7. Pedido de arquivo com a pausa aberta: atendido ao voltar à luta (`rodada 3 terminou` pela pausa, `recarga pedida (arquivo)`,
   `rodada 4 terminou`, a carga).
8. JSON cortado (`{"objects":[{"handle":`) + pedido: `JSON invalido, patch ignorado: fim inesperado (linha 1)` depois da segunda
   leitura; a fase carrega do disco.
9. Segundo boot com o arquivo já com `CENTRAL 9` e `CENTRAL 10` escrito durante o boot: nenhuma recarga além da carga normal.
Foto depois de uma recarga pelo F10 (`PrintWindow`, sem foco): `docs/screenshots/2026-11-06/2026-11-06_recarga_fase.png`.

### Riscos que sobraram
- Só testado no treino em CENTRAL com 1 jogador. Aventura (o RESTART restaura os continues), minigames 7/8/9 e 3-4 jogadores do
  `wotm_modes4.inc` passam pelas mesmas escritas do RESTART do retail, mas não foram rodados.
- O `+0x2BA4 = 1` do pedido dura um quadro (até o `displayDialog`); um gancho nativo que leia a pausa nesse quadro a vê ligada.
- F10 só é lido enquanto o desenho nativo roda (janela não minimizada, `PS2X_SCENE=1`).
- O tempo de uma recarga é o do `LOAD` da fase (alguns segundos; não medido com precisão).
- Arquivos sobrepostos do disco (`PTS`, `.NGP`) editados depois do boot continuam com o tamanho do boot (`Registry::assignLsns`):
  fora desta fase.

## B, implementação: revisão independente

### O que foi conferido no código
- Escritas do `dialogHook` contra o asm de `game/ShellFinished` (`pauseLevelOverlay`, item 1, e o "sim" do `areYouSure`): batem
  (`+0x2B40`/`+0x2B44` <- `+0x2B48`, `+0x2B4C`/`+0x2B50` = 0, `+0x2BA8` = 1, `+0x2BD0` = 1, `+0x2BCC` = 0, `snd_StopAllSounds` com
  `game+0x1204C0+0x109C == 0`). As que ficam de fora só servem ao `areYouSure` (próximo diálogo 0x10, `calledFrom`,
  `gCurrentMenuSelection`). `shellInfo`/`shellGame` são os objetos (o asm lê os ponteiros `shell`/`game`), como no `wotm_pausemenu.inc`.
- Condições do `fightFrame` contra o `rtPauseRT`: as mesmas (`+0x2BA4`, `moviePlaying`, `game+0x120458`, `game+0x120454`).
  `rtReturnToShell(2, 0)`: contador 0, o `rtTimeToReturnToShell` original devolve 1 no mesmo quadro.
- `EvaluateGameStatus(2)`: chama `displayDialog(0)` logo na entrada, em qualquer modo (antes da tabela por modo), então o
  `g_pending` não fica esperando em modo nenhum; o `ra` cai na faixa conferida. Os ganchos do `wotm_modes4.inc`/`wotm_split4.inc`
  que chamam o `displayDialog` só o fazem com r = 1.
- O que o `displayDialog` inteiro faria e fica pulado: zera `g_frame` na entrada; `initPause` do som e `gUseUnifiedView`
  (equilibrados pela própria saída); na saída, com `m_inSession == 0` e `game+0x1204C0+0x109C == 1`, zera esse campo e devolve os
  `snd_SetMasterVolume` de `shell+0x2A30/0x2A34`. Com o campo em 1 na hora do pedido, a recarga não para os sons nem faz essa volta.
  Não acontece no treino (os testes passam); não sei em que momento da luta o campo vale 1.
- Threads: `g_wotmLevelReloadKey` é atômico; `g_fileStage`/`g_fileLoads` vão sob `g_mu`; o resto só é lido na thread do EE. O
  `g_loads` sobe antes de o JSON ser lido, então um pedido não se perde: no pior caso (pedido visto entre o `++g_loads` e a leitura)
  vem uma recarga a mais.
- Linux: só `std::filesystem`, `std::thread`, `std::mutex`, `std::chrono`; nenhuma API Win32 no diff.
- `python tools/wotm_env.py`: 187 de 187. `python tools/doclint.py` nas linhas novas (índice temporário com `git add` dos quatro
  `.md`, sem tocar no índice real): limpo.

### Testes no jogo (wotm.exe do build_dev de 15:54, mais novo que os fontes; `WOTM_MONITOR=1 WOTM_MUTE=1`, janela 720)
Mod `RevA/levels/CENTRAL.json` na pasta de rascunho; `reload.txt` com `CENTRAL 0` antes do boot; JSON e `reload.txt` gravados por
`.tmp` + `os.replace`.
1. Treino (`WOTM_MATCH=3,1`): `CENTRAL 1` escrito 1,5 s depois de abrir, antes da carga: `recarga pedida (arquivo) antes da ultima
   carga da fase: ja atendida`, e 6 s de luta sem outra carga (o jogo não trava nem recarrega). JSON -> B + `CENTRAL 2`: `recarga
   pedida (arquivo): CENTRAL`, `rodada 1 terminou (rtMain devolveu 2)`, `CENTRAL <- RevA ...: moved 1, paths redone 1, spheres grown
   1` + `link 10-11 cortado` (as contas do `lvl.py patch` para B). `CENTRAL 3` escrito logo depois de `recarga pedida`, durante a
   carga: cai como `ja atendida`. JSON -> A + `CENTRAL 4` e `CENTRAL 5` 0,6 s depois: uma recarga só (os 4 links de A de novo), o 5
   cai. Três cargas no total, jogo vivo no fim.
2. `WOTM_LEVELPATCH=0`: `patches de fase desligados`, nenhuma linha de recarga, o pedido na luta não faz nada.
3. Pasta de mods vazia: `CENTRAL: sem patch` nas duas cargas; a recarga pelo arquivo funciona do mesmo jeito.
4. Versus sem treino (`WOTM_MATCH=3,1,1`, com a intro da luta): pedido 0,6 s depois da carga, durante a intro: atendido ali mesmo
   (as condições do START valem na intro), e a carga seguinte aplica B.
Logs: `rev/game_*.log` e `rev/notes_*.txt` na pasta de rascunho da sessão.

### O que sobrou da revisão
- Um pedido que a thread do EE já viu e que espera mais de 10 s sem quadro de luta cai (`kExpireMs`). O `MODS.md` diz que ele espera
  o filme ou o diálogo acabar e não fala desse limite. Caso raro: F10 no mesmo quadro em que o START abre a pausa, e a pausa fica
  aberta mais de 10 s.
- O `reload.txt` com o mesmo conteúdo da última vez é ignorado. Se o editor reiniciar o contador com o jogo aberto, o primeiro pedido
  pode repetir o texto anterior; vale usar a hora no lugar do contador. Gravar sem `os.replace` pode deixar a thread ler um nome
  cortado, que cai como "outra fase".
- `[fase] <F>: sem patch` sai agora em toda carga de fase de qualquer jogador, mesmo sem mods.
- A lista de teclas do topo do `NATIVE.md` (F3, F11, F1) não tem o F10; só o `RUNTIME.md` o lista.
- `MODS.md`, "Level patches": "Three ways reload it" está sem o "to".
- Não testado aqui: aventura, coop, minigames, 3 e 4 jogadores, F10 de verdade (só o arquivo-sinal) e um pedido durante o agarrão.
