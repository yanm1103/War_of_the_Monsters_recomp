# Desenho mais fiel no editor de fase (névoa, fundo, céu, luz e mistura do GS)

Pesquisa para o editor (`tools/level_editor/`, three.js r160). Nada do repo foi alterado além deste arquivo. Os scripts de contagem
ficaram na pasta de rascunho da sessão (não vão para o repo); as contagens abaixo são de todas as 19 fases de `disc/LVL` (`3-MILE`,
`AIRPORT`, `BIGSHOT`, `CANYON1`, `CANYON2`, `CAPITOL`, `CENTRAL`, `CRUSH`, `DBALL`, `ISLAND`, `METRO`, `MINISF`, `SANFRAN`, `TOKYO`,
`TOKYODM`, `TRAINING`, `UFO`, `VEGAS`, `VOLCANO`), percorrendo a hierarquia como o `lvlglb.py` (LOD 0, todos os estados dos
destrutíveis, todos os filhos dos SWITCH) e herdando o ctx do GS como o `pose.py`/`Scene::CtxScope`.

Marcação: [certo] = lido no código e conferido contra o disco ou um dump de RAM; [hipótese] = não conferido.

## 1. Névoa

### 1.1 De onde vem [certo]
- `RST/<FASE>.MRS` é lido pelo `resetcom::readKey` (`src/common/resetcom.cpp`): chave 4 (`FogKey`, 0x40 bytes com o cabeçalho) chama
  `fogSetFogParms(minR, maxR, maxV, farClip, r, g, b)` (`src/common/fog.cpp`). Os outros chamadores de `fogSetFogParms` são o
  `reset::oldStyleRead` (versão velha do arquivo) e o `uidev` (menu de depuração). `fogSetVolumeParms` e `fogAdjust` não têm chamador
  fora do `fog.s`; a hora do dia (chave 7) vem com `tod 0` em todas as fases (`todSetTOD` deixa `todTimeOfDayOn = 0`). Então a névoa de
  cada fase é constante e é a do `.MRS`.
- `fogCalcFogValues(maxR, minR, maxV, &FA, &FB)`: `FB = (maxV - 1)·minR·maxR / (maxR - minR)`, `FA = 1 - FB/maxR`.
- `fogUpdate` manda ao VU1 `vu1FogParams` (0x25B280) = `(FA, maxV, 1, FB)` e ao GS o `FOGCOL = r | g<<8 | b<<16` (`fogColorCtx[4]`).
  Retorna cedo, sem mandar nada, se `maxR <= minR`.
- O programa do VU1 faz, por vértice das seções com `PRIM.FGE`: `F = clamp(burn + FA + FB/W, 0, maxV)`, com `W` = linha 3 da
  `worldToScreenMat`·(p − olho) = profundidade ao longo da frente da câmera em unidades do mundo (`NATIVE.md`: "row 3 = forward").
  `burn` = `fogBurnThru` do OBJECT (+0x2C, `include/hieri_types.h`). O GS mistura `(F·C + (255 − F)·FOGCOL) >> 8` antes da mistura de
  alfa. É exatamente o que o nativo faz (`Scene::readFog`, `Scene::fogF`, shader do `beginShader` em `native/wotm_native.hpp`).
- Conferido nos dumps `build/dumps_live/`: `ram_energy_robot.bin` (METRO) tem `vu1FogParams = (-107,857; 255; 1; 326571,4)` e
  `ram_B1.bin`/`ram_congar2.bin` (CENTRAL) `(-19,079; 255; 1; 328894,9)`, iguais às contas abaixo.

### 1.2 Quem tem névoa [certo]
- **Toda** seção de malha e todo sprite das 19 fases tem `FGE = 1` (886.705 triângulos e 2.507 sprites, todos com FGE).
- `fogBurnThru` só tem dois tipos de valor: **1** (todo o resto; soma um nível de F, desprezível) ou **≥ 3500** (3500, 4000, 8000,
  10⁴, 10⁵, 10⁶, 10⁷). Como o menor FA das fases é −361,9 (CAPITOL), `burn ≥ 3500` dá `F = maxV` sempre: **nunca tem névoa**. São os
  50 objetos do céu (todos) e alguns do mundo: VOLCANO 87 com 4000, e luzes/brilhos com 10⁵–10⁶ em ISLAND, METRO, MINISF, SANFRAN,
  TOKYO, TOKYODM, UFO e VEGAS (~260). Para o editor, basta um booleano por OBJECT: `fog = burn < 255`.

### 1.3 Valores por fase [certo]
`maxV = 255` em todas. A cor da névoa é também a cor de fundo (seção 2). Distâncias em unidades do jogo, com `burn = 1`:

| Fase | minR | maxR | farClip | cor | FA | FB | 50% | 90% | 100% |
|---|---|---|---|---|---|---|---|---|---|
| 3-MILE | 1000 | 3500 | 3700 | (19, 36, 11) | -100,600 | 355600,0 | 1566 | 2843 | 3570 |
| AIRPORT | 1160 | 16380 | 16350 | (27, 7, 27) | -18,359 | 317096,1 | 2189 | 7399 | 18267 |
| BIGSHOT | 1200 | 16380 | 16380 | (37, 20, 13) | -19,079 | 328894,9 | 2259 | 7547 | 18192 |
| CANYON1 | 1720 | 16380 | 16380 | (36, 60, 90) | -28,801 | 488137,4 | 3143 | 9158 | 17558 |
| CANYON2 | 1800 | 16380 | 16380 | (0, 0, 0) | -30,358 | 513644,4 | 3275 | 9363 | 17496 |
| CAPITOL | 2500 | 4250 | 16380 | (38, 13, 13) | -361,857 | 1542142,9 | 3158 | 3991 | 4274 |
| CENTRAL | 1200 | 16380 | 16380 | (37, 20, 13) | -19,079 | 328894,9 | 2259 | 7547 | 18192 |
| CRUSH | 1200 | 16380 | 16380 | (37, 20, 13) | -19,079 | 328894,9 | 2259 | 7547 | 18192 |
| DBALL | 1000 | 3500 | 3700 | (19, 36, 11) | -100,600 | 355600,0 | 1566 | 2843 | 3570 |
| ISLAND | 1100 | 16380 | 16380 | (37, 20, 13) | -17,285 | 299513,9 | 2083 | 7168 | 18392 |
| METRO | 900 | 3000 | 16380 | (45, 33, 20) | -107,857 | 326571,4 | 1393 | 2467 | 3056 |
| MINISF | 8180 | 16380 | 15220 | (0, 0, 0) | -252,380 | 4150372,4 | 10954 | 14990 | 16510 |
| SANFRAN | 7680 | 16380 | 16380 | (38, 28, 15) | -223,221 | 3672734,9 | 10502 | 14826 | 16527 |
| TOKYO | 2000 | 4000 | 15220 | (3, 6, 16) | -253,000 | 1016000,0 | 2677 | 3661 | 4032 |
| TOKYODM | 2000 | 4000 | 15220 | (3, 6, 16) | -253,000 | 1016000,0 | 2677 | 3661 | 4032 |
| TRAINING | 1200 | 16380 | 16380 | (37, 20, 13) | -19,079 | 328894,9 | 2259 | 7547 | 18192 |
| UFO | 8180 | 16380 | 16380 | (0, 0, 0) | -252,380 | 4150372,4 | 10954 | 14990 | 16510 |
| VEGAS | 1500 | 8000 | 16370 | (0, 0, 0) | -57,615 | 468923,1 | 2547 | 5711 | 8283 |
| VOLCANO | 1000 | 11450 | 12220 | (90, 36, 92) | -23,306 | 278306,2 | 1858 | 5822 | 12477 |

### 1.4 Por que `THREE.Fog` não serve [certo]
- O F do jogo é **hiperbólico** em W (linear em 1/W): na CENTRAL a névoa já está em 50% a 2259, perto do `minR` 1200, e não no meio
  de 1200..16380. O `THREE.Fog` do r160 é `smoothstep(near, far, depth)` (conferido em `vendor/three.module.min.js`): com
  `near = minR, far = maxR` daria ~1% de névoa a 2259. O `FogExp2` também não tem essa forma.
- Reproduzir no three.js trocando os pedaços de shader da névoa uma vez, antes de compilar qualquer material, e reaproveitando os
  uniformes que o three já preenche (`fogNear` := FA, `fogFar` := FB, `fogColor` := FOGCOL cru):
  ```js
  THREE.ShaderChunk.fog_pars_vertex = '#ifdef USE_FOG\nuniform float fogNear; uniform float fogFar; varying float vGsFog;\n#endif';
  THREE.ShaderChunk.fog_vertex = '#ifdef USE_FOG\nfloat gsW = -mvPosition.z;\n' +
    'vGsFog = gsW > 0.001 ? clamp(1.0 + fogNear + fogFar / gsW, 0.0, 255.0) / 255.0 : 1.0;\n#endif';   // 1.0 = burn
  THREE.ShaderChunk.fog_pars_fragment = '#ifdef USE_FOG\nuniform vec3 fogColor; varying float vGsFog;\n#endif';
  THREE.ShaderChunk.fog_fragment = '#ifdef USE_FOG\ngl_FragColor.rgb = mix(fogColor, gl_FragColor.rgb, vGsFog);\n#endif';
  scene.fog = new THREE.Fog(new THREE.Color().setRGB(r / 255, g / 255, b / 255), FA, FB);
  ```
  `-mvPosition.z` é a mesma W do jogo (frente da câmera, unidades do jogo: o `lvlglb.py` só troca os eixos, sem escala). F por vértice
  e interpolado, como o GS. `maxV` = 255 em todas as fases (pode ir constante). `fog_fragment` vem antes do
  `premultiplied_alpha_fragment` no `meshbasic`, então o aditivo recebe a névoa antes da mistura, como no GS.
- O que mais usa névoa na cena do editor precisa de `fog: false` (as linhas da `GridHelper` e as caixas de seleção usam
  `LineBasicMaterial`, que tem `fog: true` por padrão).
- Com névoa, quem edita de longe vê a fase sumir (3-MILE e DBALL ficam 100% cor de névoa a 3570; METRO a 3056; CAPITOL a 4274).
  Precisa de um botão para ligar e desligar o "visual do jogo".

### 1.5 Far clip [certo, só a parte do jogo]
`fogGetFarClipRange` vai para o `hierCacheForAsm`, e o programa 0x0 do VU0 descarta o OBJECT/GROUP cuja esfera está toda além do
`farClip` (plano 1.z; comentário de `Particles::inViewVolume` em `native/wotm_fx.hpp`). Nas fases o `farClip` fica perto do `maxR` (ou
acima), e além do `maxR` tudo já é cor de névoa; o efeito visível é só o céu aparecer onde o prédio sumiu. Opcional no editor (esconder por
esfera do objeto a cada quadro). O nativo usa `rlSetClipPlanes(0.5, 30000)` e não aplica o `farClip` [hipótese: não achei teste].

## 2. Cor de fundo [certo]
- A chave 2 do `.MRS` chama `viewSetBgColor`, mas `fogBackgroundColorIsSlaved` vale 1 (`fogInit`) e nada fora do `fog.s` o
  referencia. O `fogUpdate` chama `viewSetBgColor(r, g, b, 0)` com a cor da névoa, então **o fundo é a cor da névoa**. Nas 19 fases
  a chave 2 já é igual à cor da névoa. Conferido nos dumps: `viewBgR/G/B` = (45, 33, 20) na METRO e (37, 20, 13) na CENTRAL,
  `fogBackgroundColorIsSlaved` = 1.
- Para o editor: `renderer.setClearColor` com a cor da névoa (crua, ver 4.1) no visual do jogo.
- Achado lateral [hipótese]: o nativo limpa o quadro de preto (`ClearBackground(BLACK)` em `native/runtime/wotm_live.inc`) e não lê
  `viewBg*`. Só aparece onde nada é desenhado; a TRAINING não tem céu (nenhum nó id 2), então lá o PS2 mostraria (37, 20, 13).

## 3. Céu [certo]
- O céu é a subárvore de um nó com id 2 (bits altos da primeira palavra; `viewSetSkyEntry`). **Em 7 fases esse nó não é a raiz**: está
  abaixo de uma raiz com id 0 (3-MILE raiz 4, CANYON1 raiz 0, CENTRAL raiz 3 = TRANSLATE 0xA002B0 com o GROUP id 2 em 0xFC8870, CRUSH
  raiz 1, DBALL raiz 0, ISLAND raiz 4, MINISF raiz 2). O `lvlglb.py` marca só `extras.ident` da raiz, e o `app.js` mostra as raízes de
  ident 0 e 1: nessas 7 fases o céu sai hoje como geometria comum, na origem e com profundidade (o `editor_pc.md` diz "raiz 3 (id 2)"
  na CENTRAL; a raiz 3 tem id 0). Nas outras 11, a raiz é o próprio nó id 2 e o editor o esconde. TRAINING não tem céu.
- 50 objetos de céu nas 18 fases que têm: todos com `burn ≥ 3500` (sem névoa), 18 debaixo de `ZMSK`, quase todos com ABE e ALPHA 0x44;
  7 aditivos (0x48, UFO). Domo de raio ~8600 a 13000 (esfera do OBJECT, +0x1C), centro perto de (0, 0, 400). Sem `uvScroll`.
- O nativo desenha a lista do céu primeiro, sem teste de profundidade (`Scene::draw`: `rlDisableDepthTest` em volta de
  `drawList(sky)`), na posição do `trans` do `_cs` do céu, que na luta fica onde a última câmera de locator o deixou (comentário em
  `Scene::collect`). As partes com ABE misturam com o fundo, que é a cor da névoa.
- Para o editor: marcar os grupos de céu por nó id 2 em qualquer profundidade; desenhar primeiro (`renderOrder` bem negativo, materiais
  com `depthTest = false`, `depthWrite = false`, `fog = false` e `transparent = false` com `CustomBlending` nas partes com ABE, para
  ficarem na lista opaca, que o three desenha antes da transparente); posição presa à câmera (só translação), como nos menus. A
  paralaxe da luta (céu parado num ponto) não é reproduzível sem saber a câmera de locator; presa à câmera nunca mostra o domo por fora.

## 4. Cor, alfa e mistura do GS

### 4.1 Espaço de cor (o maior erro de hoje) [certo]
- O GS modula e mistura sobre os bytes como estão. O three r160 com os padrões (`ColorManagement.enabled = true`,
  `outputColorSpace = SRGB`) e o `GLTFLoader` (que põe `SRGBColorSpace` no `baseColorTexture`) faz `codifica(decodifica(T)·Cv)`: um
  vértice 0x40 (metade) sai com ~0,73 do texel em vez de 0,5. O `app.js` não mexe em espaço de cor (nenhum `colorSpace`,
  `ColorManagement` ou `outputColorSpace`), então toda a sombra pintada nos vértices aparece mais clara e chapada, e a mistura de alfa
  acontece em espaço linear.
- Corrigir no editor: `THREE.ColorManagement.enabled = false`, `renderer.outputColorSpace = THREE.LinearSRGBColorSpace` e, logo depois
  do `loadAsync` (antes do primeiro desenho), `map.colorSpace = THREE.NoColorSpace` em todo material da fase. Com o gerenciamento de cor
  desligado, as cores em hex da interface (gizmo, caixas, grade) também ficam cruas, como antes do r152.

### 4.2 Modulação e alfa [certo]
Todo TEX0 das fases é `TFX = MODULATE`, `TCC = 1` (43.698 slots). No GS:
- com textura: `C = min(255, (Ct·Cv) >> 7)`, `As = min(255, (At·Av) >> 7) / 128` (até 1,99);
- sem textura (`PRIM.TME = 0`, slot 31): `C = Cv`, `As = Av / 128`.

O que o `lvlglb.py` faz hoje e onde erra, por slot de malha com ABE (contagem nas 19 fases):
- COLOR_0 = `min(1, Cv/128)`: o Cv acima de 0x80 clareia no jogo e aqui satura. 368.553 de 2.659.821 vértices (14%) passam de 0x80.
- Ganho do alfa: o vértice leva `Av · alfa_max_da_textura / 128` e a PNG leva o alfa com 0x80 saturado em 255. Só é exato quando o
  alfa máximo da textura é 0x80. Com textura até 0xFF, um texel abaixo do máximo sai até 2 vezes mais opaco: **3.969 slots**.
- `blend` só quando algum vértice tem alfa < 0,9 (e os ≥ 0,9 viram 1): um slot com ABE, vértices opacos e textura de alfa parcial
  vira MASK com corte em 0,06 (perde o degradê dos brilhos e bordas macias): **1.983 slots**; vértices entre 0,9 e 1 forçados a 1 em
  **1.549 slots**.
- Sem textura, COLOR_0 = `Cv/128`: dobra a cor (o GS usa `Cv` direto). Pouco: 4.737 triângulos de mundo nas 19 fases (1%).
  Achado lateral [hipótese forte]: o nativo também dobra, porque desenha o "sem textura" com uma textura branca 0xFF de alfa 0x80
  (`gsWhiteTexture`, `native/wotm_fx.hpp`) e o shader faz `texel·Cv/128`. Exemplo para conferir no PCSX2: CENTRAL, OBJECT 0x10FFD00
  (24 triângulos sem textura, vértices (192, 0, 0)).

Exato e simples: exportar **cru** e fazer as duas contas do GS num pedaço de shader.
- `lvlglb`: COLOR_0 = `(r, g, b, a) / 255` sem limite, sem ganho e sem limiar de 0,9; PNG com o alfa cru do GS
  (`tex.Library.image(..., hi=True)`; atenção: com `TCC = 0` o `image` devolve 255, que cru vale 1,99; o certo seria 0x80, mas nenhuma
  fase usa `TCC = 0`). Uma marca `extras.gsRaw = true` no asset (e mudar a chave do cache do `.glb`) para o editor saber qual versão
  chegou.
- Editor, `material.onBeforeCompile` nos materiais da fase (todos com `customProgramCacheKey` igual, para dividir o programa):
  ```glsl
  #include <color_fragment>
  #ifdef USE_MAP
    diffuseColor.rgb = min(diffuseColor.rgb * 1.9921875, vec3(1.0));   // (Ct*Cv)>>7: (Ct/255)(Cv/255)*255/128
    diffuseColor.a = min(diffuseColor.a * 3.96899, 1.9921875);          // ((At*Av)>>7)/128: (At/255)(Av/255)*255*255/128/128
  #else
    diffuseColor.a = min(diffuseColor.a * 1.9921875, 1.9921875);        // Av/128; a cor fica Cv
  #endif
  ```
  O teste de alfa do GS vira `alphaTest = (aref + 0,5) / 128` sobre esse As (descarta `As·128 <= aref`).

### 4.3 Modos de mistura que as fases usam [certo]
O ALPHA_1 vem do ctx do nó mais próximo com ctx (GROUP, TRANSLATE, ROTATE, SCALE em +4; ANIM_XFORM e ANIM_SCALE em +8), herdado na
descida, a partir do `ctxDef` (ALPHA 0x44, sem teste de alfa, grava Z): `Scene::groupState`/`CtxScope` no nativo, `pose.gs_apply` no
Python. Contagem por OBJECT com ABE, nas 19 fases:

| ALPHA_1 | Conta do GS | Objetos (mundo / céu / outras raízes) | Hoje no editor |
|---|---|---|---|
| 0x44 | (Cs − Cd)·As + Cd | 10.671 / 41 / 2.278 | certo (BLEND normal), menos os casos de 4.2 |
| 0x48 | Cs·As + Cd (aditivo) | 243 / 7 / 939 | **errado**: sai como mistura normal (o brilho escurece o que está atrás e mostra a borda do quad) |

- Aditivo no mundo por fase: UFO 82, 3-MILE 64, CAPITOL 35, VEGAS 33, CANYON2 18, METRO 6, TOKYO 4, TRAINING 1; as "outras raízes"
  (armas, power-ups, pickups, grupos de pedestres) trazem os halos aditivos em todas as fases (ex. tex 92/97 na CENTRAL).
- Nenhum OBJECT de fase está debaixo de 0x42, 0x46 ou 0x81 (esses aparecem só no Kineticlops). Nenhum ctx com ALPHA ≠ 0x44 cai em
  malha sem ABE. Cada OBJECT é alcançado com um único estado do GS (0 nós com dois estados), então o estado pode ir por nó.
- Teste de alfa (TEST_1): sem teste em 18.999 objetos, `aref` 64 em 895, `aref` 1 em 60. ZBUF_1.ZMSK (não grava Z) em 4.780 objetos
  (3.110 no mundo). Slots com ABE sem ZMSK, que o GS desenha gravando Z e que o three hoje desenha sem gravar (o `GLTFLoader` põe
  `depthWrite = false` em todo BLEND): 2.547. Esse é o motivo do limiar de 0,9 do `lvlglb`, e o ZMSK por material resolve na raiz.
- `alphaSort` (bit 0x8000 das flags do OBJECT): 11.692 objetos vão para a OT de alfa, desenhada depois de tudo, de longe para perto em
  faixas de 8 unidades até 1016 (`Scene::alphaBin`); o resto sai na ordem da travessia, opacos e translúcidos misturados, gravando Z
  conforme o ZMSK.

### 4.4 Mapeamento para o three.js [certo para 0x44/0x48; tabela geral a partir do `gsBlendFactors`]
GS: `cor = (A − B)·C + D`, A/B/D em {0 Cs, 1 Cd, 2 zero}, C em {0 As, 1 Ad, 2 FIX}. Com `blending = THREE.CustomBlending` (o three só
desliga a mistura quando ela é `NormalBlending` e o material não é `transparent`; a `CustomBlending` vale também na lista opaca):

| ALPHA | equação | blendSrc | blendDst | premultipliedAlpha | nota |
|---|---|---|---|---|---|
| 0x44 | Add | SrcAlpha | OneMinusSrcAlpha | não | As > 1 satura em 1 (o GS extrapola; o nativo faz 3 passes, o editor pode ignorar) |
| 0x48 | Add | One | One | sim | o `premultiplied_alpha_fragment` faz rgb·As com As até 1,99, como o GS; `AdditiveBlending` pronto serve de aproximação (perde As > 1) |
| 0x42 | ReverseSubtract | SrcAlpha | One | não | Cd − Cs·As |
| 0x46 | Add | Zero | OneMinusSrcAlpha | não | Cd·(1 − As) |
| 0x81 | ReverseSubtract | SrcAlpha | SrcAlpha | não | (Cd − Cs)·As (As > 1 perde-se) |
| C = 1 (Ad) | | DstAlpha | | | |
| C = 2 (FIX) | | ConstantAlpha | | | `material.blendAlpha = FIX / 128` (bits 32..39 do ALPHA; existe no r160) |

Alfa do framebuffer: `blendSrcAlpha = Zero`, `blendDstAlpha = One` (ninguém lê). Sem ABE: `NoBlending`.

Por material (`material.userData.gs` vindo de `extras.gs` do glTF):
- `depthWrite = !zmsk` (desfaz o `false` que o `GLTFLoader` põe nos BLEND);
- `alphaTest = aref >= 0 ? (aref + 0,5)/128 : (abe ? 0,5/128 : 0)`. O `0,5/128` sem teste é desvio deliberado: no GS um texel com
  `As = 0` ainda grava Z; com a ordem do three diferente da travessia isso escondia coisas atrás de quads invisíveis;
- `transparent = alphaSort` (o three ordena de trás para frente, perto da OT de alfa); os demais com ABE ficam `transparent = false`
  com `CustomBlending` e `renderOrder` = índice da travessia, o que reproduz a ordem do hier. Primeiro passo mais barato, se a ordem não
  importar: `transparent = abe` para todos com ABE.

## 5. Luz da fase [certo]
- Chave 3 (ambiente): 0 em todas, menos UFO (15). Não entra na cor das fases: o q5 que chega ao VU1 vale 1 (comentário de
  `Scene::objectColor`: aplicado, a UFO sairia 2,13 vezes mais escura). O editor ignora.
- Chave 5 (três luzes paralelas): só valem para OBJECT com `dirLight` (bit 0x100 das flags em +0xC): 338 de 27.057 objetos do mundo
  (1.284 nas outras raízes: armas, power-ups). O resto da fase tem a luz pintada nos vértices. Conta do VU1 (comentário de
  `Scene::readParaLights`): `cor = min(255, Cv·Σ (cor_i/128)·max(0, n̂·L_i))`, sem ambiente (+ especular com `specLightMe`).
- Direção (`plightUpdateParaLights`, `src/common/plight.cpp`; `mathfRotMatrixRPH` não está decompilada, mas o resultado bate com o
  dump): `L = (cos p · sin h, cos p · cos h, sin p)` no jogo (Z para cima), apontando para a luz. METRO luz 0 (h 120, p 45) =
  (0,612; −0,354; 0,707) e luz 2 (h 0, p −50) = (0; 0,643; −0,766); CENTRAL luz 2 (h 0, p 25) = (0; 0,906; 0,423): iguais ao `paraL`
  (0x6DFC00, ×32) dos dumps; as cores `vu1LightColor` (0x25B238) = `cor/128`, também iguais.
- No editor: uniforme com as três direções (em eixos do three: `(x, z, −y)`) e cores; nos materiais de objetos `dirLight`, injetar no
  vertex shader `vec3 n = normalize(mat3(modelMatrix) * normal)` e multiplicar o `vColor.rgb` por `Σ cor_i·max(0, dot(n, L_i))`
  (girar o objeto no editor muda a luz, como no jogo). Vale para poucos objetos; pode ficar para depois.

## 6. O que é editável e como
- A névoa, o fundo e as luzes são só o `.MRS`. Um mod pode trazer `mods/<mod>/RST/<FASE>.MRS`: os arquivos `RST/*` passam pelo
  mesmo mecanismo de troca dos outros arquivos do disco e cabem em `resetcom::m_data[0xC000]` (`MODS.md`, "Fixed-size formats"). **Não
  precisa mudar o hook nativo.** Gravar copiando o `.MRS` do disco e trocando só o corpo das chaves 4 (`4 floats + 3 s32`), 2 (mesma
  cor: com o fundo escravo da névoa, a chave 2 sozinha não faz nada) e 5 (`15 floats`), sem mudar tamanhos. Cuidado: `maxR <= minR`
  faz o `fogUpdate` não mandar nada (fica a névoa da fase anterior).
- Alternativa, se o dono quiser tudo num arquivo só: um bloco `"env"` no `levels/<FASE>.json` aplicado por um hook depois do
  `resetcom::read` do `InitBeforeDbLoad` (`fogSetFogParms`, `plightSetParaLight` ×3, `plightUpdateParaLights`). Mais trabalho e sem
  ganho para o jogo; o `.MRS` já é o formato do jogo.
- `fogBurnThru` (+0x2C) e o ctx do GS também são editáveis no patch da imagem, mas não vejo uso para mod de fase agora.

## 7. Plano para o editor
`tools/lvlglb.py` (exportador; não editado nesta pesquisa):
1. Pré-passada com o estado do GS por nó (`pose.gs_apply` nos ops 1/3/4/30 em +4 e 17/38 em +8, a partir de `pose.GS_DEFAULT`), como
   o `monglb.py` faz com `gs_of`; e uma marca de céu para todo nó abaixo de um nó id 2.
2. No `mesh()`: COLOR_0 cru (`/255`), sem ganho nem limiar de 0,9; `material(node, slot, md.abe, gs_of[node])`, e acrescentar à chave
   e aos `extras` do material `fog` (`burn < 255`) e `ot` (`alphaSort`). O `gltf.Materials` já põe `extras.gs = {blend, aref, zmsk}`
   quando recebe `gs`; falta um parâmetro opcional para os extras novos (o `gltf.py` é dividido com o `monglb`/museu: não quebrar o
   padrão). Com ABE, `alphaMode = BLEND` sempre.
3. Textura com o alfa cru (`hi=True`) e `extras.gsRaw = true` no asset; trocar a chave do cache do `.glb`.
4. Nó do céu com `extras.sky = true` (o grupo do nó id 2), em vez de depender do id da raiz.
5. `mesh.extras.dirLight` (bit 0x100 das flags do OBJECT), se for fazer a seção 5.

`tools/level_editor/editor.py`: no `/api/stage`, um campo `env` lido com `lvl.read_mrs`: `{fog: {minR, maxR, maxV, farClip, rgb, fa,
fb}, bg: rgb, lights: [{rgb, h, p, dir}]}` (FA/FB já calculados como o `fogCalcFogValues`).

`tools/level_editor/web/app.js`:
1. Espaço de cor cru (4.1).
2. Pedaços de névoa (1.4) e `scene.fog` com FA/FB/cor do `env`; `fog = false` em grade, caixas e materiais com `extras.fog = false`.
3. `onBeforeCompile` da modulação (4.2) em todo material da fase quando `gsRaw`.
4. Mistura, Z e teste por `userData.gs` (4.4).
5. Céu: visível, preso à câmera, desenhado primeiro (3). Hoje ele aparece como geometria nas 7 fases da seção 3 e some nas outras.
6. Fundo = cor da névoa; botão "visual do jogo" (névoa, fundo, céu) para editar de longe sem névoa.
7. Depois, se valer: luz `dirLight` (5) e o far clip por objeto (1.5).
8. Conferir com o jogo: o mesmo ponto de vista no editor e no `run_live.bat` (fases com névoa curta: METRO, 3-MILE, TOKYO; aditivos:
   UFO, 3-MILE); a câmera livre F5 do nativo ajuda a casar o ângulo.
