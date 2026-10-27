# Mods: como fazer

Um mod é uma pasta dentro de `mods/` (ao lado do ELF, na raiz do disco extraído que o jogo usa: `<runtime>\discroot\mods\`). Ela
repete a árvore do disco e o que estiver lá vale no lugar do arquivo do disco. Nada do jogo é alterado nem vai para o repo.

```
discroot/mods/
  my_mod/
    MON/lizard0_t0004.png      textura do Togera em PNG (qualquer resolução)
    MON/lizard0_rtx_t0003.png  retrato do Togera no HUD
    VAG/CITYAMB.VPK            música trocada
    SND/LIZARD_S.BNK           banco de sons trocado
  _work/                       pastas de trabalho do kit, uma por mod (começa com '_': o jogo não lê)
    my_mod/                    PNGs e WAVs em edição do my_mod, com o modkit.json dela
```

Regras do carregador (`native/wotm_mods.hpp`): pasta que começa com `_` ou `.` fica desligada; dois mods com o mesmo arquivo, vence o
último em ordem alfabética (dá para usar `10_base`, `20_extra`); `WOTM_MODS=<pasta>` troca a pasta e `WOTM_MODS=0` desliga tudo. O
log (`logs\live.err.log`) diz cada arquivo e cada textura que veio de um mod (`[mods] ...`).

**Mods do projeto** ficam em `mods/<mod>/` na raiz do repo e são só arte e configuração próprias (JSON, PNG desenhado): o Goliath
Prime (`mods/Goliath Prime/`), o Cerebulon (`mods/Cerebulon/`) e o logo "Recompiled" da abertura (`mods/recompiled_logo/`). O
`tools/sync_mods.ps1` espelha cada pasta de `mods/` para `<runtime>\discroot\mods\` (o `run_live.ps1` o chama a cada boot e o
`setup_runtime.ps1` na instalação; `WOTM_RECOMP_DIR` ou `.wotm_recomp_dir` dizem o runtime). Só as pastas que existem no repo são
espelhadas; as que só existem no `discroot\mods\` (as suas, `_work`) ficam como estão. Sem uma pasta `mods` (nesse nome exato: no
Windows o caminho `mods` cai no `MOD\` do disco, a dos IRX) ao lado do ELF, o jogo e o launcher leem a `mods/` do repo
(`WOTM_REPO`, que o `run_live.bat` e o launcher passam). Textura do disco editada (PNG saído do
`modkit.py`) é arte do jogo e não entra no repo, a não ser como arte nova por cima (caso do logo).

## Interface (`tools/modkit_gui.py`, botão "Mod kit" do launcher)

`pythonw tools/modkit_gui.py`, ou o botão **Mod kit** do `WotM_Launcher.exe`. Precisa do Python 3 com Pillow (`pip install pillow`). A janela é em inglês.

Um mod é um projeto: no topo, **Mod: [lista] [New mod...]** escolhe o mod (New mod cria a pasta do mod e a de trabalho dele,
`<mods>/_work/<mod>/`; nome só com letras, dígitos, `_` e `-`, começando por letra ou dígito). Ao escolher o mod, os arquivos dele
que a pasta de trabalho não tem (mod feito antes ou à mão) são copiados para ela e aparecem como editados; o Build os leva de volta.
Arquivo removido da pasta de trabalho de propósito não volta. O mod escolhido governa o Extract, a lista da direita e o Build; trocar o mod troca a área de trabalho
inteira. Sem mod, a lista da direita fica vazia e os botões ligados a ele ficam desligados, com a dica "Pick or create a mod first".

1. **Pick from the disc** (esquerda): monstros por nome e traje (Togera › Costume 1, e os bancos de som dele), chefes, fases pelo nome do
   jogo, menu e telas, bancos de som, músicas e falas. Campo de busca; Ctrl/Shift escolhem vários; duplo clique ou "Extract selected".
2. **Edit <mod>'s work folder** (direita): um grupo por arquivo do disco ("MON/MONKEY0 (Congar, costume 1)"); depois de extrair, o
   grupo novo abre e os outros fecham. Miniaturas, tamanho e estado ("edited" = mudou desde a extração). Textura que o export não
   conseguiu colorir sai em cinza com `_sempaleta` no nome; o `pack` grava sem o sufixo (o jogo aceita os dois). Prévia
   da imagem (com xadrez atrás do alfa) ou a forma de onda do som. Som toca na própria janela ao clicar ("play sounds on click"
   desliga) ou no duplo clique; "Stop" para. Duplo clique numa textura abre no editor escolhido em **Editors...** (um programa por
   tipo, `.png` e `.wav`; vazio = programa padrão do Windows; com editor de `.wav` definido, o duplo clique no som abre nele).
   Botão direito na lista: Play, Open in ..., **Open with...** (escolhe o programa e pergunta se vira o padrão), **Replace with...** (só com um arquivo
   escolhido: troca pelo arquivo que você escolher e grava com o nome certo; JPG, BMP, TGA etc. viram PNG; avisa se a proporção
   muda; som só aceita WAV), Show in folder, **Revert to original** (só aparece se há editados na seleção; num grupo, volta os
   editados dele; extrai de novo do disco e confirma antes),
   **Save to folder...** (copia os selecionados, ou o grupo inteiro, para uma pasta) e Remove (tecla Del também). Clicar numa
   seção só a seleciona: abrir e tocar valem só para arquivos escolhidos um a um; na seção, o botão direito salva ou remove tudo.
3. **Build** (embaixo): **Build mod** (o `pack` do mod escolhido), **Check mods**, **Open mods folder** e **Open work folder**. O log
   fica embaixo, com divisória arrastável para cima e para baixo.

As pastas (disco e mods), o último mod, os editores, a altura do log e a última pasta do "Save to folder" ficam em
`~/.wotm_modkit.json`. Por baixo são as mesmas funções do `modkit.py` abaixo.

**Skins pela janela.** **New skin...** (no cabeçalho da lista, ao lado de Refresh) abre um diálogo: *Monster* (lista com os dez
jogáveis e os monstros custom conhecidos; aceita também um id digitado, que vale como id de monstro custom: sem texturas extraídas
e `skin.json` sem `base`), *Base costume* (0 a 3, com o retrato de cada traje; traje que não está no disco fica desligado; para um
custom o padrão é o `costume` do `monster.json`), *Name*, *Id* (sai do nome, editável) e "CPUs may pick this skin". Cria o
`skin.json` em `mods/<mod>/skins/<id>/` e extrai as texturas do traje base para a pasta de trabalho. Na lista, a skin é um grupo
próprio no topo ("Skin: NAME (monster/id, base N)"); selecionado, a prévia vira o painel de propriedades (Name, Base, CPUs may pick,
**Save** regrava o `skin.json`). Botão direito no grupo: **Duplicate skin...** (copia a skin no mod e na pasta de trabalho com o id
novo) e **Delete skin**. Com um grupo de traje de monstro jogável selecionado (ex. "MON/SHOGUN3"), o botão **Turn into skin...** do cabeçalho (também
no botão direito) cria a skin com esse monstro e traje e move os PNGs do grupo para a pasta dela, de modo que o traje original fica intocado no Build; se o mod já tem
esses PNGs empacotados em `MON/`, a janela avisa e oferece apagá-los.

**Monstros custom pela janela.** **New monster...** (ao lado de New skin) abre um diálogo: *Base type* (os dez jogáveis e os três
chefes aceitos, com uma nota do que o chefe não tem), *Costume* (com os retratos; para o Goliath o padrão é o 1), *Name*, *Id* (sai do
nome; não pode ser o nome de um jogável), *Camera* (a do próprio tipo ou a de outro monstro, com a distância em %; para o Goliath e o
Jelly já vem a do Togera 200% e 300%), *Narrator* (os `narrator/*.mp3` que o port acha), "Own look" e "CPUs may pick this monster".
Cria o `monster.json` em `mods/<mod>/monsters/<id>/` e, com "Own look", grava `"files": true` e extrai as texturas do traje para
`monsters/<id>/files/` na pasta de trabalho: os PNGs editados vão no Build para a pasta `files/` do monstro e valem só para ele (o
traje do disco fica intocado). Na lista, o monstro é um grupo no topo ("Monster: NAME (custom id, base X costume N)"); selecionado, a
prévia vira o painel de propriedades: Name, Camera, Narrator, *Rules* (o `kind`), CPUs, *HUD portrait* (**Set...** copia a imagem para
`portrait.png` e grava o `portraitTex` certo do `.RTX` do traje; **Remove** volta ao do traje) e *Look* (quantas texturas e o modelo
próprio; **Import model files...** copia `.NGP` + `.PTR`, e `.TEX`/`.RTX` opcionais, para `files/`). **Save** regrava o `monster.json`
mantendo as outras chaves. Botão direito no grupo: **New skin for this monster...**, **Duplicate monster...** e **Delete monster** (as
skins dele ficam e o log avisa). Num grupo de traje extraído (jogável ou chefe, ex. "MON/ASSBOSS1"), o botão direito tem **Turn into
custom monster...**, que move os PNGs do grupo para o monstro novo.

## O kit na linha de comando (`tools/modkit.py`)

```
python tools/modkit.py export my_mod MON/LIZARD0          # .TEX e .RTX do Togera em PNG, na pasta de trabalho do my_mod
python tools/modkit.py export my_mod SND/LIZARD_S.BNK VAG/CITYAMB.VPK
  (edite os arquivos em mods/_work/my_mod/)
python tools/modkit.py pack my_mod [--inject]             # leva o que mudou para mods/my_mod/
python tools/modkit.py check [my_mod]                     # confere nomes, tamanhos e texIds dos mods e os conflitos entre eles
python tools/modkit.py revert my_mod MON/lizard0_t0004.png   # volta um arquivo da pasta de trabalho ao original do disco
python tools/modkit.py monster new my_mod fire_togera --base togera --costume 2 --name "FIRE TOGERA" [--ai]
    [--camera togera:200 | --camera none] [--narrator togera] [--no-textures]   # monstro custom (seção "Monstros custom")
python tools/modkit.py monster portrait my_mod fire_togera retrato.png   # retrato do HUD (2:1) e o portraitTex
python tools/modkit.py monster model my_mod fire_togera X.NGP X.PTR [X.TEX] [X.RTX]   # modelo próprio para files/
```

- Um mod é um projeto: a pasta de trabalho é `<mods>/_work/<mod>/`, com o `modkit.json` dela, e o `pack` de um mod nunca leva o que
  foi editado para outro. `--work <pasta>` troca a pasta de trabalho.
- Lê o disco extraído do repo (`disc/`, ou `--disc`/`WOTM_DISC`) e grava nos mods do runtime (`.wotm_recomp_dir` + `discroot\mods`,
  ou `--mods`). Curingas valem (`MON/LIZARD*`); sem extensão pega `.TEX` e `.RTX`.
- O `pack` só leva o que mudou desde o export (sha1 em `_work/<mod>/modkit.json`). Exportar de novo não sobrescreve o que já foi editado.
- O `check` ignora `_work`. Cada linha de arquivo, PNG, skin ou monstro marca "(overrides X)" ou "(overridden by Y)" quando outro
  mod ligado tem a mesma chave, e a saída termina com a seção "conflicts between enabled mods": uma linha por chave repetida (PNG
  por stem/rtx/texId, arquivo por caminho, skin por `monster/id`, monstro custom por id) com os mods e quem vence (o último em
  ordem alfabética). `check <mod>` mostra só os conflitos que envolvem esse mod. Também avisa a skin cujo `monster` não é jogável
  nem custom conhecido.

| No disco | Na pasta de trabalho | O `pack` grava |
|---|---|---|
| `.TEX` | `<arquivo>_tNNNN.png` (um por textura) | o PNG como está |
| `.RTX` | `<arquivo>_rtx_tNNNN.png` (retratos, telas, fontes) | o PNG como está |
| `.VPK` / `.VAG` | `<arquivo>.wav` | `.VPK`/`.VAG` (mesma taxa e volumes do original) |
| `.BNK` | `<banco>_sNNN.wav` (um por som com amostra) | o banco com todos os sons editados trocados (amostra que outro som também usa: o editado ganha uma cópia própria e o outro fica com a original) |
| outros | cópia | cópia |

## Texturas em PNG

- O jogo continua carregando o `.TEX`/`.RTX` do disco; o desenho nativo troca a imagem na hora de desenhar
  (`native/wotm_texmods.hpp`). Por isso o PNG pode ter **qualquer resolução** (texturas HD): os UVs dos objetos são normalizados, e os
  quadrados de tela (retratos das barras, fonte) escalam o trecho pela razão entre o PNG e a textura original. Mantenha a
  proporção da original (o `check` avisa).
- Vale em qualquer pasta do mod (o nome do arquivo basta) e para todas as paletas que usam a textura.
- Alfa: o PNG usa a mesma escala da textura original, como o export a grava. A maioria das texturas do jogo vai de 0 a 0xFF (o dobro
  do opaco 0x80 do GS); nelas o alfa do PNG vale cru, 255 = 0xFF, e o log diz "alfa cru, 255 = 0xFF". Nas que param em 0x80,
  255 = 0x80 (o resto / 2). Na prática: 255 é o opaco que o jogo usa, e o export e o `pack --inject` vão e voltam sem perder nada.
  Isso importa em texturas como o logo do título (SHELL t140/t141), que o jogo desenha com o vértice em meia opacidade e contando
  com o 0xFF. PNG semitransparente de uma versão antiga do kit pode sair mais opaco: exporte de novo. Texturas sem alfa no jogo
  (TEX0.TCC = 0) ignoram o alfa do PNG.
- Imagem dividida em várias texturas (o logo do título são duas de 256 × 256 lado a lado, de cabeça para baixo): escale a imagem
  inteira uma vez e corte no meio exato, sem sobreposição; cortes feitos à mão com sobra repetem um pedaço na emenda.
- Partículas (fogo, faíscas, explosões, destroços, pedestres, lens flare): as texturas delas moram no `.TEX` da fase (no Midtown
  Park, `central_t0004` a `t0059` e `t0289`), então o PNG vai com o nome da fase e vale só nela; para trocar em todas, um PNG por
  fase. Os UVs das partículas também são normalizados: qualquer resolução serve. Foto do fogo em verde no dobro da resolução:
  `docs/screenshots/2026-10-25/2026-10-25_mods_png_particulas.png`.
  - No Midtown Park: explosão = `t0008` a `t0039` (32 quadros de 64 × 64); arco elétrico (raio do Kineticlops, calota do Robo-47)
    = `t0057` (128 × 64); pedestres = `t0289` (folha de 128 × 128, bonecos de ~8 × 16 de cabeça para baixo).
  - O tamanho na tela vem do efeito do jogo, não do PNG: um PNG maior só tem mais pixels no mesmo quadrado. Upscale (Real-ESRGAN
    e parecidos) só aparece quando a partícula fica na tela maior que a textura original. No raio e na explosão vistos na luta
    não se nota (`docs/screenshots/2026-10-25/2026-10-25_mods_png_raio_sem_e_com_upscale.png`); nos pedestres de perto se nota,
    mas os modelos inventam forma onde a original tem poucos pixels
    (`docs/screenshots/2026-10-25/2026-10-25_mods_png_pedestres_upscale.png`,
    `docs/screenshots/2026-10-25/2026-10-25_pedestres_folha_original_e_animesharp.png`). Para partículas menores ou mais nítidas,
    redesenhe: o desenho menor no centro, com mais borda transparente, aparece menor.
- Não vale: na poeira/fumaça composta (o jogo a monta na VRAM a cada quadro) e na imagem do GS emulado
  do `ps2EntryRunner` (a referência da checagem VU1; a janela dele usa o PNG). Para essa, `pack --inject` também regrava o `.TEX`/`.RTX` do mod quando o PNG tem o
  tamanho original (as cores vão para a paleta atual).
- Como o nativo acha o arquivo: o pacote de upload de `texInfo[texId]` está dentro do `.TEX` (ou `.RTX`) carregado em
  `fileStatus.texAddr[i]` (`resAddr[i]`); o deslocamento dentro dele dá o texId do disco, e o nome é `fileStatus.names[i]`. As
  residentes (T8H/T4HL/T4HH) vêm pelo DBP, como no `decodeResident`.

## Skins (trajes a mais, sem substituir)

Uma skin é uma pasta em `mods/<mod>/skins/<pasta>/` com um `skin.json`:

```json
{ "monster": "ultra-v", "id": "roxo", "name": "PURPLE ULTRA V", "base": 3, "ai": true }
```

- `monster`: nome do monstro jogável (`congar`, `robo-47`, `togera`, `ultra-v`, `preytor`, `kineticlops`, `magmo`, `raptros`,
  `agamo`, `zorgulon`), o prefixo do arquivo (`SHOGUN`) ou o `id` de um monstro custom (`moddedtogera`; ver "Monstros custom"). A
  skin de um custom só aparece na lista dele, nunca na coluna do monstro base nem no sorteio das IAs; sem `base`, vale o `costume`
  do `monster.json`.
- `id`: letras minúsculas, dígitos e `_` (sem ele, o nome da pasta). O save guarda `"ultra-v/roxo"`.
- `name`: o que a seleção mostra. `base`: traje do disco (0 a 3) que dá o que a pasta não trouxer, e os sons do traje.
  `ai`: se as IAs sorteadas podem usar (padrão não).
- Skin de mod é sempre aberta: não passa pela tela de destrave.
- O que vai na pasta, do mais simples ao mais completo:
  - **só PNGs**: modelo e texturas do traje base; os PNGs trocam as texturas. Nome como o export do traje base
    (`shogun3_t0010.png`, `shogun3_rtx_t0011.png`) ou só `t0010.png`/`rtx_t0011.png`. Valem **só para a skin**: o traje 3 de verdade
    continua com a textura dele, mesmo na mesma luta.
  - **`.TEX` e/ou `.RTX`** (qualquer nome, pela extensão): texturas próprias, com os mesmos texIds do base (o `modkit pack --inject`
    grava assim).
  - **`.NGP` + `.PTR`** (sempre os dois): modelo próprio, com o mesmo esqueleto e animações do traje 0.
- O jogo vê cada skin como mais um traje do monstro (`MON/SHOGUN4.*`, `SHOGUN5.*`...; números dados a cada boot, na ordem
  mod/id). Mesmo monstro e id em dois mods: vence o último em ordem alfabética. O log diz o que entrou:
  `[mods] skin ultra-v/roxo ("PURPLE ULTRA V", meu_mod) = traje 4 (SHOGUN4); base 3, 2 png`.
- No menu (seleção nativa das telas 3, 6, 7 (minigames de 2), 18 e 19 e de 3 e 4 jogadores): cima/baixo passa pelos trajes do disco e depois pelas skins do
  monstro, sempre abertas, e mais de um jogador pode escolher a mesma. Pelo `WOTM_MATCH` também (`WOTM_MATCH=4:roxo,...`) e pelo
  launcher (a caixa de skin de cada monstro lista as skins de mod dele). O modelo 3D da seleção e da lista de saves mostra os PNGs da skin.
- Na luta a skin usa os sons do traje `base` (o Preytor base 1 e o Agamo base 3 têm sons próprios). Log:
  `[mods] skin ultra-v/roxo (traje 4) na luta com os sons do traje 3`.
- **Save:** a aventura guarda a skin pelo nome (`"costume": "ultra-v/roxo"`; os trajes do disco seguem como 0 a 3). Sem o mod, o slot
  carrega com o traje 0 (log `[wotm] save: traje ... não achado (mod ausente?); usando traje 0`, e "skin missing" na lista de saves);
  gravar a mesma aventura mantém o nome, e a skin volta quando o mod voltar.
- **IAs sorteadas** (resistência): com `"ai": true`, a skin entra no sorteio do traje da IA daquele monstro, junto com o traje que o
  jogo escolheria (log `[mods] IA N sorteada com a skin ...`). As IAs da aventura ficam com os trajes do jogo.
- **Kit:** `python tools/modkit.py skin new <mod> <monstro> <id> [--base N] [--name "GREEN TOGERA"] [--ai]` cria o `skin.json` e exporta as
  texturas do traje base para `mods/_work/<mod>/skins/<id>/`; edite os PNGs e rode `modkit.py pack <mod>`: só os editados vão para a
  pasta da skin. `<monstro>` também pode ser o id de um monstro custom: se o kit acha o `monster.json` dele (em `mods/` do repo ou na
  pasta de mods), exporta as texturas do tipo base no `costume` do custom (ou no `--base`), e o `skin.json` sai sem `base` quando o
  `--base` não é dado. Id desconhecido cria só o `skin.json`, com aviso.
  Na janela, o mesmo com **New skin...**, o painel de propriedades da skin e **Turn into skin...** (seção "Interface").
- Ainda não: o elimination. Nele o jogo carrega o traje 0 de todos os monstros uma vez só e os
  jogadores trocam de monstro entre as rodadas sem recarregar: escolher traje ali pede mudar a carga, não só a tela.

Exemplo (`WOTM_MATCH=4:roxo,4:3,1`, IA): a skin com o PNG recolorido sai verde e o traje 3 ao lado continua roxo;
skin só com `skin.json` (`togera/demonio`, base 3) carrega o modelo do traje 3. Foto:
`docs/screenshots/2026-10-23/2026-10-23_skin_mod_png_so_na_skin.png`.

## Monstros custom (um monstro a mais na seleção)

Monstros custom são experimentais: mods do dono para o próprio jogo, sem garantia de que tudo funcione (o Goliath não tem
animações de escalar nem IA fora da fase dele). Um monstro custom é uma pasta em `mods/<mod>/monsters/<pasta>/` com um `monster.json`. Ele usa o código
de um tipo do jogo (`base`) e o modelo e os arquivos desse tipo, ou arquivos próprios (`files`, abaixo; o Mecha). Um tipo livre do
jogo (outro prefixo em `MON/`) ainda não existe. O primeiro é o Goliath Prime (`mods/Goliath Prime/monsters/goliath/monster.json`, no repo):

```json
{ "id": "goliath", "name": "GOLIATH PRIME", "base": "assboss", "costume": 1,
  "camera": { "from": "togera", "scale": 200 }, "narrator": "goliathprime",
  "portrait": "goliathprime.png" }
```

- `id`: letras minúsculas, dígitos e `_` (sem ele, o nome da pasta). É o que o `WOTM_MATCH` aceita (`WOTM_MATCH=goliath,3,1`).
- `name`: o que a seleção e o HUD mostram. O HUD tem 11 letras: vale a última palavra que couber, em minúsculas ("goliath").
- `base`: o tipo do jogo, pelo nome ou prefixo: os dez jogáveis (`togera`, `LIZARD`...) ou um chefe que é `Monster` (`kCustomBases`,
  `native/wotm_mods.hpp`): `assboss` (13, o Goliath), `jelly` (14, a segunda fase do Cerebulon) e `final` (15, o Cerebulon, a última
  fase). Um custom de base jogável roda o código e a IA desse monstro; só o Goliath foi testado (para trocar só a aparência, use
  uma skin).
- `costume`: o traje do disco (`ASSBOSS1`; o `ASSBOSS0` é um protótipo com metade das animações e o retrato pequeno; `JELLY0` e
  `FINAL0` só têm o traje 0).
- `camera`: a câmera de outro monstro (`from`, pelo nome do jogo) com as distâncias escaladas (`scale` em %, 10 a 1000; o JSON do
  port só lê inteiros). O `ASSBOSS.CHR` nunca foi usado pelo jogo (na aventura a câmera é a da fase) e deixa a câmera em cima do
  monstro, e a do `JELLY.CHR` fica entre as pernas do Jelly; a do Togera 2x (Goliath) e 3x (Jelly) fica como a de um monstro
  comum. A do `FINAL.CHR` já serve. O port lê a câmera do `.CHR` do `from` (de `mods/` ou do disco) e a põe no monstro a cada troca
  de POV (log `[custom] jelly: camera do RST/LIZARD.CHR, distancias x3.00 (POV 0: ...)`).
- `narrator`: `narrator/<nome>.mp3` dito na confirmação (vazio = o do tipo).
- `files`: arquivos próprios do modelo, numa subpasta do monstro (`true` = `files/`, ou o nome dela) com `<qualquer nome>.NGP` e
  `.PTR` (e `.TEX`/`.RTX`, opcionais): viram um traje de mod só dele (como uma skin implícita, traje 4 a 99), que passa a ser o traje
  padrão; o `costume` do JSON dá o que os arquivos não trazem (sons, o que faltar). Sem a pasta, o log avisa e fica o traje do disco.
- `kind`: regras próprias do port para o monstro (`wotm_custom.inc`). Hoje só `"mecha"` (abaixo); vazio = as do tipo.
- `ai`: se uma CPU pode usá-lo (padrão não); sem ele a seleção mostra "PLAYERS ONLY" no quadro da CPU. Com `"ai": true` a CPU pode
  escolhê-lo na casa CUSTOM da seleção; o launcher só oferece customs ao jogador. Um custom de base jogável luta como CPU
  (`WOTM_MATCH=1,moddedtogera,1`, com `"base": "togera"`: anda, ataca, solta fogo e perde a rodada como um Togera), e o Jelly e o
  Cerebulon também (`WOTM_MATCH=1,cerebulon,1`: a IA comum do jogo, a mesma que move o Cerebulon na fase final). O Goliath não: a IA
  dele (`Ai::updateAssBoss`) precisa dos pontos da fase dele, e como CPU (`WOTM_MATCH=3,goliath,1`) carrega mas não aparece na luta.
- **Na seleção** (telas 3, 6, 7, 18 e 19 e a de 3 e 4 jogadores, fora da eliminação): a casa CUSTOM da barra de monstros. X abre a lista dos customs dentro do quadro do
  jogador (nome, mod, quantas skins; no quadro de CPU, "PLAYERS ONLY" nos sem `"ai"`): cima e baixo percorrem, X escolhe, triângulo
  fecha. Escolhido, o quadro mostra o monstro como os do jogo: cima e baixo trocam de traje (o `costume` do `monster.json` e as
  skins do custom, em dourado), X confirma, triângulo volta para a casa fechada. Com a lista aberta, esquerda e direita não saem da
  casa. Nas telas do jogo, na confirmação o cursor do jogo fica na coluna de onde o quadro saiu, que faz as vezes dele (a vaga dela
  não é tomada). Na de 3 e 4 (`customStep` no `wotm_padsel.inc`) a escolha é toda nossa: o tipo do custom vai ao `m_monsterSel` do
  quadro e o traje ao dele (jogadores 3 e 4 pelo `wotmMultiSetCostume`, CPUs no `m_costume[2 + c]`).
- **Kit:** `modkit.py monster new <mod> <id> --base <tipo>` cria o `monster.json` (sem `--costume` e `--camera`, os padrões do tipo: o
  Goliath no traje 1 e com a câmera do Togera 200%, o Jelly com a do Togera 300%) e, sem `--no-textures`, grava `"files": true` e
  exporta as texturas do traje para `mods/_work/<mod>/monsters/<id>/files/`; o `pack` leva as editadas para a pasta `files/` do
  monstro, onde viram o traje próprio dele (só PNGs bastam: o que a pasta não traz vem do `costume`). `monster portrait` copia o
  retrato e acha o `portraitTex` (a última textura 128 × 64 do `.RTX` do traje); `monster model` põe `.NGP` + `.PTR` próprios em
  `files/`. O `check` avisa de `base` não aceito, id inválido e pasta de `files` faltando. Na janela: **New monster...** (seção
  "Interface").
- **Launcher:** os customs aparecem em "Your monster", depois do Random (só para o jogador).
- **O que o port faz pelo Goliath** (`native/runtime/wotm_custom.inc`): ele fica visível e controlável com IAs na luta (o
  `SetAIMonster` o desligava), toma dano delas (o `takeHit` só aceitava golpe do jogador 1), carrega o banco de som do chefe fora
  da aventura e põe o nome no HUD. Também: X+Quadrado faz o giro do chefe (`StateTopSpin`, no lugar do `StateCrowdControl` dos
  comuns; o golpe cheio vale contra a IA mais perto), a torre não segue mais o inimigo o tempo todo (o "olhar" do chefe gira o
  tronco inteiro; com o lock ela mira) e o dardo (empalar) vira um golpe comum em quem não tem as animações de empalado (antes
  travava). Investigações (encerradas): `git show 18bb1d2:docs/investigacoes/2026-10-23/goliath_topspin.md`, `goliath_lockon.md`, `goliath_impale.md`.
- **Retrato do HUD:** o do `ASSBOSS1.RTX` (texId 6) é provisório, com a cara do Congar. `"portrait": "goliathprime.png"` (na pasta
  do monstro ou na raiz do mod; 128 × 64 ou maior na mesma proporção, ex. 512 × 256) troca a imagem do retrato do traje do custom
  (`"portraitTex"`: o texId no `.RTX`; padrão 6 para o `ASSBOSS1`). Vale também para o chefe da aventura, que usa o mesmo arquivo.
  Log: `[mods] ASSBOSS1.RTX t6 <- Goliath Prime/goliathprime.png`.
- Ainda não: animações que o ASSBOSS não tem (escalar, alguns bloqueios e contra-ataques; `18bb1d2:docs/investigacoes/2026-10-23/goliath_codigo.md`,
  seção 3) fazem o jogo segurar a pose anterior; elimination e minigames não foram testados com ele; o save da aventura não guarda
  custom (a aventura não oferece a casa).

### Cerebulon (o mod `mods/Cerebulon/`, no repo)
As duas fases do chefe final que são `Monster`: `jelly` ("JELLY", base `jelly`, câmera do Togera 3x) e `cerebulon` ("CEREBULON",
base `final`), os dois com `"ai": true` e o narrador `cerebulon`. A primeira fase, o Mecha, é peça da fase final
(`FinalBoss::updateMecha`), não `Monster`: entra como monstro montado (mod `mods/Mecha/`, seção "Mecha" abaixo).
- **O que o port faz por eles** (`wotm_custom.inc`): visíveis e controláveis com IAs (o `SetAIMonster` desligava os tipos 0x1C0 e
  0x1E0 e os contava como IA), visíveis também como CPU fora da aventura, banco de som `Final_s.bnk` fora da fase final e nome no
  HUD ("JELLY WINS", "CEREBULON CRUSHED").
- **Como o jogo os trata** (do retail, valem para o jogador): o Jelly não tem fúria (`Monster.cpp`, rage), não agarra nem é agarrado
  (`Grapple.cpp`) e o Cerebulon não pega o item de especial (`PowerUps.cpp`). Os golpes, pulo e arremessos vêm das animações do
  `JELLY0`/`FINAL0`.
- **Choque do Jelly** (`StateUltraTazer`, o especial dele): **Triângulo + Círculo** (a combinação do especial dos monstros comuns
  no tipo A), com o especial disponível. O `transitionOK`
  do jogo não pede botão (na fase final o roteiro do `FinalBoss::updateJelly` decide), e com o Jelly jogador o choque saía
  sozinho; o port só o deixa começar com os dois botões apertados no controle do jogador. O choque gasta o especial, e o Jelly
  jogador pega o item de especial como um monstro comum (o retail o recusa aos tipos 0x1C0/0x1E0). Jelly CPU: como no retail.
  **Alvo:** o `StateUltraTazer::update` mira o `game->m_monsters[0]` (na fase final, o jogador), sem limite de distância; com o
  Jelly jogador, durante o choque esse alvo é o inimigo vivo mais perto (qualquer monstro ativo, jogador ou CPU). O choque só
  começa com um inimigo a até 1200 (o Jelly é enorme: na luta o inimigo fica a ~700–1100) e acaba se nenhum ficar a até 1500.
  Log: `[custom] Jelly: alvo do choque tipo 1 a 787`, `[custom] Jelly: choque (Triangulo+Circulo)`, ou
  `[custom] Jelly: choque sem inimigo a 1200 (o mais perto a 1250)`.
- O retrato do HUD fica cinza (sem `"portrait"`).

Exemplo: seleção da tela 6 (1 jogador contra CPU), CUSTOM → GOLIATH PRIME, a CPU recusada na casa,
luta em Midtown Park com câmera, som e nome. Foto: `docs/screenshots/2026-10-23/2026-10-23_selecao_casa_custom_goliath.png`.

### Mecha (o mod `mods/Mecha/`, no repo só o `monster.json`)
O robô da primeira fase do chefe final, montado como monstro de base Jelly (`"base": "jelly"`, `"files": true`, `"kind": "mecha"`,
câmera do Togera 3,5x, `"ai": true`, narrador `cerebulon`). Os arquivos do modelo são do jogo e não vão para o repo: cada um os
gera do próprio disco com `python tools/mechamon.py mods/Mecha/monsters/mecha/files` (grava `MECHA.NGP`, `.PTR`, `.TEX`, `.RTX`;
os quatro estão no `.gitignore`).
- **O que o `mechamon.py` faz:** o `MON/JELLY0` é o molde (sombra, retícula, `COLL_GRID`), e a raiz 0 passa a apontar para a
  instância do Mecha copiada do `LVL/CAPITOL.NGP`. A instância principal vira o tipo 0x1C0, o escudo 0x1D8 e o feixe 0x1D9. O
  emissor do laser (0x27AA) vira o localizador 0x80B, e pontas das garras viram 0x802/0x803/0x808. As animações são renumeradas para
  as que o `Monster` pede (`ANIM_MAP`: andar, recuo, o especial 160/161/162, morte 120; o giro e a inclinação da torre, 11 e 12 do
  chefe, viram 290 e 291). As texturas do `CAPITOL.TEX` vão para o fim do `JELLY0.TEX`. Detalhes: `docs/investigacoes/2026-10-27/`
  (`mecha_pecas_capitol.md`, `mecha_formato_monstro.md`, `mecha_carga_custom.md`).
- **Escudo** (como o `FinalBoss::updateMecha`): começa ligado, e com ele o Mecha toma 1/4 do dano. Depois de cada disparo o escudo
  baixa por 240 quadros (dano cheio, sem laser) e religa.
- **Laser** (o único ataque especial; sempre disponível com o escudo ligado): **Triângulo + Círculo**. Usa o `StateUltraTazer` do
  Jelly como roteiro de animação e tempo (preparar, 180 quadros de rajada, desligar). Na rajada o raio é o do chefe: da boca (0x80B)
  até 4000 na direção da mira (`hdLosCSTest`). O monstro atingido toma 0,4 por quadro (`takeDamage` creditado ao Mecha) e é
  empurrado; prédio e objeto acumulam num `HurtHistory` próprio e levam um golpe de 5,01 a cada 5 (`dispatchHit`); faíscas no ponto.
  A torre gira e inclina para a mira (blends das animações 290/291), e o feixe se estica até o ponto.
- **Mira:** o jogador mira pela **câmera da torre**. Durante o laser a vista dele fica 220 acima da ponta do canhão, olhando para a
  mira, com uma retícula no centro (só sem tela dividida); o feixe vai para o centro da tela. Os dois analógicos e o mouse giram a
  mira (sensibilidade e eixo invertido da aba CAMERA), e a câmera entra e sai em 12 quadros. A CPU mira o peito do inimigo mais perto.
- **Sons:** os do chefe (`FinalBossSound`, `Final_s.bnk`): preparar, o laço do laser, o laço do impacto no monstro atingido
  (`Monster+0x1A84`, como o chefe) e o escudo descendo e subindo. Os laços são desligados à mão no fim da rajada.
- **Memória:** `PS2_RAM_SIZE-0x1400` a `-0x1000`, no topo dos 64 MB do runtime: 2 Mechas × 0x1C0 (6 `_animHandle`, 2 `AnimBlend`, o
  `HurtHistory` e o `FinalBossSound`) e 0x80 de rascunho para as chamadas. Logo acima ficam os `kQuadDefs` do split4.
- Não pula (decisão do dono). Teste sem controle: `WOTM_MATCH=mecha,1,1 WOTM_MECHATEST=1` (dispara sempre e a mira varre sozinha);
  `WOTM_AIVSAI=1` para IA x IA. Log: `[custom] Mecha (dup 0): escudo, laser e torre achados`,
  `[custom] Mecha: escudo baixou (...); rajada: 180 quadros no monstro (-72.0 de vida), 0 em objetos`.

## Fotos da seleção de fase
`mods/<mod>/levelpics/<nome>.png` troca a foto das fases que o `SHELL.TEX` não tem (`training`, `lakepowell`, `tokyodeathmatch`) e
a da entrada RANDOM (`random`; sem ela, um mosaico das fotos do jogo). Proporção 2:1, qualquer resolução. Ganha da pasta
`levelpics/` do repo (`NATIVO.md`, "Seleção de fase").

## Por dentro (para quem mexe no código)

- **Arquivos** (`native/wotm_mods.hpp`, `native/runtime/wotm_mods.inc`): todo arquivo que o jogo lê passa pelo `fileCdSearchFile`
  (fases, monstros, `SHELL`, bancos, músicas, filmes) e depois pelo `sceCdRead` (`fileReadf/a/s`, `zipInflateAll`,
  `snd_StreamSafeCdRead`). O gancho da busca roda a original e, se o arquivo estiver em `mods/`, devolve um LSN falso (faixa a partir
  de 0x800000, além de qualquer DVD) e o tamanho do mod; o do `sceCdRead` atende essa faixa lendo do arquivo do PC. O som nativo
  (`readLsnRaw`) lê bancos e músicas pela mesma faixa. A camada nativa que lê o disco por nome também procura antes em `mods/`:
  paletas do `.RTX` (`Scene::loadResources`, que lê o arquivo direto e não da RAM), filmes (`wotm_fmv.hpp`), ícone, fotos e monstro
  da lista de saves (`wotm::disc::readFile`). Sem nenhum arquivo em `mods/` nenhum gancho é instalado. Arquivo que não existe no disco
  também é servido ("arquivo novo").
- **Log:** `[mods] N arquivo(s) de <mods> em <pasta>` no boot e, na primeira busca de cada arquivo, `[mods] MON/LIZARD0.TEX <-
  teste_togera (110080 bytes; disco 110080)`, com AVISO quando o do mod é maior (fora de `MON/*`, cuja carga é contígua acima de
  32 MB).
- **PNG** (`native/wotm_texmods.hpp`, `Scene::modTexture`): o jogo segue carregando o `.TEX`/`.RTX` do disco; o desenho nativo troca a
  imagem no `textureId` (objetos) e no `residentTexture` (quadrados de tela: barras dos inimigos, fonte). O texId do disco sai do
  deslocamento do pacote de `texInfo[id]` dentro do arquivo carregado (`fileStatus.texAddr`/`resAddr`, nome em `names`); as
  residentes, pelo DBP. PNG maior que a original ganha mipmaps. Partículas: `Scene::modParticle` no `particleTexture` e no `fxTexture`. Monstro 3D da seleção e da lista de saves: `model::Model::decodeTex` (`wotm_model.hpp`). Fora: poeira composta e o GS emulado (para
  esse, `modkit.py pack --inject`).

## Cuidados

- Arquivo de mod maior que o do disco (`.TEX`, `.RTX`, `.NGP`, `.BNK`): o jogo carrega em regiões de tamanho fixo e pode sobrescrever
  o que vem depois (o log e o `check` avisam). Com PNG isso não acontece: a textura do jogo não muda de tamanho.
- Os monstros custom usam o código de um tipo do jogo; um tipo novo ainda não existe (`TODO.md`, item 8).

Exemplo (`WOTM_MATCH=3,1`, `3,1,2,5,1` e `1,3,1` com IA): a pele do Togera em PNG 4x (1024 × 1024 no lugar de 256)
e o retrato 2x no HUD do jogador e na barra do inimigo; o kit faz export, edição, pack (PNG, `.VAG`, um som do `SHELL_S.BNK`),
re-export sem perder edição e check. Fotos: `docs/screenshots/2026-10-22/2026-10-22_mods_png_*`.
