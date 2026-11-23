# Respostas à lista `melhorias_mod_kit.md` (kit no navegador)

O que foi feito em cada ponto, o que ficou diferente do texto pedido (e por quê) e as dúvidas para o dono. Testado com o kit
num Edge sem janela (fotos por CDP, sem erro de JS) e, no item 8, no jogo.

## Bugs, melhorias e ajustes

1. **Editors no Setup:** feito. O Setup tem agora três partes: disco e pasta de mods, **Sounds** (volume e "Play sounds on
   click") e **Editors** (um campo por tipo, com `…` e Clear; salva ao mudar). O botão "Editors..." saiu da prévia.
2. **Play/Stop só em som:** feito. Play, Stop, volume e "play sounds on click" ficam numa barra que só aparece com um `.wav`
   escolhido; textura e modelo mostram só Open in editor, Show in folder e Remove. O "play sounds on click" está nos dois
   lugares (Setup e barra do som), sempre iguais.
3. **UV errado na prévia do `.NGP`:** era a textura, não o UV. A prévia do arquivo lia só o `.TEX` ao lado; as texturas
   novas de um rig import (as do DK) são PNGs na mesma pasta. Agora o `.NGP` sai com os PNGs da pasta por cima, igual à
   prévia do grupo e ao jogo.
4. **Retrato do custom no ícone:** feito. O ícone do grupo levava o mesmo endereço antes e depois da troca e o navegador
   mostrava o antigo; o endereço agora leva a data do arquivo.
5. **Volume:** feito, no Setup e embaixo do espectrograma, guardado no `modkit.json` (`volume`).
6. **Espectrograma:** no máximo 200 px de altura, com os botões logo embaixo.
7. **Checkboxes no tema:** feito (e os radios também): escuros por dentro, moldura clara, laranja quando marcados.
8. **Skin nova do DK:**
   - A lista "Monster" era um campo com sugestões (datalist), que já vinha preenchido com o DK e por isso não mostrava os outros.
     Virou uma lista de verdade: monstros do jogo, customs dos mods e, no fim, "Another custom monster (type its id)".
   - A skin de um custom saía com o modelo do Congar porque o kit extraía as texturas do traje do disco, e no jogo uma skin
     sem `.NGP` na pasta usa o traje base do disco. Agora a skin de um custom que tem modelo próprio nasce como **cópia do
     modelo dele** (`.NGP/.PTR/.TEX/.RTX`, PNGs, mapa de ossos e o `.blender.json`, com os nomes do traje da skin). Conferido
     no jogo com uma skin de teste (`WOTM_MATCH=donkey_kong:dkalt,3,1`): carrega o modelo e as 21 texturas da pasta da skin,
     com o retrato do DK.
   - A aba 3D da skin de um custom ganhou os mesmos caminhos do custom: **Import rigged model...** / **Adjust bones...** e
     **Import character with animations...**, valendo só para aquela skin (`modkit.py skin rigged|rig`). Testado o `skin rig`
     na cópia (25 s).
   - A prévia 3D de uma skin com modelo próprio mostra esse modelo (antes mostrava sempre o do disco).
   - A `skin1` que você já criou continua com o Congar: ela foi criada antes. Apague e crie de novo para começar do DK.
9. **Check model em outros formatos:** feito para custom e skin de custom. `.gltf` e `.dae` são conferidos para o
   **Import rigged model** (o único import que lê esses formatos): o kit encaixa os ossos numa pasta temporária, com o mapa de
   ossos que já estiver na pasta ou o automático, e passa o resultado pelas mesmas conferências. Com o `.dae` do DK: o import
   aceita, "no problems". Skin de monstro do jogo segue só com `.glb` (só o Import mesh vale para ela).

## Strings

| Pedido | Ficou | Nota |
|---|---|---|
| Double-click... | "Double-click to extract it to your mod's work folder. Ctrl+click or Shift+click picks several to extract at once." | |
| `.TEX`/`.RTX` | `.TEX`: "the pictures painted on the 3D model"; `.RTX`: "resident texture file... the color palettes of the .TEX's pictures and the pictures shown outside the 3D model, such as the HUD portrait" | A diferença existe: o `.RTX` tem as paletas e as imagens fora do modelo (o retrato). Arquivo sem desenho (`.TEX`, `.PTR`, `.CHR`...) não deixa mais o espaço vazio: o texto sobe. |
| Texture X of Y... | "Texture no. X of the file Y. Replace it with an edited or new picture of any resolution (bigger is sharper in the game); keep its aspect ratio (width to height), or the game shows it stretched." | **Diferente do pedido:** o texto pedido diz que a proporção é forçada com nearest neighbor ao reimportar; não é: o jogo estica a imagem (as UVs são normalizadas) e o `check` só avisa. Ver dúvida 1. |
| Texto do custom monster embaixo do painel | removido (o da skin também, era igual) | |
| LOOK | APPEARANCE | O texto agora é curto: arquivos próprios ou traje do disco; o modelo fica na aba 3D. |
| distance | "Distance from monster to camera" (numa linha embaixo da lista de câmera) | A aba 3D de câmera ficou como dúvida 2. |
| RULES | "Special rules", com explicação no hover | É o `kind` do `monster.json`: regras do port além do tipo base. Só existe `mecha` (o robô do chefe final: laser pela câmera da torre, escudo, sem pulo). Ver dúvida 3. |
| Now: own model... | "Currently using:" em lista: o modelo próprio ("the mesh, the skeleton and the animations are all inside the .NGP"), de onde veio ("Made from FitDonkey00 Fix.dae: its bones fitted to the base's (the fitted copy is FitDonkey00_Fix_rig.glb)") e os fontes | Você estava certo: o esqueleto vai para o `.NGP`. O `.glb` listado é só o fonte de onde o `.NGP` saiu. |
| Downloaded model with a skeleton | "Replace with an already rigged model" | |
| A .glb, .gltf or .dae... | "Formats: .glb, .gltf or .dae, with bones and weights (e.g. a model from another game or a 3D site). Its bones are fitted to the base type's, so it plays the base's animations. After the import, [Adjust bones...] reopens the bones page to fix the fit (e.g. a foot or a hand turned the wrong way, an open hand that should be a fist) and imports it again." | Tirei "a closed mouth that should be open": a página de ossos não tem um caso conhecido disso. |
| Edit in a 3D editor | "Edit the base model and import it back" | |
| Reshape the body... | o texto pedido, com os botões entre colchetes | |
| Own skeleton | "Import an entirely new character" | |
| A .glb with its own armature... | o texto pedido + onde mudar os golpes: os dados da luta (animação de cada golpe, dano, alcance, tempo) ficam os do tipo base; [Extract stats] na aba Stats & sounds dá ao monstro o `.CHR` próprio, e o MODS.md diz como editar a tabela de golpes | Ver dúvida 4. |
| Import rig... | "Import character with animations..." | Os logs do check também usam "Import character". |
| Check a model first | "Check your model before importing it" (primeiro bloco da aba) | "first before" repetia a ideia. |
| Reads a .glb and changes nothing... | "Use this before replacing the current model. It reads your file and changes nothing: the Messages box says which of the imports below can take it and lists what would go wrong (...)" | "which import takes it" quer dizer qual dos botões de import aceita o arquivo; o log agora escreve "the import that takes it: ...". |
| Extracts the base type's .CHR... | "Extracts the base type's .CHR (fight numbers and move list) and .AIC (how the CPU fights) to this monster's files. The edited ones go in with [Build mod], for this monster only." | |
| NARRATOR | hover: "The line the narrator says to announce this character when it is picked: narrator/<name>.mp3 in the mods. Empty = the base's." | Camera, Special rules e Special também ganharam hover. |
| Custom monster, built on Congar | "Custom monster, built on top of Congar" | |
| New skin for it... | "New skin for this monster..." | |

## Dúvidas

1. **Proporção da textura:** hoje uma textura com outra proporção sai esticada no jogo (o `check` avisa). Quer que o kit
   force a proporção (no **Replace with...** e/ou no Build), e de que jeito: esticar com nearest neighbor (o que o texto pedido
   dizia), cortar, ou completar com transparente?
2. **Aba 3D da câmera:** dá para fazer uma aba que mostra o monstro e a câmera da luta na vista 3D, com a distância num
   controle arrastável. Quer isso? É trabalho médio (ler a câmera do `.CHR` do monstro escolhido em "Camera", como o port faz).
3. **Special rules:** só serve para o Mecha. Esconder a linha quando estiver vazia (e deixar só no `monster.json`)?
4. **Editor de golpes:** a tabela de golpes do `.CHR` só se edita pelos scripts `tools/chr_moves.py` e `tools/chr_punch.py`
   (linha de comando, fora do exe do kit). Quer um editor de golpes no kit (lista de golpes com animação, dano, janela de
   acerto)? Seria um item novo no TODO.
5. **Skin de custom sem arquivos, no jogo:** uma skin de custom sem `.NGP` na pasta usa o traje do disco (o Congar), não o
   modelo do custom. O kit agora contorna copiando o modelo. Quer que o runtime também passe a usar os arquivos do custom
   nesse caso (vale para skins feitas à mão ou por kits antigos)?
6. **Check de um `.glb` baixado:** um `.glb` com esqueleto que não veio do kit é conferido para o "Import character" (esqueleto
   próprio, ações `a090`...), não para o "Import rigged model". Se o arquivo não tem ações com os ids do jogo, conferir como
   rigged faz mais sentido. Quer que o check decida pelo que o `.glb` tem (sem ações `aNNN` = rigged)?
