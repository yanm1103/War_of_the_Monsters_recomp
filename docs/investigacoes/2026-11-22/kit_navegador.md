# Mod kit no navegador: plano

Pedido do dono (2026-11-22): a interface inteira do Mod kit numa janela de navegador. O primeiro passo é a página de ossos tocar uma animação nos dois modelos, para acertar os pés do Donkey Kong. Este arquivo é o fio entre as sessões. **No começo de cada sessão:** ler "Estado" e a próxima etapa. **No fim:** atualizar "Estado", marcar as etapas e anotar as decisões novas.

Branch: `ste-monstros`. Depois, se o dono preferir, uma branch própria (`kit-web`) saindo dela.

## Estado
- **Ajustes pedidos no teste da etapa 8 (feitos, testados por CDP com `scratchpad/cdp_ui9.py`):** luz da prévia 3D igual à do museu (Lambert, normal sem o flip `faceDirection`, ambiente 0,7 + direcional 0,6 presa à câmera, cores sem sRGB) no `viewer.js` e no `orientpage.html`; divisórias arrastáveis (`split.js`: disco | trabalho, lista | prévia, altura da 3D; duplo clique volta ao padrão; tamanho no localStorage); painel do monstro em abas (Settings / 3D model em blocos com explicação / Stats & sounds; `panel.js`, `mesh.js`) e da skin (Settings / 3D model); barras de rolagem no tema; modelo nos eixos do jogo mostrado de frente (a prévia parada mostrava o DK de costas).
- **Cara do jogo (pedido do dono, 2026-11-22 noite, sem poder testar):** tema dos menus do jogo (painéis pretos com moldura clara, títulos laranja espaçados, seleção vermelha como a lista de saves, botão principal laranja como o READY), cabeçalho = horizonte do drive-in (céu e árvores do SHELL.TEX), logo do título, retratos dos trajes nas listas, logos dos monstros sobre a 3D, fotos das fases (`modkit_web_art.py`, `GET /api/art`; logos e fotos com as paletas do `wotm_savepics.hpp`). Para o usuário: Setup recolhido (abre sozinho se falta disco ou pasta), passos 1-2-3, boas-vindas sem mod ou com mod vazio (New mod / New skin / New monster / From the disc), atalhos na prévia vazia, botão ⋯ nas linhas, "Turn into..." sob a 3D do traje. Testado por CDP (`scratchpad/cdp_look.py`, 1600x950 e 1280x720; `cdp_ui9.py` de novo): sem erros de JS. **Falta o dono ver.** A página de ossos (`orientpage.html`) ainda tem o visual antigo.
- **Pés do DK (a decidir com o dono):** o jogo está fiel ao arquivo (a prévia do kit e o `FitDonkey00_Fix_rig.glb` mostram o mesmo). Pés do DK 23,7 x 24,6, do Congar 17,3 x 24,6 (37% mais largos); centros dos pés a 20,6 em repouso (Congar 22, com ~5 de folga; o DK encosta) e a 12 andando (Congar 18): cruzam. O map já vira os pés para fora (LFootJ turn [0,0,30], RFootJ [0,0,-30]). Saídas: diminuir os pés (escala por osso no map e na página de ossos, recurso novo) ou abrir a base (afastar os quadris). Medido com `scratchpad/feet_overlap.py`.
- **Etapa atual:** 8, **esperando o teste do dono**. O kit do navegador é o padrão (`modkit_gui.py` sem flag e o `WotM_ModKit.exe` abrem ele; o Tk ainda abre com `--tk`). O exe foi montado e testado por CDP (`scratchpad/cdp_exe8.py`: abre em ~2 s, uma cópia só, lista o disco, DK andando com os nomes das animações, Check pelo `--modkit-cli`, sai ~20 s depois de fechar a aba). Depois do teste do dono: apagar o Tk do `modkit_gui.py` (fica só o `main` com `--modkit-cli` e a abertura do kit), tirar o `--tk` do `MODS.md` e o Tk do `THIRD_PARTY.txt` se os diálogos de arquivo deixarem de usá-lo (não deixam: o `Dialogs` usa o `tkinter.filedialog`). Etapa 7 feita e testada por CDP (`scratchpad/cdp_web7.py`: o DONKEY KONG do mod donkeykong com 173 animações na lista, Walk carregado em 6 s e tocando, pausa no quadro 12, volta ao parado). Etapa 6 feita e testada por CDP (`scratchpad/cdp_web6.py`: New monster com câmera inválida recusada, painel com 3D, Save de regras, Extract stats, Turn into custom monster do traje extraído, Delete pelo menu, Import rigged model com a página de ossos dentro do kit e Close cancelando o import). Etapa 5 feita e testada por CDP (`scratchpad/cdp_web5.py`: 3D do traje extraído, New skin com retratos e id automático pelo nome, painel com 3D e Save, Duplicate e Delete pelo menu, Turn into skin com o aviso dos arquivos já empacotados). Etapa 4 feita e testada por CDP (`scratchpad/cdp_web4.py`: busca no disco, Extract de um traje e de um banco de som, grupos e miniaturas, prévia da textura e da forma de onda, Replace com o aviso de proporção, menu do botão direito, Revert pela tarefa, Remove com a confirmação da página). Etapa 3 feita e testada por CDP (`scratchpad/cdp_web3.py`: New mod com nome ruim e bom, botões do mod desligados sem mod, Build mod com log). Etapa 2 feita e testada por CDP (`scratchpad/cdp_web2.py`: estado, troca de mod, Check com log por SSE, segunda cópia abre aba na mesma porta, servidor sai ~20 s depois de fechar a aba). A etapa 1 foi conferida pelo dono na página (animações certas); os pés tortos no jogo eram o build antigo em `mods/` (Adjust bones regrava só a pasta de trabalho; falta o Build mod).
- **Como abrir o kit novo:** `python tools/modkit_gui.py --web` (ou `tools/modkit_web.py`; `--no-browser`, `--port N`, `--conf ARQ` para testes não mexerem na config do dono).
- **Feito antes deste plano:**
  - a página de ossos (`tools/orientpage.html` / `orientpage.py`), com ossos estilo Blender, slider e campo por eixo, giro líquido na frente;
  - o "Adjust bones..." (commit `6bd0fddc`);
  - o import rodando em processo separado (`run_proc` no `modkit_gui.py`).
- **Esperando o dono:**
  - testar a barra de animação e o Adjust bones nos pés do DK;
  - o roteiro do vídeo da malha (`roteiro_video_malha.md`).

## Decisões
- **O que se abre continua sendo o `WotM_ModKit.exe`** (ou `python tools/modkit_gui.py`). Ele sobe um servidor em `127.0.0.1`, numa porta livre, e abre a página no navegador padrão. Não há pywebview nem Electron: o kit tem que rodar igual no Windows e no Linux, sem dependência nova.
- **O trabalho fica no `tools/modkit.py`.** O servidor só chama as funções que o Tk já chama. Tarefas longas (build, import, export) rodam em processo separado, como o `run_proc` de hoje, com o log chegando à página por SSE (`text/event-stream`).
- **Fechar a aba encerra o kit.** O sinal de vida é o fluxo do log (SSE) aberto: o servidor sai 15 s depois do último fechar (ou pelo botão "Quit"). Timer na página não serve: o navegador desacelera os timers de aba em segundo plano para um por minuto. Uma segunda cópia do kit, aberta com uma já rodando, só abre outra aba na mesma porta (a porta fica gravada na config).
- **Escolher arquivo ou pasta:** o navegador não entrega o caminho, e o `.dae`/`.glb` precisa das texturas da mesma pasta. O botão pede ao servidor, que abre a janela nativa do sistema. Hoje isso é o `tkinter.filedialog`, num Tk escondido e numa thread própria; no Linux, `zenity` ou `kdialog` quando existirem. O caminho volta para a página.
- **A config é a mesma do Tk** (`load_conf`/`save_conf`): pastas, disco, mod aberto e editores externos. Dá para alternar entre os dois kits durante a migração.
- **O Tk fica até a última etapa.** Cada parte nova é conferida contra a mesma função no Tk. Quando tudo tiver passado, o `modkit_gui.py` vira só o lançador (ou sai) e o `MODS.md` muda.
- **3D e página:**
  - o three.js já está em `tools/vendor/` (importmap do `orientpage.html`);
  - sem build de JS (sem vite nem npm): módulos ES servidos direto, como no editor de fase (`web/`);
  - o visual segue o da página de ossos (escura, painéis à direita).
- **Prévia da animação do modelo = `retarget.build` de um clipe só** (~0,4 s por clipe no DK, 1,2 MB de .glb), não o `.NGP` do `ngprig`. Diferença conhecida: a regra das juntas presas e os blends do especial (camadas somadas) são do `ngprig`, então uma camada toca sozinha na página. O caminho fiel (ngprig + monglb) fica para a prévia do custom montado (etapa 7).
- **Nomes das animações:** `tools/anim_ids.json` é cópia do `src/anim-ids.json` do museu (gerado lá por `npm run anim-ids` a partir do `docs/ANIM_IDS.md` e das combos dos `.CHR`). Mudou o `ANIM_IDS.md`: rodar o gerador no museu e copiar de novo. (O plano dizia gerar em Python; copiar evita duplicar o gerador.)
- **Textos do jogador:** em inglês, curtos, sem frase de justificativa (`feedback-sem-texto-justificativa`).

## O que aproveitar do `wotm-model-museum` (`C:\Users\TwistZero\repos\wotm-model-museum`)
- **Nome e tipo de cada animação:** `src/anim-ids.json`, gerado por `scripts/anim-ids.mjs` a partir do `docs/ANIM_IDS.md` e das combos de cada `RST/<PREFIXO>.CHR` (`tools/chr_dump.py --csv`). A lista do museu agrupa por tipo (chips de filtro) e mostra id, nome curto e como toca (loop, hold, layer). O kit gera o mesmo JSON em Python a partir do `ANIM_IDS.md` e do `.CHR`, para não depender do repo do museu.
- **Animações em glTF:** o `tools/monglb.py ... --split --anim-dir X` grava a malha com o idle e cada animação num `aNNN.glb`, carregado quando é escolhido. Animações são identificadas por `k`, o índice na tabela do jogo, não pelo id, porque ids se repetem.
- **Interface do museu:** `src/main.js` traz o enquadramento (vista 3/4, caixa fixa sobre o idle), play/pausa, velocidade e o liga/desliga da interpolação entre quadros. Copiar o que servir, sem trazer o resto do site.

## Etapas
Cada etapa termina com: teste pela página num Edge sem janela via CDP (receita do `scratchpad/cdp_sliders.py` e do editor de fase), commit e push, e esta seção atualizada. Etapa que muda o que o dono usa termina com o passo a passo para ele testar, sem commit de "pronto" antes do teste dele.

### 1. Animação na página de ossos
- [x] **Base:** o `base_glb` (export `--edit` do traje base, `<PREFIXO><traje>.glb` na pasta de trabalho) já traz todas as animações (`aNNN`); a página toca com o `AnimationMixer`. Não precisou de `--split`.
- [x] **Modelo:** a mesma animação no modelo importado, com os giros e o mapa atuais.
  - Primeiro medir o caminho fiel: montar o `.NGP` com `ngprig` numa pasta temporária, só com a animação escolhida, e exportá-lo com `monglb.py`. Assim o que se vê é o que vai para o jogo, incluindo a regra das juntas presas e os blends do especial.
  - Se for lento demais por clique (alvo: ~1 s), calcular as poses do `retarget.py` quadro a quadro direto em Python, para a prévia, e deixar o caminho fiel para o "OK".
  - Feito: `retarget.build(..., clips=["a001"])` (nome exato do clipe), endpoint `/anim` + `/anim.glb` no `orientpage.py` (`Bones.anim`). Ver "Decisões".
- [x] **Na página:**
  - lista das animações com nome, como no museu;
  - play/pausa, barra de quadro e velocidade;
  - o base e o modelo lado a lado, tocando juntos e no mesmo quadro;
  - mexer num osso recalcula a animação do modelo e mantém o quadro.
  - Feito: barra embaixo (`#playbar`), lista agrupada por tipo, ossos seguindo a animação (juntas fora do esqueleto mantido vão rígidas na mantida acima), ganchos de teste `wotmPage.anim/frame/bonePos/basePos`. Teste: `scratchpad/cdp_anim.py` (63 juntas, 168 clipes, Walk em 0,8 s, giro no meio da animação recarrega no mesmo quadro, volta à pose parada, sem erro de console).
- [x] **Doc:** `MODS.md` ("Match the bones").
- [ ] **Teste do dono** com os pés do DK (escolher andar e guarda, ajustar `LFootJ`).

### 2. Esqueleto do kit web
- [x] **`tools/modkit_web.py` (servidor):**
  - porta livre gravada na config;
  - sinal de vida e saída;
  - SSE do log;
  - janela de arquivo nativa;
  - fila de tarefas em processo separado, com o mesmo `--modkit-cli` do exe.
- [x] **`tools/modkit_web/` (página):**
  - casca com a barra de cima (mod aberto, disco, pasta de trabalho);
  - log embaixo e área principal;
  - um botão de tarefa por vez, como no Tk.
- [x] **Abrir pelo `modkit_gui.py`:** `--web` abre o novo, sem flag continua o Tk. Isso dura até a etapa 8.
- Feito: `window.kit` (api, state, onState, pick, run, say) é a base das próximas telas; ações do servidor = métodos `act_<nome>` do `Kit` (POST `/api/do/<nome>`); tarefas = `Kit.run(título, args do modkit.py)`. A janela de arquivo nativa (`Dialogs`) não foi exercitada por CDP (abriria janela na tela do dono); é a mesma chamada do Tk.

### 3. Mods
- [x] lista, novo mod, abrir a pasta de trabalho, pastas e disco (`pick_dir`, `pick_disc`);
- [x] Build mod, Check, os avisos de nome inválido.
- Feito: `kit.dialog({title, text, fields, ok})` é o diálogo da página (o `ok` devolve o erro para mostrar ou nada para fechar); trocar de mod copia para a pasta de trabalho o que o mod tem e ela não (`adopt_mod`, como no Tk). "Open mods/work folder" não foi clicado no teste (abriria o Explorer na tela do dono).

### 4. Arquivos do disco e do mod
- [x] **Árvore:** a árvore do disco e da pasta de trabalho (`fill_disc`, `refresh_work`), com os grupos e as skins.
- [x] **Ações nos arquivos:**
  - export e replace;
  - revert e remove;
  - abrir com o editor configurado, mostrar na pasta;
  - salvar numa pasta.
- [x] **Prévia:** imagem com o tamanho original; som com forma de onda e tocar/parar (no navegador, por `<audio>`, em vez do player do Tk).
- Feito: `tools/modkit_web_files.py` (`FilesMixin` do `Kit`: `act_disc`, `act_work`, `act_extract`, `act_replace`, `act_save_to`, `act_revert`, `act_remove`, `act_open_files`, `act_editors`, `act_reveal`; GET `/api/file?rel=` serve o arquivo da pasta de trabalho) e na página `disc.js`, `work.js`, `select.js` (seleção como no Explorer). As tabelas de nomes (`MONSTERS`, `LEVELS`...) estão copiadas do `modkit_gui.py` até o Tk sair. Para as próximas etapas: `kit.work.panels[kind] = (grupo, caixa, legenda) => ...` põe um painel no lugar da prévia quando um grupo de skin (`skin`) ou de custom (`monster`) é escolhido; `kit.work.menus.push((ctx, add, sep) => ...)` acrescenta itens ao menu do botão direito.

### 5. Skins
- [x] nova, duplicar e apagar skin; virar skin (`turn_into_skin`); painel de propriedades; retratos;
- [x] a prévia da skin no modelo 3D (nova: o Tk não tem; o dono decidiu que entra).
- Feito: `tools/modkit_web_skins.py` (`SkinsMixin`: formulário validado no servidor, `skin_new`/`skin_adopt`, `packed`, props, duplicar, apagar; a linha de malha `mesh_*` que o custom também usa), `tools/modkit_web_model.py` (`ModelMixin`: GET `/api/model.glb?group=` = o traje, a skin ou o custom parado no idle pelo `pose.py`, com os PNGs da pasta de trabalho por cima das texturas do disco; `?rel=` = um arquivo 3D da pasta, .glb direto, .dae/.gltf pelo leitor do retarget, .NGP pelo `pose.py`; cache por data dos arquivos) e na página `skins.js`, `mesh.js`, `viewer.js` (um renderer só, levado para a caixa que mostra modelo). Tarefas rápidas rodam no processo do servidor (`Kit.run_fn`, como o `run` do Tk); as pesadas no `modkit.py` à parte (`Kit.run`).
- Cuidado ao editar com script: heredoc do shell come a barra de `\n` (vira quebra de linha real dentro de string); gravar o script com Write.

### 6. Monstros custom
- [x] **Monstro:** novo, duplicar, apagar, virar custom; painel de propriedades e câmera; retrato.
- [x] **Malha e rig:**
  - Import model, Import mesh, Import rig, Import rigged model e Adjust bones;
  - Check, Export mesh, Extract stats e Extract sounds.
- [x] **Página de ossos:** vira uma tela do kit, no mesmo servidor, em vez de subir um servidor próprio (`orientpage.ask`). O `orientpage.py` continua para a linha de comando.
- Feito de outro jeito (mais barato e igual para o usuário): o import roda no `modkit.py` à parte com `--embed`; o `orientpage.ask` continua com o servidor dele e, em vez de abrir aba, imprime `[page] <url>`; o `Kit.run` transforma essa linha num evento e a página do kit mostra a de ossos num iframe por cima (`#taskPage`), até a tarefa acabar; fechar o iframe dispara o `pagehide` dela e cancela o import. `tools/modkit_web_monsters.py` (`MonstersMixin`) e `monsters.js` têm o resto (diálogo, painel com 3D, retrato do HUD, stats, sons, modelo, rig).

### 7. Prévia 3D do monstro custom (nova)
- [x] o custom montado (malha e animações) numa tela do kit, com a lista de animações da etapa 1 (o dono decidiu que entra).
- Feito: o painel do custom, o da skin e a prévia de um traje extraído têm a barra de animações (`animBar` do `viewer.js`): a lista vem do `.NGP` do modelo (`pose.Model.anims`, GET via `act_anims`), os nomes do `anim_ids.json` (servido em `/anim_ids.json`), e cada animação é um `.glb` do `monglb.Exporter` com um clipe só (`/api/anim.glb?group=&k=`, ~1,5 a 6 s, em cache), com os PNGs da pasta de trabalho por cima. É o caminho fiel: o `.NGP` montado, como o `pose.py` o anima (sem as molas do pelo e as camadas, como o museu).

### 8. Empacotar e trocar
- [x] **Exe:** o `WotM_ModKit.exe` (PyInstaller, `modkit_build/`) leva a página e o vendor e abre o kit web.
- [ ] **Teste:** Windows e Linux (AppImage, se o kit for junto).
- [ ] **Tk e docs:** (docs feitas: `MODS.md`, seção da interface; falta o Tk sair depois do teste do dono)
  - o Tk sai, ou fica atrás de `--tk` se o dono quiser;
  - `MODS.md` e `TOOLS.md` no presente;
  - o roteiro do vídeo da malha, se já estiver aprovado, passa a mostrar o kit novo.

## Decisões em aberto (perguntar ao dono quando chegar lá)
- Branch própria ou seguir na `ste-monstros` (até lá, `ste-monstros`).

Decididas pelo dono (goal de 2026-11-22): as prévias 3D novas entram (skin, custom e arquivo de modelo 3D selecionado mostram o modelo); o Tk sai de vez na etapa 8, sem `--tk`.

## Diário
- 2026-11-22: plano escrito. Goal do dono: prévias 3D entram quando um monstro ou um arquivo de modelo 3D é selecionado (etapas 5 e 7 decididas); o Tk sai de vez no fim. Etapa 1 feita (barra de animação na página de ossos); o dono viu as animações certas na página e os pés tortos no jogo vinham do build antigo em `mods/` (o `.NGP` da pasta de trabalho, com o Around de 30°, era mais novo). Etapa 2 feita (servidor e casca). Etapa 3 feita (mods). Etapa 4 feita (listas do disco e da pasta de trabalho). Etapa 5 feita (skins, com 3D). Etapa 6 feita (monstros custom; a página de ossos aparece dentro do kit). Etapa 7 feita (animações na prévia 3D). Etapa 8: o kit do navegador virou o padrão (exe montado e testado); o Tk sai depois do teste do dono.
