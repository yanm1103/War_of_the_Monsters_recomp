# Malha nova pelo Mod kit: plano (2026-11-22)

Pedido do dono: depois do vídeo de skins, deixar o próximo nível (malha nova, por exemplo um Donkey Kong baixado no lugar do Congar) ao alcance de quem não é especialista. Melhorar a interface na exportação e importação de malhas e validar as texturas antes de importar.

## 1. O que já existe

- **Export to Blender** e **Import mesh...** já estão na GUI (skins e customs). O usuário exporta o traje, troca a malha no Blender, prende nos ossos do monstro e importa.
- **Import rig...** (esqueleto próprio) também está na GUI. Não vale para o Congar: o código dele mexe em 8 ossos pelo número (`BoneSpring`; `ngprig.py` recusa).
- **Modelo baixado que já vem com esqueleto** (`retarget.py`): o modelo fica com o esqueleto dele e recebe as animações do monstro base. Só existe pela linha de comando, só lê `.dae` e só tem mapa de ossos pronto para o Biped do 3ds Max (`tools/rigmaps/biped_togera.json`). Foi o caminho do Godzilla, do CJ e dos Godzillas do STE.

## 2. Onde um usuário comum trava (do código e dos docs; falta o teste de verdade, passo 0)

1. **Pose.** O import recusa esqueleto movido ou com a escala mudada, então o usuário tem que encaixar a malha baixada (quase sempre de braços abertos) na pose do monstro. Isso é feito à mão no Blender.
2. **Nomes dos ossos.** O `.glb` exportado chama os ossos de `bone12`, `bone27`. Para prender a malha (pintar pesos), o usuário não sabe qual osso é o braço.
3. **Texturas no Import mesh.** Cada pedaço da malha só pode usar uma textura que o traje já tem (`ngpmesh.py`: material `tex<texId>`; outro material é desenhado com a primeira textura, com aviso no log). A textura do modelo baixado precisa ser reorganizada à mão dentro das imagens do monstro, no mesmo tamanho. O `Import rig` já aceita imagem nova; o `Import mesh` não.
4. **Erros que só aparecem no fim.** Paleta (um objeto com dois blocos de paleta é recusado), mais de 3 ossos por vértice (corta calado), vértice sem peso, esqueleto mexido: o usuário só descobre ao importar, e pelo log.

## 3. Plano

0. **Teste de verdade, só pela GUI** (eu): um modelo gratuito com esqueleto sobre um monstro, seguindo o que um usuário faria. Anoto onde trava. Sai daqui o roteiro do vídeo e a ordem dos passos abaixo.
1. **Validação antes de importar** (GUI: botão "Check" ao lado de "Import mesh...", e o mesmo relatório no começo do import). Uma lista com OK, aviso e erro, em linguagem de usuário:
   - texturas: cada material, a imagem, o tamanho, se vai ser reduzida, se cabe na memória de textura do monstro;
   - malha: vértices com mais de 3 ossos, vértices sem peso, UV fora da imagem, contagem de vértices e triângulos contra o limite;
   - esqueleto: osso movido ou com escala mudada, osso que falta;
   - pose: a malha longe dos ossos (sinal de que não foi encaixada).
2. **Import mesh aceitando textura nova**, como o Import rig: material com imagem vira textura própria (reduzida para potência de 2 até 256, cores para 8 bits com paleta), sem o usuário refazer a textura dentro das do monstro. Reaproveita o código do `ngprig.py`.
3. **Ossos com nome no Export to Blender** (`R_Hand`, `Spine`, `Tail3`; o número fica no nome para o import achar). A tabela de nomes sai dos localizadores (mão, pé, boca) e da hierarquia, por monstro.
4. **Modelo com esqueleto pela GUI** ("Import rigged model..."): escolhe o arquivo (`.glb` ou `.dae`), o kit reconhece o esqueleto (Mixamo `mixamorig:*`, Biped `Bip01_*`) e escolhe o mapa de ossos para o monstro base; tamanho e frente como opções. Pede mapas novos (Mixamo sobre os bípedes do jogo) e o `retarget.py` lendo `.glb`. Não serve para o Congar (passo 2 da seção 1).
5. **Docs e vídeo:** `MODS.md` com o fluxo novo; roteiro do vídeo de malha a partir do passo 0.

Ordem sugerida: 0, 1, 2, 3, 4. O 1 e o 2 ajudam os dois caminhos (malha sobre o esqueleto do monstro e modelo com esqueleto próprio); o 4 é o maior e o que mais muda o resultado para modelos baixados.

## 4. Decisões do dono

- Plano e ordem aprovados pelo dono (2026-11-22), no `TODO.md`; base do teste e do vídeo: Robo-47.
- Escopo (dono, 2026-11-22): skinning não é do kit ("não somos o Mixamo"). Importa o modelo que já vem com esqueleto e pesos: esqueleto transferido (`retarget.py`) ou próprio (`Import rig`). Saem do plano a textura nova no `Import mesh` e o encaixe da malha no esqueleto do monstro; os ossos com nome ficam, porque são o que deixa ligar os ossos do modelo aos do monstro. Modelo do teste: Buzz Lightyear (esqueleto `*_jnt` com 99 ossos, nem Mixamo nem Biped).
- Correção do dono (2026-11-22): o caminho A (malha nova presa pelo usuário nos ossos do monstro, `Import mesh`) fica; o modder tem as duas opções. Só o weight painting automático pelo kit está fora. A textura nova no `Import mesh` volta ao plano.
