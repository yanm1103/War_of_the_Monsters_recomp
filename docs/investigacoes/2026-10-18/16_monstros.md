# 16 monstros numa luta: viabilidade (2026-10-18)

**Veredito: dá, no port de PC, e 16 é o teto exato.** A engine foi dimensionada para 16 monstros carregados; quem limita a 4 é o
menu, a RAM de 32 MB do PS2 e algumas tabelas do Shell e do carregador. Nada disso exige mexer em VU1/GS nem no laço da luta. No PS2
de verdade não cabe (falta RAM).

## O que já suporta 16 (é do retail)
| Estrutura | Onde | Tamanho |
|---|---|---|
| `TheGame::m_slots` (os `Monster`, stride 0x11190) | `game.h`, 0xB80 | 16 |
| `TheGame::m_slotInteractive` | 0x1203FC | 16 |
| `EnemyInfo::s_info` (tabela par a par, refeita a cada quadro) | `enemy_info.h` | `[16][15]` |
| `m_monsters` (ponteiros: jogadores em 0..3, IAs a partir de 4) | 0x120380 | o header diz `[8]`, mas há 0x20 bytes livres até 0x1203C0: cabem **16**. O Endurance já grava `m_monsters[8]` (1 jogador + 5 IAs, `screen.cpp`, `m_numAIs = 5`) |
| `hdIgnored` (cs ignorados na colisão) | `hd.cpp` | 32 |
| `Interactives` | | 1024 (a luta usa 180..400) |
| `texInfo` (texturas carregadas) | 0x50F100 | 1000; fase ~310..330 + ~12..18 por monstro → 16 monstros ≈ 620 |
| Seleção de som (`SoundManager::setSoundVolumeInfo`, `[4]` por `m_index`) | | só grava para `m_playerNum == 1` (jogador humano): IA não estoura |

Os laços da luta (`gameResolveCollisions`, `gameResolveLifeAndDeath` nos modos 2/3/11, `getClosestMonster`, `GetMonsterFromName`,
câmera via `m_numAIs`) já andam por `m_numSlots` ou `m_monsters[4 + i]`.

Também provado no retail: o Endurance mantém 6 monstros carregados e o `m_mode == 6` do Shell carrega os 10 monstros de uma vez
(`LoadMonstersDB`, `LoadTexture`, `LoadResTexture`).

## O que impede hoje (medido)
1. **RAM do convidado (o bloqueio principal).** Fase e monstros entram contíguos a partir de 0xA00000 (`fileOnlyNgpFile` +
   `getNextNgpLoadAddr`). Medido em dumps (`build/dumps*/ram_*.bin`, `fileStatus` em 0x445390):
   - Centro + 2 monstros: termina em 21,6 MB, ~5,6 MB livres acima → cabem uns 4 monstros a mais (~6, como o Endurance).
   - Metro + 2 monstros: termina em 29,0 MB, ~1,4 MB livres.
   - Um monstro custa ~1,2..1,6 MB (`.NGP` 1,0..1,4 MB, `.TEX` 0,11..0,19 MB, `.RTX` ~20 KB). 16 monstros ≈ 20..25 MB além da fase.
   - **Saída:** o runtime do PS2Recomp é paramétrico em `PS2_RAM_SIZE` (`ps2_memory.h`, potência de 2, 147 usos pela constante):
     subir para 64 MB e carregar os monstros acima de 32 MB (gancho no carregador). Ponteiros são de 32 bits e o banco é plano,
     então a relocação do `dbs` funciona igual. A camada nativa já usa `wotm::Ram{rdram, PS2_RAM_SIZE}`.
2. **`fileStatus` tem 14 slots** (`FILE_MAX_SLOTS`; o slot seguinte guarda o fim): no máximo 13 arquivos = fase + 12 monstros.
   Precisa de tabela nativa maior, com gancho nos acessores de `file.cpp` (todos pequenos e já decompilados; conferir se há uso
   direto fora de `file.cpp`).
3. **Shell:** `m_monsterSel[4 + j]` passa do `[8]` do header mas cai em `pad2958` (livre até 0x29E0, cabe até j = 11, conferir);
   **`m_costume[2 + j]` atropela `m_mode` (0x2C10) a partir da IA 10.** Os trajes das IAs têm que vir de fora (o gancho do carregador).
4. **Menu:** as telas de seleção vão até 4 (2P/3P/4P). O caminho barato é escalar a luta por configuração (variável/ini lida num
   gancho antes do `LoadLevelFiles`), sem mexer no menu.
5. **Texto de `LoadMonstersDB`:** jogadores 3/4 montam o caminho com o monstro do jogador 2 (peculiaridade do retail); o gancho
   novo não deve herdar isso.

## Riscos a medir (não bloqueiam, podem pedir ajuste)
- **Desempenho.** Com 4 monstros a CPU fica em 6,2 ms medianos (`NATIVE.md`). Por monstro cresce a lógica, o skinning e o
  `EnemyInfo::update` (um `hdLosCSTest` por par: de 1..6 hoje para até 120..240 por quadro). Estimativa grosseira 12..20 ms: 60 fps
  no limite; medir antes de otimizar.
- **Som.** `loadRTSoundBanks` usa bancos "lo-res" quando há mais de 3 slots e carrega um banco por tipo de monstro (até ~10 tipos
  distintos) na RAM do SPU (2 MB). Conferir se a camada nativa de som (`PS2X_SND=1`) contorna o limite do SPU; se não, faltam sons.
- **VRAM residente (`.RTX`).** `maxResAddr` sobe 128 blocos (32 KB) por monstro: 4352 → ~6400 com 16, abaixo dos 7298 onde
  começa a área do `.TEX`. Deve caber; conferir o que mora entre os dois.
- **Pontos de largada.** `StartPoints` tem poucos pontos por tipo; com 16 os monstros podem nascer sobrepostos. `gameGetStartPoint`
  tenta evitar, mas pode precisar de pontos extras (mod de fase) ou espalhamento no gancho.
- **Fases com monstros extras** (chefes 0x1C0/0x1E0 no Capitólio, 0x1A0) gastam slots: ali o teto fica abaixo de 16.
- **Jogabilidade.** IA, câmera e HUD foram pensadas para 1..2 humanos e até 3 IAs. Modos livres para todos (2/3/11) já tratam
  todos os slots; história e chefes não servem.
- **Passar de 16** exigiria mudar `sizeof(TheGame)` (os 16 `Monster` ocupam 1,1 MB no meio do objeto) e o `EnemyInfo`: fora de
  questão sem decompilar e reescrever `TheGame` inteiro.

## Plano sugerido (em degraus, cada um testável)
1. **6..8 monstros sem mexer na RAM:** gancho em `Shell::LoadMonstersDB/LoadTexture/LoadResTexture/InitPlayers` lendo a escalação
   de uma variável (ex.: `WOTM_LINEUP`), modo livre para todos, fase leve (Centro). Prova `m_monsters[>8]`, `pad2958`, IA e câmera.
2. **RAM de 64 MB** no fork do runtime + carga dos monstros acima de 32 MB; regenerar `tools/runtime_patch/wotm-runtime.patch`. **Feito em 2026-10-19** (`92af448:docs/passos/POS_FRONTEIRA.md`, item 1): o Metro com 8 monstros roda.
3. **`fileStatus` nativo** (32 slots) e trajes das IAs fora do Shell. Daí 16.
4. Medir quadro e som com 16; otimizar o `EnemyInfo` se for ele.

Esforço estimado: degrau 1 em um ou dois dias; 2 e 3 juntos em alguns dias; o resto depende do que a medição mostrar.

## Resultado do degrau 1 (8 monstros, mesmo dia)
Feito sem mexer na RAM: `WOTM_MATCH` aceita até 7 IAs (`native/runtime/wotm_overrides.cpp`) e `tools/lote.ps1 -AIs` vai até 7.
O bloco em 0x1B70000 que parecia ocupado é sobra do menu: a área de carga vai até ~0x1F10000 (o Metro com 2 monstros chega a
0x1D0C380). Centro + 8 monstros terminou em 0x1CC3400, com 2,3 MB de folga; luta de 150 s sem travar, todos lutando, 60 fps,
~9 ms de CPU por quadro. Detalhes em `docs/guides/NATIVE.md` ("Até 8 monstros").

### 8 monstros em todas as fases do lote (30 s cada, IA contra IA)
Fim da carga com 8 monstros sorteados (folga até o teto 0x1F10000): Centro 2,6 MB, Vegas 2,1, Aeroporto 7,5, San Francisco 1,8,
Ilha 2,9, Tóquio 5,2, UFO 4,6, Canyon1 8,8, MiniSF 4,6, Vulcão 8,1, TokyoDM 5,9. Todas rodaram sem travar, cair nem ficar lentas.
**Metro (fase 4) não cabe:** a carga foi até 0x23B4300 (35,7 MB), passou dos 32 MB e o jogo congelou na carga (vigia, `g_frame`
205). Só com a RAM de 64 MB (degrau 2). San Francisco e Vegas ficam com ~2 MB: monstros mais pesados (Zorgulon, Raptros, Kineticlops)
podem encostar no teto.
