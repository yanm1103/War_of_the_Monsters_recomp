# Roteiro do vídeo: um modelo com esqueleto como monstro novo (rascunho para o dono aprovar)

Último item do TODO "Malha nova pelo Mod kit". Mesmo pipeline do vídeo da skin (`tutorial-skin:docs/investigacoes/2026-11-12/tutorial_skin.md`,
`tools/tutorial/` naquela branch): roteiro por cenas, takes pelo OBS com `act.py`, TTS cena por cena só depois do roteiro aprovado,
montagem com `montage.py`. Narração em inglês, voz "Anime announcer", abre com "Hello everyone". Nenhum áudio é gerado antes da aprovação.

O vídeo segue o caminho B do teste (`teste_malha_buzz.md`), que é o único inteiro pela GUI: modelo baixado com esqueleto e pesos,
**Import rigged model...**, a página 3D, Build, luta. O caminho A (pesos pintados pelo usuário no Blender) entra numa cena só, como
"o outro jeito", sem ensinar Blender (como o vídeo da skin não ensina edição de imagem).

## Para o dono decidir antes dos takes

1. **Modelo do vídeo.** O teste usou o Buzz Lightyear (models-resource, personagem da Disney/Pixar). Num vídeo público isso pesa
   mais que no repo. Opções: o Buzz mesmo; ou um modelo de licença livre com esqueleto (CC0/CC-BY do Sketchfab, por exemplo, citado
   na descrição do vídeo). O texto abaixo diz "this model" e não cita o nome, para servir aos dois.
2. **Versão que aparece.** O vídeo da skin gravou o release v0.2.2. Check model, Import rigged model e a página 3D só existem na
   `ste-monstros` (ainda fora da master e de release). Gravar depois do merge e de um release novo (o espectador baixa o que vê), ou
   gravar de um build local com a mesma pasta de release.
3. **Cena 11 (caminho A no Blender):** manter (um take curto do Blender mostrando os ossos com nome) ou cortar.
4. Você ainda não testou esse fluxo pela sua GUI; vale testar antes de eu gravar.

## Cenas

| id | Fala (inglês) | O que aparece | Take |
|---|---|---|---|
| 01_intro | Hello everyone! Today I'm gonna show you how to bring a whole new monster into War of the Monsters Recompiled. Not a new color this time: a completely new body, fighting with Robo-47's moves. | Teaser: o modelo novo lutando (soco, viga, especial), câmera da luta girando. | game_teaser |
| 02_model | All you need is a 3D model that already comes with a skeleton and weights. A glb, a gltf or a Collada dae file works. The kit doesn't rig or paint weights for you, so the model has to be ready to animate. | Explorer na pasta com o arquivo do modelo; cursor em cima dele. | explorer_model |
| 03_new_monster | Open the Mod Kit and pick your mod. Click New monster. The base type is Robo-47: our model gets his moves, his sounds and his special. Give it a name, check Own look, and hit OK. | New monster...: base Robo-47, nome, "Own look", OK; o grupo do monstro aparece na lista. | kit_03_new_monster |
| 04_check | Before importing, click Check model and pick the file. The kit reads it and tells you what it found: textures, meshes, bones, and anything that would go wrong. Green light? Let's go. | Check model... > arquivo; o log com as linhas OK (e um WARNING inofensivo, se houver). | kit_04_check |
| 05_rigged | Now click Import rigged model, and pick the same file. | Import rigged model... > o mesmo arquivo. | kit_05_rigged |
| 06_page | A page opens in your browser, with your model standing right next to Robo-47. If it's lying down or facing the wrong way, turn it with these buttons until it faces you, just like him. Looks good? Click OK. | Página 3D: os dois modelos lado a lado; um clique em Turn (para mostrar), outro para voltar; OK. | page_orient |
| 07_import | Back in the kit, the rest happens on its own. It matches the model's bones to Robo-47's, moves every one of his animations onto your model, and builds the files the game needs. | O log do kit correndo (mapa de ossos, retarget, Import rig) até o fim. Mosaico no caminho do usuário. | kit_07_import |
| 08_build | Click Build mod. That's it! | Build mod; o log termina sem erro. | kit_08_build |
| 09_select | Open the game, single player, free for all. Your monster is right there on the select screen. | Jogo: título > single player > FFA > 0 CPUs > o monstro novo > fase. | game_menu |
| 10_fight | And here it is, punching, climbing, and firing Robo-47's beam. Every move he has, your model has too. | Luta: andar, soco, pegar um prédio, a viga, o especial. | game_fight |
| 11_blender | Want to weight a mesh yourself instead? Export to Blender gives you the monster with named bones: left hand, chest, tail. Weight your mesh to them, and bring it back with Import mesh. | Blender com o .glb do Export to Blender: a lista de ossos (L_Hand_bone1, Chest_bone11...). Depois o botão Import mesh... no kit. | blender_bones |
| 12_outro | Share it like any mod: zip the folder inside mods, and your friends drop it into theirs. Thanks for watching! | Explorer: mods > a pasta do mod selecionada. | explorer_outro |

Contagem aproximada: ~1600 caracteres (o vídeo da skin teve 1802).

## Takes novos que o pipeline não tem

- `page_orient`: o navegador que a página abre. Gravar a janela do navegador num retângulo fixo (`winpos.py`), sem a barra de
  endereço (mostra `127.0.0.1` e a porta, nada pessoal, mas polui) e sem abas.
- `kit_07_import`: o import leva perto de 1 minuto no Buzz (retarget + Import rig de 158 animações); o `squeeze` da montagem corta as
  esperas paradas, e a fala da cena cobre ~12 s. Pode precisar de um corte com "a minute later".
- `blender_bones` (se a cena 11 ficar): o Blender também mostra caminhos no título e no outliner; recortar.
- `game_teaser`/`game_fight`: a viga e o especial do Robo-47 pedem o modo treino (memória: o especial se mostra pelo treino) ou uma
  luta com 0 CPUs e um alvo; decidir na gravação.
