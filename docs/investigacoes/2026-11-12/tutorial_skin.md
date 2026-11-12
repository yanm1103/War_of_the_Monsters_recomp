# Vídeo tutorial narrado: fazer uma skin de mod

Pedido do dono (2026-11-12): um vídeo narrado ensinando a fazer uma skin. O que se aprender aqui serve depois para os vídeos do editor
de fase e do monstro custom (mesmo pipeline, outro roteiro).

## Decisões do dono

- Narração em **inglês**, voz custom **"Anime announcer"** da conta ElevenLabs do dono (35k créditos). Abre com **"Hello everyone"**.
- **Créditos com cuidado:** o roteiro fica pronto e aprovado antes de gerar qualquer áudio. Gerar cena por cena, sem regravar à toa.
- Áudio pelo **MCP do ElevenLabs** (configurado no `~/.claude.json` deste projeto, `https://api.elevenlabs.io/v1/mcp`). Ele só aparece
  numa sessão nova do Claude Code (e pode pedir autenticação no `/mcp`). Antes de gerar: achar a voz e conferir o saldo.
- Quem grava: Claude, por script (mouse, teclado e OBS). O dono sai do PC durante os takes.
- Exemplo: **Preytor traje 0** (o verde) com a textura azul, para a diferença ficar óbvia na hora.
- Edição da imagem fora do vídeo: a narração diz "I'm gonna edit this texture in Photocraft and show you the end result", corta, e
  volta com a textura já azul. Não ensinar edição de imagem.
- Sem música de fundo. No jogo: música em 0, efeitos ligados.
- Sem legenda embutida; legenda soft (`.srt`) para o YouTube, com os tempos do TTS.
- Publicação: nenhuma por enquanto (o dono faz à mão).
- Gravar no **monitor 1** (2560×1440, o que o OBS já grava; perfil `Sem_nome`, 60 fps, NVENC HEVC, `hybrid_mp4`, em `C:\Users\TwistZero\Videos`).
- WebSocket do OBS ligado (porta 4455, com senha; a senha fica só no `config.json` do OBS, nunca no repo).
- Luta: **Baytown** (fase 7, `sanfran`), free for all com **0 CPUs**, **exército desligado**, para girar a câmera sem interrupção
  (modo foto, F9).
- Pasta limpa no desktop (`Desktop\WotM_tutorial\`, ver "Pastas").
- Reuso aprovado: `tools/tutorial/` com o roteiro por cenas, o TTS, a gravação e a montagem; a mídia (gravações, áudio) fica fora do repo.

## Texturas do Preytor traje 0 (`MON/MANTIS0`)

- `mantis0_t0008` (256×256): o corpo, verde-oliva com os brilhos verdes.
- `mantis0_t0006` (64×64): verde vivo.
- `mantis0_rtx_t0009` (128×64): o retrato da barra.
- O resto (t0001..t0014) é cinza/amarelo (partículas, sombra): fica como está.
- O azul sai de um script de matiz (verdes 40..180° levados a ~212°, saturação ×1,7); o dono aprova o PNG antes da versão final.

## Pipeline

1. Roteiro por cenas (`tools/tutorial/skin/script.json`): texto da narração e o que aparece.
2. Takes gravados pelo OBS (websocket) com o mouse e o teclado por `pyautogui`; cada take sobra tempo e a montagem corta.
3. TTS cena por cena (MCP do ElevenLabs), depois do roteiro aprovado.
4. Montagem com ffmpeg: cada cena com a duração da fala, cortes, zoom na janela do kit quando o texto ficar pequeno.
5. `.srt` dos tempos do TTS.

## Pastas

- `Desktop\WotM_tutorial\WotM\WotM-v0.2.2-win64\`: ensaio e takes do jogo (settings.ini com SKIPINTRO=1, ARMY=0, RES 1440, tela
  cheia; o mod `blue_preytor` do ensaio).
- `Desktop\WotM_tutorial\show\WotM-v0.2.2-win64\`: extração nova do zip, só com o `wotm.ini` copiado. É a que aparece no Explorer e
  no kit (os takes do kit criaram ali o `modkit.json`, `mods/_work/blue_preytor` e `mods/blue_preytor`). Para regravar um take do
  kit, apagar esses três e recomeçar.
- `Desktop\WotM_tutorial\takes\`: os takes brutos (2560×1440, 60 fps).
- O `%LOCALAPPDATA%\WotM` do dono ficou guardado durante a gravação e voltou no fim (conferido igual à cópia antes de apagá-la).

## Takes gravados

| Arquivo | Cena | Conteúdo |
|---|---|---|
| `game_orbit_1.mp4` | 01 | câmera da luta girando em volta do Blue Preytor em Baytown |
| `explorer_setup.mp4` | 02, 03 | pasta do release; cursor em `wotm.exe`, `mods`, `WotM_ModKit.exe`; duplo clique no kit |
| `kit_04_new_mod.mp4` .. `kit_09_build.mp4` | 04..09 | um take por cena do kit |
| `game_menu.mp4` (144,7 s) | 10, 11 | título > single player > FFA > 0 CPUs > Preytor > BLUE PREYTOR > Baytown > carga > uma volta |
| `game_orbit_2.mp4` | 12 | segunda volta e o retrato da barra |
| `explorer_outro.mp4` (14 s) | 13 | `mods` > `blue_preytor` selecionado |

## Retângulos para a montagem (monitor 1)

- Kit e Explorer na mesma janela fixa: `winpos.py "<título>" 480 270 1600 900` (tela = janela + 480,270).
- Explorer: recortar só o conteúdo (x≈720..2025, y≈406..1140), sem a barra lateral e o endereço (pastas pessoais, nome de usuário).
  O painel Detalhes foi escondido nos takes e religado no fim.
- Kit: os campos de caminho e o log mostram `C:\Users\<usuário>\...`; borrar ou recortar na montagem.
- Jogo: tela cheia 2560×1440; zoom no retrato da barra na cena 12.

## Como os takes foram feitos

- `tools/tutorial/act.py` (mouse que desliza, teclas, setas como tecla estendida via SendInput, arrasto relativo para a câmera),
  `obsctl.py start|stop` (websocket; a senha vem do config do OBS), `winpos.py` (janela num retângulo fixo), `scriptinfo.py`
  (caracteres e duração estimada por cena).
- Jogo: depois de abrir, clicar na janela para dar foco (sem isso o ENTER do título se perde). Cursor parado embaixo (1280,1380): em
  cima do menu ele muda a seleção. ESPAÇO = X, ENTER = start, segurar 0,15..0,2 s. Caminho: baixo 1 (FFA), esquerda 1 (0 CPUs),
  direita 1 (Preytor), baixo 2 (BLUE PREYTOR), pronto, direita 6 (BAYTOWN), carga de ~26 s. Câmera da luta: arrasto relativo,
  ~2900 px por volta.
- Kit: o diálogo New skin abre em 0,0 (mover com `winpos.py "New skin" 972 545 617 350`); a caixa "CPUs may pick this skin" vem
  desmarcada (o roteiro manda marcar).

## Achados (fora do vídeo)

- **Check mods** do kit acusa erro em mod que funciona (falso positivo); o vídeo não usa o botão.
- O diálogo **New skin** abre no canto 0,0 da tela, não centrado no kit.
- **CPUs may pick this skin** vem desmarcada por padrão.
- O brilho verde perto do Preytor no começo da luta em Baytown é o pickup de vida, que nasce no lugar do jogador 1 (não é do
  modelo; a narração não fala dele).

## Estado

- [x] Levantamento e decisões.
- [x] Pasta limpa com o v0.2.2; texturas azuis.
- [x] Takes do kit, do Explorer e do jogo.
- [x] `%LOCALAPPDATA%\WotM` do dono devolvido.
- [x] Roteiro aprovado pelo dono (`tools/tutorial/skin/script.json`, 1802 caracteres, ~133 s estimados).
- [ ] Áudio cena por cena (MCP do ElevenLabs), só depois da aprovação.
- [ ] Montagem (ffmpeg NVENC, cada cena com a duração da fala) e `.srt`.
