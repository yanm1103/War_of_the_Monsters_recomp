# Editor de fase: plano de retoque visual e de UX

Pedido do dono: "dar uma boa retocada visual, pra deixar a UX bem mais intuitiva e visualmente agradável (atualmente está super
crua)". Restrições: ícones só MDI ou Remix Icon, em SVG vendorizado em `web/vendor/`, nunca emoji; nada de CDN (a página roda
local e empacotada no exe); interface em inglês; tema claro e escuro com tokens no `style.css`; o público são modders de PC, não
programadores. Logo entram o modo de pedestres (`web/peds.js`, `pedsapi.py`), o diálogo de HUD (`web/hud.js`, `hudapi.py`) e o
botão Reload in game, que já existe.

Fontes: três revisões independentes (fluxo, visual, descoberta), conferidas uma a uma no código de `f151def` (`app.js`,
`index.html`, `style.css`, `hud.js`, `peds.js`, `editor.py`, `hudapi.py`, `pedsapi.py`, `tools/lvl.py`,
`native/runtime/wotm_levelpatch.inc` e `wotm_levelreload.inc`) e nas capturas (`docs/screenshots/2026-11-06/2026-11-06_editor_*.png`
e, na pasta de rascunho, `multi.png`, `editor_shot2.png`, `final_*.png`). Nada foi editado nem rodado.

Marcação: **[certo]** = lido no código ou visto na captura; **[hipótese]** = dedução que precisa ser conferida.

## 1. Como está hoje

- **Capturas.** `2026-11-06_editor_central_gs.png` e `_ufo_gs.png` (1400×900, as mais novas) mostram a barra de cima com
  `WotM Level Editor | Stage [Midtown (CENTRAL)] | Mod [Teste] | Undo Check Save [Test in game] Reload in game (cinza) ... engrenagem`.
  Todos os botões têm o mesmo peso, menos o Test, que é azul.
- **Esquerda.** Filtro, três checkboxes (Buildings, Pickups, Props) e "Changed" sozinho numa segunda linha. A lista é monoespaçada
  e tem três colunas: `prop | Prop (itálico cinza) | 0xFF91C0`. No CENTRAL são dezenas de "Prop" iguais; no UFO,
  `destr  Building (50 hp)  0xDB4100`. O rodapé diz `335 shown · 1 changed`.
- **Centro.** A cena, com "Game look / Grid" em checkboxes no canto superior direito. Embaixo, a dica corrida de cerca de 25
  atalhos em 11 px, que quebra em duas linhas e no UFO passa por cima da estação.
- **Direita.** 290 px vazios com "Select an object in the view or in the list.".
- **Rodapé.** Log de 140 px com uma linha só (`CENTRAL: 331 objects, patch of mod "Teste" with 1 entries`).
- **`multi.png`.** Versão anterior a `a8ea2f0`: título ainda é a alça, lista sem nomes. Mostra a lista com 119 linhas em azul
  cheio, a seleção em laranja e amarelo na cena e "Carry the objects on top ( 0 )" com espaços. O painel direito tem barra de
  rolagem horizontal e o campo Z aparece cortado (ver D1).
- **`editor_shot2.png`.** Ainda mais antiga: vista vazia, lista só de alças.

Resumo: a página funciona e o desenho da fase está fiel, mas a moldura tem cara de formulário de depuração. Não mostra estados
(mudança não salva, resultado do Check, jogo aberto) e fala com quem escreveu o `lvl.py`.

## 2. Problemas por gravidade

Os achados repetidos entre os revisores foram juntados. Cada item diz de onde saiu (F = fluxo, V = visual, D = descoberta, N =
achado novo desta conferência).

### Alta: perde trabalho ou quebra o fluxo

**P1. Um original apagado não volta pela lista** (F) [certo].
- `select()` (app.js l.526) troca item invisível por `null`, e `selectMany` (l.537) filtra os invisíveis.
- Na lista, a linha riscada não responde ao clique, e `Reset` exige seleção. Só um Undo em sequência traz o objeto de volta, e
  perde o que veio depois.

**P2. O Save pode apagar dados do arquivo do mod** (F + D + N) [certo].
- **a)** `save_patch` (editor.py l.358) só grava com `objects` ou `names`. Um patch só com `peds` faz o Save apagar o arquivo
  (`removed ...`).
- **b)** `buildPatch` (app.js l.341) refaz o patch só com `objects` e `names`. Qualquer outra chave do arquivo carregado some no
  próximo Save: o `peds` que o `MODS.md` (l.574) ensina a escrever à mão, e as chaves futuras.
- **c)** Uma entrada cuja alça não existe na fase vira uma linha de log no carregamento (`applyPatch`, l.375) e some do arquivo
  no próximo Save, sem pergunta.
- **d)** `check_patch` (editor.py l.308) chama `lp.apply` e `cut_nets`, mas não `lp.apply_peds`. O `lvl.py patch` chama, na
  ordem do gancho (lvl.py l.1343). O Check e o Save não conferem os pedestres.

**P3. O teclado age no lugar errado** (F + D + N).
- **a)** Com foco num campo, o `keydown` sai antes de tudo (l.918). Ctrl+S abre o "Salvar página como" do navegador, e Ctrl+Z
  desfaz o texto do campo [certo].
- **b)** O teste `input, select, textarea` também pega checkboxes. Depois de clicar em Snap, Grid, Game look, num filtro de tipo
  ou em Carry, nenhum atalho funciona (nem WASD) até clicar na cena [certo].
- **c)** Com o seletor Stage focado depois de trocar de fase, as setas e as letras mudam a fase: S vai para "Sunshine Canyon", D
  para "Dodge ball", seta para baixo para a seguinte. Sem mudança pendente, o `onchange` carrega outra fase sem perguntar
  [hipótese forte: é o comportamento do Chromium com `<select>` fechado; conferir no harness].
- **d)** Com um diálogo aberto, o foco está num botão e Delete, G, R, setas e WASD agem sobre a seleção atrás do modal. Esc fecha
  o diálogo e também desseleciona [certo].

**P4. Estados invisíveis** (F + V + D) [certo].
- **Mudança não salva:** só um " *" no `document.title`, que o pywebview e o app mode não mostram. O botão Save fica igual nos
  dois estados.
- **Check:** despeja no log a saída do `lvl.py`, repetida em todo Save (`entry 3 skipped: ...`, `box rule: 331 ok (error <= 1)
  ...`). O Save grava mesmo com a checagem falhando, e a falha só aparece na cor vermelha das linhas.
- **Mod vencedor:** o aviso põe `ok=false` (editor.py l.520), e o resultado inteiro fica vermelho.
- **Jogo:** que ele está aberto só se nota porque o Reload deixa de ficar cinza (consulta a cada 4 s). Não aparece qual fase está
  carregada nem se o patch entrou.

**P5. O ciclo do jogo é incoerente** (F) [certo].
- `launch_game` não confere `game_running()`. Test in game com o jogo aberto abre outra instância e sobrescreve `GAME["proc"]`:
  o Reload passa a mirar a nova, e a primeira fica órfã.
- Não há como fechar o jogo que o editor abriu.
- O Reload grava antes de pedir a recarga, sem dizer.
- Com o jogo em outra fase, o runtime ignora o pedido (`recarga pedida (arquivo) para X, mas a fase e Y: ignorada`), e isso só
  aparece como linha crua.
- `watchGame` só termina com `depois:|ignorad|desligad|sem patch` (l.835). As linhas `... sem quadro de luta em 10 s: descartada`
  e `... ja atendida` (wotm_levelreload.inc l.80-84) não batem: o editor espera 30 s e diz "no stage load seen yet" depois que o
  jogo já respondeu (N).

**P6. O campo Mod engana** (F + D + N) [certo].
- É texto livre com datalist. Mod vazio só dá erro depois de Save, Test ou Reload, e a mensagem está errada: diz "letters,
  digits, no leading _", mas o servidor só recusa `\ / : * ? " < > |` e `_` ou `.` no começo.
- O mod só vai para a config depois de Save, Test ou Reload (l.820).
- O conflito com um mod vencedor só aparece depois de gravar.
- **Novo:** ao cancelar o `confirm` de troca de mod (l.851), o nome novo fica no campo. O próximo Save grava a cena do mod antigo
  no mod novo e sobrescreve o `levels/<STAGE>.json` dele, se houver.

**P7. A primeira abertura sem disco não guia** (D) [certo].
- A vista fica cinza, a lista vazia, e aparece uma linha vermelha no log (`no disc set: pick the game's .iso ...`).
- A saída é uma engrenagem sem rótulo no canto.

### Média: atrapalha ou confunde

**P8. Linhas do jogo em português, cruas** (F + D) [certo].
- `game [fase] CENTRAL <- Teste (...)`, `JSON invalido, patch ignorado`, `antes:/depois:`, `vence`.
- O vermelho é decidido por regex em português (`ignorad|invalid|falh`).

**P9. Atalhos e câmera não se descobrem** (F + V + D) [certo].
- A dica corrida (l.250-253) é a única fonte de ajuda.
- Não há Redo nem Frame all: `frameAll` só roda ao carregar.
- No modo voo (o padrão), arrastar com o botão esquerdo não faz nada.
- Fly e Orbit só trocam pela tecla V.
- WASD usa `e.key` (l.932), o que quebra em AZERTY: Z não anda e Q desce.

**P10. Lista ilegível** (F + V + D) [certo].
- Monoespaçada, com a coluna de tipo cru (`destr/pickup/prop`), que não bate com os rótulos dos filtros (Buildings, Pickups,
  Props). O handle disputa atenção com o nome.
- Sem seções nem contagem por tipo.
- O selecionado é azul cheio: com 119 itens vira uma parede azul.
- O ponto de alterado é um caractere colado ao nome.
- "Changed" fica misturado com os tipos. O filtro de tipo vale também para a seleção por caixa, sem aviso.
- Desmarcar um tipo não o esconde na cena.

**P11. Painel de propriedades em jargão** (F + V + D) [certo].
- `pSub` = `0xFD0830 · pickup · 120 × 80 × 340 · id 0x1393 · 0 states · 0 collision paths · on ...`.
- Position são 3 campos sem X/Y/Z e sem dizer que Z é a altura.
- Move, Turn e Snap valem para a cena, mas ficam no painel e somem sem seleção.
- "Reset" fica ambíguo ao lado de Undo.
- "Model" renomeia o modelo em todas as fases, na hora e fora do Undo (o próprio `_test.html` mostra que o Undo não o desfaz).
- "No collision of its own." não diz a consequência.
- "( 0 )": os pedaços de texto do `label.row` viram itens flex com `gap`.
- Título em Consolas.
- Sem seleção, o painel não mostra nada útil.

**P12. Tema** (V) [certo].
- `data-theme="dark"` está fixo no HTML e só é trocado depois do `/api/init`: no tema claro, a página abre escura e depois
  troca.
- Não há opção System.
- Os tokens só cobrem cor.

**P13. Botões e controles** (V) [certo].
- Mesmo peso para tudo.
- Sem `:focus-visible` e sem `:active`.
- O hover só muda a borda.
- O desligado é só `opacity .5`.
- Undo nunca fica desligado.
- Tooltips só com a tecla (`title="G"`, `title="Ctrl+D"`).

**P14. Settings e erros do servidor** (F + V + D) [certo].
- Sete campos de caminho com rótulos longos. Três deles (ELF, working folder, dev ISO) são só de quem tem o clone.
- Sem Browse e sem dizer se o caminho vale.
- Erro 500 chega como `RuntimeError: game not found...` (editor.py l.478 e l.530), com o nome da exceção Python.

**P15. Cena** (V + N) [certo].
- `BoxHelper` com teste de profundidade: o prédio ativo some atrás de outros.
- Sem destaque ao passar o mouse, e o cursor não muda.
- O objeto apagado some de vez.
- Grade única de 100 unidades.
- O amarelo de hover do gizmo (0xffff00) é quase o da seleção (0xffcc33).
- Os campos X/Y/Z não têm as cores das setas.
- **Novo:** `toThree = (x, z, -y)`, então a seta azul do gizmo aponta para o **-Y** do jogo. Arrastar para a ponta da seta
  diminui o Y do campo.

**P16. Diálogo Fight HUD** (F + V) [certo].
- Os erros vão para o log atrás do modal (`HUD: choose a mod first`): clicar Replace sem mod parece não fazer nada.
- Grava no mod na hora, fora do Undo e do estado "não salvo", e não diz isso.
- Cita "Reload in game", mas esse botão fica atrás do modal.
- O aviso da versão B cita `CANYON1` e `TOKYODM`, nomes de arquivo.

**P17. Rodapé e carga** (V + D) [certo].
- O log ocupa 140 px fixos quase sempre com uma linha.
- Erro e acerto se distinguem só pela cor.
- "Loading…" em 18 px, sem dizer que a primeira abertura leva segundos. Com erro de carga, o véu some e a vista fica vazia.

### Baixa

- **P18.** Seleção múltipla (F) [certo]. "119 selected: the others follow this one." não explica amarelo e laranja. Delete com
  Carry ligado apaga também os objetos de cima, sem contagem.
- **P19.** `peds.js` (F + V) [certo].
  - `info()` fala em índices de nós (`walks the sidewalk 3-4 (250 long)`).
  - Mostra `lane ${width*10}` sem unidade, e os limites (20 grupos, 127 calçadas) só aparecem como exceção.
  - O grupo é 0xff8a3f, o mesmo laranja da seleção secundária (0xff8a3d), e `laneSel` 0xffd23f fica perto do amarelo da seleção.
- **P20.** Diálogos (F) [certo]. "Test the saved file" é confuso. Trocar de fase ou de mod usa `confirm()` nativo, só com
  OK/Cancel. O Test é sempre Togera em treino, e a tela não diz.
- **P21.** Detalhes (F + V) [certo]. "UFO (UFO)" repete o nome.

### Descartados ou corrigidos na conferência

- **D1. Estouro horizontal do painel direito (`multi.png`).** A captura é anterior a `a8ea2f0` (título ainda é a alça). O CSS
  atual (`#props .row input[type=number] { flex: 1 1 0; width: 0; min-width: 0 }`, style.css l.28) encolhe os campos. Pelo código
  não reproduz [hipótese: conferir a 1280 px]. Fica só o cuidado de `minmax(0,1fr)` na grade nova.
- **D2. "As cópias ficam soltas no fim da lista".** Errado: `items()` (l.474) põe cada cópia logo depois do original. Falta só o
  recuo.
- **D3. "hud.js injeta CSS fora dos tokens".** Errado em parte: usa `--warn`, `--line`, `--panel2` e `--muted`, e só o xadrez
  (#808080/#a0a0a0) é fixo. O problema real é o CSS morar no JS. Os nomes `--panel`/`--panel2` ficam como apelidos para não
  quebrar o módulo.
- **D4. "Manter `setSize` 1.0 no gizmo".** Hoje é 0.9. Fica 0.9.
- **D5. "O Reload desabilitado não diz por quê até o primeiro `setRunning`".** O `init` chama `setRunning` logo; irrelevante.
- **D6. "Midtown Park".** O título vem de `STAGE_TITLE` (editor.py), que diz "Midtown". Os textos abaixo usam o que o servidor
  manda.
- **D7. "Renomear Check para Validate".** Fica "Check": curto, já está no `MODS.md`, e o tooltip explica.

## 3. O sistema visual

### 3.1 Tokens de cor

Dois acentos com papéis separados: **azul** para controles, botão principal e foco; **âmbar** para seleção, na lista e na cena
(como no Blender). Os nomes antigos continuam como apelidos (`--panel: var(--surface-1)`, `--panel2: var(--surface-2)`), porque
`hud.js` e `peds.js` os usam.

| token | escuro | claro | uso |
|---|---|---|---|
| `--bg` | #15171B | #E9ECF0 | fundo da página, trilho |
| `--surface-1` | #1C1F24 | #FFFFFF | barras, painéis, diálogos |
| `--surface-2` | #24282E | #F4F6F8 | cards, cabeçalho de seção, botão secundário |
| `--surface-3` | #2D323A | #EBEEF2 | hover, popover |
| `--input-bg` | #16191D | #FFFFFF | campos |
| `--line` | #2E333B | #DDE1E7 | divisórias |
| `--line-strong` | #3D434D | #C5CBD3 | borda de controle |
| `--fg` | #E6E9EE | #1B1F24 | texto |
| `--fg-2` | #AEB6C2 | #4A525E | texto secundário |
| `--muted` | #7F8895 | #6B7380 | rótulos, metadados (contraste ≥ 4.5 nos dois temas) |
| `--disabled` | #565D68 | #A3AAB4 | desligado |
| `--accent` | #2563EB | #2563EB | botão principal (texto branco 5.2:1) |
| `--accent-hover` / `--accent-press` | #3B74F0 / #1E4FC4 | #1D55D6 / #1846B5 | |
| `--accent-text` | #7AB0FF | #1D5BD8 | ícone ativo, link |
| `--sel` | #FFC233 | #E0A100 | seleção ativa (borda, barra) |
| `--sel-2` | #FF8A3D | #F07A2A | outros do grupo |
| `--sel-bg` | `color-mix(in srgb, #FFC233 16%, transparent)` | `color-mix(in srgb, #F5B400 22%, transparent)` | fundo da linha selecionada |
| `--ok` / `--changed` | #3FB97F | #1F9D63 | passou, alterado |
| `--added` | #7AB0FF | #1D5BD8 | cópia |
| `--danger` | #F0605A | #D13B35 | erro, apagar |
| `--warn` | #E8A33A | #B7791F | aviso |
| `--ax-x` / `--ax-y` / `--ax-z` | #F0605A / #4C9AFF / #4CC38A | #D9443D / #2F74D0 / #1F9D63 | X, Y e Z do jogo (= cores do gizmo) |
| `--view` | #262A31 | #CAD2DC | fundo da cena sem Game look |
| `--checker-a` / `--checker-b` | #5A5F68 / #6C717A | #C8CCD2 / #E2E5E9 | xadrez das miniaturas do HUD |
| `--shadow-1` | `0 1px 2px rgba(0,0,0,.40)` | `0 1px 2px rgba(16,24,40,.08)` | |
| `--shadow-pop` | `0 8px 24px rgba(0,0,0,.45), 0 0 0 1px var(--line)` | `0 8px 24px rgba(16,24,40,.16), 0 0 0 1px var(--line)` | flutuantes, diálogos |
| `--focus` | `0 0 0 2px var(--bg), 0 0 0 4px color-mix(in srgb, var(--accent) 75%, transparent)` | igual | anel de foco |

**Tema System.** `:root[data-theme="system"]` usa os valores escuros, e
`@media (prefers-color-scheme: light) { :root[data-theme="system"] { ...claro } }`. Para não abrir escuro e trocar: tirar o
`data-theme` fixo do `<html>` e pôr no `<head>`, antes do CSS, um `<script>` inline que lê `wotmLevelEditor.theme` do
`localStorage` (com try/catch) e o aplica. A fonte de verdade continua o `/api/config`; o `localStorage` é só o cache do primeiro
quadro. O fundo da cena já lê `--view` (`applyLook`).

### 3.2 Tipografia

- **Fontes:** só as do sistema, nada baixado.
  - `--font-ui: "Segoe UI Variable Text", "Segoe UI", system-ui, -apple-system, Ubuntu, Cantarell, "Noto Sans", sans-serif`.
  - `--font-mono: "Cascadia Mono", Consolas, "DejaVu Sans Mono", ui-monospace, monospace`.
- **Escala** (tamanho/altura de linha):
  - 11/16: handles, títulos de seção, kbd;
  - 12/16: metadados, log;
  - 13/20: base;
  - 15/22: título do painel;
  - 17/24: título de diálogo.
- **Pesos:** 400 e 600.
- **Títulos de seção:** 11 px, 600, maiúsculas, `letter-spacing: .06em`, `--muted`.
- **Números** (campos, contagens): `font-variant-numeric: tabular-nums`.
- **Monoespaçada:** só em handles, ids e na saída crua do Output; nunca em nomes nem no título.

### 3.3 Espaçamento e medidas

- **Espaço:** base 4. `--s-1: 2px`, `--s-2: 4px`, `--s-3: 6px`, `--s-4: 8px`, `--s-5: 12px`, `--s-6: 16px`, `--s-7: 24px`.
- **Raio:** `--r-sm: 4px` (kbd, chips internos), `--r-md: 6px` (controles), `--r-lg: 8px` (flutuantes, cards), 10 px nos
  diálogos.
- **Alturas:** `--h-ctl: 28px` (controles), `--h-bar: 44px` (barra de cima), `--h-row: 26px` (linha da lista),
  `--h-status: 28px`.
- **Colunas:** trilho 44 px | esquerdo 272 px | cena `1fr` | direito 304 px. Padding interno dos painéis: 12 px.
- **Linhas do `body`:** `grid-template-rows: var(--h-bar) 1fr auto`. O log de 140 px sai.

### 3.4 Componentes

**Botão (secundário, o padrão).**
- Base: `height: 28px`, `padding: 0 10px`, `gap: 6px`, `border-radius: 6px`, fundo `--surface-2`, borda `--line-strong`,
  peso 500.
- `:hover` → fundo `--surface-3`.
- `:active` → `color-mix(in srgb, var(--surface-3), #000 8%)`.
- `:focus-visible` → `box-shadow: var(--focus)`, sem outline.
- `:disabled` → cor `--disabled`, fundo transparente, borda `--line`, `cursor: not-allowed` (sem `opacity`).

**Variantes de botão.**
- `.primary`: fundo `--accent`, texto branco, hover `--accent-hover`.
- `.ghost`: transparente, hover `--surface-3`.
- `.icon`: 28×28 com ícone de 18 px.
- `.on` (toggle): fundo `color-mix(accent 18%)`, borda `color-mix(accent 45%)`, cor `--accent-text`.
- `.danger`: texto `--danger`; no hover, fundo `color-mix(danger 14%)`.
- Selo de "não salvo" no Save: `::after` de 7 px, redondo, `--sel`, no canto superior direito.

**Campo e select.**
- 28 px de altura, `padding: 0 8px`, fundo `--input-bg`, borda `--line-strong`.
- `:focus` → borda `--accent` e `box-shadow: 0 0 0 3px color-mix(accent 25%)`.
- Número alinhado à direita, `tabular-nums`, sem setinhas.
- `.field`: wrapper com prefixo (ícone ou letra) dentro, à esquerda.

**Controles de escolha.**
- **Segmentado** (`.seg`): `inline-flex`, `padding: 2px`, fundo `--surface-2`, borda `--line`. Itens de 24 px. O ativo leva
  fundo `--surface-1` (claro) ou #353B44 (escuro) e `--shadow-1`.
- **Chip** (filtros): 24 px, raio 12, borda `--line-strong`, contagem 11 px `--muted`. Ligado: fundo `color-mix(accent 16%)`.
- **Switch** (Carry, pedestres): trilho 28×16 `--line-strong`, bolinha branca de 12 px, ligado `--accent`, transição de 120 ms.

**Mensagens.**
- **Callout** (`.callout.warn`, `.info`, `.err`): `flex`, `gap: 8px`, `padding: 8px 10px`, raio 6, fundo
  `color-mix(<cor> 12%)`, `border-left: 3px solid <cor>` e ícone de 16 px. Substitui `#pWarn`, `.note` e `.hud-warn`.
- **Toast:** card de 300 px, `--surface-1`, `--shadow-pop`, `border-left: 3px` (`--ok`, `--warn` ou `--danger`), ícone e uma
  linha, com botão opcional (Undo, Choose mod). Some em 4 s, ou em 8 s quando tem botão. Fica no canto inferior direito da cena,
  acima da barra de status, e também por cima de diálogos abertos (`z-index` acima do `::backdrop`, num `popover` ou dentro do
  `<dialog>` aberto).

**Teclas e dicas.**
- **kbd:** `600 11px/1 var(--font-mono)`, `padding: 2px 5px`, borda `--line-strong` com 2 px embaixo, raio 4, fundo
  `--surface-2`.
- **Tooltip:** o `title` nativo (barato, funciona no pywebview), sempre no formato "Verb what it does (Shortcut)": `Duplicate
  (Ctrl+D)`. Botões só de ícone ganham `aria-label`.

### 3.5 Cores da cena

Valem iguais nos dois temas, porque o fundo pode ser o da fase.

**Seleção e destaque.**
- **Ativo:** `BoxHelper` 0xFFC233 com `depthTest = false`, `transparent`, `opacity .95` e `renderOrder 1000`, para aparecer
  através dos prédios.
- **Grupo:** 0xFF8A3D com `depthTest = true` e `opacity .8`. Com 119 itens, atravessar as paredes vira ruído.
- **Hover:** um `BoxHelper` 0xFFFFFF com `opacity .55` e `depthTest = false`. Raycast no `pointermove` a no máximo 30 Hz, pulado
  durante arrasto, gizmo ou olhar do voo. `canvas.style.cursor = 'pointer'` sobre um objeto clicável.
- **Apagado:** quando está selecionado ou com o chip Changed ligado, caixa tracejada (`LineDashedMaterial` 0xF0605A,
  `dashSize 20`, `gapSize 14`, `opacity .7`) no lugar do objeto. A linha da lista e o clique numa caixa assim selecionam o
  original apagado.
- **Alterados:** com o chip Changed ligado, caixa 0x3FB97F com `opacity .5` nos alterados não selecionados.

**Gizmo e grade.**
- **Gizmo:** recolorir uma vez depois de criar, percorrendo `gizmo.traverse` e trocando `material.color`:
  - 0xff0000 → 0xF0605A;
  - 0x00ff00 → 0x4CC38A;
  - 0x0000ff → 0x4C9AFF;
  - amarelo de hover 0xffff00 → 0xFFFFFF.

  O `ColorManagement` já está desligado, então os hex valem como são.
- **Grade:** duas `GridHelper`, com `fog = false`, `depthWrite = false` e `transparent`.
  - Menor: 20000/200 (100 u), `opacity .10`.
  - Maior: 20000/20 (1000 u), `opacity .28`.
  - Cor 0xFFFFFF no tema escuro e com Game look; 0x000000 no tema claro.
  - Com o snap ligado e o passo abaixo de 100, a menor usa o passo do snap.

**Pedestres** (substitui as cores de `peds.js`).
- **Calçada:** faixa 0x36C5F0 com `opacity .22`, eixo 0x36C5F0 sólido.
- **Pontas:** esfera branca de raio 7.
- **Grupo:** cone 0xB57BFF; ligação tracejada 0xB57BFF (dash 12, gap 8).
- **Grupo sem calçada:** 0xF0605A.
- **Selecionado:** 0xFFC233 (`opacity .45` na faixa).
- **Modo Objects:** os marcadores de pedestres ficam com `opacity .35`, ou escondidos pela camada Pedestrians.

### 3.6 Ícones MDI por ação

Só MDI (`@mdi/svg`, Pictogrammers Free License, já em `web/vendor/mdi/LICENSE`); o Remix Icon não é necessário. Cada SVG vai para
`web/vendor/mdi/<nome>.svg` e é usado como hoje (`.ic` com `mask` e `currentColor`). Hoje há 6: `undo`, `content-save`,
`check-circle-outline`, `play`, `refresh` e `cog`. Uma leva de 54 já baixada está em
`C:\Users\TWISTZ~1\AppData\Local\Temp\claude\48f4d2e784f1de73\fb24de58-aacd-4528-9ec4-878ce1a0dcff\scratchpad\mdi_dl\`.
Copiar dali o que coincidir e baixar o resto do pacote npm (download feito uma vez, por quem desenvolve; nada de CDN em tempo de
execução).

Tamanhos: 16 px em botões e linhas, 18 px em `.icon`, 20 px no trilho, 32 px em estados vazios.

**Barra de cima**

| ação | ícone |
|---|---|
| marca | `city-variant-outline` |
| Stage | `map-outline` |
| Mod | `puzzle-outline` |
| Undo | `undo` |
| Redo | `redo` |
| Check | `shield-check-outline` |
| Save | `content-save-outline` |
| Test in game | `play` |
| Reload in game | `refresh` |
| Stop game | `stop` |
| ajuda | `help-circle-outline` |
| Settings | `cog-outline` |

**Trilho de modos**

| ação | ícone |
|---|---|
| Objects | `cube-outline` |
| Pedestrians | `walk` |
| Fight HUD | `monitor-dashboard` |

**Lista**

| ação | ícone |
|---|---|
| busca | `magnify` |
| limpar a busca | `close-circle` |
| nada encontrado | `magnify-close` |
| Buildings | `office-building-outline` |
| Pickups | `package-variant-closed` |
| Props | `cube-outline` |
| Terrain | `terrain` |
| alterado | `circle-medium` |
| cópia | `content-duplicate` |
| apagado | `delete-outline` |
| apoiado em outro | `link-variant` |
| abrir / fechar seção | `chevron-down` / `chevron-right` |

**Propriedades**

| ação | ícone |
|---|---|
| nada selecionado | `cursor-default-click-outline` |
| copiar handle | `content-copy` |
| Drop | `arrow-collapse-down` |
| Duplicate | `content-duplicate` |
| Restore original | `restore` |
| Delete | `delete-outline` |
| Carry | `package-up` |
| girar ±90° | `rotate-left` / `rotate-right` |
| avisos | `alert-outline` / `information-outline` |

**Cena**

| ação | ícone |
|---|---|
| Move | `cursor-move` |
| Turn | `rotate-3d-variant` |
| Snap | `magnet` |
| Game look | `weather-fog` |
| Grid | `grid` |
| camadas | `layers-outline` |
| Fly | `airplane` |
| Orbit | `rotate-orbit` |
| Focus selection | `crosshairs-gps` |
| Frame all | `fit-to-screen-outline` |
| atalhos | `keyboard-outline` |

**Pedestres**

| ação | ícone |
|---|---|
| grupos | `account-group-outline` |
| Add group | `account-multiple-plus` |
| calçadas | `road-variant` |
| Add sidewalk | `vector-polyline-plus` |
| camada visível / escondida | `eye-outline` / `eye-off-outline` |

**Barra de status e Output**

| ação | ícone |
|---|---|
| ok | `check-circle-outline` |
| erro | `alert-circle-outline` |
| informação | `information-outline` |
| jogo | `gamepad-variant-outline` |
| abrir a gaveta | `chevron-up` |
| limpar | `delete-sweep-outline` |
| copiar | `content-copy` |

**Diálogos**

| ação | ícone |
|---|---|
| fechar | `close` |
| Browse | `folder-open-outline` |
| aviso de não salvo | `content-save-alert-outline` |
| Replace | `image-edit-outline` |
| sem substituição | `image-plus-outline` |
| Save original as PNG | `download-outline` |
| seta | `arrow-right` |
| disco | `disc` |
| tema | `theme-light-dark`, `weather-night`, `white-balance-sunny` |

Conferir cada nome no `@mdi/svg` vendorizado antes de usar [hipótese: todos existem na 7.x]. Atualizar a linha "Icons: ..." do
`MODS.md`, que já cita MDI.

## 4. A nova organização da tela

### 4.1 Tela inteira (modo Objects, 1600×950)

```
+--------------------------------------------------------------------------------------------------------------------+
|[city] Level Editor | [map] Midtown (CENTRAL) v | [puzzle] Teste v | [undo][redo] |      [shield] Check  [save] Save* |
|                                         ( o Game running · Midtown )  [play] Test in game | [refresh] Reload in game | [stop] | [?][cog] |
+----+------------------------+-----------------------------------------------------------+--------------------------+
|[O] | [magnify] Search objects |[move][turn] | [magnet] 10 u 15°        [fog][grid][layers] | [cube] Hotel   Moved   |
|[P] | (Buildings 67)(Pickups 115)|                                       [Fly|Orbit] [+][fit][?]| 0xDB4100 · Building   |
|----| (Props 149)(Changed 3) |                                                           | TRANSFORM                |
|[H] | v BUILDINGS        67  |                                                           |  X[ 120.0] Y[-936.5] Z[ 0.0] |
|    |  [bld] Hotel  o  0xDB41|                    cena 3D                                |  Heading [  90 ]° [<][>] |
|    |    [cpy] Copy 1        |                                                           | ACTIONS                  |
|    |  [bld] Building   0xDC2|                                                           |  [Drop  End][Duplicate ^D] |
|    | > PICKUPS         115  |                                                           |  [Restore  ][Delete   Del] |
|    | > PROPS           149  |                                                           |  (o) Move objects on top 3 |
|    | > TERRAIN           2  |                                                           | > NAME                   |
|    |                        | [Right-drag look · WASD fly · Click select · ? Shortcuts]  [toasts] | > DETAILS     |
|    | 331 objects · 3 changed · 1 selected                                               |                          |
+----+------------------------+-----------------------------------------------------------+--------------------------+
| [^][i] Saved levels/CENTRAL.json  |  * Unsaved changes | [ok] Check passed | [pad] Patch applied in game 14:03 | 3 changed |
+--------------------------------------------------------------------------------------------------------------------+
```

As duas linhas do topo são uma só barra de 44 px (dividida aqui por largura). Abaixo de 1280 px, Check, Save, Test e Reload perdem
o rótulo e ficam com ícone e `title`. A ordem e os ids (`#stage`, `#mod`, `#undo`, `#check`, `#save`, `#test`, `#reload`,
`#settings`) se mantêm.

**Grupo do jogo**, conforme o estado:

| estado | indicador | botões |
|---|---|---|
| fechado | nada | `[play] Test in game` (primário), `[refresh] Reload in game` (desligado; title "Start the game with Test in game first") |
| abrindo | `o Starting game…` (ponto `--warn` pulsando) | os dois desligados |
| aberto | `o Game running · Midtown` (ponto `--ok`, pulsa 2 s) | `Test in game` (secundário), `Reload in game` (primário), `[stop]` |
| recarregando | `o Reloading… waiting for a fight` | Reload desligado |
| aberto com mudança não salva | | o Reload diz `Save & reload` |

**Trilho de modos** (44 px, à esquerda):
- `[O]` Objects (1) e `[P]` Pedestrians (2) são modos exclusivos: trocam o painel esquerdo, o direito e o que se clica na cena.
- `[H]` Fight HUD abre o diálogo e não é modo.
- Modo ativo: barra de 3 px `--accent` à esquerda (`inset box-shadow`) e fundo `--surface-2`.
- O trilho resolve a largura: a barra de cima não cabe também as abas a 1280 px.

**Barra de status** (28 px), da esquerda para a direita, todos os campos clicáveis (abrem o Output na aba certa):
- a última mensagem, com ícone;
- o salvamento: `Unsaved changes` (ponto âmbar), `Saved 14:02` ou `Not saved yet`;
- a checagem: `Check passed` (verde), `2 problems` (vermelho) ou `Not checked since the last change` (cinza), que volta a esse
  estado a cada `snapshot()`;
- o jogo: `Game closed`, `Patch applied in game 14:03` ou `Patch ignored in game`;
- a contagem: `3 changed · 2 selected`.

**Output** (gaveta de 200 px, com alça para redimensionar e altura guardada em `localStorage`):
- abas Messages, Check e Game, cada uma com `[Copy]` e `[Clear]`;
- cada linha leva hora `HH:MM:SS` em mono 11 px `--muted`, ícone e texto;
- a gaveta só abre sozinha num erro;
- a saída crua do `lvl.py` fica em "Details" dentro da aba Check.

### 4.2 Painel direito

```
NADA SELECIONADO                        UM OBJETO                            APAGADO
+--------------------------+   +--------------------------+   +--------------------------+
|   [cursor-click 32px]    |   | [bld] Hotel        Moved |   | [bld] Hotel      Deleted |
|    Nothing selected      |   | 0xDB4100 · Building [cp] |   | 0xDB4100 · Building      |
| Click   select           |   | TRANSFORM                |   | (!) Deleted from this    |
| Shift+Click add / remove |   |  X[...]  Y[...]  Z[...]  |   |     stage in your mod.   |
| Shift+Drag  box select   |   |  Heading [ 90 ]° [<][>]  |   |  [restore] Restore       |
| Ctrl+A  select listed    |   | ACTIONS                  |   +--------------------------+
| Esc  deselect            |   |  Drop / Duplicate /      |
| ------------------------ |   |  Restore original/Delete |   VÁRIOS (5)
| Midtown · CENTRAL        |   |  (o) Move objects on top |   +--------------------------+
| 331 objects · 3 changed  |   | > NAME (fechado)         |   | 5 objects selected       |
| Mod: Teste               |   | > DETAILS (fechado)      |   | 3 buildings, 2 pickups   |
| Saves to                 |   | (!) No collision of its  |   | (i) [amber] edited  [orange] |
| mods/Teste/levels/       |   |     own. (callout)       |   |  follow, keeping their   |
|   CENTRAL.json           |   +--------------------------+   |  distance.               |
| ------------------------ |                                  | TRANSFORM (do âmbar)     |
| Getting started          |                                  | ACTIONS (para todos)     |
| 1 Click a building.      |                                  |  Delete 5 (+3 on top)    |
| 2 Press G, drag arrows.  |                                  +--------------------------+
| 3 Save, Test in game.    |
+--------------------------+
```

**Cabeçalho.**
- Quadrado de 36 px com o ícone do tipo, o nome em 15/600 com a fonte de UI e um selo de estado (`Moved`, `Turned`, `Copy`,
  `Deleted`).
- Embaixo, chip mono 11 px com o handle: clique copia, com o ícone `content-copy` no hover. Ao lado, o tipo por extenso
  (`Building`, `Pickup`, `Prop`).

**Seção TRANSFORM.**
- Grade `repeat(3, minmax(0,1fr))` com `gap: 6px`. Cada campo tem a letra dentro, à esquerda: X `--ax-x`, Y `--ax-y`, Z `--ax-z`,
  11/700.
- Tooltips: Z: "Height (up)", Y: "The blue arrow points to -Y". Arrastar a letra na horizontal muda o valor: passo = snap ou 1,
  Shift ×10, `cursor: ew-resize`.
- Heading com sufixo `°` e `[rotate-left]` / `[rotate-right]` para -90° / +90°.

**Seção ACTIONS.**
- Grade de 2 colunas, botões de largura cheia com `kbd` à direita: `Drop End`, `Duplicate Ctrl+D`, `Restore original`,
  `Delete Del` (`.danger`).
- Switch "Move objects on top" com a contagem em chip (`3`). Fica desligado e esmaecido com 0. Tooltip: "Moving, turning,
  copying or deleting this also does it to the pickups resting on it".

**Seção NAME** (fechada por padrão; aberta ou fechada guardado em `localStorage`).
- "Name": placeholder = o nome atual. Ajuda: "Only this object. Saved in your mod; the game ignores it."
- "Model name". Ajuda: "Every object with this model, in every stage (4 in this stage). Not undoable."
- Sem malha: campo tracejado e "This object has no mesh, so it has no model name."

**Seção DETAILS** (fechada por padrão). `<dl>` de 2 colunas (110 px | 1fr):
- Size `120 × 80 × 340`
- Kind `Building`
- Model id `0x1393` (mono)
- Damage states `3`
- Collision boxes `2`
- Resting on `Hotel` (link que seleciona o de baixo)

### 4.3 Painel esquerdo (Objects)

**Busca e filtros.**
- Busca com prefixo `magnify` e botão `close-circle` para limpar. Placeholder "Search by name or handle". O filtro continua
  aceitando regra, kind e id.
- Chips de tipo com contagem: `Buildings 67`, `Pickups 115`, `Props 149`. Depois um separador e o chip `Changed 3`, com o ponto
  `--changed`.
- Tooltip dos chips de tipo: "Show in the list (also limits box select and Ctrl+A)".

**Seções.**
- Cabeçalho grudado (`position: sticky`), 24 px, 11/600 maiúsculas, chevron e contagem.
- Ordem: Buildings, Pickups, Props, Terrain. `Terrain piece` fica numa seção própria, fechada por padrão.

**Linha** (26 px): grade `16px 1fr auto auto`.
- **Ícone** do tipo.
- **Nome** com reticências. O nome de regra fica em `--fg-2` sem itálico; o nome próprio ou de modelo em `--fg` 500.
- **Selos** de 14 px: `circle-medium` `--changed` (movido ou girado), `content-duplicate` `--added` (tem cópias),
  `delete-outline` `--danger` (apagado: linha com `opacity .55` e nome riscado), `link-variant` `--muted` (apoiado; title
  "On Hotel").
- **Handle** mono 11 px `--muted` com `opacity .6`, que vai a 1 no hover e na seleção.

**Cópias e seleção.**
- Cópias logo abaixo do original (como hoje), com recuo de 18 px e nome `Copy 1`.
- Selecionado: fundo `--sel-bg` e `inset 3px 0 0 var(--sel)`. Grupo: `--sel-bg-2` e a barra `--sel-2`. Nada de azul cheio.

**Rodapé e lista vazia.**
- Rodapé: `331 objects · 3 changed · 2 selected`.
- Lista vazia por filtro: `magnify-close` de 28 px, "No objects match" e `[Clear filters]`.

### 4.4 Barras flutuantes na cena

**Estilo comum** (`.float`): `position: absolute`, `padding: 4px`, `gap: 2px`,
fundo `color-mix(in srgb, var(--surface-1) 86%, transparent)`, `backdrop-filter: blur(8px)`, borda `--line`, raio 8 e
`--shadow-pop`.

**Barras.**
- **Topo esquerdo (ferramenta):** `[cursor-move] Move (G)` e `[rotate-3d-variant] Turn (R)` como `.seg`; separador;
  `[magnet] Snap` (toggle). Com o snap ligado, dois campos de 56 px com sufixo `u` e `°`. Saem do painel direito, já que valem
  para a cena (mesmos ids `#tMove`, `#tTurn`, `#snap`, `#snapMove`, `#snapTurn`).
- **Topo direito (vista):** `[weather-fog] Game look (L)`, `[grid] Grid`, `[layers-outline]` (menu com Pedestrians e, no futuro,
  Collision); separador; `[airplane] Fly | [rotate-orbit] Orbit (V)`; `[crosshairs-gps] Focus selection (F)`,
  `[fit-to-screen-outline] Frame all (Home)`; `[keyboard-outline] Shortcuts (?)`.
- **Rodapé esquerdo:** um chip com a dica do contexto (ver 5.3), 12 px `--fg-2`, no lugar do `#hint` corrido. Some depois de
  8 s sem mudança de contexto e volta quando ele muda.
- **Canto inferior direito:** pilha de toasts.

**Banda e carga.**
- Banda de seleção (`#band`): borda 1 px `--sel` e fundo `color-mix(--sel 12%)`.
- Carga (`#busy`): véu `color-mix(--bg 55%)` e card `--surface-1`, raio 10, com spinner CSS de 24 px (borda 3 px
  `--line-strong`, topo `--accent`, giro de .8 s), "Loading Midtown…" e, embaixo em 12 px `--muted`, "The first time a stage
  opens takes a few seconds."

### 4.5 Modo Pedestrians

```
+----+------------------------+-------------------------------------------------+--------------------------+
|[O] | Groups      7 / 20     |[move] | [account-multiple-plus] Add group        | [walk] Group 3           |
|[P]*| [====-------------]    |       | [vector-polyline-plus] Add sidewalk      | People [====|----] [ 50 ]|
|----| Sidewalks  34 / 127    |                                                 | Walks on sidewalk 12     |
|[H] | [=====------------]    |  objetos esmaecidos, não clicáveis;             |  (250 units long)  [go]  |
|    | People 350             |  calçadas ciano, grupos violeta                 | ACTIONS                  |
|    | (o) Pedestrians in     |                                                 |  [Duplicate] [Delete]    |
|    |     this stage         |                                                 +--------------------------+
|    | v GROUPS            7  |                                                 | [road] Sidewalk 12       |
|    |  [grp] Group 1   50    |                                                 | Length 250 units         |
|    |  [!]  Group 4    30    |  legenda: [cyan] sidewalk [o] end [violet] group | Width [====|--] 40 units |
|    | v SIDEWALKS        34  |           [red] group with no sidewalk           |   each side              |
|    |  [road] Sidewalk 1 250 |                                                 | Drag an end to stretch.  |
+----+------------------------+-------------------------------------------------+--------------------------+
```

**Painel esquerdo.**
- **Medidores** com barra de 4 px (fundo `--line`, preenchido `--accent`; acima de 90%, `--warn`; no limite, `--danger`) e o
  tooltip "A stage holds 20 pedestrian groups at most".
- **Add group / Add sidewalk** ficam desligados no limite, com o motivo no `title`. O item novo é posto no ponto do chão sob o
  centro da cena (raycast de `(0,0)`) e já selecionado.
- **Switch** "Pedestrians in this stage" (o `off` do patch).

**Cena e painel direito.**
- Os objetos da fase ficam como cenário, não clicáveis e com `opacity .6` via `scene.overrideMaterial`, ou só com as caixas
  escondidas [hipótese: escolher no teste].
- **Grupo:** People com slider de 1 a 255 e campo de 64 px.
- **Calçada:** Width com slider mostrado em unidades (`width × 10`) e editado nas mesmas unidades (arredonda para múltiplo de 10).
- Os textos do `info()` mudam (ver 5.4).
- Undo, Redo e Save valem para os dois modos, porque o patch é um só.

### 4.6 Diálogo Fight HUD (760 px)

```
+-------------------------------------------------------------------------------------+
| Fight HUD · Midtown                                                          [x]   |
| (i) Changes are written to your mod right away. Undo does not reach them.          |
| [!] This stage uses a different HUD layout (like Tokyo and Grand Canyon). Pictures |
|     made for other stages will not line up here.                (só na versão B)   |
| +---------------------------------------------------------------------------------+ |
| | [96x96 xadrez] -> [96x96 tracejado]   Health bars            Replaced           | |
| |    Original         In your mod       128×64 · 8-bit · .TEX                      | |
| |                     Not replaced      [Replace…] [Save original as PNG] [Remove] | |
| |                                       [ ] Apply to all 9 stages with this picture | |
| +---------------------------------------------------------------------------------+ |
|                                    [refresh] Reload in game (com o jogo aberto) [Close] |
+-------------------------------------------------------------------------------------+
```

**Sem mod escolhido.** No topo, o callout "Choose or create a mod to replace HUD pictures." com o mesmo seletor de mod dentro do
diálogo. Replace fica desligado com `title` igual.

**Linhas.**
- Aceitam arrastar um PNG sobre a linha (`dragover`/`drop`), com o mesmo fluxo de `replace()`.
- `ctx.log` passa a usar o toast enquanto o diálogo está aberto. O histórico continua no Output.
- O CSS sai do `hud.js` para o `style.css`, com tokens (xadrez `--checker-a/b`).

**Versão B.** A frase cita só títulos: o servidor manda `STAGE_TITLE` dos que usam B [hipótese: "Grand Canyon" é como o jogo
chama `CANYON1`; usar o título do `editor.py`].

### 4.7 Settings (600 px)

```
+--------------------------------------------------------------+
| Settings                                               [x]  |
| APPEARANCE                                                   |
|  Theme   [moon Dark | sun Light | System]                    |
| GAME FILES                                                   |
|  Game disc                                     [check] Found |
|  [C:\Games\War of the Monsters.iso           ] [folder]      |
|  The .iso, or a folder with LVL and PTS inside.              |
|  Mods folder                                   [check] Found |
|  [C:\...\mods                                ] [folder]      |
|  Game (wotm.exe)                         [alert] Not found   |
|  [                                           ] [folder]      |
| > Advanced (for developers)   (ELF, Working folder, Dev disc)|
|                                          [Cancel] [Save]     |
+--------------------------------------------------------------+
```

- **Validação:** o selo `Found` / `Not found` vem do servidor, num `POST /api/paths` que devolve `disc_error`, `isdir(mods)` e
  `exists(game)` para os valores digitados (a cada 400 ms sem digitar).
- **Advanced:** fecha num `<details>` e só aparece quando o servidor diz `clone: true` (há um repo ao lado).
- **Botão `[folder]`:** só existe quando o servidor diz `canBrowse: true`, ou seja, na janela pywebview
  (`window.create_file_dialog`). No navegador comum, só o campo (ver seção 7).

### 4.8 Outros diálogos

Todos com a mesma base: cabeçalho de 52 px, corpo e rodapé grudado com os botões à direita.

- **Test in game com mudança não salva:** título "Save before testing?". Corpo, com `content-save-alert-outline` `--warn`: "The
  game loads the saved file. Your last 3 changes are not saved yet." Botões: `[Cancel] [Test without them] [Save and test]`
  (primário).
- **Trocar de fase com mudança:** título "Unsaved changes". Corpo: "Midtown has 3 unsaved changes." Botões:
  `[Cancel] [Discard] [Save and switch]`.
- **Trocar de mod com mudança:** título "Switch to mod "Zeta"?". Corpo: "You have 3 unsaved changes for Teste." Botões:
  `[Cancel] [Discard and load Zeta] [Move my changes to Zeta]`.
  - "Move" troca o mod sem recarregar: o próximo Save grava ali.
  - Se Zeta já tem `levels/CENTRAL.json`, uma linha extra: "Zeta already changes this stage; saving will replace its file."
- **Novo mod** (item "New mod…" do seletor): campo "Mod name", validado na hora com a regra do servidor, e a ajuda "The folder
  mods/<name>/ is created when you save." Erro: "A mod name can't contain \ / : * ? " < > | or start with _ or .".
- **Test com o jogo aberto:** título "The game is already open". Corpo: "It is on Midtown. Reload this stage in it, or restart
  the game?" Botões: `[Cancel] [Restart game] [Reload in game]` (primário).
- **Reload com o jogo em outra fase:** título "The game is on Tokyo". Corpo: "Reload only works on the stage the game is
  showing." Botões: `[Cancel] [Restart on Midtown]`.

## 5. Primeiro uso e ajuda

### 5.1 Estados vazios

**Sem disco** (`discError`). Card no centro da cena com o ícone `disc` de 32 px:
- Título: "Find your game disc".
- Texto: "The editor reads the stages from your copy of War of the Monsters (NTSC-U, SCUS-97197). Choose the .iso, or a folder
  extracted from it with LVL and PTS inside."
- Embaixo, em 12 px `--muted`, o motivo reescrito (5.4).
- Botão primário `[Open settings]`, que abre Settings com o foco no Game disc.

**Carga falhou.** Mesmo card, com o título "Couldn't open Midtown", o motivo e `[Retry]`.

**Sem mod.** A fase carrega normalmente, porque editar não exige mod.
- O seletor Mod mostra "Choose a mod" com borda `--warn`.
- Save, Test e Reload ficam ligados. Clicados, abrem "Save into which mod?" (o seletor e o "New mod…") em vez de errar.

### 5.2 Atalhos ("?", F1 ou o botão)

Diálogo "Keyboard shortcuts" de 560 px, em duas colunas, com as teclas em `kbd`. A `keydown` ignora atalhos de cena com um
`<dialog>` aberto. Conteúdo exato:

**Camera**

| tecla | ação |
|---|---|
| `V` | Switch Fly / Orbit |
| `W` `A` `S` `D` | Fly: move |
| `E` / `Q` | Fly: up / down |
| `Shift` | Fly: faster |
| Right-drag | Fly: look around |
| Wheel | Fly: move forward / Orbit: zoom |
| Left-drag | Orbit: orbit |
| Right-drag | Orbit: pan |
| `F` | Focus the selection |
| `Home` | Frame the whole stage |

**Select**

| tecla | ação |
|---|---|
| Click | Select |
| `Shift`+Click / `Ctrl`+Click | Add or remove |
| `Shift`+Drag | Box select (the kinds shown in the list) |
| `Ctrl`+`A` | Select everything in the list |
| `Esc` | Deselect |

**Edit**

| tecla | ação |
|---|---|
| `G` / `R` | Move / Turn tool |
| Arrows | Nudge along the axis nearest to the view |
| `PgUp` / `PgDn` | Nudge up / down |
| `Shift` + nudge | Ten steps |
| `End` | Drop to the surface below |
| `Ctrl`+`D` | Duplicate |
| `Del` | Delete |
| `Ctrl`+`Z` | Undo |
| `Ctrl`+`Y` / `Ctrl`+`Shift`+`Z` | Redo |

**View**

| tecla | ação |
|---|---|
| `L` | Game look |
| `1` / `2` | Objects / Pedestrians |

**File**

| tecla | ação |
|---|---|
| `Ctrl`+`S` | Save |
| `?` / `F1` | This list |

WASD e QE passam a usar `e.code` (`KeyW`...), o que corrige AZERTY; a lista mostra as letras QWERTY.

### 5.3 Dica do contexto (chip no rodapé esquerdo da cena)

| contexto | texto |
|---|---|
| nada selecionado, voo | `Right-drag to look · WASD to fly · Click to select · ? for shortcuts` |
| nada selecionado, órbita | `Drag to orbit · Right-drag to pan · Wheel to zoom · Click to select` |
| 1 selecionado | `Drag the arrows to move · R to turn · End to drop · Ctrl+D to copy · Del to delete` |
| vários | `The yellow one leads, the orange ones follow · Shift+click adds or removes` |
| arrastando a caixa | `Release to select the objects whose middle is inside` |
| arrastando o gizmo | `Ctrl+Z undoes the move` |
| Pedestrians, nada selecionado | `Click a group or a sidewalk · Add them from the toolbar` |
| Pedestrians, calçada | `Drag an end to stretch it · Drag the middle to move it` |

Ajuda pontual, mostrada uma vez por sessão: um arrasto com o botão esquerdo no modo voo, sem efeito, gera o toast "In Fly view,
hold the right button to look around (or press V for Orbit)."

### 5.4 Mensagens reescritas (texto exato)

O servidor passa a devolver `{error, code, detail}`. O tipo da exceção vai para `detail`, que fica atrás de "Details" no Output.

**Disco e caminhos** (`editor.py`)

| hoje | novo |
|---|---|
| `no disc set: pick the game's .iso or the extracted disc folder in Settings` | `Game disc not set.` + [Open settings] |
| `%s has no LVL folder` | `This folder isn't an extracted disc. Pick the folder with LVL and PTS inside, or the .iso itself.` |
| `%s does not exist` | `File not found. Was it moved or renamed?` |
| `%s is not a disc image (...); a .bin/.cue is not read: use the .iso` | `This file isn't a disc image the editor can read. Use the .iso (not .bin/.cue).` |
| `no stages in <disc>: set the game disc in Settings` | `No stages on this disc. Is it War of the Monsters (NTSC-U)?` |

**Jogo e mod** (`editor.py`)

| hoje | novo |
|---|---|
| `game not found: set it in Settings (wotm.exe)` | `Can't find the game (wotm.exe).` + [Open settings] |
| `disc image not found: set the ISO in Settings (...)` | `The dev runtime needs a disc image (.iso).` + [Open settings] |
| `no game opened by the editor is running: use Test in game` | (Reload desligado) title `Start the game with Test in game first` |
| `pick a mod name (letters, digits, no leading _)` | diálogo "Save into which mod?" |
| `warning: mod "X" also has levels/CENTRAL.json and the game uses that one ...` | callout âmbar fixo no topo da cena, já ao carregar (`/api/stage` devolve `winner` e `others`): `Mod "Zeta" also changes Midtown and loads after "Teste", so the game uses Zeta's file. Mods load in alphabetical order; the last one wins.` Deixa de pôr `ok=false`. |

**Página** (`app.js`)

| hoje | novo |
|---|---|
| `the scene is from an older tools/lvlglb.py: restart the editor ...` | `The 3D cache is out of date. Restart the editor to rebuild it.` |
| `patch: 0x... is not an object of CENTRAL; kept out` | callout âmbar: `2 changes in this file are for objects Midtown doesn't have. They are kept in the file as they are.` (ver passo 1) |
| `dropped 3 of 5 on the surface below (nothing under the others)` | toast `Dropped 3 of 5 (nothing under the other 2)` |
| `HUD: choose a mod first (Mod, at the top).` | callout no diálogo (4.6) |
| `model named "X" (file)` | toast `Model renamed to "X" in every stage` |
| `game: no stage load seen yet (is a fight running? ...)` | `The game hasn't reloaded yet. It does when a fight on this stage is running.` |

**Toasts de ação.**
- `Deleted 1 building and 3 objects on top` + [Undo]
- `Copied 4 objects`
- `Saved to mods/Teste/levels/CENTRAL.json`
- `Saved, but the check found 2 problems` + [Show]

`No collision of its own.` fica neutro até confirmar o efeito no jogo: `This object has no collision of its own.` [hipótese: que
monstros o atravessam].

**Check** (aba Check e selo da barra de status).
- **Resumo** a partir de `lp.stats`:
  - passou: `Check passed: 3 moved, 1 turned, 2 copies, 1 deleted. Collision OK.`
  - falhou: `2 problems`
- **Problemas** como lista, com ícone e clique que seleciona e enquadra o objeto quando há handle:
  - `entry k skipped: <motivo>` → `Hotel was not applied: <motivo>`. O `editor.py` mapeia `k` para `patch.objects[k].handle`.
  - `pedestrian group k skipped: <motivo>` → `Pedestrian group 3 was not applied: <motivo>`.
  - `box rule ... N off` → `Collision boxes: N out of place (worst X units)`.
  - `cell rule ... a of b` → `Collision cells: b - a boxes listed in the wrong cells`.
- **Informação:** as linhas de `cut_nets` resumidas em `Cut N car paths and M pedestrian paths` [hipótese: formato das linhas de
  `cut_nets` a ler].
- **Mudança no `lvl.py`:** `check_collision(st, out=None)` passa a preencher um dict
  (`box_ok`, `box_bad`, `worst`, `cell_ok`, `paths`) além de imprimir. O `editor.py` devolve
  `{ok, summary, problems: [{level, text, handle?}], lines}`.

### 5.5 Linhas do jogo traduzidas (aba Game e campo de jogo da barra)

O editor mapeia os padrões de `wotm_levelpatch.inc` e `wotm_levelreload.inc`. O que não bate fica cru na aba Game.

| linha `[fase]` | estado do jogo | texto |
|---|---|---|
| `<STAGE> <- <mod> (<rel>): <resumo>` | applied | `Patch applied in game (<resumo traduzido>)` |
| `<STAGE> <- ...: nada mudou` | applied | `Patch loaded in game: nothing changed` |
| `antes: ... / depois: ...` | (fim da carga) | só na aba Game |
| `<rel> (<mod>): JSON invalido, patch ignorado: <err>` | ignored | `The game rejected the file: it isn't valid JSON (<err>)` |
| `imagem da fase sem grade de colisao ..., patch ignorado` | ignored | `The game couldn't apply the patch to this stage` |
| `LEVELS/<S>.JSON: <m> vence <mod>` | ignored | `The game uses mod "<m>"'s file for this stage, not yours` |
| `<STAGE>: sem patch` / `sem patch (... sumiu)` | applied | `Game: no patch for this stage` |
| `recarga pedida (...): <STAGE>` | reloading | `Reloading Midtown…` |
| `recarga pedida (arquivo) para X, mas a fase e Y: ignorada` | ignored | `The game is on <título de Y>: reload skipped` |
| `... sem quadro de luta em 10 s: descartada` | idle | `Reload dropped: no fight was running for 10 s` |
| `... antes da ultima carga da fase: ja atendida` | applied | `Already up to date` |
| `patches de fase desligados (WOTM_LEVELPATCH=0)` | ignored | `Level patches are off in this game (WOTM_LEVELPATCH=0)` |

- O fim do `watchGame` passa a ser qualquer linha que leve a `applied`, `ignored` ou `idle`, e não só quatro palavras.
- O estado do jogo guarda a fase vista por último (nas linhas `<STAGE> <-`, `recarga pedida ...: <STAGE>` e `mas a fase e Y`).
  Sem linha, vale a fase que o editor abriu.

## 6. Mudanças em ordem de implementação

Custo: P = até ~1 h, M = meio dia, G = um dia ou mais. Tudo em `tools/` e `docs/`, sem gate (não muda a ROM). Começar depois que
o agente que está ligando `peds.js` e `hud.js` terminar o `app.js`, o `index.html`, o `style.css` e o `editor.py`, para não
brigar no mesmo arquivo. Os passos 0 e 1 corrigem bugs e valem antes do visual.

**Como conferir, em geral.**
- Editor com `python tools/level_editor/editor.py --serve --port 47199`.
- Edge headless como em `scratchpad/shot.sh`:
  `msedge --headless=new --use-angle=swiftshader --enable-unsafe-swiftshader --virtual-time-budget=90000 --window-size=1400,900 --screenshot=out.png "http://127.0.0.1:47199/_shot.html?stage=CENTRAL&..."`.
- Para o texto do `_test.html`, `--dump-dom` e ler o `<pre id="out">`. O `_test.html` (`?debug`, `window.wotmDebug`) ganha as
  checagens de cada passo.
- Cuidado: o trecho de `pModel` do harness grava no `~/.wotm_level_editor_names.json` do usuário.
- Capturas novas nos dois temas, a 1280×800 e a 1600×950, em `docs/screenshots/2026-11-06/` (ou na data do dia), com nome
  `AAAA-MM-DD_editor_<o que>.png`.

### Passo 0: o arquivo do mod não perde nada (P2). Custo P.
- `editor.py save_patch`: grava quando há `objects`, `names` ou `peds` (ou qualquer chave além de `objects`), e mantém
  `objects: []`.
- `check_patch`: depois de `lp.apply(patch)`, monta `nets` e chama `lp.apply_peds(patch, nets)` antes de `cut_nets`, na ordem de
  `cmd_patch`.
- `app.js`: `applyPatch` guarda o que não entende: chaves de topo além de `objects`/`names`/`peds` e as entradas de `objects` com
  alça desconhecida, numa variável `keep`. `buildPatch` as devolve iguais. O callout de 5.4 avisa.
- **Conferir:** no harness, carregar um mod com `{"objects":[{"handle":"0xDEAD",...}],"peds":{"off":true},"x":1}`, chamar
  `D.buildPatch()` e ver as três coisas de volta. `POST /api/save` só com `peds` deve criar o arquivo. `/api/check` com
  `peds.groups` deve trazer as linhas `pedestrian group`.

### Passo 1: fluxo e teclado (P1, P3, P5, P6). Custo M.
**Restaurar apagado.** `select()` aceita item apagado vindo da lista ou da caixa tracejada (5.3): `gizmo.detach()`, painel
"APAGADO" com `[Restore]` (= `resetSel` só daquele `rec`). `selectMany` continua ignorando os apagados.

**Teclado.**
- Ctrl+S, Ctrl+Z, Ctrl+Y e Ctrl+Shift+Z são tratados antes do teste de foco. Ctrl+Z dentro de um campo de texto continua sendo do
  campo.
- O teste de foco vira `input:not([type=checkbox]):not([type=radio]):not([type=range]), textarea, select:focus`.
- `#stage` e o seletor de mod dão `blur()` depois do `change`.
- `keydown` sai cedo quando há um `dialog[open]`.
- WASD e QE por `e.code`.

**Redo.** Pilha `future`: o Undo empurra o estado atual, e `snapshot()` a limpa. Undo e Redo ficam desligados com a pilha vazia,
com `title` "Undo: <o que> (Ctrl+Z)". `snapshot(label)` recebe um rótulo curto ("move 3 objects", "delete Hotel").

**Frame all.** `Home` e o botão chamam `frameAll()`.

**Jogo.**
- O servidor recusa `/api/test` com o jogo aberto (409 `{code: "running", stage}`) a menos que venha `restart: true`, e aí faz
  `terminate()` no anterior.
- `POST /api/stop` faz `terminate()` só no `GAME["proc"]`.
- `GAME["stage"]` guarda a fase aberta, e `/api/gamelog` a devolve.
- O `watchGame` usa a tabela de 5.5.

**Mod.**
- Cancelar a troca restaura o valor anterior.
- A mensagem do servidor fica com a regra certa.
- `POST /api/config {mod}` ao escolher, não só no Save.

**Conferir no harness:**
- apagar um original, `D.select(it)` e ver o painel "Deleted"; Restore volta a posição do disco;
- `KeyboardEvent('keydown', {key:'s', ctrlKey:true})` com foco em `#filter` chama o save (stub de `post`);
- foco em `#snap` e tecla `g` muda a ferramenta;
- `D.undo(); D.redo()` volta ao mesmo `buildPatch()`;
- `/api/test` duas vezes devolve 409 na segunda (sem abrir o jogo: `game` apontando para um exe falso, como `cmd.exe /c exit`
  [hipótese: serve como processo vivo curto; usar um `.bat` com `timeout 30`]).

### Passo 2: tokens, tema System e componentes (3.1-3.4). Custo P.
- `style.css` reescrito por cima dos mesmos seletores, com os apelidos `--panel`/`--panel2`.
- O script anti-flash no `<head>` do `index.html`.
- `Theme` com Dark, Light e System, e `applyTheme` gravando `localStorage`.
- **Conferir:** capturas da tela atual nos três temas. Com `localStorage.theme=light`, o primeiro quadro já sai claro: captura
  com `--virtual-time-budget=50`, antes do `/api/init`.

### Passo 3: ícones vendorizados. Custo P.
- Copiar de `scratchpad/mdi_dl/` e baixar os que faltam (3.6) para `web/vendor/mdi/`.
- Conferir a linha de licença do `MODS.md` e do `tools/release/THIRD_PARTY.txt`.
- **Conferir:** `ls web/vendor/mdi | wc -l`, e cada `url(vendor/mdi/...)` do HTML e do JS existe (um `grep -o` mais `test -f`).

### Passo 4: barra de cima, barra de status, Output e toasts (4.1, P4, P8, P17). Custo M.
- **HTML:** a barra nova; `<footer id="status">` com os campos; `<section id="output">` com as abas; `<div id="toasts">`.
- **`log()`:** vira `msg(text, {level, tab, toast})`. As linhas antigas continuam no Output.
- **`post()`:** mostra o resumo, e não as linhas cruas.
- **Estados:** `dirty`, `savedAt`, `checkState` (invalidado em `snapshot()`) e `gameState`. `document.title` continua com o
  " *".
- **Conferir no harness:**
  - depois de `D.nudge`, `#status .save` diz "Unsaved changes";
  - um `post('check')` falso deixa o selo verde ou vermelho;
  - um erro abre a gaveta;
  - capturas a 1280 (rótulos escondidos) e a 1600.

### Passo 5: trilho de modos e lista (4.1, 4.3, P10). Custo M.
- `refreshList` monta as seções (`<details>`-like com estado em `localStorage`), os ícones, os selos e o handle discreto.
- Os chips trocam os checkboxes, com os mesmos `data-kind` e `#changedOnly`.
- O trilho, com o modo Pedestrians ainda desligado até o passo 10.
- **Conferir:**
  - captura do CENTRAL com 119 selecionados: sem parede azul;
  - `listed()` igual ao de antes para os mesmos filtros (o `_test.html` já faz Ctrl+A em pickups);
  - linha de cópia logo abaixo do original, com recuo.

### Passo 6: painel de propriedades (4.2, P11, P18). Custo M.
- As seções, o estado vazio com o resumo da fase e o "Saves to ...", e o multi com contagem por tipo.
- O Delete com Carry diz quantos vão: "Delete 5 (+3 on top)".
- **Conferir:**
  - captura de um prédio, de um pickup, de um apagado e de 5 selecionados;
  - a 1280 px, sem barra horizontal no `#right` (`scrollWidth <= clientWidth`, checado no harness).

### Passo 7: barras flutuantes, atalhos e dica (4.4, 5.2, 5.3, P9). Custo M.
- Move, Turn e Snap saem do painel com os mesmos ids.
- Overlay "?" com a tabela de 5.2.
- Chip de dica por contexto e o `#busy` novo.
- **Conferir:**
  - capturas do UFO e do CENTRAL com Game look ligado: a dica legível sobre a estação;
  - `?` abre o diálogo, e `Esc` fecha sem desselecionar.

### Passo 8: cena (3.5, P15). Custo M.
- Recolorir o gizmo; caixas do ativo com `depthTest=false`; hover com cursor; grade dupla; caixa tracejada dos apagados; caixas
  dos alterados com o chip Changed.
- **Conferir:**
  - captura com um prédio selecionado atrás de outro (o contorno aparece);
  - `gizmo` sem nenhum material 0xffff00 (percorrer no harness);
  - FPS no CENTRAL igual ao de antes com hover ligado: medir `renderer.info.render.frame` em 5 s no harness, com tolerância.

### Passo 9: diálogos (4.7, 4.8, P14, P20). Custo M.
- Settings em seções.
- `POST /api/paths` (validação) e `clone`/`canBrowse` no `init_reply`.
- Os diálogos de troca (fase e mod), Test, New mod e jogo aberto, no lugar dos `confirm()`.
- **Conferir:**
  - `/api/paths` com um caminho inexistente devolve "Not found";
  - captura do Settings nos dois temas;
  - trocar de fase com `dirty` abre o diálogo novo, e não o `confirm`: `window.confirm` substituído no harness por um que
    falha o teste.

### Passo 10: Check estruturado e mod vencedor ao carregar (5.4, P4). Custo M.
- `lvl.check_collision(st, out)`, `check_patch` devolvendo `summary` e `problems`, e `/api/stage` com `winner` e `others`.
- Aba Check com a lista clicável.
- Save com a checagem falhando grava e mostra o toast âmbar. Não pergunta: o Save precisa continuar valendo, já que o jogo pula a
  entrada.
- **Conferir:**
  - um patch com `from` errado devolve `problems[0].handle`;
  - clicar seleciona o objeto;
  - um mod "Zeta" com o mesmo arquivo mostra o callout ao carregar.

### Passo 11: modo Pedestrians (4.5). Custo G (junto com a ligação do `peds.js`).
- O trilho troca a lista, o painel e o raycast (`peds.items()` em vez de `itemOf`).
- As cores de 3.5, os medidores, Add group e Add sidewalk no centro da cena, e os textos novos do `info()`:
  - grupo: `Walks on sidewalk 12 (250 units long)`;
  - grupo sem calçada: `Not on any sidewalk: move it closer to one.`;
  - fase sem rede: `This stage has no sidewalks yet: add one first.`;
  - calçada: `Sidewalk 12 · 250 units long · 40 units each side`.
- Os números das calçadas são a posição na lista (1..N), não os nós.
- **Conferir:**
  - harness: `peds.counts()` bate com os medidores;
  - `buildPatch().peds` depois de mover um grupo;
  - Save e `/api/check` trazem as linhas de pedestres (passo 0);
  - captura do METRO no modo Pedestrians.

### Passo 12: Fight HUD (4.6, P16). Custo P (junto com a ligação do `hud.js`).
- Botão `[H]` do trilho, o CSS movido para o `style.css`, o callout de mod, os rótulos Original e In your mod, Save original as
  PNG, arrastar o PNG e Reload in game no rodapé.
- `ctx.log` para toast.
- **Conferir:**
  - abrir sem mod mostra o callout e Replace desligado;
  - com mod, `/api/hud/replace` com um PNG pequeno muda a miniatura;
  - captura nos dois temas e de uma fase B.

### Passo 13: documentação. Custo P.
- `MODS.md`, "Level editor", reescrito para a tela nova (presente, em inglês): trilho, barra de status, Output, Restore, Redo,
  Home, Stop game, Pedestrians e Fight HUD. Ícones MDI.
- `TODO.md`, item 2: tirar "retoque visual da UX" quando terminar.

## 7. O que fica de fora e por quê

- **Linhas `[fase]` do runtime em inglês, ou uma linha-máquina estável (`[fase] @applied CENTRAL mod=Teste moved=2`).** Mexe no
  `native/` e muda o que os outros diagnósticos esperam. É decisão do dono. Até lá, a tabela de 5.5 no editor resolve, e uma
  linha desconhecida fica crua na aba Game.
- **Tour de 3 dicas na primeira vez.** O estado vazio, a dica do contexto e o "?" cobrem a descoberta com menos código.
  Reavaliar depois do teste do dono.
- **Browse num navegador comum.** `<input type=file>` não dá o caminho, e um `tkinter.filedialog` aberto pela thread do servidor
  HTTP é frágil (a Tk quer a thread principal; no exe congelado não há outro processo Python para chamar). Só na janela
  pywebview (`create_file_dialog`). No navegador ficam o campo e o selo Found.
- **Rótulos 3D (CSS2DRenderer) e contorno por pós-processo.** Precisariam vendorizar mais addons. O `EffectComposer`/`OutlinePass`
  ainda passaria por `OutputPass`, que converte espaço de cor, e o editor desenha os bytes do GS crus (`ColorManagement`
  desligado, `LinearSRGB`). As caixas com `depthTest=false` dão o efeito principal.
- **Inverter a seta azul do gizmo para +Y.** O `TransformControls` desenha os eixos do mundo do three. Trocar o sentido exigiria
  espelhar a cena ou reescrever o gizmo. Ficam a cor do campo Y e o tooltip.
- **Esconder na cena os tipos desmarcados.** Os chips continuam valendo só para a lista, a caixa e o Ctrl+A, com tooltip que
  diz isso. Para esconder na cena, o lugar é o menu Layers, quando houver pedido.
- **Tooltip próprio em CSS, `navigator.keyboard.getLayoutMap`, escolha do monstro no Test.** São de valor baixo agora. O Test
  continua Togera em treino, e o tooltip diz isso: "Open the game on this stage in training mode, as Togera".
- **Remix Icon.** O MDI tem todos os ícones.
- **Fontes baixadas.** Só as do sistema, que já são boas no Windows e no Linux.
- **Texto do aviso "No collision".** Fica neutro até confirmar no jogo o que acontece com um objeto sem caminho de colisão.
