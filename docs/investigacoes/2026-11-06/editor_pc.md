# Editor de fase como programa de PC: exportação, stack, "testar" e edição no jogo

Pedido do dono: um editor de fases **separado do jogo**, com tema claro/escuro, que abra uma fase (CENTRAL), mostre em 3D, deixe
escolher prédios, pickups e props, mover/girar/copiar/apagar, grave o patch JSON (`MODS.md`, "Level patches") e tenha um botão
"testar". Depois: fase nova a partir de template e objetos vindos do Blender.

Marcação: **[certo]** = lido no código ou medido; **[hipótese]** = dedução não verificada.

## Resumo
1. **Dá para exportar a fase inteira para desenho, com um nó por objeto (nome = alça), em ~3 s.** A prova (`fase_glb.py`, abaixo)
   gera o CENTRAL em 2,4 s + 0,5 s do `lvl.py`, 9,0 MB, 1269 malhas, 1604 instâncias, 318 materiais, e bate triângulo a triângulo
   com o `tools/gltf.py` (71 728 triângulos visíveis nos dois, mesma caixa do mundo) [certo]. Os 331 objetos do `lvl.py objects`
   ganham nó, com os estados danificados como filhos escondidos. Texturas com paleta já saem certas (0 não achadas) [certo].
2. **Stack recomendada: Python (backend com `lvl.py` dentro do processo) + página three.js numa janela `pywebview`**, com o
   navegador padrão como reserva. É o que mais reaproveita (o `lvl.py` inteiro, `LevelPatch`/`cut_nets`/checagem de colisão, o
   exportador; o three.js tem `GLTFLoader`, raycast e `TransformControls` prontos), o tema claro/escuro sai de graça em CSS, e
   empacota como o kit de mods (PyInstaller) no Windows e no Linux. Segunda opção: PySide6.
3. **"Testar"** = gravar `mods/<mod>/levels/<FASE>.json`, rodar `lvl.py patch` como pré-checagem e abrir o `wotm.exe` com
   `WOTM_MATCH=<monstro>,<índice da fase>` (sem IA: o monstro sozinho na fase), `WOTM_FULLSCREEN=0`, `WOTM_LEVELPATCH=2`; ler as
   linhas `[fase]` do `wotm.log` de volta no editor.
4. **Edição dentro do jogo:** o patch só vale na próxima carga da fase (entrada do `dbInitDb` chamado por `Shell::LoadLevelFiles`).
   O JSON é relido do disco a cada carga, então "sair para o menu e entrar de novo" já aplica a versão nova; um atalho "recarregar
   fase" que faça isso sozinho é barato e de risco baixo. Mover ao vivo na RAM serve só como pré-visualização (o desenho muda, a
   colisão, os Destructibles, os `_cs` dos pickups e as redes de carros ficam onde estavam).

## 1. Exportar a fase para desenho

### O que já existe [certo]
- `tools/ngp.py`: `decode_mesh` (malha do `polyPkt`, a mesma do nativo), `walk` (TRANSLATE/ROTATE/SCALE, GROUP só visual, SWITCH pelo
  `whichChild` do disco, LOD por índice, CONTROL child1, CHAR_INSTANCE, ANIM_XFORM em identidade, ANIM_SCALE, INTERACTIVE_STATE e
  DESTRUCTIBLE no estado 0).
- `tools/gltf.py`: `.glb` com texturas do `.TEX`/`.RTX` (`tex.Library`, paletas T8/T4 com o CSM1), UV, cor de vértice, normais,
  material unlit. Mas **achatado**: um nó por OBJECT com a matriz do mundo já aplicada nos vértices, sem dono, sem instanciamento.
  CENTRAL: 2,9 s, 7,5 MB, 1284 objetos, 71 728 triângulos, 306 texturas.
- `tools/lvl.py`: `Stage.objects()` dá cada objeto colocável com alça, posição, rumo, estados e caminhos de colisão; `path_corners`
  dá as caixas de colisão; `LevelPatch` aplica o patch na imagem e refaz caixas, células e redes.

### A prova: `fase_glb.py` (fora do repo)
`C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\editor\fase_glb.py`
(`python fase_glb.py CENTRAL saida.glb [--lod N]`; importa `lvl`, `ngp`, `gltf`, `tex` de `tools/`; só lê o disco). O `.glb` fica
na mesma pasta e **não vai para o repo** (é derivado do jogo). `confere.py` (mesma pasta) compõe as matrizes do `.glb` e compara com
o do `gltf.py`.

O que faz:
- Percorre as raízes como o `ngp.walk`, mas carrega o **dono**: ao chegar numa alça do `lvl.py` (o TRANSLATE/ROTATE acima do
  `INTERACTIVE_STATE` ou do GROUP do pickup, ou o próprio nó do prop), abre um nó `"0xB18310 destr"` com a matriz do mundo da alça
  e recomeça a matriz na identidade. Os `extras` do nó trazem `kind`, `handle`, `from` (a posição que o patch exige), `heading`,
  `id` e `states`. O three.js põe `extras` em `userData`: o editor sabe direto o que gravar no JSON.
- `DESTRUCTIBLE`: cada estado vira um filho `state0`, `state1`, `state2` (os danificados com `extras.hidden`), para o editor
  mostrar como o prédio fica quebrado.
- Alça aninhada (placa ou caixa d'água num estado de outro prédio) vira filho do estado do dono, com matriz relativa: mover o pai no
  editor leva os aninhados, como no jogo.
- Uma malha por OBJECT, no espaço do objeto, reaproveitada pelas instâncias (as janelas e caixas de ar instanciadas entre prédios).
- O que não tem alça fica em `root<N>` (raiz do `.NGP`).
- Eixos: o jogo usa Z para cima; a matriz do nó é `C⁻¹·M·C` com a mesma troca do `gltf.to_y_up`.

Medidas [certo]:

| fase | objetos do `lvl.py` | tempo (lvl + glb) | `.glb` | malhas / instâncias | triângulos únicos | materiais |
|---|---|---|---|---|---|---|
| CENTRAL | 331 (67 destr, 115 pickup, 149 prop) | 0,5 + 2,4 s | 9,0 MB | 1269 / 1604 | 80 206 | 318 |
| VEGAS | 315 | 0,5 + 2,7 s | 9,3 MB | 1246 / 1596 | 84 540 | 337 |
| TOKYO | 266 | 0,3 + 1,8 s | 7,1 MB | 1430 / 1708 | 56 961 | 245 |
| METRO | 720 | 1,3 + 3,9 s | 17,1 MB | 3304 / 3843 | 143 204 | 321 |

- CENTRAL, só o visível (estado 0 e raízes): 71 728 triângulos e caixa `[-8808,-926,-10541]..[10932,3527,10541]`, igual ao
  `gltf.py` [certo]. A primeira versão perdia 148 triângulos por não seguir `ANIM_SCALE`; com ele, bate.
- Os 331 objetos do CENTRAL ganham nó. 4 deles não têm malha no estado 0 (destrutíveis `0xA4B7C0`, `0xA50250`, `0xA54E00`,
  `0xA98930`, 0 OBJECTs: gatilhos invisíveis). O editor precisa desenhar esses como marcador.
- VEGAS: 3 destrutíveis (`0xC14370`, `0xCC7850`, `0xCE2730`) ficam sem nó porque o percurso não chega neles (um GROUP acima não tem
  o bit visual, o mesmo corte do `Scene::walk`) [certo]. Provavelmente são os GROUPs por modo de jogo (`726b99b:docs/investigacoes/2026-11-06/objetos_fase.md`:
  "GROUP→NULL por modo") [hipótese]. O editor deve abrir o nó dessas alças pela posição do `lvl.py` mesmo sem passar pelo GROUP.
- Tempo: ~3 s no CENTRAL, ~5 s no METRO. Vale guardar o `.glb` num cache (`<dados>/cache/editor/<FASE>_<sha1 do .NGP>.glb`) e
  refazer só quando o disco ou o mod mudar.

### O que falta para o desenho ficar fiel
- **LOD** [certo]: a prova pega o mais detalhado (`--lod 99`). O LOD 0 vem em coordenadas do mundo, desfeito por um TRANSLATE
  inverso abaixo do LOD (`726b99b:docs/investigacoes/2026-11-06/objetos_fase.md`); com o dono na alça isso continua certo, porque o TRANSLATE inverso é filho. Um
  seletor de LOD no editor é só exportar de novo.
- **SWITCH** [certo]: sai o `whichChild` do disco. Os pickups sobem com `whichChild = 0` (o `Pickup::init` escreve isso), e no disco
  já é 0 no CENTRAL (a contagem bate). SWITCH de estado em tempo de execução não importa para o editor.
- **Estados quebrados** [certo]: já saem (`state1`, `state2`). Emissores de fumaça (`PART_EMITTER`) e destroços (ações dos estados)
  não saem.
- **Céu** [certo]: raiz 3 (id 2), 2 instâncias no CENTRAL, sai como `root3`. No jogo o céu acompanha a câmera [hipótese]; o editor
  deve desenhá-lo preso à câmera, ou escondê-lo.
- **Raiz 1** (armas, power-ups, texturas de partícula, HUD; 178 instâncias no CENTRAL) fica amontoada na origem: o editor esconde.
- **Texturas com paleta** [certo]: já resolvidas pelo `tex.Library` (318 materiais, 0 não achadas). Faltam as PNG de mod
  (`mods/<mod>/` sobre o `.TEX`); dá para trocar pelo mesmo nome que o `wotm_texmods.hpp` usa [hipótese].
- **Fora da prova**: sprites (halos dos power-ups, 185 malhas com sprite), malhas skinned (152 vazias ou skinned), `ANIM_XFORM`
  em identidade (peças animadas na pose de repouso), modo de mistura do GS (o `gltf.py` já põe em `extras.gs` quando tem a pose;
  aqui sai só BLEND/MASK), neblina, hora do dia e luzes do `.MRS`. A luz base é a cor de vértice, então o unlit fica perto do jogo.
- **Colisão** [certo]: o `lvl.py` já dá as 8 quinas de cada caminho (`path_corners`) e, depois do `LevelPatch`, as caixas refeitas.
  O editor pode mostrar em arame e conferir ao vivo a regra da caixa e a das células (`check_collision`).

## 2. Stacks para o editor

O que pesa neste projeto: (1) a lógica do patch, a colisão e as redes já estão em Python (`lvl.py`, ~1150 linhas) e em C++ dentro do
runtime (`wotm_levelpatch.inc`, ~1100 linhas, preso ao `rdram`, ao `Registry` e ao `PS2Runtime`); (2) o kit de mods já é Python +
tkinter + PyInstaller, com tema escuro; (3) o `native/` desenha a partir da RAM e da VRAM do jogo rodando, não do disco; (4) Windows
e Linux; (5) o repo é GPLv3.

| | (a) Python + tkinter + OpenGL | (b) Python + PySide6 | (c) C++ raylib + Dear ImGui | (d) Python + three.js (pywebview ou navegador) | (e) Godot |
|---|---|---|---|---|---|
| Desenho | o que se escrever em GL; sem gizmo | QOpenGLWidget (GL próprio) ou Qt Quick 3D (glTF pronto) | raylib cru; o `Scene` do `native/` precisa da RAM e da VRAM do jogo | `GLTFLoader` + unlit + cor de vértice; perto do jogo | importa glTF em tempo de execução; bom |
| Seleção e gizmo | escrever (raycast, setas) | escrever no GL; no Quick 3D, picking pronto e gizmo escrito | ImGuizmo (MIT) | raycast e `TransformControls` prontos | picking pronto; gizmo escrito (o do editor do Godot não vai no jogo exportado) |
| Reuso do `lvl.py` | total, mesmo processo | total | nenhum: portar ou chamar `python` por subprocesso | total: backend Python, página só desenha | subprocesso ou portar para GDScript |
| Tema claro/escuro | o do modkit (ttk, à mão) | paleta Fusion; Qt 6.5+ segue o sistema | `StyleColorsDark/Light` | CSS (`prefers-color-scheme` + botão) | tema do Godot (editável, mas trabalhoso) |
| .exe / Linux | PyInstaller, já feito; `pyopengltk` pouco mantido | PyInstaller, ~80-150 MB; Linux bom | CMake, exe pequeno; Linux bom (raylib já roda lá) | PyInstaller pequeno; Windows usa o WebView2 (já no Win 10/11), Linux precisa de WebKitGTK; sem eles, abre no navegador | export oficial dos dois; precisa dos templates |
| Licença | PSF, zlib | LGPLv3 (Qt Quick 3D é GPLv3: serve, o repo é GPLv3) | zlib, MIT | MIT (three.js), BSD (pywebview) | MIT |
| Esforço até o MVP [hipótese] | alto (3D na mão) | médio-alto | alto (UI, diálogos, port da lógica) | médio | alto (lógica fora do Python) |

Notas:
- **(a)** O tkinter não tem 3D. `pyopengltk` dá um contexto GL, mas câmera, picking, gizmo, texturas e transparência ficam por
  conta do editor. O tema do modkit ajuda só nos painéis.
- **(b)** É a opção "app de verdade": árvore de objetos, propriedades, desfazer (`QUndoStack`), diálogos nativos, tema que segue o
  sistema. O 3D é o ponto fraco: QOpenGLWidget é GL escrito à mão (como (a), só que mais estável); o Qt Quick 3D carrega glTF, mas
  mistura QML no projeto. Juntar com (d) (QtWebEngine com three.js) leva o Chromium junto (~200 MB).
- **(c)** Fidelidade alta só se o editor rodar o loader do jogo (inflar o `.NGP` em 0xA00000, relocar o TEX0, montar
  `fileStatus` e a VRAM como o GS faria) para o `Scene` desenhar. O `viewer.cpp` hoje precisa de um dump da RAM e da VRAM. O
  `wotm_model.hpp` (monstro direto do disco) mostra que dá para decodificar direto do disco, mas para fase isso ainda não existe.
  A lógica do patch existe em C++, mas dentro do runtime. Para fidelidade total, o caminho barato é o próprio jogo (item 3).
- **(d)** three.js é a mesma base do viewer web do projeto (https://yanm1103.github.io/wotm-model-museum/), então há experiência
  e código de material e mistura para reaproveitar [hipótese: não li o código do museu]. A página não decodifica nada: recebe o
  `.glb` da prova e a lista de objetos do Python. O Python faz tudo que mexe na fase (exportar, `LevelPatch`, checar, gravar,
  testar). A ponte do `pywebview` (`js_api`) chama funções Python direto; sem `pywebview`, um `http.server` local com
  `webbrowser.open` faz o mesmo. Vendorizar o three.js (sem CDN; MIT, licença junto ao exe, como o modkit faz com Pillow e numpy).
- **(e)** Godot faz um ótimo visualizador, mas o editor dele não vai no jogo exportado: gizmo, seleção, desfazer e painéis seriam
  escritos de novo, e a lógica do patch ficaria fora do Python (subprocesso) ou duplicada pela terceira vez.

### Recomendação
**(d): `tools/lvled.py` (Python) + página three.js, janela `pywebview` com o navegador como reserva.** Porque:
1. o `lvl.py` roda no mesmo processo: o editor pode aplicar o `LevelPatch` a cada mudança e mostrar na hora as caixas refeitas,
   as redes cortadas e a checagem de colisão, com o mesmo resultado que o jogo vai ter (o C++ segue o `lvl.py` na mesma ordem);
2. seleção, gizmo, câmera orbital e glTF já vêm do three.js; o gizmo fica limitado ao que o patch aceita (mover XYZ e girar em Z);
3. tema claro/escuro em CSS, com o botão e a preferência guardados (`localStorage` ou o `settings` do editor);
4. empacota como o modkit (outro `.exe` no zip do release ou dentro do mesmo kit) e roda no Linux;
5. o caminho para "objetos do Blender" é glTF nos dois sentidos, e a página já sabe mostrar glTF.

Plano curto [hipótese]: (1) passar o `fase_glb.py` para `tools/` (função `export_stage` no `gltf.py` ou no `lvl.py`), com cache;
(2) página com lista de objetos (filtro por tipo), câmera, seleção (raycast sobe até o nó `0x...`), gizmo, apagar e copiar,
desfazer; (3) gravar o JSON (posição do mundo da alça, `heading`, `from` dos `extras`); (4) botão testar (item 3); (5) depois:
template de fase nova e importação de glTF.

## 3. Botão "testar"

O que existe [certo]:
- O jogo lê `mods/<mod>/levels/<FASE>.json` (`MODS.md`). A pasta de mods é `WOTM_MODS`, ou `<dados>/mods`, ou `<jogo>/mods`
  (`wotm_standalone.cpp`).
- `WOTM_MATCH=<jogador>[:<traje>][,<ia>...],<fase>` pula o menu (`wotm_overrides.cpp`, lido por `getenv`, vale para o `wotm.exe`
  aberto direto). `3,1` = Togera sozinho no Midtown Park (CENTRAL), sem IA. Índices das fases em `GLOSSARY.md` (1 CENTRAL,
  2 VEGAS, 4 METRO, 9 TOKYO...).
- `WOTM_TRAINING=1` liga o modo treino (luta sem fim, opções na pausa). `WOTM_FULLSCREEN=0`, `WOTM_RES`, `WOTM_MONITOR` para abrir
  em janela. `WOTM_LEVELPATCH=2` loga cada caixa refeita. F9 é o modo foto (câmera livre), bom para olhar o resultado.
- O log vai para `<dados>/wotm.log` quando o stderr não é arquivo nem pipe; se o editor redirecionar o stderr, ele mesmo recebe
  as linhas `[fase] CENTRAL <- <mod> ...`.

Fluxo proposto:
1. Gravar o JSON em `mods/<mod do editor>/levels/<FASE>.json` (mod escolhido pelo usuário; padrão um mod próprio do editor).
2. Rodar `LevelPatch` (o `lvl.py patch`) e mostrar o que mudou e a checagem; parar se a colisão quebrar.
3. Abrir o `wotm.exe` (caminho configurado no editor; o da pasta do release por padrão) com `WOTM_MATCH=<monstro>,<fase>`,
   `WOTM_MODS=<pasta>`, `WOTM_FULLSCREEN=0`, `WOTM_LEVELPATCH=2` e, se marcado, `WOTM_TRAINING=1`; stderr num pipe.
4. Mostrar as linhas `[fase]` no editor: "aplicado", "entrada pulada: ..." e a checagem antes/depois.
5. Um botão fecha o jogo que o editor abriu (nunca outro).

Cuidado [certo]: o registro de mods (`Registry`) é montado no boot; um JSON criado depois do boot não é visto até reiniciar o
jogo. O conteúdo, sim, é relido a cada carga da fase (`applyFor` abre o arquivo de novo). O editor deve gravar o JSON antes de
abrir o jogo.

## 4. Edição dentro do jogo ("só se for ok")

[certo] O patch entra num único ponto: a entrada do `dbInitDb` chamado por `Shell::LoadLevelFiles`, com a imagem já inflada e nada
registrado. A carga copia posição e matriz para Destructibles, Pickups e seus `_cs`, emissores e grupos de pedestres, e as redes
de carros e pedestres são cortadas antes de qualquer veículo nascer (`726b99b:docs/investigacoes/2026-11-06/objetos_fase.md`, `MODS.md`). O RESTART da pausa
(`TheGame::ResetLevel` + `rtMain(0)`) **não** recarrega a fase, então não reaplica o patch.

Opções:
- **Na próxima carga (seguro):** sair da luta para o menu e entrar de novo já relê o JSON. Um atalho "recarregar fase" (tecla ou
  item da pausa) que faça QUIT e volte sozinho para a mesma luta, pelo caminho do `WOTM_MATCH`, dá o ciclo "editar, recarregar"
  em segundos, sem fechar o jogo. Risco baixo: é o fluxo normal de sair e entrar [hipótese: a volta automática ao mesmo
  `WOTM_MATCH` depois de um QUIT ainda não existe].
- **Pré-visualização ao vivo (só o desenho):** escrever o `trans`/`ROTATE` da alça na RAM move o desenho na hora, mas a colisão
  (células e caixas), as tabelas copiadas na carga (Destructible, `_cs` do pickup), os emissores já ativados e as redes ficam no
  lugar antigo. Serve como "fantasma" enquanto se arrasta, com um aviso, e a recarga confirma. Apagar e copiar ao vivo exigem a
  imagem de antes do `dbInitDb`: não dá sem recarregar.
- **Ligação editor ↔ jogo:** o editor grava o JSON e manda "recarregar" (arquivo vigiado ou socket local). O jogo só lê; nada de
  edição estrutural fora da carga. O risco de mexer na RAM ao vivo é colisão e IA errada até a recarga; o de corromper a fase é
  nulo se ninguém escrever ponteiros fora do `dbInitDb`.

Conclusão: o editor fica no PC (item 2), e o jogo ganha, se o dono quiser, só "recarregar fase" e, mais tarde, o fantasma ao vivo.
