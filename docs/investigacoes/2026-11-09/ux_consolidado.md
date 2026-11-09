# Editor de fase: UX consolidado (crítico de completude e arquiteto de UX)

Entrada: os quatro relatórios desta pasta (arquitetura da tela, Nielsen/fluxos, painéis e diálogos, visual e acessibilidade), 97 achados
(IA 18, NH 30, PN 26, VA 23). Este arquivo (1) confere por amostragem no código, (2) acrescenta o que ninguém
viu, (3) funde os repetidos, (4) fixa **um** layout e (5) dá um plano de execução. Escala de severidade de Nielsen 0 a 4, esforço S/M/L.
Somente leitura no código do editor; nada foi gravado fora desta pasta.

## 0. Veredito

O núcleo de edição de objetos está sólido e deve ficar como está (seleção, mover/girar, duplicar, apagar com Desfazer, Arrange, a pergunta
"Cancel / Discard / Save" ao trocar de fase). Os problemas se concentram em três causas:

1. **O que não é o patch da fase não tem dono nem regra de gravação** (pontos, redes, luz, texturas, HUD, imagem da fase, props gerados).
   Cada um grava de um jeito, nenhum entra no Desfazer nem no "unsaved changes", Fechar/Esc descarta em silêncio e "Back to the disc's" apaga o
   arquivo do mod sem perguntar. É a causa de metade dos achados de severidade 3.
2. **Não existe critério para onde uma ferramenta mora.** Barra de vista, trilho, menu Add prop, barra de projeto e painel vazio recebem
   itens por ordem de chegada; é o "posicionamento arbitrário" que o dono percebeu (Terrain dentro de Add prop, seis ferramentas de dados da fase
   na barra de vista, o catálogo de todos os modelos só em Ctrl+P).
3. **O molde de diálogo modal foi usado para ferramentas que precisam da vista** (Start points, Power-ups, AI net, Vehicle net, Light) e o
   modal congela justamente a câmera de que "Aim" depende.

Achado que **ninguém tinha visto e é o mais grave de todos**: o servidor Python aceita POST de qualquer página aberta no navegador do
usuário (N-1 abaixo). Corrigir antes de qualquer redesenho.

Outra correção de premissa do enunciado: **os testes `test_*.mjs`/`test_*.py` do repo não são um harness de UI.** São testes de aritmética
(`test_arrange.mjs`, `test_tilt.mjs`, `test_tools_view.mjs`) e de servidor/codec (`test_*.py`, importam `editor` ou fazem HTTP). Nenhum toca
um id do DOM. O harness CDP existe só no scratchpad do dono (`cdp_*.mjs`) e nas páginas `web/_*.html` (ignoradas pelo git por
`.gitignore:54`) que dirigem o editor num iframe. Ver 7, etapa 0.

## 1. Verificação por amostragem

Critério: abri a linha citada no código atual (branch `editor-fase`, HEAD 5d4948a). 22 achados conferidos, 15 de severidade >= 3 ou que
sustentam uma decisão do plano.

| Achado | Veredito | Prova no código |
|---|---|---|
| IA-1 (contagem do overlay) | PARCIAL | `index.html:133-152`: 16 botões, conferido. Mas o título diz "só 7 são de vista" e o corpo lista 9 de vista (look, grid, layers, fly, orbit, focus, frame, ruler, shot). Certo: **9 de vista, 7 de dados/ajuda** (gridBtn, sptBtn, pupBtn, aipBtn, fopBtn, picBtn, keysBtn). A conclusão (separar) vale |
| IA-7 / VA-1 (barras se cobrem) | CONFIRMADO | `style.css:416,421-422`: `.float{position:absolute;z-index:2}`, `#toolBar{left:8px}`, `#overlay{right:8px}`, independentes, sem `flex-wrap`. O caso "Add prop sob o overlay a 1600 com Snap ligado" é conta não medida (PARCIAL); o de 1100 px está na captura 14 |
| VA-3 (grade fixa) | CONFIRMADO | `style.css:221`: `grid-template-columns: 44px 272px minmax(0,1fr) 304px` |
| IA-8 / NH-7 (paleta sem botão) | CONFIRMADO | `grep openPalette`: definição `app.js:4240` e uma única chamada, o atalho `app.js:4675`. Nenhum `onclick` nem item de menu |
| NH-1 / PN-2 (Fechar descarta) | CONFIRMADO | `app.js:3589` (`sptDlg` `close` recarrega do arquivo se `ptKeep != ptSaved`), `app.js:3904-3907` (`netDlg`), `light.js:104` (`ctx.restore()`); `lightClose()` em `loadStage` (`app.js:1030`) também descarta ao trocar de fase com Light aberto |
| NH-2 (apaga arquivo sem perguntar) | CONFIRMADO | `app.js:3585-3588` `sptReset` e `app.js:3900` `netReset` chamam `ptPost/netPost({reset:true})` direto, sem `ask()`; servidor faz `os.remove` (`editor.py:741` citado, mesmo padrão de `save_patch:1528`) |
| NH-3 / PN-1 (modal bloqueia a vista) | CONFIRMADO, severidade 4 rebaixada para 3 | `showModal()` em `app.js:3549` e `3733`; "Add at the view's aim" (`app.js:3556`) e o "Aim" por linha (`3514`, `3684`) usam `viewPoint()` com a câmera congelada pelo modal: todo ponto novo cai no mesmo lugar. Rebaixo de 4 para 3 porque dá para digitar x/y/z; o efeito composto com NH-1 (fechar para mirar apaga o rascunho) é o que torna grave |
| NH-4 (falha ao abrir fase) | CONFIRMADO | `app.js:1035` seta `stageName = name` antes do `api()`; `recs`/`byHandle`/`history` só são refeitos na linha 1056, depois do `await`. O `catch` (1104) só emite mensagem: o `<select>` mostra a fase nova, a cena foi removida, `recs` é da fase antiga e `post()` (2399) manda `buildPatch()` dos `recs` antigos com `name: stageName` novo |
| NH-8 / PN-4 (Light mata atalhos) | CONFIRMADO (o relatório deixou "verificar") | `app.js:4676`: `if (document.querySelector('dialog[open]')) { ...; return; }`; `light.js:210` usa `dlg.show()`, que também põe o atributo `open`. Resultado: WASD, G/R, F, Ctrl+Z e Ctrl+D morrem com o Light aberto; Ctrl+S só dá `preventDefault`. Ctrl+P escapa porque vem antes do teste |
| NH-12 / PN-25 (Remove apaga PNG) | CONFIRMADO | `tex.js:172-178` `remove()` chama `/api/tex/remove` direto, sem `ctx.ask`; `hud.js:104` idem |
| NH-13 (caixa ignora props novos) | CONFIRMADO | `app.js:2248-2250`: `kinds.has(it.rec.info.kind)`, mas `makeProp` (`app.js:707`) usa `kind: 'newprop'` e `listed()` (1156) traduz para `'prop'`. A caixa nunca inclui props novos, enquanto Ctrl+A (que usa `listed()`) inclui |
| NH-14 (campo vazio vira 0) | CONFIRMADO | `app.js:2003`: `+$('px').value` com `''` é 0; ligado em `app.js:4512` por `onchange` sem teste de vazio |
| NH-15 (Test without them + Reload grava) | CONFIRMADO | `testGame` (`app.js:3015-3021`) faz `post('test', false)`; o servidor responde 409 `running` (`editor.py:2086`) e `askRunning` (2524-2531) chama `post('reload', true, ...)`, que tem `writes = true` (2399) e `save: true` |
| NH-16 (nova fase descarta) | PARCIAL | `app.js:4340-4362`: `dirty = false` e `loadStage` sem perguntar. O patch aberto só é preservado se `nsCopy` está marcado **e** a base é a mesma (`withPatch`, 4346); com base diferente as edições abertas somem |
| NH-27 (Save após desfazer tudo remove o arquivo) | PARCIAL | `editor.py:1519-1531` remove, mas `app.js:2434-2436` mostra "Removed mods/...: your mod no longer changes X" (com toast em `kind === 'save'`). Não é "anunciado como sucesso" mudo; rebaixo para 1 |
| VA-4 (lista não é alcançável por teclado) | CONFIRMADO | `rowEl` (`app.js:1191-1226`) só define `li.onclick`; nenhum `tabindex` em `app.js` (grep vazio) |
| IA-11 (Reload sempre visível) | CONFIRMADO | `index.html:38`: `#reload` com `disabled` e sem `hidden` |
| IA-12 (Check pesa como Save) | CONFIRMADO | `editor.py:2090-2091`: `/api/save` e `/api/test` (com save) chamam `check_patch` antes de gravar |
| IA-4 (Textures x HUD se sobrepõem; "a verificar o every") | PARCIAL, dúvida resolvida | `hud.js:93-106` tem a caixa "Apply to all N stages with this picture" (`every`); `tex.js` não tem `every` (grep). O HUD **não** pode virar só um filtro do Textures antes de o Textures ganhar `every` |
| IA-1/IA-3 (contagem "doze coisas da fase") | CONFIRMADO | Conferi os ids em `index.html`: 22-26, 49-51, 126-127, 144-149 |
| NH-9 (botão Test options abre "Test in game") | CONFIRMADO | `testopts.js:121` título, `index.html:37` rótulo |
| IA-10 (contagem 4x) | CONFIRMADO | `index.html:66` (chip), `74` (`#count`), `184` (`#sumCounts`), `362` (rodapé) |
| IA-17 (popovers reimplementados) | CONFIRMADO | Esc de `layersMenu` e `propMenu` tratados à mão em `app.js:4698-4699` |
| NH-20 (aviso só em toast de 4 s) | NÃO REVERIFICADO | confio no relatório |
| VA-7/VA-8 (razões de contraste) | NÃO REVERIFICADO | são contas; não recalculei |

**Falsos:** nenhum achado inteiro era falso. Correções pontuais: IA-1 (contagem 7 vs 9), NH-27 (rebaixado), NH-8 (de "verificar" para
confirmado), IA-4 (resolvida), PN-1 (4 para 3).

## 2. Achados novos (ninguém viu)

| ID | Sev | Esf | Achado e prova |
|---|---|---|---|
| N-1 | **4** | S | **Servidor local aceita POST de qualquer site (CSRF) e a configuração leva a executar um programa.** `editor.py:1845-1847` lê o corpo como JSON sem olhar `Content-Type`; o único header lido em todo o arquivo é `Content-Length` (`grep headers.get`). Não há teste de `Origin` nem de `Host`. Uma página qualquer faz `fetch('http://127.0.0.1:47197/api/config', {method:'POST', mode:'no-cors', body:'{"game":"C:\\\\...\\\\x.exe"}'})` (corpo `text/plain` não dispara preflight), depois `POST /api/test`, e `launch_game` (`editor.py:1701-1725`) faz `Popen([cfg["game"], ...])`. Também grava/remove arquivos do mod por `/api/save` e `/api/hud/*`. DNS rebinding contorna a porta fixa 47197. Correção: exigir `Host` = `127.0.0.1:<porta>`/`localhost:<porta>`, rejeitar `Origin` presente e diferente, exigir `Content-Type: application/json` nos POSTs (isenta `/api/bye`, que o `sendBeacon` manda como `text/plain`) |
| N-2 | 3 | S | **Servidor fora do ar não é explicado.** `api()` (`app.js:41-46`) deixa `fetch` rejeitar com "Failed to fetch"; `cleanError` (2285) mostra a string crua; não há indicador de conexão (grep `Failed to fetch|offline` vazio). O servidor encerra sozinho 10 s depois de um `pagehide` sem pedido novo (`editor.py:2135,2177`; `app.js:4763`). Cenários: janela fechada por engano, `pagehide` por navegação/descarte da aba, console fechado. O usuário com patch sujo vê "Couldn't save: Failed to fetch" sem saber que o programa saiu, e não tem como exportar o patch |
| N-3 | 2 | S | **Nenhum guarda de requisição em andamento em Save/Check.** `post()` (`app.js:2397-2500`) só marca estado para `test`/`reload` (`setGameState`); `#save` e `#check` ficam ativos durante o `check_patch` (segundos em fase grande), então duplo clique/Ctrl+S repetido manda dois POSTs concorrentes ao mesmo `levels/<STAGE>.json`, e não há indicação de "Saving…" (heurística 1) |
| N-4 | 2 | M | **Sem recuperação do rascunho.** O `localStorage` só guarda preferências (`app.js:50-51,460,4080`); o patch sujo existe só na memória. `beforeunload` (4726) cobre fechar, mas não queda do navegador, descarte da aba, nem o servidor sair (N-2). Perder uma hora de edição é o pior erro de trabalho do editor |
| N-5 | 2 | S | **Save sobrescreve sem checar se o arquivo mudou no disco.** `save_patch` (`editor.py:1519-1531`) não compara mtime/hash; o `loadStage` é a única leitura. O próprio editor documenta que o arquivo aceita edição à mão e chaves de "um editor mais novo" (comentário 1521). Edição manual, outro mod do dono ou o `modkit.py` entre abrir e salvar é perdida em silêncio |
| N-6 | 2 | S | **Erro de permissão/disco cheio em texto técnico.** `do_POST` (`editor.py:2104-2106`) devolve 500 com `PermissionError: [Errno 13] Permission denied: '<caminho>'`; o front só tira o nome da classe (`cleanError`) e não oferece ação (a ação "Open settings" só aparece para `code === 'settings'`). Mod em pasta somente leitura, antivírus ou arquivo aberto no jogo (o `.tmp` + `os.replace` falha no Windows se o jogo está lendo) dão a mesma mensagem |
| N-7 | 2 | S | **Props gerados/importados ficam órfãos ao descartar.** Import .glb e Terrain gravam o prop na hora no mod (IA-6, `terrain.js:165` pergunta só ao substituir). Depois de Discard ou de Desfazer, o arquivo do prop continua em `mods/<mod>/props/` e entra no Pack mod. A lista do menu Add prop passa a mostrar peças que nenhuma fase usa |
| N-8 | 1 | S | **Ctrl+P é interceptado antes do guarda de diálogo** (`app.js:4675` vem antes de 4676), então abre a paleta por cima de qualquer diálogo, inclusive durante o `ask()` "Unsaved changes". Combinado com o `dlg.close()` do `ask` (`app.js:2490`) a pergunta pode ser descartada (resposta `cancel`) por um atalho |
| N-9 | 1 | S | **O título da janela perde o marcador de alterações durante o bake** (`document.title = 'Baking pictures n/m'`, `app.js:4031,4037`, sobrescreve o `*` de `app.js:1269`). Só afeta quem vê pelo título/barra de tarefas |

Cobertura de heurísticas depois desta rodada: heurística 1 (N-3, N-2), 3 (N-8), 5 (N-1, N-7), 9 (N-6), 10 (já em NH-28). A de "fase grande
lenta" **não foi medida**: `refreshList` refaz a lista inteira a cada edição (`app.js:1232`); com 331 objetos é invisível, com milhares não
sei. Fica como pergunta, não como achado.

## 3. Achados fundidos (clusters)

Cada linha substitui vários achados; o plano usa estes ids.

| Cluster | Sev | Funde | Resumo |
|---|---|---|---|
| K1 Modelo de gravação | 3 | NH-1, NH-2, NH-5, NH-10, NH-11, NH-12, NH-18, PN-2, PN-3, PN-11, PN-25, IA-6, IA-15, N-5, N-7 | quatro semânticas, rascunho descartado ao fechar, apagar sem confirmar, sem aviso do que está onde |
| K2 Ferramentas de vista em modal | 3 | PN-1, NH-3, IA-5, PN-5, PN-6, NH-8, PN-4 | modal congela a câmera; Light "não modal" mata os atalhos |
| K3 Onde cada coisa mora | 3 | IA-1, IA-2, IA-3, IA-4, NH-6, PN-8, PN-9, PN-7, VA-2, IA-13 | propriedades da fase em cinco lugares; gerador no Add prop |
| K4 Geometria da tela | 3 | IA-7, VA-1, VA-3, VA-19, VA-6 | barras se cobrem, grade fixa, barra superior corta |
| K5 Descoberta e ajuda | 3 | IA-8, NH-7, IA-9, NH-28, NH-23, PN-22 | paleta sem botão, ajuda só em atalhos, comandos duplicados |
| K6 Teclado, foco, leitor de tela | 3 | VA-4, VA-5, VA-9, VA-13, VA-14, VA-16, PN-18, NH-23 | lista sem foco, Tab longo, forced-colors, tabelas sem semântica, Enter |
| K7 Barra de projeto e teste | 2 | IA-11, IA-12, IA-18, NH-9, NH-26, NH-30, PN-17 | cinco controles de teste, Check pesado, comandos de projeto soltos |
| K8 Bugs de dados | 3 | NH-4, NH-13, NH-14, NH-15, NH-16, NH-17, NH-27 | falha ao abrir fase grava na errada; campos vazios; caixa; reload grava |
| K9 Segurança e robustez do servidor | 4 | N-1, N-2, N-3, N-4, N-6 | CSRF, queda do servidor, guarda de requisição, rascunho, erro de permissão |
| K10 Vocabulário e consistência | 2 | NH-22, NH-29, NH-20, PN-19, PN-23, PN-24, IA-10, IA-14, IA-16, NH-25, VA-11, VA-23 | jargão, "preciso de um mod" em 4 jeitos, contagens repetidas, ícones repetidos |
| K11 Visual e acessibilidade | 2 | VA-7, VA-8, VA-10, VA-12, VA-15, VA-17, VA-18, VA-20, VA-21, VA-22 | contraste, alvos, ícones sem rótulo, tokens, tipografia |
| K12 Painéis específicos | 2 | PN-10, PN-12, PN-13, PN-14, PN-15, PN-16, PN-21, PN-26, NH-19, NH-21 | terreno sem prévia, redes por número, Arrange sem prévia, escopo de Name/Details |

## 4. Proposta de layout (única)

### 4.1 Princípios

1. **A vista só olha e manipula. Nada que grave mora nela.** Câmera, Display (camadas), régua, captura, e à esquerda Move/Turn/Snap.
2. **Criar mora no menu Add, ao lado da lista que ele povoa** (painel esquerdo), contextual ao modo.
3. **Propriedades têm duas casas no painel direito:** aba *Selection* (o que está selecionado) e aba *Stage* (o que é da fase).
4. **Três moldes e só três:** (a) **diálogo modal** para formulário curto e navegador de assets (New stage, Settings, Import, Textures, HUD,
   Terrain); (b) **painel de ferramenta acoplado** (não modal) para tudo que depende da vista: Start points, Power-ups, AI net, Vehicle net, Light, Stage size;
   (c) **pergunta** (`ask`). A paleta Ctrl+P é uma variante de (a).
5. **Todo estado gravável diz como grava**, com três rótulos únicos: *Patch* (vai no Save, tem Desfazer), *File* (arquivo do mod, tem Save próprio mas entra no
   contador de não salvos e pede confirmação ao fechar), *Now* (grava na hora, não desfaz, pede confirmação ao apagar).

Resolução dos conflitos entre os relatórios:
- Arquitetura propunha Light/Textures como seções da aba Stage; Painéis propunha acoplar tudo no `#right`. **Decisão:** a aba Stage lista as
  ferramentas como linhas rotuladas; ferramentas do molde (b) abrem o **painel de ferramenta** que substitui o corpo do `#right` (cabeçalho
  "<- Stage / Start points", rodapé Revert/Save); ferramentas do molde (a) abrem diálogo. Textures e HUD continuam diálogos (navegador de assets, não dependem da vista).
- A tabela da rede tem 8 colunas e o `#right` tem 304 px. **Decisão:** o painel de ferramenta usa linha em duas linhas (nó + tipo, depois x y z + ligações) e o
  `#right` ganha `--right-w` (304 normal, 380 com painel de ferramenta aberto). Isso é o que a proposta de "acoplar" ignorava.
- Marcadores como modos de edição (selecionar na vista e arrastar com gizmo) ficam **fora desta rodada** (L, decisão do dono; ver 8).

### 4.2 Antes e depois (1600x900, modo Objects)

Antes:

```
+- #bar ---------------------------------------------------------------------------------------------------------+
| Level Editor | [Stage v] + pencil | [Mod v] pack | undo redo | .... | Check Save | [Test] pin tune [Reload] stop | ? cog |
+----+-------------+------------------------------------------------------------+---------------------------+
|rail| #left       | #toolBar: Move Turn Snap [Add prop v]   #overlay (16 botoes) | #right                    |
|Obj | search      |   propMenu: props, Prefabs, Sky, Clouds,  look grid layers  | Nothing selected          |
|Ped | chips       |   Import, TERRAIN                         Fly Orbit focus   |  keys, summary, Saves to  |
|----| list        |                                           frame SIZE START  | ou Selection:             |
|Tex |             |                                           POWER AI VEH PIC  |  Transform Actions Arrange|
|Lit |             |                                           ruler shot keys   |  Behavior Name Details    |
|HUD | N objects   |  (barras se cobrem em < 1400 px)                            |                           |
+----+-------------+------------------------------------------------------------+---------------------------+
| v mensagem                                  Saved?  Check  budget  Game  N changed                        |
+-----------------------------------------------------------------------------------------------------------+
```

Depois:

```
+- #bar ---------------------------------------------------------------------------------------------------------+
| Level Editor | [Stage v] [Mod v] [File v] | undo redo | .... | Save | [> Test in game | v]  (stop; Reload so com jogo) | ? cog |
|   File v: New stage, Edit stage, New mod, Pack mod, Check collision          Test v: Test from here, Test options.. |
+----+--------------+-----------------------------------------------------------+-----------------------------+
|rail| #left        | #viewTop (flex, quebra linha, sem sobreposicao)           | #right  [Selection | Stage] |
|Obj | [+ Add v][find]| Move Turn | Snap steps        Fly Orbit focus frame     | Selection: como hoje        |
|Ped |  Browse models  |                           [Display v] ruler shot       | Stage:                      |
|    |  Props of <mod> |                                                         |  Stage      nome, base, Saves to |
|    |  Prefabs        |  Display v: Look: Game look                             |  Size       Stage size...      |
|    |  New model from |             Guides: Grid, Minimap                       |  Environment Light and fog,  |
|    |  a file: Import |             Overlays: Collision, Peds, Vehicle net,      |              Sky, Clouds     |
|    |  .glb, Terrain  |                       AI net, Start points, Power-ups    |  Pictures   Textures, HUD,   |
|    | chips, lista    |                                                         |             Stage picture    |
|    |                 |                                                         |  Gameplay   Start points,    |
|    |                 |                                                         |   Power-ups, AI net, Veh net |
|    | 331 objects     |  (modo Peds: o slot Add mostra Add group/Add sidewalk) | (tool panel substitui o     |
|    |                 |                                                         |  corpo; --right-w 380)      |
+----+--------------+-----------------------------------------------------------+-----------------------------+
| v Midtown . ZZNetTest  mensagem                 Saved  Check  budget  Game  N changed . M selected        |
+-----------------------------------------------------------------------------------------------------------+
```

Trilho: só modos (Objects, Pedestrians; os marcadores viram modos depois, se o dono quiser). Painel esquerdo e direito colapsáveis
(botão no cabeçalho de cada um) para a vista não ser "o que sobra" abaixo de 1100 px.

### 4.3 Destino de cada controle

| Controle atual | Novo lugar | Mudança |
|---|---|---|
| `#stage`, `#mod` | barra, igual | sem mudança |
| `#newStage`, `#editStage`, `#packMod` | menu `File v` da barra | rótulo visível; Edit stage mostra o motivo quando desabilitado |
| opção "New mod" do select `#mod` | `File v` > New mod (a opção do select continua) | duplicado de propósito (descoberta) |
| `#undo`, `#redo` | barra, igual | |
| `#check` | `File v` > Check collision; o item `Check` do rodapé passa a executar quando "Not checked" | perde o destaque |
| `#save` | barra, igual | único botão com peso, mais o estado `Saved/N unsaved` que inclui arquivos |
| `#test` | botão dividido `Test in game` + `v` | vira "Reload in game" quando o jogo roda |
| `#testHere`, `#testOpts` | itens do `v` do Test | o botão do diálogo Test options vira só "Test in game" |
| `#reload` | mesmo botão dividido, só com jogo aberto | `hidden` fora disso |
| `#stop` | ao lado, só com jogo aberto; pergunta se há luta em curso | rótulo "Stop game" |
| `#help` | barra, igual | `#keysBtn` removido da vista |
| `#settings` | barra, igual | |
| `#modeObjects`, `#modePeds` | trilho, igual | |
| `#texBtn` | aba Stage > Pictures > Textures | linha rotulada, diálogo |
| `#hud` | aba Stage > Pictures > Fight HUD and sky | diálogo; só migra para filtro do Textures quando Textures tiver `every` |
| `#lightBtn` | aba Stage > Environment > Light and fog | painel de ferramenta (b) |
| `#pmSky`, `#pmClouds` | Light and fog, seção Sky and clouds | saem do menu Add; mesmos ids dos selects |
| `#gridBtn` (Stage size) | aba Stage > Size | painel (b); ações destrutivas viram "Start from empty" com `ask()` |
| `#sptBtn`, `#pupBtn` | aba Stage > Gameplay | painel (b) |
| `#aipBtn`, `#fopBtn` | aba Stage > Gameplay | painel (b) |
| `#picBtn` | aba Stage > Pictures > Stage picture | confirma antes de sobrescrever; explica o requisito antes do clique |
| `#editStage` (nome/base) | também em aba Stage > Stage (botão "Edit") | |
| `#addProp` e `#propMenu` | botão `+ Add` no cabeçalho do painel esquerdo | mesmo menu; ids mantidos |
| paleta (Ctrl+P) | primeira linha do Add: "Browse models... Ctrl+P" e lupa "Find..." ao lado da busca | |
| `#pfTurn`, `#pfSave` | seção Prefabs do Add (rodapé da seção) | |
| `#pmImport`, `#pmTerrain` | seção "New model from a file" do Add | texto igual; Terrain ganha "Edit terrain..." na peça selecionada (depois) |
| `#pedAddGroup`, `#pedAddSeg` | mesmo slot do Add, no modo Peds | |
| `#look`, `#grid` | popover `Display v` | |
| `#layersBtn`, `#layersMenu` | vira o popover `Display v` | Pedestrians, Collision, Minimap, Vehicle net, + AI net, Start points, Power-ups |
| `#vFly`, `#vOrbit`, `#focusSel`, `#frame` | `#viewTop`, direita | |
| `#rulerBtn`, `#shotBtn` | `#viewTop`, direita | |
| `#tMove`, `#tTurn`, `#snap`, `#snapMove`, `#snapTurn` | `#viewTop`, esquerda | |
| `#count` (esquerda) | removido | o chip `Changed (n)` e o rodapé bastam |
| `#sumCounts` | removido da aba Selection vazia; o texto vai para o cabeçalho da aba Stage | **o id é a sonda de "carregou" dos scripts de teste do dono**; manter um `<span id="sumCounts" hidden>` até migrar os scripts (ver 7) |
| `#sumStage`, `#sumMod`, `#sumPath` | aba Stage > Stage | |
| `#noSel` (atalhos, Getting started) | aba Selection quando vazia, igual; "Getting started" só até o primeiro Save | |
| `#output`, `#status` | igual | contagem de alterações só no rodapé |
| `#sptDlg`, `#netDlg` | painel de ferramenta (b) | não modal; rascunho entra no contador |
| `#gridDlg` | painel de ferramenta (b) | |
| `#texDlg`, `#hudDlg`, `#terrainDlg`, `#propDlg`, `#pfDlg`, `#stageDlg`, `#dlg`, `#modDlg`, `#keysDlg`, `#palDlg`, `#askDlg` | diálogos (a), iguais | molde único (`.dlg-head/body/foot`, Enter = primário, foco inicial no primeiro campo) |

### 4.4 Padrões

- **Menu/popover:** um componente (`menu.js`): abre e fecha, `aria-expanded`, Esc devolve o foco, setas, fecha ao clicar fora, um aberto por vez. Serve `Add`, `Display`, `File`, `Test v`.
- **Diálogo (a):** rodapé `[Cancel] [Primary]` com o verbo do que faz ("Save to mod", "Create stage"); Enter aciona o primário (`type="submit"`); Esc = Cancel;
  foco inicial no primeiro campo; mudou algo e vai fechar sem aplicar: pergunta.
- **Painel de ferramenta (b):** cabeçalho com voltar e título; corpo em linhas de duas linhas; rodapé `Revert` (volta ao arquivo) e `Save to mod`; tem estado
  `unsaved` próprio, somado ao contador global e ao `beforeunload`; Esc sai se limpo, pergunta se sujo; **os atalhos da vista continuam valendo**
  (o guarda do teclado passa a olhar `dialog:modal`, não `dialog[open]`).
- **Pergunta:** `ask()` para todo ato destrutivo ou irreversível, com o nome do que será perdido ("Take mods/X/PTS/CENTRAL.SPT out of the mod?").
- **Gravação:** rótulo `Patch`/`File`/`Now` como `<small>` em cada linha da aba Stage e no cabeçalho do painel de ferramenta.
- **Pré-requisito "mod":** uma única função `requireMod(action)` (selo na barra + o `pickMod` que já existe); os seis jeitos atuais saem.
- **Ícones:** um ícone por significado (tabela de IA-14 mais `flag-outline`, `heart-plus-outline`, `graph-outline`, `image-frame`).

## 5. Fora do escopo

- Marcadores (largada, power-ups, nós das redes) como objetos selecionáveis e arrastáveis na vista, com gizmo (IA-5 passo 3): é a solução certa, mas é L e
  mexe em modelo de dados e Desfazer. O painel acoplado resolve a dor imediata.
- "Edit terrain..." reabrindo o gerador a partir da peça (PN-10), prévia do Arrange (PN-13), editor de rede por grafo (PN-12): valor real, esforço M a L cada.
- Tradução da interface (hoje toda em inglês; documentação em português): decisão do dono, não achado.
- Medição de fase muito grande (lista com milhares de objetos): sem medida; só se aparecer.
- Redesenho visual (tipografia escalável, paleta nova): só os consertos de contraste e de foco.
- Mudar o formato do patch ou os endpoints, exceto o endurecimento de N-1.

## 6. Ordem e critério

Ordem por (severidade x valor) / esforço, com a regra de que **nenhuma etapa de layout começa antes de existir teste de UI no repo**. Cada etapa é
um commit que deixa o editor funcionando e é revertível sozinha. As etapas 1 a 9 não mexem na posição de nenhum botão.

## 7. Plano de execução

Teste de referência para todas as etapas: o **smoke de UI** da etapa 0 (`tools/level_editor/test_ui.mjs`). Hoje não existe nenhum teste do repo que toque o
DOM, então "testes que quebrariam" abaixo são os do scratchpad do dono e as páginas `web/_*.html` (ignoradas pelo git) mais os testes de servidor.

| # | Etapa | Sev x valor | Esforço | Resolve |
|---|---|---|---|---|
| 0 | Smoke de UI no repo | habilitador | M | premissa do enunciado |
| 1 | Endurecer o servidor local | 4 | S | N-1 |
| 2 | Falha ao abrir fase e servidor fora do ar | 3 | S | NH-4, N-2 |
| 3 | Lote de bugs de dados | 3 | S | NH-13, NH-14, NH-15, NH-17, NH-16 |
| 4 | Destrutivos pedem confirmação | 3 | S | NH-2, NH-12, PN-25, NH-18, N-8 |
| 5 | Rascunhos de arquivos do mod entram no "unsaved" | 3 | S | NH-1, PN-2, PN-3 (metade) |
| 6 | Atalhos com painéis não modais | 3 | S | NH-8, PN-4 |
| 7 | Guarda de Save/Check e erro de permissão | 2 | S | N-3, N-6 |
| 8 | Quick wins de layout | 3 | S | IA-7, VA-1, IA-9, IA-11 (parte), PN-18, VA-16, IA-14 |
| 9 | Teclado e foco | 3 | S | VA-4, VA-9, VA-8 (foco), VA-17 |
| 10 | Componente `menu.js` | habilitador | M | IA-17 |
| 11 | Aba Stage + mover os botões | 3 | M | IA-1, IA-3, IA-4, VA-2, PN-8 |
| 12 | Menu Add no painel esquerdo | 3 | M | IA-2, IA-8, NH-6, NH-7 |
| 13 | Popover Display | 2 | M | IA-13, VA-2 |
| 14 | Barra: Test dividido, File, Check | 2 | M | IA-11, IA-12, IA-18, NH-9, PN-17, NH-30, NH-26 |
| 15 | Contagem, vocabulário e `requireMod` | 2 | S | IA-10, NH-25, IA-16, NH-22, NH-29, PN-19, PN-23, VA-23 |
| 16 | Painéis acoplados (Light, Stage size) | 3 | M | K2 (parte), PN-5, NH-5, IA-15, PN-11 |
| 17 | Painéis acoplados (pontos, redes) | 3 | L | PN-1, NH-3, IA-5 passo 2, PN-12 (layout) |
| 18 | Rascunho do patch e conflito de arquivo | 2 | M | N-4, N-5 |
| 19 | Contraste, alvos, ARIA | 2 | M | VA-7, VA-10, VA-12, VA-13, VA-14, VA-15, VA-22 |
| 20 | Grade colapsável | 2 | S | VA-3, VA-19 |

Detalhe:

**Etapa 0, smoke de UI no repo.** Copiar a receita do scratchpad do dono (Edge headless com `--use-angle=swiftshader --enable-unsafe-swiftshader
--remote-debugging-port`, `editor.py --serve --port 47391` com `mods` temporário, WebSocket nativo do Node 22, `?debug` e `window.wotmDebug`) para
`tools/level_editor/test_ui.mjs`. Casos mínimos: carrega CENTRAL e `sumCounts` casa /objects/; seleciona, move, `undo`; `post('save')` num mod temporário; abre e
fecha com Esc cada diálogo (`gridDlg`, `sptDlg`, `netDlg`, `texDlg`, `lightDlg`, `keysDlg`, `dlg`, `palDlg`); confere a lista de ids estáveis (tabela 4.3, coluna "Controle atual").
Arquivos: novo `test_ui.mjs`, `LEVEL_EDITOR.md` ou o guia existente (uma linha). Risco: Edge/Node na máquina do dono; renderização por software lenta (esperar por polling, como em `test_collision.py`).
Não pode escrever em `~/.wotm_level_editor.json` do dono: o teste usa `--config` temporário ou restaura (a memória do projeto avisa que o editor carrega o mod ativo do dono).

**Etapa 1, servidor.** `editor.py`: em `do_GET`/`do_POST`, rejeitar com 403 se `Host` não for `127.0.0.1:<porta>` ou `localhost:<porta>`; se há `Origin`, deve ser
`http://<esse host>`; em POST exigir `Content-Type` JSON, com exceção de `/api/bye` (o `sendBeacon` manda `text/plain`; ou trocar por `Blob` JSON em `app.js:4763`). Teste novo
em `test_custom_stages.py` (Host errado e Origin de fora devem dar 403; os atuais mandam JSON, `Host` correto e sem `Origin`, então passam). Risco: `webview`/`--app` mandam `Origin` igual; porta alternativa "free port"
(`editor.py:2168`) precisa entrar na lista; `--serve` acessado de outro host (WSL) deixa de funcionar sem `--allow-host`. Nenhum teste de UI quebra.

**Etapa 2, falha ao abrir.** `app.js` `loadStage`: guardar `prevName`; no `catch`, voltar `stageName`, `$('stage').value`, e se a cena já foi descartada, recarregar a anterior ou
travar Save/Test (`recs = []`) com callout "Stage X didn't open" e botão Retry. `api()`: capturar `TypeError` e dizer "The editor program isn't answering. It may have been closed; your changes stay in this window until it is." com botão Copy patch. Risco: `loadStage` é chamado em 4+ lugares
(`nsOk`, troca de mod); o smoke deve abrir uma fase inexistente (`?stage=XXXX`) e confirmar que `post('save')` recusa.

**Etapa 3, bugs.** NH-14: `editFromInputs`/`tiltFromInputs` ignoram campo vazio (volta ao valor anterior). NH-13: usar `kind === 'newprop' ? 'prop' : kind` na caixa. NH-15: `askRunning` passa o
`save` original: `post('reload', save, ...)` e o servidor só grava se `save`. NH-17: `dragging-changed` só tira o `snapshot` se houve movimento (comparar `startMove`). NH-16: `nsOk` pergunta se `dirty` e a base é outra. Tudo em `app.js`. Risco: `post('reload', ...)` com `save=false` precisa ser aceito pelo servidor (`editor.py:2064-2096`; hoje `/api/reload` sempre grava); o teste de servidor deve cobrir.

**Etapa 4, destrutivos.** `ask()` (já existe) antes de: `sptReset`, `netReset`, `netClear` (`app.js:3585,3900,3872`), `.lt-reset` (`light.js:198`), `remove()` de `tex.js`/`hud.js`, sobrescrever `levelpics` (`app.js:3926`), e N-8 (Ctrl+P ignora diálogo `askDlg` aberto). Texto com o caminho que será removido. Risco: `_spt.html`/`_gd.html` (ignorados) clicam em `sptReset`/`netReset` sem esperar; `test_textures.py` e `test_spt.py` chamam o servidor, não o botão, então não quebram.

**Etapa 5, rascunhos.** Um registro `drafts` em `app.js` (`spt`, `pup`, `aip`, `fop`, `light`) com `dirty()`; `unsavedCount()` (2505) e `beforeunload` (4726) somam; o `close` dos diálogos pergunta "Keep editing / Discard / Save". Aviso `File` no cabeçalho dos diálogos. Arquivos: `app.js`, `light.js`. Risco: os `close` listeners atuais (3589, 3904, `light.js:104`) são a lógica de descarte; reescrever sem perder o `loadNet`/`updateNetView` ao fechar. Smoke: editar um ponto, Esc, esperar `ask`.

**Etapa 6, atalhos.** O guarda (`app.js:4676`) passa a `document.querySelector('dialog:modal')` (Edge/Chrome 105+; o editor roda em Edge/Chrome ou pywebview). `Ctrl+S` com Light aberto salva o patch, com aviso de que o Light tem Save próprio. Esc em `light.js` fecha o painel. Risco: o foco do Light dentro de `<input>`: `typing()` já cobre. Verificar que WASD não vira nada quando se digita no painel.

**Etapa 7, requisição em andamento.** `post()`: `if (inflight) return` por tipo, `aria-busy` e rótulo "Saving…" em `#save`/`#check`; erro de `OSError` no servidor com `code: "disk"` e ação "Open the folder". Arquivos: `app.js`, `editor.py` (capturar `PermissionError`/`OSError` em `do_POST`). Risco: nenhum teste do repo.

**Etapa 8, quick wins de layout.** `#viewTop` (CSS + dois `div`, `flex-wrap`, filhos `position: static`, menus ancorados com `top: var(--view-top-h)`); remover `#keysBtn` e seu `onclick`; ícones únicos em `vendor/mdi/` (4 novos); `#reload` `hidden` quando fechado (`showGame`); Enter aciona o primário em todos os diálogos (`type="submit"` nos botões `type="button"` primários: `nsOk`, `gdOk`, `sptSave`, `netSave`, `ipOk`, `pfOk`, `tr-ok`); foco inicial no primeiro campo. Arquivos: `index.html`, `style.css`, `app.js`, `terrain.js`. Risco: `#overlay` e `#toolBar` são citados em `style.css:416-453` e em `@container (max-width: 820px)` (a regra de ícones só depende do container da vista; manter); scripts do dono clicam em `$('gridBtn')`, `$('sptBtn')`, `$('layersBtn')`: ids mantidos.

**Etapa 9, teclado.** `rowEl`: `li` com `tabindex="-1"` + roving (`role="listbox"`, setas, Enter/Espaço), foco visível por `outline` (não só `box-shadow`) e bloco `@media (forced-colors: active)`; `prefers-reduced-motion` no `dot-pulse`. Arquivos: `app.js` (`rowEl`, `refreshList`), `style.css`. Risco: `refreshList` recria as linhas a cada edição e perde o foco; guardar o handle focado e restaurar.

**Etapa 10, `menu.js`.** Extrair abrir/fechar/Esc/foco/`aria-expanded` e portar `#propMenu` e `#layersMenu` sem mudar o visual (`app.js:3079-3151`, 4698-4699). Risco: `showPropMenu(false)` e `showLayers(false)` são chamados de vários lugares; manter as funções como finas.

**Etapa 11, aba Stage.** Criar o par de abas em `#right` e **mover** (não copiar) `#texBtn`, `#lightBtn`, `#hud`, `#gridBtn`, `#sptBtn`, `#pupBtn`, `#aipBtn`, `#fopBtn`, `#picBtn`, `#editStage` (mantêm id); levar `#sumStage/#sumMod/#sumPath` para a aba. Commit em duas partes: (a) abas e resumo, (b) botões. Trilho passa a ter só `#modeObjects`/`#modePeds`. Selecionar um objeto troca para Selection. Arquivos: `index.html`, `style.css`, `app.js` (`showProps`, `showNoSel`, handlers de `#noSel`). Risco **alto**: nove botões mudam de lugar de uma vez; `style.css:228-238` (regras do `#rail`) e `#overlay`; `.tgl`/`:has(#snap:checked)`; scripts do dono que fazem `$('gridBtn').click()` continuam valendo se o botão estiver visível (aba Stage ativa) ou for acionado por JS (funciona com `hidden`). `sumCounts` fica (oculto ou na aba Stage) como sonda de carga.

**Etapa 12, menu Add.** Mover `#addProp` e o conteúdo de `#propMenu` para o cabeçalho de `#left`; "Browse models..." chama `openPalette()`; seção "New model from a file" com `#pmImport` e `#pmTerrain`; `#pmSky`/`#pmClouds` saem para o Light (ids dos selects iguais). Arquivos: `index.html`, `app.js` (`showPropMenu` popula os selects de céu, 3142-3149), `terrain.js`, `light.js`. Risco: `terrain.js` e o `pmImport` ligam por id; o Light precisa carregar a lista de modelos do mod (hoje vem de `showPropMenu`); `pedBar`/`propBar` trocam por modo em `applyPedsView`.

**Etapa 13, Display.** Fundir `#look`, `#grid` e `#layersMenu` num `menu.js`; adicionar camadas AI net, Start points e Power-ups (hoje sem toggle, só diálogo). Teclas L/C/P/N mantidas. Risco: `#pedsOn` fica desabilitado no modo Peds (`app.js:1689`); `#collInfo` está dentro do menu.

**Etapa 14, barra.** Botão dividido (Test/Reload), `File v`, Check para o menu e ligado ao rodapé, Stop com pergunta se há luta, texto do Test options sem o título duplo. Arquivos: `index.html`, `app.js` (2674-2686 `showGame`), `testopts.js`. Risco: `test_testopts.py` é de servidor; o smoke deve cobrir o ciclo Test -> running -> Reload -> Stop sem o jogo (mock: `setGameState`).

**Etapa 15, texto.** Remover `#count` e `#sumCounts` (vide aviso de 4.3), um só `requireMod()`, renomear "Pickups" na lista, trocar "handle/RANDOM/patch/arena/grid/coll_/hull_" por palavras do usuário nas dicas, grafia única (US). Arquivos: `index.html`, `app.js`, `tex.js`, `light.js`, `arrange.js`. Risco: scripts do dono leem `sumCounts`.

**Etapa 16, Light e Stage size acoplados.** Primeiro caso do painel de ferramenta (b) com `--right-w`; Light deixa de cobrir o painel e a barra; Stage size separa "Start from empty" (`gdDelAll`, "Take out scenery") com `ask`. Risco: `lightClose()` (1030) e `dlg.show()` em `light.js:210`; `style.css:781-782` (`#lightDlg position: fixed`).

**Etapa 17, pontos e redes.** Mesmo painel de (b) para `#sptDlg`/`#netDlg`: linhas em duas linhas, "Add at the view's aim" agora com câmera livre. Atenção: `nets[...]`, `updateNetView`, raycast desligado com o modal (`app.js:3465,3646`) precisam religar. Risco **alto**: `test_spt.py` não quebra (servidor) mas o smoke precisa cobrir Add/Aim/Save; `_spt.html` do dono usa `#sptBtn`/`#sptDlg`.

**Etapa 18, rascunho do patch e conflito.** `localStorage` (try/catch, como `store`) guarda `{stage, mod, patch, savedJson}` a cada edição com debounce; ao abrir a fase oferece "Restore unsaved changes?". `/api/stage` devolve `mtime` do arquivo do mod; `/api/save` recebe `since` e responde 409 se mudou ("The file changed on disk: Reload / Overwrite"). Arquivos: `app.js`, `editor.py`, `test_custom_stages.py`.

**Etapa 19, visual.** Tokens `--muted`/`--ok`/`--warn` no claro, `--line-strong` e trilho do `switch` a 3:1, alvos mínimos 24 px, `role=menu` só onde há setas, `aria-selected` nas abas do `#output`, `th`/`aria-label` nas tabelas, rótulo visível a partir de 1100 px. Só `style.css` e atributos. Risco baixo, mas visual: capturar antes e depois.

**Etapa 20, grade.** `main` com `--left-w`/`--right-w` e botões de colapsar; abaixo de 1100 px os painéis começam recolhidos. Risco: `@container` da vista (`style.css:447`) depende da largura; a regra `820px` continua valendo.

## 8. Perguntas para o dono

1. Concorda em tratar N-1 (servidor aceita POST de qualquer site) como a primeira correção, antes de qualquer UX?
2. Os scripts de teste do scratchpad (`cdp_*.mjs`, `web/_*.html`) podem ser promovidos a `tools/level_editor/test_ui.mjs` no repo? Eles dependem de `#sumCounts` e `?debug`.
3. Marcadores (largada, power-ups, redes) como objetos selecionáveis na vista valem uma rodada L, ou o painel acoplado é suficiente?
4. Textures e Fight HUD: o dono quer um só diálogo (Textures ganha "Apply to all N stages" e um filtro HUD) ou os dois ficam?
5. "Pickups" na lista e "Power-ups" no botão: qual é o termo do jogo para vida/stamina?
6. Rascunho automático do patch (etapa 18) em `localStorage` é aceitável? Ele grava objetos da fase no navegador.
7. Pode abrir mão do Chrome/Edge antigos (< 105) para usar `dialog:modal` no guarda de teclado?
