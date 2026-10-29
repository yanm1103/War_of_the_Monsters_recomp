# WotM: decompilação de War of the Monsters (PS2, NTSC-U, SCUS-97197)

Projeto de decompilação do executável `SCUS_971.97`. O objetivo final é um port nativo de PC fácil de modar. O dono do projeto escreve em português; responda em português.

**Direção: port primeiro.** A base é o jogo recompilado com o PS2Recomp (fora do repo; o C++ gerado é código do jogo) mais a camada nativa deste repo
(`native/`: desenho com GPU, som, filmes, teclado), que já roda a luta a 60 fps. A decompilação (`src/`) é a camada legível que substitui, por hook, só o que importa para modar (`Shell::Load*`, `MonsterParse`,
câmera); o runtime cobaia com as equivalentes está parado. **Byte match não é requisito por função** (`equivalent` basta; `matched` quando sai barato; o `gate.sh` continua obrigatório). O VU1/GS emulado do
runtime é só a referência da checagem (`docs/guides/VU1CHECK.md`). As regras abaixo continuam valendo; onde disserem "matching", leia "matching quando for o objetivo da função".

**O que fazer:** **`TODO.md`** na raiz (a lista e a ordem são do dono); os relatos de bug do dono têm prioridade sobre ela.

Leia antes de começar: `README.md` (setup), `docs/ANALYSIS.md` (o que se sabe do ELF e as convenções de status), `docs/ARCHITECTURE.md` (arquitetura) e `TODO.md`. Índice de `docs/guides/`:
- `NATIVE.md`: camada nativa (desenho, texturas, monstros, partículas, sombras, menus, painel "Recompiled options", ícones), hooks de host, vigia, lote, tabela de variáveis `WOTM_*`.
- `VU1CHECK.md`: checagem VU1 x nativo, corpus, fotos do dono, bugs abertos.
- `RUNTIME.md`: runtime recompilado no Windows (build, `PS2X_*`, teclado, endereços úteis). `BOUNDARY.md`: o que do PS2 o port deixou de emular.
- `TOOLS.md` (`tools/`, headers, difftest), `PITFALLS.md` (pegadinhas do gcc 2.95 e do build NM), `SPINE.md` (fluxo do jogo), `PCSX2_TESTING.md` (halfcpp no PCSX2).
- `MODS.md` (pasta `mods/`, texturas em PNG, `tools/modkit.py`), `MODELS_3D.md` (monstro animado direto do disco), `WEB_VIEWER.md` (avaliação do viewer no navegador; o viewer está pronto em https://yanm1103.github.io/wotm-model-museum/).
- Investigações datadas em `docs/investigacoes/<data>/`. Encerrada (o que ela pedia já está no código) é apagada; o que ficar pendente vai para o `TODO.md`, e as referências viram `<commit>:docs/investigacoes/...` (ler com `git show`).
- `docs/FORMATS.md`: formatos de arquivo do jogo (fase, monstro, som, filme, save do memory card).

## Documentação: três tipos de texto, cada um no seu lugar
- **Referência** (`README.md`, este arquivo, `docs/*.md`, `docs/guides/*.md`, `GLOSSARY.md`, `CONTRIBUTING.md`): como as coisas são hoje, no presente. Escrita **em inglês**, menos este arquivo; `TODO.md`, commits e `docs/investigacoes/` seguem em português. Sem datas, sem "antes era", sem "feito/corrigido em", sem contar como foi testado. Um número medido só entra se for usado (ex.: "9619 ticks por field").
- **A fazer** (`TODO.md`): só o que está aberto, cada item curto (o que é, por onde começar, onde está o contexto). Terminou: a linha sai, sem "feito" nem riscado; o que passou a existir vai para a referência. Decisões do dono de não fazer algo ficam em "Descartado".
- **Diário** (o que aconteceu, por quê, como foi medido e testado): a mensagem do commit e `docs/investigacoes/<data>/`.
- `python tools/doclint.py` confere as linhas que o commit acrescenta e o `TODO.md` inteiro; o pre-commit do git o roda sozinho (ligar uma vez por clone: `git config core.hooksPath tools/githooks`). Linha antiga só editada não é cobrada pelo que já tinha; `--all` lista o que ainda falta migrar.

## Fluxo do port (mudança em `native/`)
1. Antes de mexer: `tools\corpus.ps1 -Check` guarda a base (`<runtime>\corpus\resumo_<data>.csv`; capture o corpus uma vez com `-Capture`). O que está errado ou faltando vem do inventário (`VU1CHECK.md`, "Port inventory"), não de lembrança.
2. Editar `native/*.hpp` ou `native/runtime/*.inc|.cpp` (hooks e código do jogo que rodam dentro do runtime). Endereços por nome (`wotm::sym::`/`wotm::fn::` de `native/wotm_syms.h`), nunca número cru. Variável de diagnóstico nova entra na tabela do `NATIVE.md` (`python tools/wotm_env.py` confere).
3. `build_runtime.bat` na raiz (configura e compila `build_dev`, sem LTCG; o ninja não rastreia `native/`, o script cuida disso). Nunca o `build.bat` da pasta velha com LTCG.
4. Conferir: `tools\corpus.ps1 -Check -Baseline <csv da base>` diz o que piorou; `run_live.bat` para ver na tela; `tools\lote.ps1` para lutas em lote com vigia.
5. Commit sem gate (`native/`, `docs/` e `tools/` fora do build não mudam a ROM). Mudança numa costura do PS2Recomp (CMake, `main.cpp`, taps) exige regenerar `tools/runtime_patch/wotm-runtime.patch`.

## Regras que não podem ser quebradas
1. **Nenhum arquivo do jogo vai para o repo.** `ISO/`, `disc/`, `asm/`, `assets/`, `build/`, `*.rom`, `*.elf` e `tools/cc/` estão no `.gitignore`. O `asm/` contém código do jogo: nunca force `git add -f`, nunca cole assembly do jogo em issues, PRs ou documentação.
2. **A ROM tem que continuar batendo com o SHA1 do retail.** Todo commit que muda o código decompilado (`src/`, `include/`, `asm/`, config lida pelo build) passa por `sh tools/wsl/gate.sh && git commit ...` (build + SHA1, sai com erro se falhar); commit que só toca `native/`, `docs/` ou `tools/` fora do build não tem como alterar a ROM e dispensa o gate. Nunca `gate.sh | tail && git commit` nem `check.sh; git commit`: o pipe esconde a falha e o commit sai com build quebrado.
3. **Sem push direto na `master` se você não é o dono.** Colaboradores trabalham em fork/branch e abrem PR (veja `CONTRIBUTING.md`).
4. **Marque com clareza o que é idêntico e o que é só equivalente.** Função que bate byte a byte fica sem marcação; função que não bate fica em `#ifdef NON_MATCHING` (com `INCLUDE_ASM` no `#else`) e o comentário diz a pontuação e a causa. Use `tools/nm_wrap.py`. Mudança de comportamento disfarçada de decomp não é aceita; se for necessária, sinalize explicitamente.
5. **Não gaste esforço em código de hardware** (GS/VIF/DMA, timers, SDK): está em `config/hw_funcs.txt` e o port o substitui. `game/Sound` e `game/StreamingSoundManager` não são hardware, mas são baixa prioridade.

## Ambiente
- Windows, com o build rodando em **WSL Ubuntu**: use `wsl -d Ubuntu` (a distro padrão é a `docker-desktop`, que não serve). Venv: `~/.venvs/wotm/bin/python`. Raiz do repo no WSL: `/mnt/c/Users/<você>/WoTM`.
- Compilador: ee-gcc 2.95.2 (SN ProDG), baixado por `tools/setup_toolchain.sh` em `tools/cc/`. Requer a sua própria ISO em `ISO/`, extraída para `disc/`.
- Scripts `.sh` precisam de LF. Gravar com Python no Windows deixa CRLF e o `sh` do WSL quebra (`sed -i 's/\r$//'` conserta).
- `config/status.csv` (estado de cada função) é gerado por `python3 tools/progress.py`; só o dono o commita. **Colaboradores não commitam esse arquivo**, para não gerar conflito em todo PR. O README não tem bloco de progresso.
- Ao escrever arquivos com regex ou `\n`, use as ferramentas Edit/Write. Heredoc de Python transforma `\n` em quebra de linha real.
- **Nunca rode Python inline** (`python3 - <<EOF`, `python -c "...\..."`): caminhos do Windows (`C:\Users\...`) e `\n` quebram. Um hook (`.claude/hooks/block_python_heredoc.py`, ligado em `.claude/settings.json`) recusa esses comandos. Grave o script com Write (pasta de rascunho) e rode `python arquivo.py`; use Edit/Write para alterar arquivos do repo.

## Fluxo por função
1. Ler `asm/nonmatchings/<tu>/<função>.s`. Para mais de ~30 instruções, `sh tools/m2c.sh <tu> <função>` dá um rascunho. Nunca confie nele sem ler o asm (os argumentos que ele mostra são ruído de registradores). Troque `unkNNN` por campos nomeados dos headers.
2. Converter o TU para C++ se ainda não estiver: `sh tools/wsl/convert_tus.sh <Tu>` (um por vez).
3. Escrever C++ natural, na ordem natural do fonte (muitas vezes já bate). Pontuar com `sh tools/wsl/scoreall.sh <TU>`; se não bater, `tools/nm_wrap.py <arquivo> <símbolo> "<nota>" [Classe::método]`. Só afine quando for barato (`tools/wsl/variants.py`, `tools/wsl/permute.py`).
4. Verificar a TU inteira num comando: `~/.venvs/wotm/bin/python tools/verify_tu.py game/<TU>` (pontuação, difftest, sequência de chamadas, layout e retornos consumidos, numa tabela por função; `--no-nm` pula o ELF das equivalentes e leva segundos em vez de minutos). Rascunhos do m2c para a fila inteira: `sh tools/m2c_batch.sh` (em `build/m2c/`).
5. Rodar `gate.sh`, depois `python3 tools/progress.py`, depois commitar.

## Armadilhas conhecidas (bugs que já aconteceram)
Uma pontuação "untuned" pode esconder erro de semântica. Depois de escrever uma função equivalente, confira:
- **Retorno usado pelo chamador.** Um `void` onde o retail devolve `$v0` (ex.: `addInteractive`: o índice do slot é o id do hat). `tools/nm_retvals.py`.
- **Sequência de chamadas.** `tools/nm_calls.py`.
- **Tamanho e stride de structs.** Adicione `static_assert` com o stride do retail (ex.: `PathNode` é 0x40, não 0x30). Classe base vazia ocupa um byte (`AiBrain`).
- **Delay slots.** Um compare invertido escondido no delay slot (ex.: `Monster::isIdle`) e alvo de branch errado (ex.: `AiGrappleAttack`, heavyPunch vs toss). Leia o asm até o fim.
- **Ordem e helpers.** Ao trocar `INCLUDE_ASM` por C++, mantenha as funções na ordem de endereço original (cada definição onde estava seu `INCLUDE_ASM`, nunca agrupadas). Helpers `static`/`static inline` vão dentro de `#ifdef NON_MATCHING`: o ee-gcc 2.95 emite helpers não usados e desloca a ROM.
- **Strings e rodata.** Mover strings de `INCLUDE_ASM` para C muda o padding do rodata (adicione `.word 0` em asm no `.rodata`). Jump tables de `switch` em C precisam do mesmo padding (por isso função com `switch` fica em `NON_MATCHING` mesmo quando bate: a tabela vem do rodata do retail). Classes virtuais: mantenha vtable, ctor e `__tf` como `INCLUDE_ASM`; `tools/place_data.py` posiciona dados por endereço.
- **Headers compartilhados.** Mover classe para header pode mudar `sizeof` e deslocar campos de structs parciais. Antes de mexer: `sh tools/wsl/scoreall.sh <todos os TUs de src/game> > ~/base.txt`; depois compare com `diff`.
- **Truques do compilador.** `__asm__("#SNFIX_SMALL sym")` marca um global como gp-relative; `register float x __asm__("$f2")` fixa registrador FPR; retorno `int` vs `void` do callee muda a alocação; argumentos são avaliados da esquerda para a direita (içar uma chamada para um local move um `addiu`); `sqrt.s` precisa de `.word 0x46040104` (em C++ NM use `eeSqrtf` de `include/vecmath.h`: o gas codifica `sqrtf` na forma MIPS32 e o EE lê `$f0`).

## Ordem de ataque
Decompile de cima para baixo ao longo do caminho real de execução, não por filtro de "funções pequenas": `main` → `Shell::LoadLevelFiles/LoadLevelDB/LoadMonstersDB` → `dbInitDb`/`dbsRelocate*`/`file*` → `InitPlayers`/`AddMonster`/`MonsterParse` → laço `rtMain` (hier/animação/desenho) → lógica de `TheGame::Update`. Esses carregadores também documentam os formatos de arquivo. Para o escopo por alcance: `python3 tools/callgraph.py <raízes> --no-libs` (grava `config/callgraph.csv`; copie e restaure se não quiser sobrescrever; só segue `jal`, então é um piso).

## Verificação de comportamento
Sem testes unitários por decisão do projeto. A verificação é por: `tools/difftest.py` / `difftest_all.py` (retail vs nossa função num emulador MIPS, mesmo estado aleatório), e, quando houver algo rodando, comparação com o PCSX2. Depurar sintoma: pausar o PCSX2, extrair a RAM do EE e recalcular a lógica do retail a partir do dump (`tools/ramdiff.py`). Se uma variante do ELF travar com "executing data", compare a RAM do EE com o ELF antes de suspeitar da função decompilada (já aconteceu: era código realocado sobrescrito por DMA).

**Dumps e instantâneos são descartáveis.** `.vu1snap` (~40 MB cada), `ram_*.bin`/`vram_*.bin` (36 MB), dumps do vigia e pastas de `lote/` ficam fora do repo e enchem o disco. Depois que uma correção é validada, apague os instantâneos e dumps que a motivaram (`snaps/`, `discroot/ram_*`, `lote/<data>/`); guarde só os que ainda mostram um problema em aberto ou que você nomeou de propósito como corpus de regressão (`tools/vu1cmp.ps1 <pasta>`). Nunca deixe `WOTM_VU1CHECK_SNAP` ligado em lote.
