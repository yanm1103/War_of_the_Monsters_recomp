# Editor de fase: visual, acessibilidade e responsividade (angulo 4, prefixo VA)

Auditoria somente de leitura de `tools/level_editor/web` (`index.html` 662 linhas, `style.css` 814, `app.js` 4790 e os modulos) e das capturas de `docs/screenshots/2026-11-09/*_editor_ux_*.png`. Nada foi editado nem rodado. Razoes de contraste calculadas com a formula WCAG 2.x (luminancia relativa sRGB, `(L1+0.05)/(L2+0.05)`) sobre os tokens de `style.css` linhas 3 a 60; as cores com `color-mix` foram misturadas em sRGB sobre a superficie real. Os temas claro e escuro foram avaliados por calculo: so ha capturas do escuro.

Escala de severidade: 0 cosmetico, 4 catastrofico (Nielsen). Esforco: S (algumas linhas de CSS/HTML), M (um dia, mexe em JS), L (reorganizacao).

## 1. Resumo

1. O problema visual mais grave nao e de cor, e de **geometria**: as duas barras flutuantes da vista (`#toolBar` e `#overlay`) sao ancoradas em cantos opostos e nao sabem uma da outra. Em 1600x900, no modo Pedestrians, a barra de vista cobre o botao "Add sidewalk" (captura 03); com Snap ligado cobre "Add prop"; em 1100x650 cobre a barra Move/Turn/Snap/Add prop inteira (captura 14). Isso e VA-1.
2. A barra de vista (`#overlay`) tem 16 controles e metade deles nao e controle de vista: sao atalhos para dialogos (Stage size, Start points, Power-ups, AI net, Vehicle net, Stage picture, atalhos, screenshot). Exatamente o sintoma que o dono descreveu ("gerador de chao dentro do Add prop"): o lugar de cada funcao foi decidido pelo espaco que sobrava na barra, nao pelo tipo. Isso e VA-2.
3. A grade e fixa: `main { grid-template-columns: 44px 272px 1fr 304px }` (style.css:166) deixa a vista com `largura - 620px`. Em 1100 de largura a vista tem 480px; nao ha colapso de paineis nem `min-width` (VA-3).
4. Acessibilidade: o teclado nao chega a lugar nenhum na vista. As linhas da lista de objetos nao sao focaveis, o canvas nao tem nome, e ha cerca de 45 paradas de Tab antes do painel direito (VA-4, VA-5). O foco visivel e so `box-shadow`, que some em modo de alto contraste, e no escuro o anel tem ~2,3:1 (VA-8, VA-9).
5. Contraste de texto e bom no essencial (texto principal 13,6:1 escuro e 16,6:1 claro; botao primario 5,17:1), mas falha em pontos especificos: handles hexadecimais da lista (2,5:1 escuro, 2,3:1 claro), `--ok` e `--warn` como texto no tema claro (2,96 a 3,64:1), `--muted` sobre `--surface-2` e hover (4,13 e 3,60:1 no escuro) (VA-7).
6. 33 botoes so tem icone e dependem de `title` nativo; ha pares de icones iguais para coisas diferentes (VA-10, VA-11).
7. Idioma: toda a interface esta em ingles; nao achei portugues. So ha mistura de grafia UK/US (VA-23, sev 0).

## 2. O que esta bom (nao mexer)

- Tokens de cor em `:root` com temas escuro, claro e sistema, e script no `<head>` que evita o flash de tema (index.html:8-12). O texto principal tem 13,58:1 (escuro) e 16,56:1 (claro) sobre `--surface-1`; `--fg-2` tem 8,08 e 7,90.
- Hierarquia do botao primario: "Test in game" e o unico preenchido de azul na barra superior (captura 01), com `--accent` + branco 5,17:1. A regra e clara: secundario por padrao, `ghost` para icone de configuracao, `primary` para a acao final do dialogo.
- Todo botao so de icone tem `aria-label` e `title`; `aria-pressed` no Move/Turn, Fly/Orbit e modos; `aria-expanded` nos menus; `<dialog>` nativo com `aria-labelledby` e armadilha de foco do navegador; `[hidden]` blindado por `!important`.
- Foco por teclado consistente nos botoes (`--focus` com anel de 2px separado por 2px do fundo), visivel nas capturas 15 e 15b; campos de texto usam borda `--accent` (3,41:1 no escuro, 5,17:1 no claro) mais halo.
- `.tgl`, `.chip`, `.switch` e `.seg` sao `<label>` + checkbox/radio reais: teclado e leitor de tela funcionam sem JS.
- Dialogos com cabeca, corpo que rola e rodape; `max-height: calc(100vh - 48px)` evita o dialogo maior que a janela. Menus `#propMenu` e `#layersMenu` limitados a `calc(100vh - 64px)`.
- A consulta de container (`@container (max-width: 820px)`, style.css:432) tira os rotulos das barras flutuantes quando a vista estreita. A ideia esta certa; o limite e que esta mal calibrado (VA-1).
- `font-variant-numeric: tabular-nums` nos campos numericos; `color-scheme` declarado; `lang="en" translate="no"`.

## 3. Quanto da vista 3D fica livre

Medido nas capturas (coordenadas em px da captura).

| Situacao | Vista (L x A) | Ocupado por barras e notas | Livre |
|---|---|---|---|
| 1600x900, tela inicial (captura 01) | 981 x 828 = 812 k px2 | `#toolBar` 344x40 = 13,8 k; `#overlay` 578x40 = 23,1 k; nota amarela 521x57 = 29,7 k; total 66,6 k | 91,8 % |
| 1600x900 com minimapa (160x160 = 25,6 k) e dica (354x28 = 9,9 k) | 812 k | 102 k | 87,4 % |
| 1600x900 com a gaveta Output aberta (200px) | 981 x 628 = 616 k | mesmas barras, 102 k | 83,4 % |
| 1100x650, tela inicial (captura 14) | 480 x 578 = 277 k | `#overlay` 472x40 dentro da vista (511 de largura, 39px vazam sobre a lista) = 18,9 k; nota amarela de 4 linhas 265x137 = 36,3 k; total 55 k | 80 % |
| 1100x650 com minimapa e dica | 277 k | cerca de 90 k | 68 % |
| 1100x650 com Output aberto | 480 x 378 = 181 k | cerca de 90 k | 50 % |

Leitura: em 1600x900 a vista esta saudavel (acima de 83 % em todos os casos). Em 1100x650 a vista inteira e menor que a barra de vista, o aviso de mod ocupa 13 % dela sozinho e, com a gaveta aberta, metade da vista some. O pior e que, em 1100, a area "livre" tem a barra Move/Turn/Snap **inalcancavel** (VA-1).

## 4. Achados

### VA-1. A barra de vista cobre a barra de ferramentas

- **Onde:** `#toolBar` (`left: 8px`) e `#overlay` (`right: 8px`), style.css:420-422 e 433; container query em style.css:432; index.html:98-114 e 132-153.
- **Heuristica:** N4 consistencia, N7 flexibilidade, N5 prevencao de erro. Severidade **3**.
- **Problema:** as duas barras sao `position:absolute` em cantos opostos sem regra de colisao. A de vista tem largura minima de ~510px so com icones (14 botoes de 28+2px, mais o seg Fly/Orbit e 2 separadores) e 578px com rotulos. A consulta `@container (max-width: 820px)` so tira os rotulos; a soma das duas so cabe a partir de ~1056px de vista (1676px de janela) com rotulos, ou ~684px (1304px de janela) so com icones. Casos reais:
  - 1600x900, modo Pedestrians (captura 03): `#pedBar` aumenta a toolbar para ~400px e a barra de vista cobre "Add sidewalk": so se ve o icone e metade do rotulo.
  - 1600x900, Snap ligado: os dois campos de passo somam ~118px e a barra de vista cobre "Add prop" (calculo: 324+462 > 710).
  - 1100x650 (captura 14): a barra de vista (277 a 788) cobre toda a toolbar (324 a ~460): Move, Turn, Snap e Add prop deixam de existir para o mouse, e a barra de vista ainda invade 39px da lista a esquerda.
- **Proposta:** envolver as duas num unico contêiner `#vbars` (`position:absolute; inset:8px 8px auto 8px; display:flex; flex-wrap:wrap; justify-content:space-between; gap:8px; pointer-events:none`) com os filhos `pointer-events:auto` e sem `position:absolute` proprio. Em vista estreita a barra de vista passa para a segunda linha em vez de cobrir. Ajustar `#notes` (`top:52px`) e `#propMenu`/`#layersMenu` (`top:48px`) para `top: calc(var(--vbars-h, 48px) + 8px)` ou posicionar os menus relativos ao botao que os abriu. Subir o limite do container query de 820px para ~1060px, ou melhor, medir (VA-2 reduz a barra a metade e resolve o resto).
- **Esforco:** S.
- **Evidencia:** capturas 03, 14; `#overlay` mede 511px em 14; style.css:420-422.

### VA-2. A barra de vista mistura controles de vista com atalhos de dialogos

- **Onde:** `#overlay`, index.html:132-153 (`gridBtn`, `sptBtn`, `pupBtn`, `aipBtn`, `fopBtn`, `picBtn`, `keysBtn`, `shotBtn`).
- **Heuristica:** N4 consistencia e padroes, N6 reconhecer em vez de lembrar, N8 estetica minimalista. Severidade **3**.
- **Problema:** o aria-label da barra e "View", mas dos 16 controles so 8 sao de vista (Game look, Grid, Layers, Fly, Orbit, Focus, Frame, Ruler, e o Screenshot). Os outros abrem dialogos de edicao de dados da fase (Stage size, Start points, Power-ups, AI net, Vehicle net) ou fazem outra coisa (Stage picture, atalhos, que duplica o `?` da barra superior). Sao 6 icones de dialogo lado a lado entre Frame e Ruler, todos `ghost` de 28px, todos parecidos com um icone de "caixa/rede" (VA-11). Editores maduros separam: Blender deixa o header do viewport com so modo de sombreamento, overlays e gizmos, e poe o que cria ou configura dados em menus Add/Object e no painel de propriedades; o Creation Kit tem Cell View/Object Window/menus para dados e a Render Window so para a vista; Unity/Godot tem a toolbar fina e a "cena" em menus.
- **Proposta:** a barra de vista fica com Look, Grid, Layers, Fly/Orbit, Focus, Frame, Ruler, Screenshot (nove controles, ~300px). Os seis dialogos de dados da fase passam para um unico botao `Stage ▾` (icone `map-outline`, rotulo visivel) na barra superior ao lado do select Stage, com um menu em dois grupos: "Layout" (Stage size, Stage picture) e "Gameplay data" (Start points, Power-ups, AI net, Vehicle net). Remover `keysBtn` (ja existe `#help`). O "Terrain from a heightmap" e o "Import .glb" saem do `#propMenu` para o menu Add (ver nota abaixo). Isso e o que o dono apontou, e este angulo mostra a consequencia visual: a barra encolhe de 578 para ~300px e some a causa de VA-1.
- **Esforco:** M (mover botoes e criar um menu; reusa o padrao `#layersMenu`).
- **Evidencia:** captura 01 (os 16 controles em 578px), captura 04 (Terrain from a heightmap dentro do menu Add prop, abaixo de dois seletores de Sky dome e Cloud layer).

### VA-3. A grade principal e fixa: a vista vira o que sobra

- **Onde:** `main { grid-template-columns: 44px 272px minmax(0,1fr) 304px }` (style.css:166); `body { overflow:hidden }` (style.css:76); `#bar { overflow:hidden; flex-wrap:nowrap }` (style.css:196).
- **Heuristica:** N7 flexibilidade e eficiencia, N8 minimalismo. Severidade **3**.
- **Problema:** os tres paineis laterais somam 620px, sem `clamp`, sem colapso e sem `min-width` no corpo. Tamanho da vista: 1920 -> 1300px; 1600 -> 980; 1366 -> 746; 1280 -> 660; 1100 -> 480; 900 -> 280. A unica concessao e a consulta de container das barras flutuantes. Na captura 14 o painel direito (304px) tem a metade da largura util do que e preciso para o aviso "Mod ZZZSpike..." e a vista fica com 44 % da janela.
- **Proposta:** (a) `grid-template-columns: 44px clamp(220px, 18vw, 272px) minmax(0,1fr) clamp(248px, 20vw, 304px)`; (b) abaixo de 1200px o painel esquerdo vira gaveta sobreposta, aberta pelo botao Objects da trilha (que ja existe e ja alterna modo); (c) `body { min-width: 960px; overflow: auto }` para que, abaixo disso, a janela rolle em vez de esmagar a vista; (d) `@media (max-width: 1199px)` na barra superior (ver VA-19).
- **Esforco:** M.
- **Evidencia:** captura 14; style.css:166.

### VA-4. Selecionar um objeto nao e possivel pelo teclado

- **Onde:** `rowEl` (app.js:1191-1226: `li.it` com so `onclick`, sem `tabIndex`, sem `role`, sem `aria-selected`); `#canvas` (index.html:94, sem nome nem `tabindex`); cabecalhos de secao (app.js:1240-1242, `li` com `role="button"` dentro de `ul`).
- **Heuristica:** N7, acessibilidade (WCAG 2.1.1 Teclado, 4.1.2 Nome/papel/valor). Severidade **3**.
- **Problema:** as 331 linhas da lista so reagem ao mouse; o canvas so reage ao mouse. O unico caminho por teclado para escolher um objeto e a paleta Ctrl+P (palDlg), que o usuario precisa conhecer. O atalho `ArrowUp/Down` e das setas de "nudge", entao nao da para usar setas para navegar a lista mesmo que ela fosse focavel. Os cabecalhos de secao viram `role="button"` num `ul`, o que quebra a semantica de lista.
- **Proposta:** transformar `#list` em `role="listbox"` com `aria-label`, linhas com `role="option"`, `aria-selected` espelhando `.sel`, `tabindex=-1` e roving tabindex (um `tabindex=0` na linha selecionada ou na primeira); Up/Down/Home/End movem e selecionam quando o foco esta na lista (como Unity/Godot na hierarquia); cabecalhos de secao como `role="presentation"` + botao interno. Dar ao canvas `role="application"`, `aria-label="Stage view"` e `aria-describedby="hint"`.
- **Esforco:** M.
- **Evidencia:** codigo; captura 15b mostra o Tab saindo da barra e indo para a trilha lateral, sem passar por objetos.

### VA-5. Ordem de Tab com 45 paradas antes do painel direito

- **Onde:** ordem do DOM: `#bar` (11) -> `#rail` (5) -> `#left` (busca, 4 chips, 3-4 cabecalhos) -> `#view` (`#toolBar` 4 a 6, `#overlay` 15) -> `#right`; `role="toolbar"` em index.html:98 e 132.
- **Heuristica:** N7, WCAG 2.4.3 Ordem do foco e 2.4.1 Contornar blocos. Severidade **2**.
- **Problema:** quem navega por teclado precisa de cerca de 45 Tabs para chegar ao painel Transform (em 1600x900 com um objeto selecionado). Os dois `role="toolbar"` declaram o padrao WAI-ARIA mas nao implementam o teclado dele (uma so parada de Tab, setas entre os botoes). Nao ha link "pular para o painel de propriedades". Captura 15: 6 Tabs ainda estao na barra superior; 15b: mais 6 e ja esta na trilha.
- **Proposta:** roving tabindex nos dois `role="toolbar"` e na `#rail` (`tabindex=-1` nos irmaos, setas movem), o que reduz as 22 paradas da vista e as 5 da trilha a 3; atalho de teclado para focar o painel direito (F6 percorre regioes, como no Creation Kit/VS Code). Estimativa: de 45 para ~20.
- **Esforco:** M.
- **Evidencia:** capturas 15 e 15b; contagem do index.html.

### VA-6. Notas de aviso permanentes sobre a vista 3D

- **Onde:** `#notes` (style.css:482-484), `#winNote` e `#keepNote` (index.html:162-167).
- **Heuristica:** N8 minimalismo, N1 visibilidade do estado (de forma boa, mas desproporcional). Severidade **2**.
- **Problema:** o aviso "Mod ZZZSpike also changes Midtown..." fica sempre sobre a cena, em ambar (o segundo elemento mais chamativo depois do botao Test in game na captura 01), com `pointer-events:auto` capturando cliques sobre a vista. Em 1600 ocupa 29,7 k px2; em 1100 sao 4 linhas e 36,3 k px2 (13 % da vista). A mesma informacao (mod ativo, quem ganha) ja existe no painel direito em `#sumMod`, na barra de estado e no select Mod, onde nao compete com a cena. O botao de dispensar (22x22) vale para a sessao, mas reaparece no proximo carregamento.
- **Proposta:** (a) uma unica linha de 28px em formato de chip ("1 warning: ZZZSpike also changes Midtown"), com a nota completa em popover ao clicar; ou (b) mover o texto para `#sumMod` (painel direito, "Nothing selected") e deixar so um ponto ambar no botao Mod; (c) lembrar a dispensa por mod em `localStorage` (por mod+fase). Qualquer uma devolve ~30 k px2 a vista.
- **Esforco:** S.
- **Evidencia:** capturas 01 e 14.

### VA-7. Contraste de texto: pontos que falham 4,5:1

- **Onde:** tokens `--muted`, `--ok`, `--warn`, `--danger`, `--accent-hover` (style.css:5-30); `li.it .hd` (style.css:293); cores fixas `#B57BFF` e `#36C5F0` (style.css:690-691).
- **Heuristica:** N1 visibilidade, WCAG 1.4.3. Severidade **2**.
- **Problema:** calculo por par (texto pequeno, 11 a 13px, exige 4,5:1):

| Par | Escuro | Claro | Onde |
|---|---|---|---|
| `--muted` sobre `--surface-1` | 4,61 | 4,78 | ok |
| `--muted` sobre `--surface-2` | **4,13** | 4,42 | seg, kbd, caixas |
| `--muted` sobre hover `--surface-3` | **3,60** | 4,11 | texto fraco ao passar o mouse |
| `--muted` sobre `--bg` | 5,01 | **4,04** | trilha e fundos |
| handle `0x...` com `opacity:.6` (lista, repouso) | **2,52** | **2,31** | `li.it .hd`, 11px mono |
| `--ok` sobre `--surface-1` | 6,65 | **3,46** | `.pstate.ok` |
| `--changed` (=`--ok`) sobre o fundo do badge (14 %) | 5,23 | **2,96** | `.badge` 11px 600 |
| `--warn` sobre `--surface-1` | 7,66 | **3,64** | `.warn`, `.perr.warn`, medidor de budget |
| `--danger` sobre o fundo do badge Deleted | **4,29** | **3,93** | `.badge.del` |
| `--danger` sobre hover do botao perigoso | **3,42** | **3,40** | `button.danger:hover` |
| branco sobre `--accent-hover` (dark) | **4,25** | 6,32 | Test in game com mouse em cima |
| violeta `#B57BFF` / ciano `#36C5F0` (icones da lista de pedestres) sobre branco | n/a | **2,90 / 2,02** | `#pedList li.it .k` (nao-texto, precisa 3:1) |
| `--disabled` sobre `--surface-1` | 2,49 | 2,34 | desabilitado e isento, mas veja abaixo |

  O handle hexadecimal e a identidade do objeto; escurecer para 2,5:1 em repouso o torna o dado mais dificil de ler da lista. Na captura 02 o "Restore original" desabilitado quase some.
- **Proposta:** (a) `li.it .hd { opacity: 1; color: var(--muted) }` (4,61 escuro / 4,78 claro) em vez de opacidade .6; (b) escuro: `--muted: #8B94A1` (4,83 sobre surface-2, 5,39 sobre surface-1); (c) claro: `--ok: #167A4D` (5,35:1 sobre branco; 4,39 sobre a mistura de 14 %, reduzir a mistura a 10 %), `--warn` para texto `#8F5C0E` (5,67), `--muted: #5F6773` (4,82 sobre `--bg`); (d) `--accent-hover` escuro `#2A68E8` (4,95:1 com branco); (e) icones de pedestres por tokens `--ped-grp` e `--ped-seg`, claros `#8A4DE0` (4,98) e `#0B7FA5` (4,57); (f) `--disabled` escuro `#6B7380` (>=3:1) para o desabilitado ao menos continuar legivel.
- **Esforco:** S.
- **Evidencia:** `python wcag.py` no scratchpad da sessao (formula WCAG); capturas 01 e 02 (handles e Restore original).

### VA-8. Contraste nao-textual: bordas de controle, trilho do switch, anel de foco

- **Onde:** `--line-strong` (style.css:6 e 38); `.switch > .track` (style.css:148); `--focus` (style.css:61).
- **Heuristica:** WCAG 1.4.11 (3:1 para contornos de controle e indicadores de estado). Severidade **2**.
- **Problema:** `--line-strong` (borda de botoes, inputs, selects, trilho do switch desligado) tem 1,66:1 sobre `--surface-1` no escuro e 1,63:1 no claro; o trilho do switch desligado e a unica pista de "off" alem da posicao do botao: 1,66:1. O anel de foco `--focus` e `accent` a 75 % de opacidade sobre o fundo: ~2,3 a 2,5:1 no escuro (captura 15 mostra o anel azul-escuro fraco ao redor de Save), 3,5:1 no claro. Os campos sao identificados tambem pelo fundo `--input-bg` (diferenca de 1,1:1, tambem sem 3:1).
- **Proposta:** escuro `--line-strong: #6A7482` (3,49:1) so para inputs/selects/trilho; manter a atual para botoes secundarios (tem texto). Claro `--line-strong: #8A929E` (3,14:1). `--focus: 0 0 0 2px var(--bg), 0 0 0 4px var(--accent-text)` (7,46:1 escuro, 5,93:1 claro).
- **Esforco:** S.
- **Evidencia:** calculo; captura 15 (anel de foco) e 10 (switch Training mode).

### VA-9. Foco e icones somem no modo de alto contraste

- **Onde:** `button:focus-visible { outline: none; box-shadow: var(--focus) }` (style.css:94); `.ic { background: currentColor; mask }` (style.css:82-83); nao ha nenhum `@media (forced-colors: active)` nem `prefers-contrast` no arquivo.
- **Heuristica:** WCAG 2.4.7 e 1.4.11; N1. Severidade **2**.
- **Problema:** em `forced-colors` o navegador remove `box-shadow` e troca `background-color` pelas cores do sistema. O foco e todo `box-shadow` com `outline:none`, entao o foco fica invisivel; os icones, que sao `background:currentColor` mascarado, pintam com a cor de fundo e desaparecem, deixando 33 botoes sem nada visivel. (Comportamento conhecido do navegador; nao testei no Windows com contraste alto.)
- **Proposta:** acrescentar `outline: 2px solid transparent; outline-offset: 2px` ao lado do `box-shadow` (o outline transparente vira visivel em forced-colors) e um bloco `@media (forced-colors: active) { .ic { background: CanvasText; forced-color-adjust: none } button:focus-visible { outline-color: Highlight } }`. Quatro linhas.
- **Esforco:** S.
- **Evidencia:** style.css:82, 94, 138; busca por `forced-colors` sem resultado.

### VA-10. 33 botoes so de icone, com dica so em `title`

- **Onde:** barra superior (`newStage`, `editStage`, `packMod`, `undo`, `redo`, `testHere`, `testOpts`, `stop`, `help`, `settings`; mais `check`, `save`, `test`, `reload` abaixo de 1440px, style.css:208-212); trilha `#rail` (5); `#overlay` (14); trilha de `#status` e `Textures`/`HUD` (acoes por cartao).
- **Heuristica:** N6 reconhecer em vez de lembrar. Severidade **2**.
- **Problema:** a descoberta depende de `title` nativo: aparece depois de ~1s de repouso do mouse, nao aparece ao receber foco do teclado e nunca em toque. Mais de metade dos botoes se identificam so por um glifo de 16 a 18px. Na trilha, os cinco botoes (Objects, Pedestrians, Textures, Light and fog, Fight HUD and sky) nao tem rotulo algum, e tres deles (Textures, Light, HUD) sao dialogos de uma ferramenta diferente dos dois modos acima; a unica dica e o separador de 1px (`.rsep`).
- **Proposta:** (a) uma dica propria em CSS (`[data-tip]::after` mostrada em `:hover` e `:focus-visible`, com atraso curto), preenchida por um laco unico que converte `title` em `data-tip` e anexa o atalho (G, R, L...); (b) legenda de 10px sob os icones da trilha (como o Blender nas abas do painel de propriedades; cabem em 44px: Objects, Peds, Tex, Light, HUD); (c) manter o rotulo visivel do Test in game e do Save abaixo de 1440px quando houver espaco (hoje a media query tira os dois ao mesmo tempo, em 1366 sobram 400px na barra).
- **Esforco:** M.
- **Evidencia:** index.html (contagem de `class="icon` e `.ic` sem `.lbl`); captura 01 (trilha).

### VA-11. Icones iguais para coisas diferentes (e diferentes para a mesma coisa)

- **Onde:** index.html linhas indicadas.
- **Heuristica:** N4 consistencia e padroes. Severidade **2**.
- **Problema:** confirmei por `grep` dos `--ic:url(...)`:

| Icone MDI | Usos | Problema |
|---|---|---|
| `image-edit-outline` | `texBtn` (trilha, linha 49) e `picBtn` (vista, linha 149) | Textures (trocar PNG de textura) e Stage picture (usar a vista como figura da selecao de fase): nada em comum; os dois ficam em barras diferentes, mas o usuario nao consegue distinguir pela forma |
| `vector-polyline-plus` | `aipBtn` (AI net, linha 147) e `pedAddSeg` (Add sidewalk, linha 110) | e o mesmo glifo para rede de IA e calcada de pedestres; aparecem lado a lado na captura 03 (1180 e 693) |
| `package-variant-closed` | `pupBtn` (Power-ups), `pfSave` (Save selection como prefab), `.st.budget` (barra de estado) | tres coisas sem relacao |
| `restore` | `restore` (apagado), `reset` (Restore original), `tiltOff` (Level the prop) | tres significados diferentes (recolocar apagado, voltar ao disco, endireitar) |
| `help-circle-outline` e `keyboard-outline` | `help` (barra superior) e `keysBtn` (vista) | dois botoes para a mesma funcao com glifos diferentes |
| `arrow-expand-all`, `fit-to-screen-outline`, `crosshairs-gps` | Stage size, Frame all, Focus | tres glifos de "cantos", adjacentes na captura 01 (1000, 1029, 1059) |
| `account-group-outline` | Start points e icone de grupo de pedestres | Start points (onde nascem os monstros) parece pedestres |
| `weather-fog` | Game look | nao diz "como no jogo" |

- **Proposta:** Textures -> `palette-swatch-outline`; Stage picture -> `image-frame` (ou `camera-image`); AI net -> `graph-outline`; Add sidewalk fica com `vector-polyline-plus`; Power-ups -> `medical-bag`; Save as prefab -> `content-save-cog-outline`; Level the prop -> `rotate-left-variant` ou `arrow-collapse-vertical`; Start points -> `flag-checkered`; Stage size -> `vector-square`; Game look -> `eye-outline`; remover `keysBtn`. Conferir se cada nome existe no `vendor/mdi` (ha 80 icones vendorizados; copiar os novos de `@mdi/svg`).
- **Esforco:** S.
- **Evidencia:** capturas 01, 03; index.html:49, 110, 124, 144-152.

### VA-12. Alvos de clique abaixo de 24px e summary de 16px

- **Onde:** `details.fold > summary` (herda `.sec-title`, `line-height:16px`; style.css:86-87 e 262-266); `#status .st` e `#status > button.icon` (22px, style.css:545-548); `.callout button.icon` (22x22); `button.small-btn` (22px); `.pal-pg` (22px); `#outGrip` (8px); `.field > .pre.scrub` (~14x16).
- **Heuristica:** N7, WCAG 2.5.8 (minimo 24x24 AA). Severidade **2**.
- **Problema:** os botoes de icone principais tem 28x28 (suficiente para desktop; Blender usa 24, Unity 20-24). Mas os `summary` de Arrange, Behavior, Name e Details, usados o tempo todo para desdobrar o painel direito, tem 16px de altura em toda a largura; os botoes da barra de estado e de fechar notas, 22px; a alca de redimensionar a gaveta, 8px (4px visiveis) e sem alternativa de teclado.
- **Proposta:** `details.fold > summary { min-height: 28px; display: flex; align-items: center }`; `#status .st, #status > button.icon, .callout button.icon, button.small-btn { min-height: 24px; min-width: 24px }`; `#outGrip { height: 12px; top: -6px }`.
- **Esforco:** S.
- **Evidencia:** capturas 02c (summaries) e 01 (barra de estado de 28px com botoes de 22).

### VA-13. Semantica ARIA que promete mais do que entrega

- **Onde:** `role="menu"` em `#propMenu` e `#layersMenu` (index.html:115, 154); `role="tablist"`/`role="tab"` em `#output` (index.html:~590); meters `#mGroups`/`#mSegs` (index.html:77-80); `#hint` (`aria-live="polite"`).
- **Heuristica:** WCAG 4.1.2, N1. Severidade **2**.
- **Problema:** `role="menu"` exige filhos `menuitem*` e navegacao por setas; o menu Layers e feito de switches (checkbox) e o Add prop de botoes com `role="menuitem"` mas sem setas (so o Tab). Um leitor de tela anuncia "menu" e o usuario espera setas que nao funcionam. As abas tem `aria-selected` mas nao `aria-controls`, nao tem `role="tabpanel"` nos painéis e nao movem com seta. Os medidores Groups 16/20 e Sidewalks 16/127 sao `div` com `<i>` de largura, sem `role="meter"` e sem valor anunciavel. `#hint` e `aria-live` e muda a cada troca de contexto: ruidoso para leitor de tela e some depois de 8s (app.js:571), entao o anuncio e a unica forma de ler.
- **Proposta:** Layers: trocar `role="menu"` por `role="group" aria-label="Layers"` num popover (switches ja sao `checkbox`); Add prop: implementar setas Up/Down/Home/End entre os `menuitem` (a paleta Ctrl+P, app.js:4258, ja faz isso para cartoes); abas: `role="tabpanel"` + `aria-labelledby` + setas Left/Right; medidores: `role="meter" aria-valuemin=0 aria-valuemax=20 aria-valuenow=16 aria-label="Groups"`; `#hint` com `aria-live="off"` e a mesma dica acessivel em `aria-description` do canvas (VA-4).
- **Esforco:** S a M.
- **Evidencia:** index.html:115, 154, 77-80; app.js:132-134.

### VA-14. Tabelas de dialogo sem semantica e controles sem nome (Start points, Power-ups, AI net, Vehicle net)

- **Onde:** `.spt-row` (style.css:455-466), `ptRows` (app.js:3483-3520) e `netRows` (app.js:3662-3690); index.html:512-545.
- **Heuristica:** WCAG 1.3.1, 4.1.2, N10. Severidade **2**.
- **Problema:** as linhas sao `div` com `display:grid` e cabecalho `div` (`#`, Type, x, y, z, Links to), sem `role="table"/"row"/"columnheader"`. Cada linha tem 5 a 8 campos (`input type=number`, `select`, checkbox) criados com `mk('input', ...)` sem `aria-label`: o leitor de tela le "campo de edicao numerico" sem dizer se e x, y ou z. O botao de apagar tem nome acessivel "×" (o `title` vira descricao); "Aim" nao diz o que mira. Os rotulos `x`, `y`, `z` do cabecalho estao em minusculas, enquanto no painel direito sao `X`, `Y`, `Z` com cor de eixo (`--ax-x`...). Ha 5 `style=""` inline no `netDlg` (index.html:532-538).
- **Proposta:** `role="table"` no contêiner e `row`/`cell`/`columnheader` nas celulas (ou trocar por `<table>` com `display:grid` no CSS); `aria-label` por campo (`Node 3, X`); `aria-label="Delete node 3"` e `aria-label="Aim node 3 at the view"` nos botoes; cabecalhos X, Y, Z em maiuscula; mover os 5 estilos inline para uma classe `.fl.inline`.
- **Esforco:** S.
- **Evidencia:** captura 13 (cabecalho "x" minusculo, botoes "Aim" e "×").

### VA-15. Hierarquia dos botoes primarios: dois azuis competindo e Save mudo

- **Onde:** `button.primary` (style.css:98-100); "Make N copies" em `#secArrange`; `Close` em `texDlg`, `hudDlg`, `lightDlg`; `#bar button.dirty::after` (style.css:109).
- **Heuristica:** N4, N1. Severidade **2**.
- **Problema:** (a) na captura 02, "Test in game" (barra) e "Make 2 copies" (painel direito, largura total) sao os dois unicos blocos azuis da tela, com o mesmo peso; a segunda e uma acao rara e de efeito local, a primeira e a acao final do fluxo. (b) em `Textures`, `HUD` e `Light`, o rodape tem so um botao "Close" em azul primario (captura 07), mas as alteracoes ja foram gravadas ao fazer (o proprio aviso diz "Changes are written to your mod right away"): primario para dispensar. (c) quando ha alteracoes nao salvas, Save so ganha um ponto de 7px no canto; Save, Check e Reload tem o mesmo peso visual (tres botoes de borda cinza).
- **Proposta:** "Make N copies" como botao secundario com borda `--accent` (`button.accent-outline`); `Close` desses tres dialogos como secundario; `#bar button.dirty { border-color: var(--sel); color: var(--fg) }` e ponto de 8px com anel, para "pendente" ser visivel em 1 segundo. Nao mexer em "Test in game", que esta correto.
- **Esforco:** S.
- **Evidencia:** capturas 02, 07, 10, 13.

### VA-16. O foco inicial dos dialogos cai no botao de fechar (X)

- **Onde:** dialogos `.dlg` (index.html:365-640): `showModal()` focaliza o primeiro elemento focavel, o `X` do cabecalho; `#dlg` trata com `.focus()` (app.js:4607) e `keysDlg` tem `autofocus`.
- **Heuristica:** N5 prevencao de erro, N4. Severidade **1**.
- **Problema:** capturas 10 e 13 mostram o X com anel de foco assim que o dialogo abre. O Enter no "Test in game" (testDlg) fecha o dialogo em vez de rodar o jogo; o primeiro Tab vai para o primeiro campo.
- **Proposta:** helper unico `openDlg(d)` que chama `showModal()` e foca `d.querySelector('[autofocus], .dlg-body input, .dlg-body select, .dlg-foot .primary')`; ou `autofocus` em cada primeiro campo.
- **Esforco:** S.
- **Evidencia:** capturas 10 e 13.

### VA-17. Movimento: so o spinner respeita `prefers-reduced-motion`

- **Onde:** style.css:178 (`dot-pulse` infinito), 536-538 (`.spin` so mais lento), `.toast.out`, `.m-bar > i` transicao.
- **Heuristica:** WCAG 2.3.3. Severidade **1**.
- **Problema:** `@media (prefers-reduced-motion: reduce) { .spin { animation-duration: 2.4s } }` e a unica regra; o ponto do estado do jogo (`.game-ind.starting .dot`) pulsa infinito e as transicoes continuam. Nao ha camera suave nem movimento grande, entao o risco e baixo.
- **Proposta:** `@media (prefers-reduced-motion: reduce) { *, ::before, ::after { animation-duration: .01ms !important; animation-iteration-count: 1 !important; transition-duration: .01ms !important } .spin { animation: none; border-top-color: var(--accent); } }`.
- **Esforco:** S.
- **Evidencia:** style.css.

### VA-18. Tokens de espaco sem uso e dialogos antigos fora do sistema

- **Onde:** `--s-1..--s-7` (style.css:62, zero usos); `border-radius` literais (12 ocorrencias com 2, 3, 4, 6, 8 e 12px); `var(--hover)` indefinido em style.css:480 e 485 (caindo em `rgba(128,128,128,.22)`); fallbacks `#4aa3ff`/`#4c8dff` (style.css:470, 480, 485); `.pal-tab { height: 30px }` e `.pal-pg { height: 22px }` fora do `--h-ctl: 28px`; ~48 declaracoes de espaco com 10, 14, 18, 20, 22px.
- **Heuristica:** N4 consistencia. Severidade **1**.
- **Problema:** o sistema de tokens existe, mas so cores e raios (27 usos de `--r-*`) o seguem. O espacamento usa 6, 8, 10, 12, 14, 16, 18 e 20px sem escala; os dialogos de paleta, Start points e redes foram escritos antes do sistema (`.spt-row button { padding: 2px 8px }`, `dialog.pal { width: min(96vw, 2400px) }`) e se destacam: 24 a 28px de altura diferente nos botoes, hover cinza translucido em vez de `--surface-3`.
- **Proposta:** definir a escala de 4px (`--s-1: 4px`, `--s-2: 8px`, `--s-3: 12px`, `--s-4: 16px`, `--s-5: 24px`) e fazer uma passada que troca os literais; trocar `var(--hover, ...)` por `var(--surface-3)` e o raio 12px por `--r-lg`. Nao e urgente: e consistencia.
- **Esforco:** S a M.
- **Evidencia:** `grep -c "var(--s-"` = 0; `grep` dos raios.

### VA-19. Barra superior corta o canto direito abaixo de ~1000px

- **Onde:** `#bar { overflow:hidden; flex-wrap:nowrap }` com filhos `flex:none` (style.css:195-197); `@media (max-width: 1099px)` (style.css:213-217).
- **Heuristica:** N5, N7. Severidade **2** (estimado).
- **Problema:** em 1100x650 a barra cabe com folga (captura 14: sobram ~100px entre Redo e Check). Abaixo de 1099px o brand some e os selects diminuem (~185px de economia), mas o restante e `flex:none`; estimo (nao medi) que abaixo de ~850px `Help` e `Settings` sejam cortados sem aviso e sem rolagem. Como a vista ja desapareceu antes (VA-3), a janela nesse tamanho nao e usavel de qualquer forma; a correcao e o `min-width` de VA-3.
- **Proposta:** `body { min-width: 960px }` (VA-3); alem disso, agrupar `New stage`, `Edit stage`, `Pack mod` num menu `⋯` abaixo de 1200px.
- **Esforco:** S.
- **Evidencia:** captura 14, style.css:195.

### VA-20. Icones por mascara externa: botoes vazios no primeiro quadro

- **Onde:** `.ic { mask: var(--ic) ... }` com `url(vendor/mdi/*.svg)` carregados um a um (80 arquivos).
- **Heuristica:** N1. Severidade **1**.
- **Problema:** nas capturas 01, 02, 04, 05, 14 e 15 os botoes da trilha Objects, Pedestrians, Textures e Light, o `Move` da toolbar e o `Add prop` aparecem sem glifo (o Objects fica como quadrado azul vazio, captura 01); nas capturas 03, 07 e 10 os mesmos aparecem completos. Parece corrida de carga de mascaras no primeiro quadro (nao confirmei a causa). Como esses sao botoes so de icone, o intervalo deixa um quadrado sem sentido e, se um arquivo faltar, o botao fica invisivel para sempre sem texto de reserva.
- **Proposta:** gerar um `icons.css` com todos os `--ic` como `data:image/svg+xml` (~30 KB) para o primeiro quadro ja ter tudo, ou `<link rel="preload" as="image">` para os 20 icones da primeira tela.
- **Esforco:** S.
- **Evidencia:** capturas 01 e 03 comparadas.

### VA-21. Selecionado primario e seguidores diferem so pela matiz

- **Onde:** `--sel #FFC233` e `--sel-2 #FF8A3D` (style.css:11 e 41), `li.it.sel`/`.sel2` (style.css:301-302), gizmo.
- **Heuristica:** WCAG 1.4.1 (uso de cor). Severidade **1**.
- **Problema:** "o ambar e o que voce edita, os laranjas seguem" depende de distinguir amarelo de laranja (texto `#pGroup` explica, mas o sinal e so cor); no tema claro a faixa de 3px `--sel` (`#E0A100`) tem 2,27:1 sobre branco, abaixo de 3:1, so o fundo suave de 22 % ajuda.
- **Proposta:** acrescentar um cue de forma: marca de estrela/ponto cheio na linha primaria (`li.sel::after`) ou faixa pontilhada nos seguidores; escurecer `--sel` claro para `#B38000` (3,3:1).
- **Esforco:** S.
- **Evidencia:** calculo; captura 02 (so uma selecao).

### VA-22. Tipografia: 24 % das declaracoes de fonte sao 11px ou menos; nada escala com a preferencia do navegador

- **Onde:** `font-size` em px em todo o style.css; 9+6+7+1 declaracoes de 11px, 1 de 10px (`.actions > button kbd`, style.css:260); corpo 13px/20px.
- **Heuristica:** WCAG 1.4.4, N7. Severidade **1**.
- **Problema:** ha texto de 11px em handles, `sec-title`, `kbd`, `.help`, `.badge`. A fonte base e 13px, abaixo de 14 a 16px de editores web comuns. O zoom do navegador funciona, mas como a grade e fixa (VA-3), 200 % em 1600x900 vira uma janela de 800px CSS e a vista quase desaparece; em 400 % nao ha reflow. Nao ha uma configuracao de "UI size" nas Settings.
- **Proposta:** piso `--fs-xs: 12px`; um controle `Interface size` em Settings (90/100/115/130 %) que ajuste `html { font-size }` com todos os `px` de fonte e `--h-ctl/--h-row` passando a `rem`.
- **Esforco:** M.
- **Evidencia:** `grep` de `font-size`/`font:` (10 em 1, 11 em 24 declaracoes).

### VA-23. Grafia: mistura de ingles britanico e americano

- **Onde:** `arrange.js:28` ("neighbours"), `arrange.js:122,148` ("centre"), `tex.js:84,90` ("grey") contra "Behavior" (index.html:250) e "color" em todo lugar.
- **Heuristica:** N4. Severidade **0**.
- **Problema:** a interface e toda em ingles e consistente no restante (nao achei nenhuma palavra em portugues em `index.html` nem nos modulos); so a grafia varia. "Back to the disc's" (index.html:~520, 541) e uma frase truncada; "Save selection..." com reticencias e "Import .glb..." tambem.
- **Proposta:** padronizar em ingles americano (Behavior, color, neighbors, center, gray). "Back to the disc's" -> "Restore the disc's net" / "Restore the disc's points".
- **Esforco:** S.
- **Evidencia:** grep.

## 5. Ordem sugerida

1. VA-1 (S) e VA-6 (S): devolvem a vista e a toolbar sem decisao de design.
2. VA-7, VA-8, VA-9, VA-16, VA-17 (todos S): um commit de `style.css` e uma funcao `openDlg`.
3. VA-11, VA-12, VA-15 (S): icones, alvos e primarios.
4. VA-2 (M): o menu `Stage ▾`, que e a resposta direta a queixa do dono e leva VA-1 e VA-10 junto.
5. VA-3, VA-4, VA-5, VA-10 (M): grade responsiva, listbox, toolbars com roving tabindex, tooltips.

Nao e preciso trocar de tecnologia: tudo acima e CSS, HTML e funcoes isoladas de `app.js`.
