# Próximos passos (atualizado em 2026-10-09)

## Direção atual (decidida com o usuário em 2026-10-09; em validação)
O objetivo continua sendo um port nativo de PC fácil de modar, mas o **meio** mudou: a base passa a ser o jogo **recompilado** (PS2Recomp, fora do repo) e a decompilação vira a
camada legível que substitui, por *hook*, só as funções que importam para modar. Consequências práticas:
- **Byte match deixou de ser requisito por função.** `equivalent` é suficiente para o que vamos modar. `matched` continua sendo bem-vindo quando sai barato, e o `gate.sh`
  continua obrigatório (a ROM do build normal tem que bater o SHA1).
- **Fila de prioridade**: `config/boot_coverage_asm.csv` (601 funções ainda em asm que o jogo chama do boot até a interface; é um piso, não cobre gameplay). Priorizar o que é
  engine/formatos/lógica de jogo (`common/animation`, `AnimCurve`, `mathf`, `hier`, `dbs`, `texm`, `view`; `game/Monster*`, `Shell`, IA) e deixar para o fim memory card,
  `input`, `vi`, `ps`, `sce/*`, `lib989snd`.
- **Verificação**: para equivalentes com VU0 ou `min/max/madd` (62 de 329) use `tools/recomp_oracle` (retail × nossa, ambos recompilados); para as demais, `tools/difftest.py`.
  O oracle já achou dois erros reais (`sqrtf` com `sqrt.s` mal codificado e `sb` virando `sw`; ver "Armadilhas").
- **Renderização**: a meta é cortar na fronteira da engine (`config/hw_boundary.csv`) e desenhar nativo; o VU1/GS emulado do runtime do PS2Recomp é só ponte para ver imagem.
  Som, FMVs e entrada completa ficam fora do primeiro port (só um mapeamento mínimo de teclado para o pad).
- **Pendente**: (1) testar o build do runtime no Windows/MSVC e ver se aparece imagem; (2) PR no PS2Recomp com a correção de `SQRT.S`/`RSQRT.S` (o upstream usa `fs` onde o R5900 usa
  `ft`; 108 + 76 funções do jogo afetadas); (3) oracle para funções com chamadas (hoje só folhas: 24 de 60 candidatas rodaram); (4) cobertura com gameplay.
- O que foi medido e como reproduzir está em `tools/recomp_oracle/README.md` e na seção "Recompilação estática" de `docs/ANALYSIS.md`.

## Runtime recompilado: estado e como retomar (2026-10-10)
**Resumo:** o jogo recompilado roda no Windows (MSVC) em tempo real (vsync 60/s, EE ~294M ciclos/s) com um combo de variáveis de teste, e um roteiro de pad leva do boot até uma fase, onde a
travessia nativa lê a hierarquia (`world`, 483 nós / 29 objetos). Ainda não há 3D nem texturas na tela: o VU1 e o GS emulados estão desligados de propósito e o desenho é papel da camada nativa.

**Onde está o código (fora do repo, por ser derivado do jogo / do PS2Recomp):**
- Clone de teste `C:\Users\TwistZero\wotm-recomp-win` (build MSVC em `build\`, `build.bat` recompila só o runner; o link LTCG leva ~10 min; `run.ps1` executa e captura).
- Mudanças de runtime (`PS2X_NO_VIF1`, `SKIP_CMOVIE`, `PEEK`, `PROF`, `STATS`, diagnósticos do escalonador) já estão versionadas na branch `wotm` do fork (`yanm1103/PS2Recomp`, submódulo `third_party/PS2Recomp`) no commit `a6b9af2`, **só local: falta o push no fork (decisão do dono)**. O `wotm_runtime_diagnostics.patch` e o `wotm_scene.inc` do clone de teste ficam como cópia de segurança (o patch é contra o upstream `2c5fbb9`, não aplica na branch `wotm`). O código gerado (`register_functions.cpp` etc.) nunca vai ao repo.

**Combo que funciona** (`PS2X_*` lidas pelo runner; `run.ps1` já põe `SKIP_MOVIES`, `STATS`, `NO_VU1`, `CD_IMAGE`):
`PS2X_NO_VIF1=1 PS2X_NO_GS=1 PS2X_SKIP_CMOVIE=1` (+ `PS2X_SCENE=1` para a travessia nativa). Roteiro de pad que chega a uma fase em ~50 s: `PS2X_PAD` = `start` (0.8 s) a cada 1,5 s de t=3 a 18,
depois `cross`@22, `down`@25, `cross`@28,31,34,37,40,43,47,51 (1 s cada). Rodar: `powershell -ExecutionPolicy Bypass -File run.ps1 -Scene 1 -Seconds 75 -Frames "3300" -Tag runN`.
Variáveis novas: `PS2X_NO_VIF1` (ignora a execução do DMA do VIF1/VU1 mas sinaliza a conclusão), `PS2X_SKIP_CMOVIE` (`CMovie::Play` no-op, `Update` devolve 1), `PS2X_PEEK="hex,hex"` (palavras da RDRAM no log `[peek]`),
`PS2X_PROF=1` (amostrador do PC ao vivo, grava `discroot/prof.<frame>`; mapear com `config/status.csv`), `PS2X_NOOP=addr,addr` (função vira no-op), e no stderr com `PS2X_STATS=1`: `[time]` (tempo de GS/upload/draw/idle),
`[ui]` (currScreen, betweenScreens, ...), `[pad]`, `[scene]`.

**O que se descobriu (e que desfaz conclusões anteriores):**
1. A "tela preta" era artefato do `TakeScreenshot` antes do `rlDrawRenderBatchActive()`; com a correção aparece imagem (fundo 2D chapado, sem textura).
2. A lentidão de ~40x vinha do DMA do VIF1 ser processado, com o VU1 interpretado, DENTRO do `sw 0x145 -> D1_CHCR` em `hierTraverseAsm` (0x207744, 99% das amostras). O EE ficava 100% ocupado, os ciclos virtuais andavam devagar e o
   vsync (que exige ciclo virtual E prazo de host) saía a ~12 Hz. O `PS2X_NO_VIF1` resolve; o GS em CPU (`processGIFPacket`) era a outra metade (86% do tempo com GS ligado): `PS2X_NO_GS`.
3. A thread de decodificação de filme girava 2M `switchThread` porque `CMovie::Play` (menu) não estava coberto pelo hook de skip.
4. `currScreen` 0x14 = `screenWaitForStart` (Press Start): só aceita `screenGetInput(1)==0x10` (Start) quando `betweenScreens==0`; antes ficava preso no fade (animação lenta pelo relógio virtual).
   Telas: 0x10 só faz `changeScreen(0x10->0x14)`, 1 menu, 99 (0x63) carregamento (`Shell::FadeScreen`, `DisplayLoadBackground`); dentro da fase `currScreen=1`.
5. **Câmera lenta na fase (34 vsync/s) era o laço do IOP**, não o VU0 (2026-10-10). Um amostrador de RIP do host (`PS2X_HOSTPROF=1`, `PS2X_HOSTPROF_SKIP=<s>` descarta o carregamento) mostrou ~18% da thread do guest em
   `IopKernel::beginNextReady/nextWakeCycle`: `EeScheduler::accountCycles` chamava `IopEmulator::runEeCycles` a cada checkpoint de EE (poucos ciclos), e cada chamada varre threads/timers/DMA do IOP. Acumular e rodar em lotes
   (`PS2X_IOP_BATCH`, em ciclos de IOP; padrão 256 = 2048 de EE, ~7 µs) leva a fase a **60 vsync/s** (batch 1: 31,8; 64: 48,7; 256: 60,1; 1024: 59,9) e a CPU de ~145% para ~50% de um núcleo. Os mesmos erros de som
   (`snd_BankLoad`) aparecem com e sem lote. O atalho do agendador do VU0 (`PS2X_VU0_VERIFY`: 0 divergências em 1,5M chamadas) não deu ganho mensurável; a reescrita do VU0 em C++ deixou de ser necessária. A mudança está só no clone
   do runtime (`wotm-recomp-win\PS2Recomp`), não no repo.
6. Endereços úteis: currScreen 0x6F8464, nextScreen 0x6F846C, betweenScreens 0x6F7E8C, screenFirstPass 0x6F7E80, targetAlpha 0x6F7E74, currAnimationIndex 0x6F7E88, fadingIn/Out 0x6F807C/0x6F8080, world 0x6F87C4.

**Próximos passos, em ordem:**
1. ~~Versionar o patch de runtime~~ (feito em 2026-10-10, `a6b9af2`); falta só o push no fork.
2. Travessia nativa de verdade: de círculos para malhas, lendo `.NGP/.PTR/.RTX/.TEX` (`docs/FORMATOS.md`) e desenhando via raylib; cortar na fronteira `hierTraverseAsm`/`pktAddVu1ObjAsm` (`config/hw_boundary.csv`). Texturas vêm por `pktAddVu1Tex`/`texmActivateTexture`.
3. Cobertura com gameplay (`PS2X_COVERAGE`, agora em tempo real) para realimentar `config/boot_coverage_asm.csv`; depois voltar à decompilação pela fila.
4. Ideia do dono: um `PS2X_FAST_BOOT` que pule esperas de abertura (cortar só o laço/fade, não a inicialização; ex. `Shell::FadeScreen` 0x1AB660, `screenTransition` 0x19E698, `uiIntro` 0x1DA3C8).
5. Pendentes antigos: PR #277 no PS2Recomp (SQRT.S/RSQRT.S), oracle para funções não-folha, `div.s` por zero e `min/max` com NaN no tradutor.

## Camada nativa (`native/`): estado em 2026-10-10 (noite)
**Funciona:** a cena da fase é lida direto da RAM do EE e desenhada com raylib, sem VU1/GS, com a **câmera do jogo** (`viewInfo[0]` -> `_cs`; HFOV ~79,6 graus, VFOV ~63,7 em 4:3). Validado com a CENTRAL: 692 itens / 3245 nós, os prédios, ruas e o céu aparecem do ponto de vista da partida.
- `native/wotm_native.hpp` (header único: `Ram`, matrizes, `decodeObject` = malha do `polyPkt`, `Scene::collect/draw`, `readGameCamera`), `native/wotm_gs.hpp` (endereçamento do GS e decodificação de textura), `native/viewer.cpp` (visualizador standalone com câmera livre/do jogo, `--shot`, `--texdump`), `native/build.bat` (MSVC; usa o raylib do build do clone de teste). O formato da malha está em `docs/FORMATOS.md` ("Malhas") e o leitor offline em `tools/ngp.py`.
- Fluxo de iteração (segundos, sem emulador): o runtime grava `ram_<q>.bin` + `vram_<q>.bin` (`PS2X_DUMP_RAM="3300,3900"`), e `build\viewer.exe ram_<q>.bin` desenha. O clone de teste tem `run_dump.ps1` (roteiro de pad até a fase + dumps).
- Variáveis novas do runtime (branch `wotm`): `PS2X_DUMP_RAM`, `PS2X_GS_UPLOADS` (com `NO_GS`: o GS só processa uploads, sem rasterizar), `PS2X_VIF1_UPLOADS` (com `NO_VIF1`: processa a cadeia VIF1 sem executar o VU1) e `PS2X_NO_VU1=2` (só `rtWaitForVu1` vira no-op).

**Texturas (resolvido em 2026-10-10, validado visualmente na CENTRAL):** cada textura é um pacote na RAM apontado por `texInfo[texId]` (0x50F100, 16 bytes por entrada): descritor em `+0x24` (largura), `+0x26` (altura), `+0x2A` (TBW), `+0x2B` (PSM) e pixels **lineares** (sem swizzle) a partir de `+0x80` (T8 = 1 byte, T4 = 2 pixels por byte, nibble baixo primeiro). O `TBP0` do TEX0 do objeto muitas vezes é 0 (o `texm` remenda na hora do desenho) e não é usado.
- **Paletas vêm do `.RTX` da fase, lido do disco** (`Scene::loadRtx`, viewer `--rtx`): entradas de `(palavra0 >> 2) * 16` bytes, cabeçalho de 16 B (descritor GS em `+8`: DBP relativo no meia-palavra `+0xA`, TW/TH nos bits 40..47), corpo em **ordem lógica** (entrada `i` = palavra `i`): 16x16 CT32 = 256 cores (1040 B), 8x2 CT32 = 16 cores (80 B). O `TEX0.CBP` do objeto é `base + DBP`, com `base = tempVramTexAddr (0x6F8818) >> 6` (6753 na CENTRAL; confere nos 565 objetos). Alfa GS (0x80 = 1,0) vira 0..255 com `fixAlpha`.
- **A VRAM do dump não serve para paletas**: guarda restos do menu (a paleta em `CBP` 7216 era outra). Por isso `PS2X_VIF1_UPLOADS`/`PS2X_NO_VU1=2` (que deixam o DMA do quadro correr e travam em `particleDraw`, 0x21B8A0) não são mais necessários para texturas; ficam no runtime só como experimento.
- Armadilha do raylib: `rlBegin` com modo diferente do lote atual **reseta a textura** para a padrão; chame `rlBegin` antes de `rlSetTexture`, e descarregue o lote (`rlDrawRenderBatchActive`) a cada troca de textura (o alinhamento automático do rlgl bagunça os triângulos).
- Céu: `worldCtx[0]` `+4/+8/+0xC` (`skyCs`, `skyCs2`, nuvens), desenhado antes do mundo, sem profundidade, com a posição trocada pela da câmera.
- Alfa: o alfa de vértice só vale com `PRIM.ABE` (bit 6 do PRIM da tag GIF); com `ABE=0` o strip é opaco. Já respeitado.

**Próximos passos (nativo):** (1) o nativo precisa do nome da fase para achar o `.RTX` (hoje `--rtx` explícito; sai de `Shell`/`m_levelId` + `LEVELS.TXT`); (2) incluir `wotm_native.hpp` no `wotm_scene.inc` do runtime (desenhar ao vivo, hoje só o visualizador usa); (3) personagens (CHAR_INSTANCE/ANIM_XFORM, esqueleto e `animMatrixPtr`), (4) estados de `DESTRUCTIBLE`/`INTERACTIVE`/`SWITCH` e `LOD` por distância, (5) partículas, HUD e fonte.

## Build rápido do runtime (sem LTCG) — 2026-10-10
O build de release do PS2Recomp liga `/GL` + `/LTCG` + IPO (`ps2xRuntime/cmake/ReleaseMode.cmake`): o `link.exe` reotimiza o programa inteiro e leva **~28 min** a cada mudança, mesmo num `.inc` pequeno. Medido no PC do dono (12 núcleos lógicos), na fase da CENTRAL: sem LTCG o jogo continua a **59,8 fps** (tempo real; o runtime segura 60 de propósito), com ~147% de um núcleo contra ~136% do build LTCG. Ou seja, sem perda visível. Reconstrução incremental: **13 s**; build completo do zero: ~4 min.
- Mudança (no clone de teste do runtime, fora deste repo): em `EnableFastReleaseMode`, logo depois do `message(...)`, `if(PS2X_FAST_LINK)` aplica só `/O2 /Ob2 /Oi /Gy /Gw /GF /Zc:inline /fp:fast /DNDEBUG /arch:AVX2 /GS- /Qspectre-` ao alvo e dá `return()` (sem `/GL`, `/LTCG`, IPO).
- Uso: `configure_dev.bat` (mesmo `configure.bat` com `-DPS2X_FAST_LINK=ON -B build_dev`), depois `build_dev.bat`; o executável fica em `build_dev\ps2xRuntime\ps2EntryRunner.exe`. `run_live.ps1` usa o `build_dev` por padrão (`-Build build` volta ao LTCG). O ninja não rastreia os `.inc`: depois de mudar `wotm_scene.inc`/`wotm_live.inc`, `touch ps2xRuntime/src/lib/ps2_runtime.cpp`.
- Medição reproduzível: `measure.ps1 -Exe <exe> -Tag <nome>` (roteiro de pad até a fase, 6 amostras de CPU e fps).
- Teclado (definido pelo dono; backend em `ps2_pad.cpp`, que antes so lia o teclado sem gamepad e agora soma): **setas = analogico esquerdo**, **I/J/K/L = D-pad** (cima/esq./baixo/dir.), **Espaco = X**, **Z = quadrado**, **X = triangulo**, **S = bola**, **Enter = Start**, **Backspace = Select**, **Q = L1**, **W = R1**, **Shift = L1+R1**, **A = L2**, **C = R2**.

## Desenho nativo ao vivo (2026-10-10)
`PS2X_SCENE=1` (com `PS2X_NO_VU1=1 PS2X_NO_VIF1=1 PS2X_NO_GS=1 PS2X_SKIP_CMOVIE=1`) faz o runtime desenhar a fase direto da RAM com `native/wotm_native.hpp`, num render texture 4:3 mostrado por cima do quadro do GS (`wotm_live.inc` -> `wotmDrawNative`). Ele detecta a troca de fase (assinatura da imagem em 0xA00000 + `tempVramTexAddr`) e escolhe sozinho o `LVL/*.RTX` que cobre as paletas dos objetos texturizados. Sem fase carregada (menus) o quadro do GS segue como antes.
- Rodar: `powershell -ExecutionPolicy Bypass -File C:/Users/TwistZero/wotm-recomp-win/run_live.ps1` (roteiro de pad automático até a fase, ~50 s; `-Auto 0` para jogar com o teclado; `-Stats 1` loga `[native]`). Build: `configure.bat` passa `-DWOTM_NATIVE_DIR=<WoTM>/native` e `build.bat` compila; **o ninja não rastreia os `.inc`**: depois de mudar `wotm_scene.inc`/`wotm_live.inc` faça `touch ps2xRuntime/src/lib/ps2_runtime.cpp`, senão "no work to do".
- **Sem VU1 ninguém conclui o pacote**: o jogo espera `objsInPacket | objsInAlphaPacket` (0x6F87A0/A4) zerarem, em `rtWaitForVu1` (hookado) e num laço **embutido em `particleDraw`** (0x21B8A0) que o hook não cobre; com isso o jogo congelava antes de `rtMain` (`g_frame` em 0x6F7E38 ficava 0). A apresentação (60 Hz) zera esses dois contadores quando `PS2X_NO_VU1` está ligado.
- O que aparece: mundo, céu, os `CS` ativos e os elementos de HUD (nós da hierarquia). **Falta**: personagens animados (esqueleto/`ANIM_XFORM`), estados de destrutíveis, LOD por distância, partículas, e o HUD/fonte como 2D de verdade.

## Personagens na camada nativa (2026-10-10, parado no meio)
**Feito e validado:**
- **Alfa de vértice em meia escala**: `0x40` = 1,0 (o microcódigo dobra). A quase totalidade dos objetos tem alfa 66 (~0x42) e é opaca; só alfa baixo (0..51, névoa/sombras) é translúcido. Alfa efetivo = `min(255, a*4)`; o bit `PRIM.ABE` do template **não** serve para decidir blend. Com isso os prédios saem sólidos.
- **Elementos de HUD** (ids 0x1C20..0x2133) são nós de espaço de tela e ficam fora do 3D (`drawCs` os pula).
- **Veículos/props**: hierarquia rígida. `CHAR_INSTANCE` (+0xC = `animOutput.atMat`, +0x14 = `animMatrixPtr`, +0x18 = `animPktPtr`, filhos em +0x2C) com nós `ANIM_XFORM` (op 17): o próprio jogo calcula a matriz local de cada nó a cada quadro (`hierAnimTransNode`) em `atMat[matrixIdx]` (`matrixIdx` em +0xC4, `visible` float em +0xC, filhos em +0xCC); o nativo só lê (`native/wotm_native.hpp`, case 17/25/38). Caminhões, escavadeira e helicóptero aparecem.
- **LOD**: `hierLod` escolhe o **maior índice cujo `switchOutDis` (distância ao quadrado) > distância**; o índice 0 é o mais grosseiro (388 vértices no monstro) e o último é o mais detalhado (pode ser um GROUP de partes). O nativo ainda usa sempre o 0: falta implementar a seleção por distância.
- **Monstros são skinned** (`CS` com ids 96/97/101/105/106/107 -> GROUP -> `CHAR_INSTANCE` -> `SKEL_BONE` + `LOD`). O `polyPkt` deles é uma **cadeia de sub-pacotes**: tag `0x600100b4` (bit 16 = skinned; os 16 bits baixos NÃO são o tamanho), `UNPACK V4-32 @0xB5` (até 190 vértices, `x,y,z` + **peso em `w`**, com um **código de osso nos 8 bits baixos de x, y e z**), depois normais `V3-8 @0x175` e as seções de strip de sempre (`MSCALF`/`MSCNT`); cada sub-pacote termina em dois NOPs e o próximo vem alinhado em qword depois de alguns qwords zerados (às vezes 0x40 bytes de zeros antes da 1ª tag). `tools/ngp.py` (`decode_object`) já segue a cadeia e devolve, por vértice, `(..., w, cx, cy, cz)`.
- **Paleta de matrizes**: `animPktPtr[lod]` é uma cadeia DMA cujo 1º tag (`3000007d <endereço>`) aponta o bloco de 125 qwords (31 matrizes de 4 qwords a partir de `+0x10`; na CENTRAL `0x11d1e80`, logo antes de `animMatrixPtr`). O código de osso é o **endereço de qword no VU1**: `slot = (código & 0x7F) / 4`, matriz = `bloco + 0x10 + 0x40*slot` (o bit 0x80 é uma flag ainda sem significado). `animMatrixPtr[i]` aponta para slots **embaralhados** dessa paleta (não use `animMatrixPtr[código/4]`). Com isso o monstro (lagarto bípede com cauda) sai coerente em pose, em wireframe.
- **Skinning medido** (preservação do comprimento das arestas, bind x pose, `exp_skin.py`/`exp_w.py` no scratchpad): osso de `x` sozinho já preserva ~tudo; a melhor mistura testada é **`w` no osso de x + `(1-w)` no osso de y** (juntas: erro médio 0,10 contra 0,16 só com x); o código de z não ajudou. Ainda não é prova (o peso pode ser outro, ex. `w^1.5` melhorou as juntas, 0,094).

**Implementado em 2026-10-10 (noite), validado com `viewer` nos dumps da CENTRAL (lagarto texturizado e na pose certa):**
- `decodePacket`/`decodeObject` seguem a cadeia de sub-pacotes skinned; `Vtx` ganhou `w` e os códigos `cx/cy/cz`; `DrawItem` guarda `animPkt` (CHAR_INSTANCE `+0x18`) e o índice de LOD; `skinVertex` mistura `w*Mx + (1-w)*My` com a paleta (`animPktPtr[lod]` -> tag ref `0x3000007d` -> bloco; slot = `(código & 0x7F) / 4`, matriz em `bloco + 0x10 + 0x40*slot`). Com o monstro do jogador em LOD máximo (2260 triângulos) a pose sai coerente, sem juntas quebradas visíveis.
- **LOD automático** (`collect(ram)` com `maxLod < 0`, o padrão): escolhe o maior índice cujo `switchOutDis` (LOD node `+0x20 + 16*i + 4`) é maior que a distância ao quadrado do centro do LOD (`+0x10`, transformado pela matriz corrente) ao olho da câmera do jogo; nenhum -> não desenha (igual a `hierLod`, `src/common/hier.cpp`). `viewer --lod n` ainda força um índice; `L` cicla auto/0..3.
- **Paletas de monstros**: o jogo empilha os `.RTX` na ordem de carga (nível, jogadores, IAs) e `texmResInit` guarda em `fileStatus` (0x445390) `names[i][9]` (+0xB1; "central", "lizard0", ...) e `maxTexAddr[i]` (u16, +0x14C; fim das paletas do arquivo `i`, em blocos). A base do nível é `tempVramTexAddr >> 6` (6753) e a do monstro `i` é `maxTexAddr[i-1]` (7298 para o 1º monstro, +36 para o seguinte). `Scene::loadResources(ram, raiz)` lê `LVL/<nome>.RTX` e `MON/<nome>.RTX` e indexa as paletas por **CBP absoluto** (`rtxPal`); é idempotente (assinatura de nomes+bases). O `viewer` usa `disc/` por padrão (`--disc <dir>`); `--rtx` força um RTX de nível. Isso substituiu a escolha por cobertura (`chooseRtx`) do runtime vivo.
- Ainda não confirmado: o significado do bit `0x80` e do código de `z` (o desenho ignora os dois); o nó skinned com `fade` do LOD (a transição entre LODs não é reproduzida).

**Correcoes visuais (2026-10-10, noite):** (a) **ADC do GIF** (STMASK + S-8 sob mascara) pula so o triangulo que termina naquele vertice; a tira continua (novo strip = ADC nos 2 primeiros vertices). Tratar como reinicio perdia 1 triangulo por reinicio (furos no monstro: 2260 -> 2618 triangulos; ceu rasgado). `tools/ngp.py` ainda tem o modelo antigo (`k in adc` abre strip novo). (b) **Alfa**: shader com `discard` abaixo de ~6% de alfa (sombras, helices, aneis; as texturas ja trazem alfa 0). (c) **zBufferFudge** (`HierObject +0x28`, ~1,0; 1,002..1,25 em camadas coplanares) = multiplicador da profundidade: o nativo escala o vertice ao longo do raio do olho (direcao assumida: >1 = mais longe; conferir ao vivo). (d) UV de predios vai a +-8 (limite do int16): repeticao nas paredes e intencional; UV do monstro fica em [-1,0], sinal ainda por conferir contra o PCSX2. (e) O teclado no runtime so valia sem gamepad (`ps2_pad.cpp`, fora do repo): agora e somado.

**Falta**: (1) ~~implementar no C++~~ feito (acima); (2) ~~seleção de LOD por distância~~ feito; (3) confirmar o significado do bit 0x80 e do código de z (olhar o microcódigo do VU1: está em RAM/`hierLoadVu1Ucode`, ou dumpar a memória de microprograma do runtime); (4) ver ao vivo (`run_live.ps1`) se os monstros aparecem no lugar certo (a câmera do jogo ficou colada em prédios nos testes, o monstro do jogador não estava visível); (5) destrutíveis (estado atual), partículas, HUD 2D.
- **UV conferido contra o retail no PCSX2 (2026-10-10):** `int16/4096` normalizado pelo tamanho da textura está CERTO para prédios (grades de janelas limpas como no retail). Testei UV em texels 12.4 (`texel = uv/16`) e vira ruído fino; a razão `|grad u|/|grad v|` ≈ 1,0 até em texturas 128x64 (`WOTM_UVISO=1 viewer ...`) só mostra que elas são arte quadrada comprimida, não UV em texels. O céu do retail também é silhueta escura chapada (igual ao nosso); só a faixa azul fina no horizonte do nosso não existe no retail.
- **Ainda errado no monstro (pendente):** o atlas do monstro (256x256 T8, ex. texId 315/327) tem faixas amarelas nas bordas esquerda/direita (espinhos/cristas), mas no nosso render o monstro sai verde uniforme e sem os espinhos que o retail mostra nos ombros, costas e cauda. UV `[-0.99, 0]` + REPEAT equivale a `[0, 1]`, então o sinal não explica; suspeitas: geometria dos espinhos ausente (outros nós/LOD), `CLAMP`/região do GS não lido (o `gsTexCtx` só traz TEX0/TEX1/MIP), ou textura/paleta do monstro. Comparar com o retail em pose parecida.
- **Menu (2026-10-10, recomeço pelo menu):** o `.RTX` do menu está em `SHELL/SHELL.RTX` (não em `LVL/`); `loadResources` agora tenta `<raiz>/<nome>/<nome>.RTX` quando `LVL/` não tem. Sem isso o menu saía cinza. Com ele aparecem céu, torre de água, cercas, carros e cartazes. Pendências vistas nos dumps do menu (`PS2X_DUMP_RAM="420,540,700,900"`, quadros do menu): telas do drive-in brancas (conteúdo dinâmico/vídeo), texto de fonte branco com contorno preto (o retail usa cor de vértice laranja), logo e 'player 1' sem cor, e ruído azul/preto nas estruturas de madeira (palette/alfa de alguma textura). Dumps e viewer: `viewer ram_<n>.bin --shot x.png`. Retail para comparar: `tools/pcsx2_auto` (`emu.ps1 start retail`; copie `disc/SCUS_971.97` para `build/pcsx2_test/SCUS_971.97_retail.elf`).
- **Texturas PSMT8H/4HL/4HH do menu (2026-10-10):** o TEX0 do objeto traz `psm` 0x1B/0x24/0x2C (telas do drive-in, fontes, logos, retratos 512x512). `texInfo[id]` **não** aponta para o pixel certo (RAM reaproveitada; as métricas de vizinhança em índices enganam). Os pixels estão no `.RTX` do disco: entradas com cabeçalho de 0x80 B (GS desc em `+8`: psm bits 32..37, TW/TH, DBP no meia-palavra `+0xA`) seguido de índices de 8 bits (ou 4 empacotados), tamanhos `0x1080` (64x64 T8H), `0x880` (4HL/4HH) e `0x40080` (512x512). **`TEX0.TBP0 == DBP` da entrada e mesmo psm** (4HL e 4HH repartem o DBP: são os dois nibbles do mesmo byte). `loadRtx` guarda em `rtxImg[(psm<<16)|DBP]`; `decodeTex` usa a paleta por CBP como nas outras. Textura `180` (128x128, tbp0 4096) não está no `SHELL.RTX`: vem de outro arquivo (SHELL2/UI/LOAD, ainda não carregado). Ainda ruidosos: carros e estruturas de madeira.
- **Ruído restante do menu (2026-10-10):** a treliça/estrutura de madeira do drive-in é o `texId` 166 (64x64 T8, `TCC=1`): RGB de madeira escura coerente e **alfa pontilhado** (1540 de 4096 texels em 0). O alfa é da paleta e vale (`TCC=1`), então o pontilhado provavelmente é original (o GS o suaviza com bilinear/mipmap, e o fundo atrás é diferente do nosso); as 185 texturas T8/T4 da RAM são idênticas às do disco. Carros escuros e granulados podem ser mapas de reflexo. Sem comparação pixel a pixel com o retail no mesmo estado, não dá para afirmar que há erro. Ferramentas: `WOTM_ONLYTEX=9,53 viewer ...` desenha só esses texId (bisseção); `WOTM_UVISO=1`; `--texdump <prefixo>`.
- **Recompilar o runtime depois de mexer em `native/`:** o ninja NÃO rastreia `native/wotm_native.hpp` (fica fora da árvore do PS2Recomp). Para o jogo ao vivo pegar a mudança, toque o `ps2_runtime.cpp` e compile: no PowerShell, em `wotm-recomp-win`: `(Get-Item PS2Recomp\ps2xRuntime\src\lib\ps2_runtime.cpp).LastWriteTime = Get-Date; cmd /c .uild_dev.bat` (~1,5 min; saída em `build_dev.log`, `ninja_exit=0` = ok). Sem o toque ele recompila só o que mudou na árvore e o jogo continua com o desenho antigo (menu cinza). `loadResources` agora também tenta `SHELL/<nome>.RTX` (LOAD, UI, SHELL2, PRESHELL).
- Arquivos de análise (fora do repo, scratchpad da sessão): `exp_skin.py` (wireframe por hipótese), `exp_w.py` (métrica de arestas).

## Onde estamos
- `sh tools/wsl/gate.sh` diz `ROM OK` (build + SHA1). Último estado medido (`python3 tools/progress.py`): `game` ~170 de 3181 funções
  decompiladas (idênticas + equivalentes), ~14 KB de 930 KB; `common` 192 de 1144. Convenções em `docs/ANALYSIS.md` ("Convenções de status das funções").
- Ritmo de hoje: ~2,6 KB/hora em funções pequenas. Decompilar o `game` inteiro nesse ritmo não fecha; por isso a estratégia mudou (abaixo).
- Objetivo do port: precisa de ~1000 funções / ~310 KB (lógica de jogo alcançável pelo laço de atualização), fora hardware. Lista em `config/callgraph.csv`.

## Estratégia (decidida com o usuário em 2026-10-07)
1. **Escopo por alcance**, não por tamanho: `python3 tools/callgraph.py Update__7TheGame,InitBeforeDbLoad__7TheGame,InitAfterDbLoad__7TheGame,ResetLevel__7TheGame --no-libs`
   grava `config/callgraph.csv` (função, TU, tamanho, profundidade). Só segue `jal`; chamadas virtuais (`jalr`) não entram, então é um piso.
   Trabalhar de cima para baixo nessa lista. Hardware (`config/hw_funcs.txt`) fica de fora; `game/Sound` e `game/StreamingSoundManager` NÃO são hardware
   (decisões de jogo) mas o port pode começar com áudio mudo, então são baixa prioridade.
2. **Equivalente natural, sem afinar**: escrever C++ natural, pontuar, embrulhar com `nm_wrap.py` se não bater. Só afinar quando é barato (ver armadilhas).
3. **m2c como rascunho** para funções com mais de ~30 instruções: `sh tools/m2c.sh <tu> <função>`. Conferir contra o assembly, trocar `unkNNN` por campos
   nomeados dos headers, ajustar tipos. Funções curtas: escrever direto. Nunca confiar no m2c sem ler o asm (os argumentos que ele mostra são ruído de registradores).
4. **Comparar comportamento com o PCSX2** assim que houver algo rodando (ainda sem testes; decisão do usuário).
5. **Unificar tipos conforme os usos se repetem**: promover campo a header só quando há evidência (mesmo offset, mesmo uso) em 2+ lugares ou o m2c/asm deixa claro.

## Como retomar
1. `wsl -d Ubuntu` (a distro padrão é a `docker-desktop`, que não serve). Venv: `~/.venvs/wotm/bin/python`. Raiz: `/mnt/c/Users/TwistZero/WoTM`.
2. **Gate de commit**: `sh tools/wsl/gate.sh && git commit ...` (de preferência o `git commit` rodando no Windows; o git do WSL não tem identidade).
   `check.sh; git commit` ou `check.sh | tail && git commit` commitam builds quebrados.
3. Converter TUs novos: `sh tools/wsl/convert_tus.sh TuA TuB ...` (um de cada vez, só mantém os que não quebram a ROM, posiciona vtables com `place_data.py`).
   Depois de um TU removido/revertido, regenerar com `~/.venvs/wotm/bin/python configure.py --split` (senão o `build.ninja` aponta para arquivo inexistente).
4. Por função: ler `asm/nonmatchings/<tu>/<func>.s` (ou rodar o m2c), escrever o C++ no lugar da linha `INCLUDE_ASM`,
   `sh tools/wsl/scoreall.sh <TU...>` (pontua com `-DNON_MATCHING`), `nm_wrap.py <arquivo> <simbolo> "<nota>" [Classe::metodo]` se não bater.
5. Antes de mexer em header compartilhado: `sh tools/wsl/scoreall.sh <todos os TUs de src/game> > ~/base.txt`; depois comparar com `diff`. Rodar `gate.sh`.

## Ferramentas (tools/)
- `wsl/gate.sh` (build + SHA1, sai com erro se falhar), `wsl/scoreall.sh` (pontuação por função com NON_MATCHING), `wsl/convert_tus.sh`, `wsl/check.sh`,
  `ccmatch.py`, `nm_wrap.py`, `new_tu.py`, `place_data.py`, `progress.py` (escreve `config/status.csv`), `callgraph.py`, `m2c.sh`, `wsl/variants.py`, `wsl/permute.py`.
- Scripts `.sh` devem ser criados por heredoc no bash; gravar com Python no Windows deixa CRLF e o `sh` do WSL quebra (`sed -i 's/\r$//'` conserta).
- README automático: `tools/update_readme.py` (só stdlib, lê `config/status.csv`) reescreve o bloco entre `<!-- PROGRESS:START -->` e `<!-- PROGRESS:END -->` do `README.md` e o `docs/progress.svg`. Hook versionado `.githooks/pre-commit` roda isso e dá `git add`; ativar uma vez por clone com `git config core.hooksPath .githooks`. O hook não roda `progress.py` (leva ~12 s via WSL e precisa de `disc/`): rodar `python3 tools/progress.py` antes de commitar, ou `WOTM_REFRESH=1 git commit ...` para o hook atualizar o `status.csv`. Nunca bloqueia o commit.
- Mod `/wotm` (HUD) carrega com `startup_command.bat` (no `.gitignore`), que inicia o Claude com `--plugin-dir ~/.claude/my-plugins/wotm-hud`.

## Headers compartilhados (include/)
Regra: uma classe/API usada por mais de um TU mora num header; não redeclarar parcialmente dentro do `.cpp`.
- `engine.h`: API do motor (matemática, timers, `_animHandle`/animation*, particleKillFx, hier/hd). `game/game.h` inclui.
- `game/game.h`: `Monster` (0x11190 bytes: `m_cs`, `m_playerNum`, `m_id`, `m_health`, `m_stamina`, `m_target`, `m_camUnify`), `TheGame`
  (`m_huds[4]`, `m_slots[16]` = os `Monster` em 0xB80, `m_monsters[]` = ponteiros, `m_gravity`, `m_gameMode`, `m_matchMode`, `m_levelId` (1 central, 2 vegas, 3 canyon2,
  5 airport, 6 threemile, 7 sanfran, 8/15 island, 9 tokyo, 10 ufo, 11 final boss, 26 bigshot, 27 crush), `m_numSlots`, `m_numMonsters`, `m_levelIdx`, `m_playerMask`),
  `Cameras` (`m_cameras`, `m_state`), acessores `gameSlotBase(idx)` (mantém a ordem `idx*0x11190 + 0xB80`), `gameHud(i)`, `gameWeapons()`.
- `point_tool_kit.h` (base de PathTool/PowerUpTool/StartPointTool/AiPathTool; derivados chamam `PointToolKit::init/loadPoints/getPoint`), `task_manager.h`, `bidir_link.h`,
  `cs_pool.h` (tudo estático), `game/{shell,hit_history,pickup,military_pickup,pickup_sound,vehicle_navigator,hud,weapons,power_ups,start_points,stamina_meter,crush_level,levels,streaming_sound}.h`.
- Mover uma classe para header pode mudar `sizeof` e deslocar campos de structs parciais que a embutem (aconteceu com `StaminaMeter`): sempre comparar antes/depois.
- Funções que o retail chama com `this` mesmo sem usá-lo (ex.: `StartPoints::getNumPoints`) só batem se declaradas não-estáticas; `isThisTypeFull` é estática.
- Ainda não unificados: `GamePad` (`GamePad.cpp` vê 6 ints, `GamePadClipPlayer.cpp` vê bytes), `GamePadClipPlayer`/`AiPadClips` (tipo do clipe diverge entre `AiGrapple` e os outros),
  `Ai`, `Destructibles`.

## Trabalho em andamento: TheGame
- `src/game/TheGame.cpp` convertido. `TheGame::Update` está escrito (embrulhado em `NON_MATCHING`, 23/252 palavras, equivalente a partir do m2c); falta conferir contra o comportamento.
- Próximos dentro de `TheGame`: `Update2`, `UpdatePadTweaks`, `SetGravity`, `GetNumAIsAlive`, `SetOkToUnify`, `InitAfterDbLoad`, `InitBeforeDbLoad`, `ResetLevel`, `gameResolveLifeAndDeath` (717 linhas), `gameCheckForCloseCombat`.
- Depois, descendo a árvore: `Monster::update`/`updatePosition`/`updateCinema` (TU `Monster`, 53 funções / ~18 KB), `AiNavigator`, uma fase completa (`tokyo`, já convertida), `PlantBoss`/`FinalBoss`.

## Teste diferencial (tools/difftest.py, tools/difftest_all.py)
Roda a função retail e a nossa (compilada com `-DNON_MATCHING`) num emulador MIPS (unicorn), com o mesmo estado aleatório, e compara retorno, chamadas a outras funções
(os callees viram `jr ra`; nome + argumentos relevantes, com strings comparadas pelo texto e ponteiros de função pelo nome), bytes alterados na arena e nos dados do retail e as variáveis
que o próprio TU define (alias para a cópia do retail). Serve para validar as funções "equivalentes (untuned)" sem PCSX2.
- Uma função: `~/.venvs/wotm/bin/python tools/difftest.py game/Monster isFullHealth__7Monster --args this --ret int --runs 60 [--objsize 0x122000]`
  (`--args` aceita `this,p,i,b,f`; `--alt OUTRAFUNC` é o controle negativo, deve dar MISMATCH; `--objsize` mantém ponteiros aleatórios fora do objeto, use 0x122000 para `TheGame`).
- Todas as equivalentes de TUs: `python3 tools/difftest_all.py game/Monster game/TheGame ... --runs 30` (adivinha a assinatura pelo nome mangled e o retorno pela definição).
- Resultado de 2026-10-07: ~95 funções equivalentes concordam; o teste achou e corrigimos 7 erros reais (Vehicle::setCs/setPos, StaminaMeter::credit, UpdatePadTweaks, TheGame::Update (2º TaskManager),
  MonkeyChains::init (FxTextureId é enum), AnimContact).
- Limites: não executa VU0/COP2 nem `min.s`/`madd.s`; funções com métodos virtuais ou estruturas encadeadas válidas são puladas ("skipped"); mais de 4 args inteiros ou 2 floats não são suportados;
  é evidência (só os caminhos que o estado aleatório alcança), não prova. Rodar em toda função equivalente nova antes de commitar.
- Armadilha do emulador: hook de escrita de memória quebra saltos com store no delay slot; por isso comparamos imagens de memória no fim, não escritas.

## Armadilhas já vistas
- Ao reescrever o fim de um `.cpp` com script, conferir que as linhas `INCLUDE_ASM` finais (static init, `__tf`, ctor, `_GLOBAL_$I$`) continuam lá.
- `ccmatch.py` sem `-DNON_MATCHING` só compila as `INCLUDE_ASM` (tudo "MATCH"); para pontuar o C++ novo use `-DNON_MATCHING` (o `scoreall.sh` já faz).
- Layout das ferramentas de pontos: pontos de 0x40 bytes, `numPoints` em 0x4000, campos próprios a partir de 0x4050; `levelData = gameSlotBase(game->m_levelIdx)`.
- `shell` é gp-relativo em alguns TUs (PowerUpTool, PathTool, AiPathTool) e não em outros (StartPointTool): `__asm__("#SNFIX_SMALL shell")` só onde o retail usa gp.
  Dentro de um delay slot (`.set nomacro`) o acesso a global é sempre gp, sem precisar do pragma (tokyo).
- gas insere 2 `nop` extras num `.p2align 3` logo depois de uma sequência `li.s` (StickShaker::DefaultSetup); o retail não tem. Sem causa achada: marcar como equivalente.
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
- Loops de espera por registrador de hardware (`objsInPacket`, `gRtReturn`): declarar `volatile`; sem isso o gcc vira laço infinito/hoisting.
- Ordem de stores em struct pequena pode inverter no gcc 2.95 (`rtReturnToShell`: escrever `code, active, delay` para sair `delay, active, code`).
- Teste automático: `powershell -ExecutionPolicy Bypass -File tools/pcsx2_auto/play.ps1 halfcpp [-pause]` (sem `-File`/`Bypass` o PowerShell recusa o script). Só exercita 1 jogador/free-for-all; telas de 2P, elimination, minigames e história não são cobertas.

## Testar o C++ novo no PCSX2 (meio asm, meio C++)
- O ROM do build normal é idêntico ao retail (equivalentes ficam como `INCLUDE_ASM`). Para rodar as equivalentes: `sh tools/wsl/build_nm.sh` (WSL) compila uma cópia em `~/wotm_nm`
  com `-DNON_MATCHING` e grava `build/pcsx2_test/SCUS_971.97_halfcpp.elf` e `..._control_matching.elf` (controle). Ambos com `p_paddr = p_vaddr` (o PCSX2 carrega por `p_paddr`).
- Rodar: `pcsx2-qt.exe -elf build\pcsx2_test\SCUS_971.97_halfcpp.elf -- "ISO\SCUS_971.97.War of the Monsters.iso"`. Dá para automatizar: `-batch -nogui -logfile <log>` e matar depois de ~30 s;
  o log mostra `microVU1: Cached Prog`, `FMV started` etc. quando o jogo anda, e `Vif0: Unknown VifCmd` / `microVU0: Possible infinite compiling loop` quando os dados estão errados.
- **Layout**: o linker script normal empilha as peças (`x.o(.sec)`) uma atrás da outra e usa `SUBALIGN(4)`, então qualquer função equivalente maior/menor desloca todos os dados que vêm depois
  (248/252/280 bytes) e os buffers de DMA ficam desalinhados: foi o que quebrou o primeiro teste. `tools/gen_nm_ld.py` gera um script onde toda peça que não cresceu fica no endereço retail
  (`. = <endereço - base da seção>`; `.cod_bss` fixo em seu endereço) e as peças que cresceram (13, quase todas `.text`) vão para `.nm_extra`, depois do bss.
- `CrushLevel` é compilado com `-G0` no build NM (as strings do código novo cairiam em `.sdata`, onde não há espaço). `Monster.cpp`: `cloaker` é o símbolo `cloaker.2691`.
- Resultado até agora: o halfcpp passa do boot e chega à FMV de abertura sem erro de VIF. Falta testar menu/fase.

### Bisseção do ELF com C++ novo (estado em 2026-10-08, ~02:00)
- Layout corrigido (gen_nm_ld.py); o halfcpp completo chega à fase mas trava/crasha. Resultados com variantes (build_nm.sh):
  `v3_game` (TheGame, MonsterMeters, StartPoints, AiGrapple, AiBrain, island, DodgeBall, CrushLevel) funciona; `v1_monster` e `a_update` (só `Monster::update`) travam no loading da fase
  (`Unrecognized op` no log, executando dados) -> **`Monster::update` tem bug**; `b_cinema`, `c_rest`, `v4_misc` ainda não testados pelo usuário.
- **Atualização 2026-10-08 (manhã)**: o erro `Jump to unaligned address (PC: 1)` do menu (`v2_common`, `bsA`, `t_hieri`) NÃO reproduz mais: refeitos com o `gen_nm_ld.py` atual,
  `t_hieri`, `h_flush`, `h_set`, `h_dma`, `t_zip`, `t_TaskManager`, `bsA` e o `halfcpp` completo passam da abertura/FMV sem `ReportErrorAsync` (45-60 s, 3x estável em `t_hieri`);
  os ELFs antigos eram de antes das correções de layout. Falta só o crash no loading da fase = `Monster::update` (não dá para automatizar: precisa de input no menu).
  Achado de build: `hierDmaHandler` chamava `hierFlushObjQ` sem protótipo quando só ela era `NON_MATCHING` (variante `h_dma` não linkava); protótipo adicionado em `hieri.cpp`.
  Script de teste automático: abrir `pcsx2-qt.exe -batch -nogui -logfile <log> -elf <elf> -- <iso>`, esperar até `ReportErrorAsync` ou 45 s, `taskkill`.
- **RESOLVIDO (2026-10-08)**: o crash no loading da fase NÃO era bug do `Monster::update` (as duas correções, `sb` em `m_x6874+0xC` e `f5C` como byte, continuam válidas; o difftest dá
  144/150 iguais, 0 divergências, e agora também confere os registradores callee-saved). A causa era **layout de memória**: o `.nm_extra` (peças que cresceram) ficava logo depois do bss, onde começa o
  heap do jogo, e depois de carregar uma fase a RAM de 32 MB inteira está em uso (nenhuma região livre acima de 0xA00000 nem em 0x1800000: DMA de pacotes sobrescreveu o início de `takeHit`).
  Correção: `patch_paddr.py --heap` empurra o início do heap para depois do `.nm_extra`, em **dois** lugares: a chamada `SetupHeap` do crt0 e a palavra `heap_ptr` de `libkernl/glue.data` (0x6E5D2C,
  o `_end` do malloc do SDK); o laço que zera o bss mantém o fim antigo. `build_nm.sh` passa `--heap` sozinho.
- **Resultado**: `halfcpp` (todas as equivalentes compiladas) entra na fase e roda uma partida inteira no PCSX2 (o CPU venceu o jogador parado: "AI WINS"). `a_update` e `m_noupd` também passam.
  `tools/pcsx2_auto/` (ver README) automatiza isso: `powershell tools/pcsx2_auto/play.ps1 halfcpp 60` troca os bindings por teclado, joga o menu até a fase, vigia o log e restaura o `.ini` com `emu.ps1 stop`.
  Próximo: testar as variantes `b_cinema`, `c_rest`, `v4_misc` (já cobertas pelo `halfcpp`), jogar de verdade (mover o monstro, ataques, outras fases) e comparar comportamento com o retail.
- difftest ganhou `--init '<python>'` (W/W16/W8/THIS/ARENA/STUB) para montar estado estruturado; `Monster::update` precisa de `m_state` -> objeto com vtable (`W(THIS+0x34,S); W(S+0x10,VT); W16(VT+0x18,0); W(VT+0x1C,STUB)`),
  mas ainda cai em escritas fora do mapa (retail e alt), falta ajustar mais ponteiros.
- O log do PCSX2 do usuário fica em `~/Documents/PCSX2/logs/emulog.txt`; erros em diálogo também aparecem lá como `ReportErrorAsync`.

### Bugs de comportamento do halfcpp achados jogando (2026-10-08)
Teste do usuário no halfcpp: veículos do chão sem glow verde e ◯ não pega; pedestres andando em fila; alguns prédios atravessáveis. Achados e corrigidos lendo as equivalentes contra o asm:
- **`PathNode` tinha 0x40 bytes, o retail usa 0x30** (links de 4 bytes a partir de 0x12, são 7 e não 9): `PathNet::getRandNode/getClosestNode` indexavam nós errados -> pedestres (e provavelmente os carros, que também seguem a PathNet) em fila. Agora `getRandNode` dá MATCH; há um `typedef char PathNodeSizeCheck[...]`.
- `AiBrain`: a classe base vazia `AiActionGroup` ocupa 1 byte no gcc 2.95 e empurrava `f_AAC` para 0xAB0 (retail 0xAAC). Sem herança agora (`AiBrain::reset` virou MATCH).
- `AiGrappleAttack::updateAction`: com `matchMode` 0/1 o retail sorteia `heavyPunch` (o nosso chamava `toss`).
- `Monster::isIdle`: último teste invertido (retail retorna 1 quando `t < state[2]`); só a câmera usa.
- **Lição**: "untuned" com muitas palavras diferentes pode esconder erro de layout/semântica (o difftest não pegou nenhum destes: estado aleatório, callees stubados). Ferramentas novas: `tools/nm_audit.py` (offsets e imediatos por função, ELF NM x retail; muito ruído de gp x lui), `tools/nm_calls.py` (sequência de chamadas; achou o `toss` extra) e `tools/ramdiff.py` (compara dois dumps de RAM do EE; com `NM_HEAP=0x8E0000 sh tools/wsl/build_nm.sh ...` o heap fica no mesmo lugar nos dois builds e os endereços dos objetos coincidem; dump por `play.ps1 <elf> <n> <arquivo>`).
- Comparação de RAM retail x halfcpp na fase (mesmo ponto): listas de pickups, `Interactives`, `ColGrid` (381 nós usados nos dois) e pools estão iguais; então o setup do nível está certo e o defeito do glow/pegar veículo é dinâmico. **Ainda não explicado**: glow/pegar veículo e prédios atravessáveis (retestar com o halfcpp novo; se persistir, reproduzir andando até um carro e ler `s_highlightPickup` na RAM).
- **Causa do glow/pegar pickup (achada com o dump do usuário, 2026-10-08)**: `Interactives::addInteractive` foi escrita como `void`, mas os callers do retail (`CarPickup::initAfterDbLoad`, `HeliPickup`, `Destructible`...) usam o `$v0` (o índice do slot) como id no `hierhead` (`(v0 & 0x7FF) << 7`). O `$v0` que sobrava era índice+1, então todo `getInteractive(idx)` devolvia o vizinho (no retail idx == posição no array nos 118 pickups; no halfcpp, +1 em 117). Isso quebra `getClosestPickup` (glow e ◯), a colisão da bola devolvida e provavelmente o dano contínuo em prédios (todos usam `getInteractive`). Agora `int addInteractive` devolve o slot. `tools/nm_retvals.py` varre as equivalentes `void` cujo valor de retorno é consumido por callers do retail (nenhum outro caso).
- Como achar isso de novo: dump da RAM com o jogo pausado (`ramdump.py`, pega a cópia da RAM com `game` != 0), `pick.py`/`cs.py`/`idx.py` (scratchpad) reproduzem a conta do `getClosestPickup` a partir do dump.

### Decomp por alcance (2026-10-08 tarde)
- Estado (`tools/progress.py`): `game` 466/3181 funções (28,4 KB de 930 KB), `common` 192/1144 (23,7 KB de 258 KB). Candidatas do `callgraph.csv` sem VU0 e com até 260 bytes: ~105 (AiNavigator 22, Pickup 12, Cameras 9, AiPathFinder 5...); o restante é VU0 (`lqc2`/`vsub`...), que exige `asm volatile` (ver `include/vecmath.h`) e não é coberto pelo difftest.
- `game/Pickup` convertido (27 funções: getters/setters, `drop`, `setVisualState`, `regenUpdate`, `getVel` x2, `initAfterDbLoad`, `update` batendo; `regen` equivalente). Truques: `(int)(bits >> n) & 1` gera o `dsll/dsra32` do retail; para testar um bit de `unsigned long long` num `if`, guardar `bits & m` num `unsigned long long` local (senão vem `dsll32/dsra32` extra); `lq/sq` por `asm volatile` com `$2` fixo.
- `game/AiNavigator`: layout (`monster` 0x0, `turn` 0xC, `strafe` 0x14, `mode` 0x18, `status` 0x1C, `ObstacleSensor` 0x30 (0x130 bytes), `AiPathFinder` 0x160 (0x28), alvo 0x188..0x19C, `PathInfo` 0x220/0x240 (0x20 cada)); 14 funções batendo (ctor, getters do sensor, wander/target/disable, seek/arrive por `DbInteractive`...) e 6 equivalentes (flee, init, orientTo, strafeTo, targetPin, updateTarget). `fabsf` (extern "C") gera `abs.s`; `max.s`/`rsqrt.s` só por `asm`; `permute.py` precisa de N <= nº de ordens possíveis (senão trava).
- Próximo: restante do AiNavigator (`PathInfo::init` x3, `nextPathPoint`, `seek`/`arrive` por vetor, `tag`, `updateFlee`, `isObstacleClimbable`...), depois `Cameras` e `AiPathFinder`. Validar sempre com o halfcpp (`play.ps1`) e jogando.

### Cadeia de carga: da `main` ao monstro no mapa (2026-10-08 noite)
O recorte por alcance agora segue a espinha real (`tools/callgraph.py main --no-libs --depth 4`; restaurar `config/callgraph.csv` depois, ele é sobrescrito). Feito e testado no halfcpp (a fase carrega e a partida roda):
`Shell::LoadLevelFiles` -> `LoadLevelDB`, `LoadMonstersDB`, `LoadResTexture`, `LoadTexture` -> `dbsRelocateFileZero` (`.PTR`, TBP0) -> `dbInitDb` -> `dbsRelocateViaPtrListFile` (`mon/<nome>.ptr`) -> `dbsTraverse` (+ `dbsPush`/`dbsPop`) -> `dbProcInteractive` (tabela de ids de objeto). Formatos e tabela de ids em `docs/FORMATOS.md`.
**Falta na espinha**: `TheGame::MonsterParse`, `AddMonster`, `GetMonsterFromName`, `SetPlayerMonster`/`SetAIMonster` (rascunhos m2c lidos: criam o slot, registram em `Interactives`, `playerInit`), `Monster::playerInit`/`initAfterDbLoad` (TU `MonsterInit`), `Shell::InitPlayers` (2468 bytes), `main` (1344), `rtMain` (laço de render: `hier`, `animation*`, `particleDraw`, `viewUpdate`). As funções `file*` e `zip*` são camada de plataforma (o port lê arquivos direto). Todas as novas estão como equivalentes (`NON_MATCHING`); a validação é o halfcpp (`tools/pcsx2_auto/play.ps1 halfcpp 15 <dump>`; `idx.py` confere que ids e índices de `Interactives` batem).

### Espinha concluída até a criação dos monstros (2026-10-08, madrugada)
Agora em C++ (equivalentes, validados no halfcpp: boot -> menu -> fase com jogador e IA em cena): `main`, `Shell::InitPlayers`, `TheGame::MonsterParse/AddMonster/GetMonsterFromName/SetPlayerMonster/SetAIMonster`, `Monster::initBeforeDbLoad/playerInit/aiInit/initAfterDbLoad`, mais toda a cadeia de carga (ver acima). Fluxo de `main` (ver o comentário em `src/game/Shell.cpp`): boot -> intro/outro -> menus (`userintMain`) -> sessão: `InitRTState`, `BootInitGame`, `InitBeforeDbLoad`, `LoadLevelFiles`, `FinishLoadBar`, `FadeScreen`, `InitAfterDbLoad`, `InitPlayers`, `initAfter` dos 4 `Hud`, init por modo (BigShot/Crush/DodgeBall), `UpdatePadTweaks`, sons, `rtMain` em laço com `EvaluateGameStatus`.
**Falta na espinha**: `rtMain` (2120 bytes; laço de frame: timer, input, `TheGame::Update/Update2`, `hier`/`view`/`particle` draw, VU1), `Shell::EvaluateGameStatus` + `Evaluate*Status` (regras de vitória por modo), `Shell::BootInit*`/`InitRTState`/`FinishLoadBar`/`FadeScreen`, `uiMain`/`userintMain`/`screen*` (menus; o port pode trocar por UI própria), `Monster::init` (816) e `initDynamics` (4444: parâmetros de física do monstro), `animation*` e a hierarquia de desenho (`hier__Fii` 1324, `hierTraverse`, `viewUpdate`), `Cameras`. Armadilha nova: ao trocar INCLUDE_ASM por C++, manter a ORDEM original das funções e pôr helpers `static` dentro de `#ifdef NON_MATCHING` (senão a ROM muda).

### Menu (`game/ui`, `game/screen`) — o que já se sabe (2026-10-08)
- As telas de `screen*` usam `screenGetInput(1)`: devolve `(pad << 16) | ação`; ações 1/2 = cima/baixo, 3/4 = esquerda/direita, 5 = voltar, 6 = confirmar. Cada tela mexe em `currentSelection[currScreen]` e troca `hierSetSwitch` do shell.
- Modos de jogo gravados em `shell->m_mode` (e `shell+0x2B54`, o modo "padrão" restaurado por `resetGameMode`): 3 = free-for-all 2P (`PO_MPFreeForAll`), 6 = elimination 2P (`PO_MPElimination`), 7/8/9 = minigames das fases 25/26/27.
- Fases 10, 11, 14, 15, 25, 26 e 27 só abrem com a flag da tabela `monsterSelectMode` (`levelUnlocked`). O monstro bônus alarga a faixa selecionável de 1..9 para até 10 (`findAvailableMonster`).
- Equivalentes escritos: `ui` (uiInit/Intro/Outro, userintReturnToShell…), `screen` (seleção, rodapé de botões, FreeForAllOptions1P/2P, ElimOptions2P, GameModes2P, MGSelect). Faltam as telas grandes `screenCharSelect*`, `screenSelect1AI`, `screenMinigames2P`, `screenMain`, `screenCtlrCfg`.

### Laço de jogo (`game/rt`) (2026-10-08)
- `rtMain(first)` (equivalente) é o laço por quadro: `startFrame` → input → por view: `CullView`, double buffer GS, `viewUpdate`, HUD, `hier(view,0)`, `TheGame::Update` (só view 0), `animationRunGlobal`, `Update2` (view 1 ou única), partículas, `TaskManager(debris)`, `hier(view,1)`, DMA; no fim do quadro `updateLevelObjectSoundManager`, `updateSoundManager`, `g_frame++` e o pacing de tempo. Retorna o código dado a `rtReturnToShell` (2 = pausa/diálogo, 3 = sair, 5 = encerrar sessão, 0/1/4 = fim de fase).
- `rtPauseRT` (START ou controle desconectado → `rtReturnToShell(2, bit do campo GS)`) está equivalente; `play.ps1 halfcpp -pause` aperta START no jogo e fotografa o diálogo (CONTINUE/RESTART/…/QUIT).
- Números dos códigos de retorno vêm de `Shell::EvaluateGameStatus` e dos `Evaluate*Status` (história, desafio, FFA com/sem IA, endurance, bigshot, crush, dodgeball escritos; falta `EvaluateMultiPlayerBattleStatusNoAI`).

### Observações de jogo ainda sem causa (2026-10-08, `halfcpp` com `takeHit` equivalente)
- A IA às vezes repete a mesma ação sem parar (pode ser comportamento do jogo original).
- Dois casos de "teleporte" depois de um golpe forte que arremessa o monstro (um no jogador, um na IA): o último `HitEvent` era tipo 3 / subtipo 30, tratado só com dano (igual ao retail); a causa pode ser o knockback/física fora da `takeHit`. Não confirmado; comparar com `bis_notakehit` (`jogar.bat bis_notakehit`) se voltar a incomodar.
- Decisão do projeto: primeiro ter código suficiente (equivalente) para um port reproduzir o jogo; acertar byte a byte e corrigir esses detalhes vem depois.

### Roteiro até ter código para um port (2026-10-08)
Prioridade combinada: cobrir o jogo com código equivalente antes de tentar casar byte a byte. Blocos grandes que ainda são só asm (nº de funções): `MonsterStates` 266, `SpecialStates` 151, `MovementStates` 99 (TU já convertido; só getters feitos), `AiReflex` 147, `AiSeek` 99, `Weapon`/projéteis (~25, `DetonateWeapon` 0x1220), `Hud` (update/print), `Cameras::Update`, `Sound`/`StreamingSoundManager`/`MonsterMc`/`McPage`/`McFile` (hardware: o port troca por áudio/save próprios, só interessa o contrato).
- **Estados de monstro**: base em `include/game/monster_state.h` (`id`@0, `flags`@4, `owner`@0xC, `vptr`@0x10; vtable `{delta, 0, função}`; ctor e vtable ficam asm, métodos sem `virtual`). Cada estado vive embutido em `Monster` num offset fixo (Recoil 0x7E30, Block 0x7DA0, Stunned 0x10714, Shocked 0x10BA0, Grappled 0xDDCC, vitória 0x10E70, estado especial 0x7980). Ordem sugerida: `transitionOK/transitionFeasible/getRelevantConfig` (pequenos) -> `update` de cada estado -> `transitionInto/handleCollis`.
- **IA**: `AiReflex` e `AiSeek` dependem de `AiBrain`/`AiNavigator` (já em C++); a navegação (A*, `seek/arrive/tag`) já está.
- Para cada função: `m2c`, escrever com offsets crus, `scoreall.sh`, depois `tools/nm_calls.py` (compara a ordem das chamadas com o retail) antes de testar jogando.


## Notas da sessão da IA (AiAction / Ai / AiReflex / AiSeek)

- **Estrutura**: `include/game/ai.h` (classe `Ai`, campos já vistos), `ai_action.h` (`AiActionTuple`/`AiActionList`/`AiActionGroup`, chamadas virtuais à mão via `AI_VENT`/`VCALL_F`/`VCALL_V`, vptr em 0x44 nas tuplas e 0x84 nas listas) e `ai_support.h` (macros `MI/MF/MB/MP`, `NAV(ai)` = `AiNavigator` embutido em `Ai+0x80`, `FMAX/FMIN`, `StateButtSlam`, `GamePadClipPlayer`...). Cada ação (`AiPunchReflex`, `AiBlockReflex`...) deriva de `AiActionTuple` (0x48 bytes) e os campos próprios começam em 0x48. Ctor, vtable e `__tf` ficam em asm.
- **min.s / max.s**: o retail usa `max.s`/`min.s`, mas `-ffast-math` (única forma de o gcc 2.95 emitir isso) também reescreve todo `a < b` em `c.le` invertido. Em `Ai` e `AiAction` (`config/tu_flags.txt`) isso fecha; nos TUs com muita comparação use `FMAX`/`FMIN` (asm inline `max.s`), que não mexe nos outros compares.
- **Build NM com .sdata novo**: constantes float do código equivalente caem em `.sdata`; TUs cujo retail não tem `.sdata` precisam de `-G0` no build NM (`tools/wsl/build_nm.sh`, lista `for t in game/AiAction`). Sintoma: `defined in discarded section .sdata`.
- **Heredoc de Python**: nesta ferramenta uma barra invertida dupla dentro de heredoc vira uma só, e sequências como barra-1 ou barra-r viram caracteres de controle. Para scripts com regex ou `sed`, grave o arquivo com a ferramenta Write em vez de heredoc (um `build_nm.sh` ficou com `^A`/CR no meio por causa disso).
- **Fluxo usado para classes de ação**: escrever o C++ a partir do asm, `tools/wsl/scoreall.sh <TU>`; o que bate sai do `#ifdef NON_MATCHING`, o resto fica com nota `untuned: N/M words`. Funções com `switch` (jump table) ficam em NM mesmo quando batem, porque a tabela precisa vir do rodata do retail.
- **Offsets do Monster vistos de fora** (por offset cru, ainda sem campo nomeado): 0x34 estado atual (id em [0], flags em [1]: 4 = atacando, 0x10 = reação), 0x49 alvo válido, 0x4A ataques ligados, 0x280 no chão, 0x448 vida (max, cur), 0x460 stamina, 0x68A4 objeto segurado (kind em +0xA0), 0x6C34/0x6C38 botão apertado, 0x7978/0x7980 estados especiais, 0xDCE0 StateGrapple, 0xFA1C StateButtSlam.

### Onde parei na IA (2026-10-09, 00:45)
- `AiReflex`: todas as classes de ação já têm C++ equivalente (ctor, vtable e `__tf` seguem em asm).
- `AiSeek`: feitos SeekMonster, SeekPickup, SeekHealth, SeekStamina, SeekSpecial, SeekCloak, DodgeRam e DodgeStomp. **Faltam**: `AiDodgeThrow` (entry 0x264, update 0x21C, exit relevance 0x160), `AiDodgeSpecial` (`getExitRelevance`, `updateAction`; entry, enter, exit e `updateFleeSpot` já casam), `AiBatThrow`, `AiCatchThrow` e `AiSwarm` (estados, `getBestTarget`, `updateAction`).
- Depois do `AiSeek`: `SpecialStates` (151 funções) e os TUs convertidos em 2026-10-09 que ainda estão só como stub (`Grapple*`, `FinalBoss`, `PlantBoss`, `Debris`, `Destructible`, `collision`, `MonsterDynamics`, `MonsterAnimBlend`, `StateThrowBack`, `PowerUps`).
- O ELF `build/pcsx2_test/SCUS_971.97_next.elf` (todas as equivalências até aqui) ainda não foi testado em jogo.
