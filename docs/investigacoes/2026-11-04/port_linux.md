# Port para Linux (PR #64, `olivrsec`)

Diário do port nativo para Linux x86-64 (build de desenvolvimento, launcher e AppImage), sem quebrar o build do Windows. Relato do autor do PR, traduzido; a conferência no Windows está no fim.

## Situação por etapa

| Etapa | Situação | O que mostra / o que trava |
|---|---|---|
| Base e requisitos | Pronta | GCC 14.2, CMake, Ninja e as bibliotecas de desenvolvimento do FFmpeg presentes. O ELF tirado da ISO bate com o SHA1 do retail; os arquivos gerados ficam locais. |
| Fonte do nativo portável | Compila | O runtime, o jogo traduzido e a camada nativa compilam no Linux. O GCC 14 avisa do `std::filesystem::u8path` e de um `-Wfree-nonheap-object` no leitor de ISO9660; a imagem NTSC-U passou na validação do SHA1. |
| Scripts de setup, build e launch | Testados por cima | Setup e build completos; o launch funciona repetido e acha a ISO em `Game/` além de `ISO/`. A primeira execução escolhe a ISO por janela (Zenity/KDialog) ou pelo terminal, com a pasta de dados no XDG. |
| Boot no menu | Conferido | Menu e seleção de monstros com as fontes e os ícones nativos e os recursos da fase. |
| Jogo | Parcial | Seletor de ISO validou a imagem e bootou no menu. Teclado levou à Aventura e a uma luta; luta de 3 IAs terminou um round e a de 4 IAs rodou sem cair. O controle DragonRise foi reconhecido como jogador 1; um controle sintético (`uinput`) igual navegou o menu de pausa, mas os botões e eixos do controle físico não foram conferidos. Os dois filmes da abertura passam e a captura do PulseAudio tem som. Uma fase da Aventura salva pela tela do jogo carregou depois de reabrir e continuou na seguinte. Tela cheia X11 conferida no i3. Não conferidos: campanha inteira, controle físico, Windows e gerenciadores de janela que não sejam X11. |
| Tela cheia X11 | Conferida no i3 | O modo sem borda pede tela cheia EWMH no monitor escolhido sem trocar o modo do XRandR; primeira execução e F11 repetido conferidos no eDP-1 a 1920×1080. |
| Pacote | Gerado e validado; não publicável | AppImage gerado no Ubuntu 22.04 x86-64, dentro do teto de ABI do GLIBC 2.35. No Jammy, a primeira execução sem helpers aceitou a ISO, conferiu o SHA1, extraiu no XDG e chegou ao Press Start. No Void/X11 montou com FUSE e chegou ao Press Start; o `LD_DEBUG` mostrou o Mesa usando o C++ do sistema. O pacote não leva ISO, ELF, arquivos do disco nem mods. Em aberto: obrigações de código-fonte correspondente, Wayland/XWayland, outras distribuições. |

## Ponto de partida

- `third_party/PS2Recomp` é um submódulo com edições locais: não resetar, sobrescrever nem regenerar.
- A tentativa anterior está em `7df1f85:docs/investigacoes/2026-10-30/linux.md`: chegou ao menu, não testou luta e usava substitutos para alguns recursos embutidos.
- A ISO local fica em `Game/`; o ELF tirado dela tem SHA1 `1ed7f8457436a9170d8f85386e267369c7fcdd4f`. Disco extraído, código traduzido, checkout do PS2Recomp e build ficam fora do Git.

## Diário

- **2026-11-04:** sem as entradas do jogo nem os arquivos gerados, o runtime ainda não rodava.
- **2026-11-04:** ramos de portabilidade para Linux, leitura dos recursos por arquivo fora do Windows, opções AVX2 do GCC e os scripts `tools/setup_runtime.sh`, `tools/build_runtime.sh` e `tools/run_live.sh`. O setup preserva a saída traduzida já existente; o launch usa o ELF extraído e uma pasta de dados gravável do usuário. Conferidos: sintaxe dos scripts, sintaxe do standalone no GCC, espaços e aplicação do patch na base fixada.
- **2026-11-04:** setup com a ISO em `Game/`, SHA1 conferido, código do jogo gerado pelo PS2Recomp. `wotm` e `ps2EntryRunner` compilados com GCC/CMake/Ninja e o FFmpeg do sistema.
- **2026-11-04:** o launch repetido abortava (o `cp -n` parava com o ELF já presente); corrigido, e a ISO passa a ser achada em `Game/`. Menu, seleção e uma luta IA x IA conferidos por capturas X11, com a janela no eDP-1. O jogo seguiu rodando sem cair.
- **2026-11-04:** instalação só de leitura (pasta de dados no XDG/HOME), escolha da ISO por Zenity/KDialog ou terminal, logs nas execuções interativas. Conferidos o XDG e a primeira execução pelo terminal com a ISO do retail.
- **2026-11-04:** seletor Zenity numa instalação limpa: escolheu a ISO, conferiu o SHA1, extraiu o ELF e bootou com a fonte e os ícones. O teclado levou à primeira luta da Aventura; o áudio abriu. Ainda sem combate longo, som audível, controle, save/load nem paridade com o Windows.
- **2026-11-04:** a extração do setup passa a usar uma pasta temporária nova antes de aceitar o ELF; um teste com a extração simulada mostrou que um `disc/SCUS_971.97` antigo não faz uma ISO sem ELF passar.
- **2026-11-04:** os dois filmes da abertura passam (`intro_m.pss` até o fim, `int_main.pss` pulado). Saída de 48 kHz estéreo com som não nulo na captura do PulseAudio. Opções salvas pela tela do jogo e relidas depois de reabrir. DragonRise reconhecido como jogador 1; um controle sintético igual moveu a seleção do menu. Luta de 4 IAs sem cair; os quadros com 4 vistas levaram de 15 a 21 ms, então não segura 60 nesse modo (o perfil curto não apontou otimização segura; nada mudou no desenho).
- **2026-11-04:** com `WOTM_CHEATS=inv,vencer=5`, venceu uma fase da Aventura, salvou no slot 1, fechou e reabriu com a mesma pasta XDG. A lista mostrou Togera em Gamblers Gulch com pontos, vidas e fase; carregar registrou `aventura do slot 1 carregada` e continuou em Vegas. Um ciclo de save/continue, não a campanha inteira.
- **2026-11-04:** tela cheia sem borda no X11: o pedido EWMH de entrar e sair, mantendo o modo de vídeo. No i3, conferidos o modo padrão da primeira execução, F11 repetido e a troca ao vivo entre sem borda e exclusivo no eDP-1 (1920×1080), com o modo do XRandR intacto. Um corpus VU1 do Linux (10 quadros de menu, luta e destroços) não mudou em relação à base de antes; não substitui a regressão no Windows.
- **2026-11-04:** AppImage gerado num rootfs do Ubuntu 24.04 com FFmpeg 7.1 só LGPL e runtime do AppImage fixados. Rodou no Void em modo extraído com pasta XDG nova: o Zenity aceitou a ISO, o SHA1 bateu, `intro_m.pss` passou e a luta apareceu.
- **2026-11-05:** AppImage refeito no Ubuntu 22.04 (teto GLIBC 2.35) com o runtime C++ do GCC estático e com símbolos ocultos: o `libstdc++` do Jammy no caminho de bibliotecas quebrava o Mesa/Z3 do Void. O `wotm` não depende mais de `libstdc++`/`libgcc_s` dinâmicos. Primeira execução limpa no Jammy (ISO com espaços no caminho, caminho colado com caractere UTF-8 no fim apagado com Backspace) chegou ao Press Start; no Void/X11 montou com FUSE e chegou ao Press Start. `tools/test_check_appimage.py`: 16 testes passam.

## Conferência no Windows (antes do merge)

- PS2Recomp local com as três mudanças do patch no `CMakeLists.txt` (`src/lib` nos includes, `WOTM_NATIVE_DIR` como `#define`, bloco X11 só no Linux; a mudança do `ReleaseMode.cmake` não vale para o MSVC). `build_runtime.bat` compilou e linkou `wotm.exe` e `ps2EntryRunner.exe` sem avisos; o build refaz o código do jogo inteiro uma vez (~17 min), porque o include novo é `PUBLIC`.
- `isoReadRoot` do PR (com a checagem nova do tamanho do nome no registro de diretório) isolado num programa compilado com o `cl`: acha o `SCUS_971.97` na ISO do retail com o SHA1 certo e devolve falso para um nome que não existe.
- `tools\corpus.ps1 -Check` contra a base gravada antes com o exe da master: nada piorou.
- Boot ao vivo por 50 s com o roteiro automático: abertura, menu e luta sem queda; log com o atlas `[prompts]` e as fontes carregados.
- O dono conferiu os ícones de botão no menu e a primeira execução do `wotm.exe` numa pasta limpa (pede a ISO, grava o ELF e o `wotm.ini`, boota).
