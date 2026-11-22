# Teste: Buzz Lightyear sobre o Robo-47 pelos dois caminhos (2026-11-22)

Primeiro item do TODO "Malha nova pelo Mod kit" (plano em `plano_malha_kit.md`). Modelo: Buzz do models-resource (`pf_buzz.dae`/`.fbx`/`.obj`/`.smd`, 8148 triângulos, 3 materiais, cor 512×512, esqueleto `*_jnt` com 99 ossos, pose em T, Y para cima, em metros). Mod de teste `teste_buzz` (`mod.json` com `"test": true`, fora do repo), customs `buzz` (caminho B) e `buzza` (caminho A), base Robo-47.

Classificação: **BLOQ** = sem saber algo de fora dos docs não passa; **CONF** = passa por tentativa e erro, ou a mensagem não explica; **CHATO** = trabalho manual. O que é trabalho de pesos e encaixe da malha (do usuário, `plano_malha_kit.md`, seção 4) não conta.

## Caminho B: o Buzz com o esqueleto dele e as animações do Robo-47

Resultado: **funciona**. O Buzz luta, segura a viga e solta o especial do Robo-47 (`docs/screenshots/2026-11-22/2026-11-22_buzz_robo47_retarget_*.png`). Foi preciso um especialista: 4 contornos à mão e 1 correção de código.

| # | Trava | Tipo | O que aconteceu |
|---|---|---|---|
| B1 | Só linha de comando | BLOQ | `retarget.py` não tem botão; a GUI só tem o `Import rig`, que espera o `.glb` já pronto. |
| B2 | Mapa de ossos à mão | BLOQ | JSON com o osso do Robo que cada junta do Buzz segue. O `ROBOT0.glb` chama os ossos de `bone0`..`bone21` (mais `xf_`, `st_`, `sc_` e `loc_8xx`); descobrir que `bone0` é a cabeça, `bone5`..`bone1` o braço esquerdo e `bone14`..`bone12` a perna esquerda pediu a árvore com as posições (no Blender: clicar osso por osso). A raiz do Robo (`bone21`) tem a coluna e o quadril (`bone20`) separados, como o `root_jnt` do Buzz; o erro "the kept joints have 2 roots (hips_jnt, spine_1_jnt); map the pelvis/root" levou à correção. |
| B3 | Só lê `.dae` | BLOQ | O Blender 5 não lê nem grava Collada (`collada_import` não existe), e o `.fbx` desse modelo é em texto, que o Blender recusa ("ASCII FBX files are not supported"). Quem só tem `.fbx` ou `.glb` não chega ao `.dae` com o Blender atual. Este modelo veio com `.dae`. |
| B4 | Z para cima | CONF | O `retarget.py` supõe Z para cima e ignora o `<up_axis>`; o Buzz é Y para cima (sem `<up_axis>`, que no Collada quer dizer Y) e sairia deitado. Contorno: um nó que gira a cena. Depois disso a frente fica em -y. |
| B5 | `--front -y` | CONF | O argparse lê `-y` como opção ("expected one argument"); só `--front=-y` funciona. A doc mostra `--front +y\|-y`. |
| B6 | Imagem que falta | CONF | O material do capacete aponta para `buzz_dome_mat`, que não existe; o `retarget.py` cai com o erro cru do Python (`[Errno 2] No such file or directory`) depois de 14 s. Contorno: apontar para `buzz_forcefeild.png` no `.dae`. |
| B7 | UV nos vértices | BLOQ (corrigido) | O `.dae` põe o `TEXCOORD` no `<vertices>`; o `retarget.py` só procurava nos triângulos e usava (0,0) calado: o Buzz inteiro da cor da bochecha. Corrigido em `e7232928`. |
| B8 | Avisos do Robo | CONF | O `Import rig` avisa "a000_59: id 0 already given by a000_0; skipped" (3 vezes): vem do `.glb` do Robo, não do modelo, e não pede ação. |
| B9 | Capacete | a conferir | O vidro (textura do campo de força, com alfa) sai escuro e opaco no jogo. |

## Caminho A: malha nova presa nos ossos do Robo-47

Simulei no Blender o que o usuário faria (`Export to Blender`, importar o Buzz, apagar as malhas do Robo, tirar o esqueleto do Buzz, pôr no tamanho do Robo, pesos automáticos, exportar `.glb`). Resultado: **o `Import mesh` recusa, e o mesmo `.glb` passa pelo `Import rig`**.

| # | Trava | Tipo | O que aconteceu |
|---|---|---|---|
| A1 | Nome da malha | BLOQ | "no obj_\<address\> mesh of a skinned OBJECT of ROBOT0.NGP in the .glb": o `Import mesh` só troca malhas que têm o nome de um objeto com pele do monstro. A doc diz para manter os nomes, mas não diz que uma malha nova precisa herdar um deles. |
| A2 | Paleta de ossos | BLOQ | Com a malha renomeada para `obj_a23e90`: "vertex 0 has no weight on a bone of the palette". Cada objeto do modelo só aceita os ossos da sua paleta, e o Robo-47 é quase todo peças rígidas (3 objetos com pele e 22 peças presas em ossos). O `Import mesh` serve para remodelar dentro das peças que o monstro tem, não para um corpo novo. |
| A3 | A saída existe, mas escondida | CONF | O mesmo `.glb` (esqueleto do Robo, exportado pelo kit) vai pelo `Import rig`: objeto novo com qualquer osso, textura própria (512 aceita), 8146 triângulos. Funcionou no jogo. O `MODS.md` fala disso numa frase ("the base type's own skeleton, edited, also goes in this way"); a GUI leva ao `Import mesh`. |
| A4 | Juntar malhas | CONF | Juntar as 3 malhas no Blender perdeu o modificador Armature (o primeiro objeto não tinha); a mensagem "has no skin (keep it parented to the armature)" explica. |

Do lado do usuário e do arquivo, sem culpa do kit: o `.obj` do pacote tem o V invertido em relação ao `.dae` (mediana do V 0,35 contra 0,70; a textura já sai errada no Blender), e a pose em T do Buzz contra os braços baixos do Robo deixa os braços duros (encaixe e pesos).

## O que muda no plano

1. **Caminho A pelo `ngprig`:** o `Import mesh` reconhece um corpo novo (malha sem nome de objeto do monstro, ou pesos fora da paleta) e oferece importar pelo caminho do `Import rig` com o esqueleto do próprio monstro. Isso já dá a textura nova (o item "Import mesh com textura nova" vira este). O `Import mesh` de hoje fica para remodelar as peças.
2. **`retarget.py` aceitando `.glb`/`.gltf`** (o Blender 5 não grava `.dae`), lendo o `<up_axis>`, aceitando `--front -y`, e seguindo sem a textura que falta (com aviso). Entra no item do "Import rigged model...".
3. **Ossos com nome no export** continua: é o que tira a trava B2 e ajuda a pintar pesos no caminho A.
4. **Mapa automático:** o Buzz não é Mixamo nem Biped. Reconhecer por nome genérico (`L_`/`R_`, `arm`, `leg`, `clav`, `spine`, `neck`, `head`, `hips`) e pela ordem na hierarquia; do lado do monstro, depende do item 3.
5. **Avisos que não pedem ação** (B8) somem ou viram uma linha.

## Depois do "Check model..." (`tools/meshcheck.py`)

Rodado nos arquivos deste teste (o mesmo que o botão faz):
- `ROBOT0.glb` do Export to Blender: sem nenhum problema (vai pelo Import mesh). A primeira versão media a distância de cada vértice ao osso que mais o move e acusava a peça `obj_a280f0` do próprio Robo (peças rígidas presas em ossos de pivô longe delas); trocada por tamanho e lugar do conjunto das malhas contra os ossos que as movem.
- `buzz_A2.glb` (corpo renomeado `obj_a23e90`): o erro de paleta (A2) aparece antes de importar, com a dica do Import rig; avisa que a imagem própria não entra pelo Import mesh e os 1092 vértices com mais de 3 ossos.
- `buzz_A.glb`: achou o que eu não tinha visto no teste: a malha `default` (6326 dos 7863 vértices, o corpo) ficou **sem pele** depois do join (A4) e foi para o jogo inteira presa na raiz. Os "braços duros" eram isso, não os pesos automáticos.
- `rig.glb` do caminho B: passa, com os localizadores 0x852..0x857 postos onde o Robo os tem.
- Cópias estragadas de propósito (`ROBOT0.glb` com as malhas ×0,02 e deslocadas 300 unidades): as duas viram erro de lugar, que o Import mesh aceitava calado.
- O aviso repetido de id dobrado (B8) virou uma linha comum no `ngprig` e some do relatório. JPEG dentro do `.glb` vira PNG no import (Pillow, que vai no exe do kit).
- O check do Import rig roda o import inteiro numa pasta temporária: ~40 s no Buzz (158 animações).

## Números para comparar depois das melhorias

- Caminho B: 3 comandos fora da GUI (`retarget.py`, conversão para Z para cima, edição do `.dae`); 5 coisas que precisei saber fora dos docs (o que é cada `boneN`, Z para cima, `--front=`, a imagem que falta, a raiz com dois filhos); 1 bug do kit.
- Caminho A: 2 recusas que bloqueiam; a saída que funciona não é indicada pela GUI nem pelas mensagens.
