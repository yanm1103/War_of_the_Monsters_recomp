# Mod kit no navegador: plano

Pedido do dono (2026-11-22): a interface inteira do Mod kit numa janela de navegador. O primeiro passo é a página de ossos tocar uma animação nos dois modelos, para acertar os pés do Donkey Kong. Este arquivo é o fio entre as sessões. **No começo de cada sessão:** ler "Estado" e a próxima etapa. **No fim:** atualizar "Estado", marcar as etapas e anotar as decisões novas.

Branch: `ste-monstros`. Depois, se o dono preferir, uma branch própria (`kit-web`) saindo dela.

## Estado
- **Etapa atual:** 1 (animação na página de ossos), feita e testada por CDP; **esperando o teste do dono** (passo a passo na mensagem da sessão de 2026-11-22: Adjust bones no DK, barra de animação, pés no Walk). Depois do teste, etapa 2.
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
- **Fechar a aba encerra o kit.** A página manda um sinal de vida (a cada ~2 s) e o servidor sai quando ele para por ~10 s, ou pelo botão "Quit". Uma segunda cópia do kit, aberta com uma já rodando, só abre outra aba na mesma porta (a porta fica gravada na config).
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
- [ ] **`tools/modkit_web.py` (servidor):**
  - porta livre gravada na config;
  - sinal de vida e saída;
  - SSE do log;
  - janela de arquivo nativa;
  - fila de tarefas em processo separado, com o mesmo `--modkit-cli` do exe.
- [ ] **`tools/modkit_web/` (página):**
  - casca com a barra de cima (mod aberto, disco, pasta de trabalho);
  - log embaixo e área principal;
  - um botão de tarefa por vez, como no Tk.
- [ ] **Abrir pelo `modkit_gui.py`:** `--web` abre o novo, sem flag continua o Tk. Isso dura até a etapa 8.

### 3. Mods
- [ ] lista, novo mod, abrir a pasta de trabalho, pastas e disco (`pick_dir`, `pick_disc`);
- [ ] Build mod, Check, os avisos de nome inválido.

### 4. Arquivos do disco e do mod
- [ ] **Árvore:** a árvore do disco e da pasta de trabalho (`fill_disc`, `refresh_work`), com os grupos e as skins.
- [ ] **Ações nos arquivos:**
  - export e replace;
  - revert e remove;
  - abrir com o editor configurado, mostrar na pasta;
  - salvar numa pasta.
- [ ] **Prévia:** imagem com o tamanho original; som com forma de onda e tocar/parar (no navegador, por `<audio>`, em vez do player do Tk).

### 5. Skins
- [ ] nova, duplicar e apagar skin; virar skin (`turn_into_skin`); painel de propriedades; retratos;
- [ ] a prévia da skin no modelo 3D (nova: o Tk não tem). Ganho do navegador, vale conversar com o dono antes.

### 6. Monstros custom
- [ ] **Monstro:** novo, duplicar, apagar, virar custom; painel de propriedades e câmera; retrato.
- [ ] **Malha e rig:**
  - Import model, Import mesh, Import rig, Import rigged model e Adjust bones;
  - Check, Export mesh, Extract stats e Extract sounds.
- [ ] **Página de ossos:** vira uma tela do kit, no mesmo servidor, em vez de subir um servidor próprio (`orientpage.ask`). O `orientpage.py` continua para a linha de comando.

### 7. Prévia 3D do monstro custom (nova)
- [ ] o custom montado (malha e animações) numa tela do kit, com a lista de animações da etapa 1. Também conversar com o dono antes.

### 8. Empacotar e trocar
- [ ] **Exe:** o `WotM_ModKit.exe` (PyInstaller, `modkit_build/`) leva a página e o vendor e abre o kit web.
- [ ] **Teste:** Windows e Linux (AppImage, se o kit for junto).
- [ ] **Tk e docs:**
  - o Tk sai, ou fica atrás de `--tk` se o dono quiser;
  - `MODS.md` e `TOOLS.md` no presente;
  - o roteiro do vídeo da malha, se já estiver aprovado, passa a mostrar o kit novo.

## Decisões em aberto (perguntar ao dono quando chegar lá)
- Branch própria ou seguir na `ste-monstros` (até lá, `ste-monstros`).

Decididas pelo dono (goal de 2026-11-22): as prévias 3D novas entram (skin, custom e arquivo de modelo 3D selecionado mostram o modelo); o Tk sai de vez na etapa 8, sem `--tk`.

## Diário
- 2026-11-22: plano escrito. Goal do dono: prévias 3D entram quando um monstro ou um arquivo de modelo 3D é selecionado (etapas 5 e 7 decididas); o Tk sai de vez no fim. Etapa 1 feita (barra de animação na página de ossos), esperando o teste do dono.
