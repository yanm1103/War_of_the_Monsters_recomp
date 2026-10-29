# Armadilhas já vistas (decomp NM / build / jogo)

(Índice dos docs no `CLAUDE.md`.)

## Armadilhas já vistas
- Ao reescrever o fim de um `.cpp` com script, conferir que as linhas `INCLUDE_ASM` finais (static init, `__tf`, ctor, `_GLOBAL_$I$`) continuam lá.
- `ccmatch.py` sem `-DNON_MATCHING` só compila as `INCLUDE_ASM` (tudo "MATCH"); para pontuar o C++ novo use `-DNON_MATCHING` (o `scoreall.sh` já faz).
- Layout das ferramentas de pontos: pontos de 0x40 bytes, `numPoints` em 0x4000, campos próprios a partir de 0x4050; `levelData = gameSlotBase(game->m_levelIdx)`.
- `shell` é gp-relativo em alguns TUs (PowerUpTool, PathTool, AiPathTool) e não em outros (StartPointTool): `__asm__("#SNFIX_SMALL shell")` só onde o retail usa gp.
  Dentro de um delay slot (`.set nomacro`) o acesso a global é sempre gp, sem precisar do pragma (tokyo).
- gas insere 2 `nop` extras num `.p2align 3` logo depois de uma sequência `li.s` (StickShaker::DefaultSetup); o retail não tem. Sem causa achada: marcar como equivalente.
- `_fvector` tem alinhamento 16 no ee-gcc: uma classe com membro `_fvector` fica alinhada a 16 e o `sizeof` arredonda para
  múltiplo de 16 (ex.: `IslandLevelSound` 0xF4 vira 0x100). Ao embutir essas classes numa maior, não ponha padding manual entre
  elas; confira com `typedef char x[sizeof(T) == n ? 1 : -1]` (game/Sound).
- `(x & 1) == 0` em vez de `!(x & 1)` muda `lw`/`xori` para `ld`/`andi` com campo de 64 bits (MilitaryPickup::kill).
- Cópia de `_fvector` por `lq/sq` com `jr` seguido de `nop` indica `asm volatile` com `lq/sq` no retail (setFormationPos, setTrans); registrador `$2` fixado com `register int t __asm__("$2")`.
- Chamada de função dentro dos argumentos da chamada final (operador vírgula) muda o agendamento do prólogo e fez `loadPoints` bater (PathTool/AiPathTool).
- Ponteiros intermediários (`VehicleNavigator *n = &nav; n->f14 = ...`) imitam os `addiu v1,v0,0x210` do retail.
- Strings de uma função que passam de `INCLUDE_ASM` para C mudam o padding do `.rodata`: acrescentar `.word 0` em `.rodata` por asm.
- `switch` em C gera jump table; o retail alinha em 24 palavras (acrescentar `.word 0` x2).
- Classes com vtable: ctor, `__tf` e `_vt$...` ficam como `INCLUDE_ASM`; escrever os métodos sem `virtual`.
- Funções estáticas sem argumentos às vezes têm um `v` no fim do símbolo retail: usar `__asm__("nome__Classev")` no membro.
- Mangling de matriz: `float (*m)[4]` vira `PA3_f` no gcc 2.95.
- O heredoc da ferramenta pode transformar `\n` em quebra de linha real dentro de scripts Python: para arquivos com regex, usar o Edit.
- `sh tools/wsl/gate.sh | tail` esconde o código de saída: para commitar só com ROM OK usar `sh tools/wsl/commit_if_ok.sh && git commit ...`.
- Global gp-relativo já definido no `.sdata` de um asm de dados (ex.: `WaterLevel`): declarar `extern float X; __asm__("#SNFIX_SMALL X");`, nunca definir de novo (duplicate symbol no link).
- Armazenar num campo de `struct` (ex.: `gameHud(i)->f0D0 = 0`) não invalida o `game` já carregado; armazenar via `*(int*)((char*)p + off)` invalida e o gcc recarrega. Por isso `Hud` ganhou campos reais.
- `EnemyInfo::s_info[i][j']` (include/game/enemy_info.h): tabela par-a-par, `j' = j - 1` quando `i < j`. `vecLenSq` (vecmath.h) = `mula.s/madda.s/madd.s` do retail.
- Em laços `for (j = 0; j < n; j++, m++)` guardar `n = game->m_numSlots` numa local (senão o gcc recarrega a cada volta).
- **`ccmatch.py` mascara os nomes dos símbolos chamados** (relocações): uma função pode dar MATCH chamando um callee com nome/mangling errado (classe local `MemoryStackG` em vez de `MemoryStack`, `transitionOK(Monster*)` em vez de `transitionOK()`). Só vale como "bate" depois do `gate.sh` (link real) com a função fora do `#ifdef NON_MATCHING`.
- **Variável vs objeto em `__asm__("sym")`**: `shell` e `game` são PONTEIROS (`Shell *shell`); `extern char x[] __asm__("shell")` dá o endereço da variável, não do objeto, e `x + 0x2C30` corrompe a memória em silêncio (o jogo congelava ~30-40 s depois, com `fileStatus`/`cdFileSystemToc` sobrescritos, `getNgpFilesLoaded()` = 80 e o laço de animação lendo lixo: `cpuTlbMiss` em `animationManager`/`AnimPlayer::UpdateAnimations` no log). Só use `char[]` para objetos de verdade (`gsPkt`, `_7Cameras$m_cameras`, `_12BigShotLevel$instance`); para ponteiros use `extern T *x`. `ccmatch.py` e o gate não pegam isso (relocações mascaradas); só jogando.
- **Build NM com erro de link deixa o `halfcpp.elf` ANTIGO** e o `play.ps1` continua dizendo "OK". Sempre ler a saída de `build_nm.sh` procurando `undefined reference` (use `| tail -3`, não esconda).
- **Ordem no arquivo**: cada função nova entra onde estava seu `INCLUDE_ASM` (ordem de endereço). Declarações, macros (`GM`, `SHI`) e classes locais que uma função NM usa precisam estar ACIMA do primeiro uso nessa ordem; funções que batem (fora do `#ifdef`) só podem usar declarações também fora do `#ifdef`.
- Chamar um símbolo retail com assinatura "criativa" (floats em `$f12`, `this` solto): declarar uma função livre com `__asm__("nome__Mangled")` (ver `src/game/rt.cpp`, bloco `SYM(...)`) em vez de adivinhar a classe; para variáveis `static` de classe: `extern char x[] __asm__("_12BigShotLevel$instance")`.
- m2c mostra argumentos a mais em chamadas (`transitionOK(this, monstro)`): o mangling diz quantos parâmetros existem de verdade (`transitionOK__12StateVictory` = só `this`). Confira o nome do símbolo antes de escrever.
- `extern int x;` de uma global gp pode sair como `lui/%lo` no primeiro store (em vez de `%gp_rel`); o remédio é o `__asm__("#SNFIX_SMALL x")` descrito acima (não testei neste caso: `numModsLeft`, `_7Cameras$m_numCameras` ficaram como equivalentes).
- **`sqrtf` em C++ NM sai com `sqrt.s` na codificação errada.** O R5900 usa `SQRT.S fd, ft` (fonte em `ft`, `fs=0`; retail `0x46020084`); o gas/ee-gcc emite a forma MIPS32 com a fonte em `fs`
  (`0x46006044`), que no EE lê `$f0`. Em `AiPathFinder::computeCostEstimate` isso descartava o termo z (comprovado rodando retail × nossa versão recompilada, 300/300 divergentes). O `difftest` (unicorn)
  não vê isso. Use `eeSqrtf` (`include/vecmath.h`, `.word 0x46040104`) em vez de `sqrtf` em código NM; para varrer um ELF NM: forma errada = `ft==0 && fs!=0` (o retail tem 127 `sqrt.s`, todos `ft`).
- Clone novo: o `configure.py --split` pode deixar de gerar os `.s` de dados (vtables como `_vt$12PointToolKit.s`) e o `ninja` para num arquivo faltando (splat 0.41.1 e 0.50.0).
  Contorno: tirar `src/` do repo, rodar o `--split` e devolver `src/`. A causa não foi investigada.
- Loops de espera por registrador de hardware (`objsInPacket`, `gRtReturn`): declarar `volatile`; sem isso o gcc vira laço infinito/hoisting.
- Ordem de stores em struct pequena pode inverter no gcc 2.95 (`rtReturnToShell`: escrever `code, active, delay` para sair `delay, active, code`).
- Teste automático: `powershell -ExecutionPolicy Bypass -File tools/pcsx2_auto/play.ps1 halfcpp [-pause]` (sem `-File`/`Bypass` o PowerShell recusa o script). Só exercita 1 jogador/free-for-all; telas de 2P, elimination, minigames e história não são cobertas.

