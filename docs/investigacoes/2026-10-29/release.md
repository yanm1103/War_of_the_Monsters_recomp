# Primeiro release: o que falta levantar

Item 14 do `TODO.md`. Decisão do dono: zip com o `wotm.exe` pronto nos Releases do GitHub (nunca no repo), sem nada do disco; o
jogador fornece a própria ISO. Este levantamento parte do estado da master depois do merge da campanha co-op (item 13, `09f79eb`).

## 1. Hoje o `wotm.exe` só roda pelo `run_live.ps1`

O exe não sobe sozinho: o `tools/runtime_template/run_live.ps1` monta o ambiente antes de abrir. Tudo o que ele faz precisa passar
a valer dentro do exe (ou de um launcher de jogador):

| O que o script faz | Onde o exe lê | Para o release |
|---|---|---|
| `PS2X_NO_VU1`, `PS2X_NO_VIF1`, `PS2X_NO_GS` = 1 | `wotm_scene.inc`, `gs_frontend.cpp` (PS2Recomp) | padrão ligado no `wotm.exe` (só o `ps2EntryRunner` precisa do contrário) |
| `PS2X_SCENE`, `PS2X_FMV` = 1 | `ps2_runtime.cpp:2665`, `main.cpp:259/335` | idem |
| `PS2X_CD_IMAGE` = ISO do setup | `main.cpp:307` | caminho guardado num ini ao lado do exe, escrito na primeira execução |
| argumento = `discroot\SCUS_971.97`, cwd = `discroot` | `main.cpp`; `wotm_save.inc:404` grava `mc0\`/`save.json` ao lado do ELF | exe acha o ELF ao lado de si (ou numa subpasta fixa) sem argumento |
| stderr para `live.err.log` (redirecionamento do PowerShell) | todos os `[tag]` do nativo e o `crashFilter` (`wotm_live.inc:297`) | o exe grava o próprio log (ex.: `wotm.log` ao lado do save), senão um relato de bug chega sem log |
| `sync_mods.ps1`: `mods\` do repo para `discroot\mods\` | `wotm_mods.inc` | o zip já traz `mods\` no lugar que o jogo lê |
| `-Auto 1` / `pad_script.ps1` | `PS2X_PAD` | nada (fora do release) |

Essas são costuras do PS2Recomp (`main.cpp`, `ps2_runtime.cpp`): mudá-las exige regenerar `tools/runtime_patch/wotm-runtime.patch`.
Alternativa sem mexer no runtime: um launcher que faça o papel do `run_live.ps1` (variáveis, argumento, cwd, log). Mais simples, mas
o exe aberto direto não funciona, e jogador abre o exe direto.

## 2. Launcher de jogador (primeira execução)

O `WotM_Launcher.exe` (`tools/launcher/WotmLauncher.cs`) é de desenvolvimento: flags de diagnóstico, trapaças, partida direta, e
procura a pasta do runtime e o `run_live.ps1` (`.wotm_recomp_dir`). Não serve para o jogador. Para o release:
- **Primeira execução:** escolher a `.iso`, ler o `SCUS_971.97` direto do ISO9660 (sem `Mount-DiskImage`/robocopy como o
  `setup_runtime.ps1`, que copia o disco inteiro), conferir o SHA1 `1ed7f8457436a9170d8f85386e267369c7fcdd4f` e gravá-lo na pasta do
  jogo; guardar o caminho da ISO. ISO de outra região: mensagem clara ("só NTSC-U, SCUS-97197").
- **Depois:** abrir o jogo direto; trocar de ISO se ela sumir do caminho salvo.
- Pode ser o próprio `wotm.exe` (diálogo nativo na primeira execução) ou um exe pequeno à parte. O lançador em C# precisa do .NET
  Framework 4, que já vem no Windows 10/11.
- Opções de vídeo, QoL e teclado já estão no painel "Recompiled options" do menu (`settings.ini`): o launcher não precisa repeti-las.

## 3. Onde o jogo grava

`save.json`, `mc0\`, `settings.ini`, `screenshots\` e o log ficam ao lado do ELF (`wotm_save.inc:404`, `wotm_settings.hpp:409`,
`wotm_photo.inc:664`). Zip descompactado em `Program Files` não grava. Decidir: avisar no README (pasta do usuário) ou mover os
graváveis para `%APPDATA%\WotM` com a pasta do jogo como fallback.

## 4. Build de release

- `wotm.exe` com LTCG (o `build.bat` da pasta do runtime; link de ~28 min), sem `ps2EntryRunner.exe`.
- Nenhuma `WOTM_*`/`PS2X_*` de diagnóstico ligada por padrão (`python tools/wotm_env.py` e a tabela do `NATIVO.md`). O vigia
  (`WOTM_WATCHDOG`) grava `ram_/vram_vigia_*.bin` de 36 MB no travamento: no release, só o log, sem dump.
- PDB fora do zip, guardado por versão, para resolver os `[CRASH] exe+0x…` que chegarem.
- Número de versão no exe e no log (commit da master + versão), para casar relato e PDB.

## 5. DLLs: o que o exe carrega de fato

`dumpbin /dependents` no `wotm.exe` do `build_dev`: além do sistema, `avcodec-61`, `avutil-59`, `swscale-8` e o runtime do MSVC
(`MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`). Fechamento transitivo dessas três (14 DLLs):

`avcodec-61 avutil-59 swscale-8 swresample-5 libwebp libwebpmux libsharpyuv jxl jxl_threads jxl_cms brotlicommon brotlidec brotlienc zlib1`

Das 28 da pasta `build_dev\ps2xRuntime`, ficam de fora: `SDL2`, `avformat-61`, `avdevice-61`, `avfilter-10`, `OpenEXR*`, `Imath`,
`Iex`, `IlmThread`, `freetype`, `harfbuzz` (dependências do `ps2EntryRunner`/ferramentas, não do jogo). Conferir de novo no exe com
LTCG. Possível enxugar mais: o jogo só decodifica MPEG-2 dos filmes; um FFmpeg com só esse decoder dispensaria webp/jxl/brotli.

Runtime do MSVC: o jogador sem o VC++ Redistributable 2015-2022 não abre o exe. Opções: link estático (`/MT`, muda a costura do
CMake), copiar as três DLLs ao lado do exe (permitido pela licença de redistribuição da Microsoft) ou mandar instalar o redist.
OpenGL 3.3 (raylib): placa sem driver (VM, Remote Desktop) não abre; mensagem de erro em vez de fechar calado.

## 6. Licenças no zip

- Este projeto: GPL-3.0, com o ponteiro para a fonte (este repo no commit do release, `tools/runtime_patch/` e o commit base do
  PS2Recomp, `2c5fbb93…` no `setup_runtime.ps1`).
- PS2Recomp: o `LICENSE` do submódulo é GPL.
- FFmpeg: o pacote baixado é o `ffmpeg-msvc-prebuilt` **LGPL** (`n7.1-241205`, `ps2xRuntime/CMakeLists.txt:259`): texto da LGPL e
  ponteiro para a fonte; mais as licenças de libwebp (BSD), libjxl (BSD), brotli (MIT) e zlib.
- raylib (zlib), imgui e rlImGui (MIT, se entram no `wotm.exe`; conferir).
- Uma pasta `licenses\` no zip com um arquivo por componente, e um `THIRD_PARTY.txt` que liste tudo.

## 7. Risco

O `wotm.exe` contém o código do jogo traduzido: deixá-lo fora do repo não muda o fato de que o zip é derivado do executável da
Sony. Um pedido de remoção pode atingir o release e o repo. O exe não é assinado: SmartScreen ("Windows protegeu o computador") e
antivírus com heurística vão reclamar; nota no README do release.

## 8. Texto para o jogador

O `README.md` é para quem compila. O zip precisa de um `LEIA-ME`/`README` curto (inglês, como o README):
- requisitos (Windows 10/11 x64, OpenGL 3.3, a ISO NTSC-U própria);
- primeira execução, onde ficam saves, mods e screenshots;
- controles padrão (teclado e controle) e o painel "Recompiled options";
- mods: pasta `mods\`, link para `docs/passos/MODS.md`;
- como relatar bug (qual arquivo de log anexar);
- bugs conhecidos, tirados do `docs/passos/VU1CHECK.md`.

E as notas do release no GitHub (o que funciona: luta, aventura até 4 jogadores, versus até 8, save em JSON, skins e monstros de
mod, modo treino, Mecha jogável).

## 9. Script do pacote

`tools/release.ps1` (não existe): compila com LTCG (ou exige o exe pronto), copia o exe, as DLLs da seção 5, o launcher, `mods\`,
`licenses\` e o texto do jogador para `release\<versão>\`, confere que nada do disco entrou (nenhum `SCUS_*`, `.iso`, pasta do
disco) e gera o zip e o SHA256 dele. O PDB vai para uma pasta à parte.

## 10. Conferência antes de publicar

Descompactar numa pasta limpa, numa conta (ou máquina/VM com GPU) sem Visual Studio, Python, CMake, o repo nem o redist. Jogar:
- versus com IA e com 2 a 4 na mesma tela;
- aventura do começo a uma fase de chefe, sozinho e em co-op; salvar, fechar e carregar;
- um filme com som; uma skin e um monstro de mod;
- primeira execução com ISO errada, sem ISO e com a ISO movida depois.

## Ordem sugerida

1. Seção 1 (o exe roda sozinho, com log próprio) e seção 3 (onde grava): sem isso nada mais se testa fora do ambiente do dono.
2. Launcher de primeira execução (2).
3. DLLs e runtime do MSVC (5), build com LTCG e versão (4).
4. Script do pacote com licenças (6, 9) e texto do jogador (8).
5. Conferência na máquina limpa (10).
