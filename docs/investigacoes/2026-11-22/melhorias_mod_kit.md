# Pontos que merecem atenção - Para o Claude Code

## Bugs, melhorias e ajustes

1. Mover as opções de dentro do Editors pra dentro do Setup.

2. Remover os botões de Play e Pause de texturas e modelos, deveriam aparecer só pra sons até onde entendo, os de play/pause de animação de 3D já são diferentes. Mesma coisa do "play sounds on click", que deveria também ser uma opção duplicada dentro do Setup.

3. O preview do NGP aparece com o UV errado.

4. Ao trocar o portrait de um custom monster, o ícone dele poderia mudar também pra refletir isso.

5. Colocar um controle de volume tanto no Setup quanto abaixo do espectograma dos áudios

6. Deixar o espectograma limitado em tamanho, pra não ficar gigante na tela. 200px de altura deve ser suficiente.

7. Checkboxes devem seguir o tema, ou seja, serem escuras por dentro, não brancas

8. Menu de new skin dentro do donkey kong -> monster não lista nada, não sei se deveria... Tentei criar uma skin nova pro Donkey Kong e apareceu o Congar skin1, até aí ok, mas não aparece mais o botão pra importar um novo mesh aqui também pra conseguir importar uma verdadeira nova skin pro DK.

9. Check model só funciona pra glb, não dá pra aceitar outros formatos também?

---

## Conversão de strings

Abaixo conversões de String original -> String revisada. Veja se a revisão faz sentido ou se daria pra ser reescrita de uma forma ainda melhor.

- Double-click extracts it to your mod. Ctrl or Shift picks several. -> Double-click to extract to this mod's work folder. Use Ctrl or Shift to pick several things to extract at once.
- The game's texture file for the model of the same name. The textures to edit are its PNGs. -> The game's texture file for the model of the same name.
(deve existir alguma diferença entre RTX e TEX, será que não dá pra anotar isso? E como não dá pra ter preview de texturas desses tipos, será se precisa ficar um espaço vazio em cima do texto de explicação?)
- Texture X of Y. Save it at any resolution, keeping the aspect ratio. -> Extracted texture no. X of file Y. Can be replaced by a new/edited texture of any resolution. Aspect ratio will be enforced with nearest neighbor when reimporting a texture of different aspect ratio. 
- Custom monster donkey_kong: its PNGs are in the work folder (monsters/donkey_kong/files); open the section to edit them. "Build mod" sends the edited ones to the mod, where they are its own look (only for it). -> nada (esse texto tá muito fora de lugar)
- LOOK -> APPEARANCE
- distance -> distance from monster to camera (será se não dá pra virar uma aba própria pra configurar isso em 3D mesmo?)
- RULES -> (não faço ideia do que seja e o mod kit não explica)
- Now: own model: MONKEY0.NGP, MONKEY0.PTR; own skeleton: FitDonkey00_Fix_rig.glb; sources: FitDonkey00_Fix_rig.glb, MONKEY0.glb -> 
    Currently using: 
    - own model [ MONKEY0.NGP | MONKEY0.PTR ]
    - own skeleton [ FitDonkey00_Fix_rig.glb ]
    (essa parte também ainda tá confusa, pq achei que o esqueleto ia pro .NGP)
- Downloaded model with a skeleton -> Replace with an already rigged custom model
- A .glb, .gltf or .dae that already has bones and weights: its bones are fitted to the base type's, so it plays the base's animations. Adjust bones... reopens that page to fix the fit (a foot, a hand) and imports again. -> Formats accepted: (.glb, .gltf or .dae). It's bones will be fitted to the base mesh type's. Use the [ Adjust Bones ] button below to fix any problems (e.g. an open hand that should be a fist, a closed mouth that should be open).
- Edit in a 3D editor -> Edit base model and reimport it
- Reshape the body or change its moves. Export 3D model writes a .glb (mesh, named bones, animations) to the work folder, for Blender or another editor that opens glTF; after editing it, Import mesh... brings back the shapes and Import animations... the moves. -> Reshape the body or change its animations. The [ Export 3D model ] button writes a .glb containing mesh, named bones and animations to the work folder. Use Blender or another editor that opens glTF to edit it. After editing, use the [ Import mesh... ] button to bring the mesh back, and [ Import animations... ] for the animations.
- Own skeleton -> Import entirely new character
- A .glb with its own armature, a weighted mesh and actions named by animation id (a090 = idle): a body with a skeleton and moves made for it. -> A .glb with its own armature, mesh and actions named by animation id (e.g. a090 = idle). This option is the hardest but allows you to import an entirely new character. (essa parte merece uma adição a mais explicando onde alterar os dados dos golpes)
- Import rig... -> Import character with animations
- Check a model first -> Check your custom model first before importing it!
- Reads a .glb and changes nothing: the log says which import takes it and lists what would go wrong (missing textures, meshes without weights, a model in other units or away from its bones). -> Before attempting to replace the current mesh, you should use this button. It reads your .glb and the MESSAGES box reports (which import takes it | o que? como assim which import takes it? ficou sem sentido essa parte) and lists what will go wrong (e.g. missing textures, meshes without weights, a model in other units or away from its bones) if you attempt to import it.
    (mover essa opção pra ser a primeira de todas tbm)
- Extracts the base type's .CHR and .AIC files to this monster's files: the edited ones become its own (the fight numbers and the punch table in the .CHR, the CPU's AI in the .AIC). -> Extracts the character's .CHR (numbers and movelist) and .AIC (CPU's AI) files to this monster's files. The edited ones are considered when you [ BUILD MOD ].
- NARRATOR -> NARRATOR (no hover, exibir "the string the narrator uses to announce this character.")
- Custom monster, built on Congar -> Custom monster, built on top of Congar
- New skin for it... -> New skin for this monster...













