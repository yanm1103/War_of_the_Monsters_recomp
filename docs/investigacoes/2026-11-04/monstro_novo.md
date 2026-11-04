# Monstro novo por mod: o que falta (levantamento, 2026-11-04)

TODO item 8, último ponto: "monstros com arquivos próprios, com um tipo livre (6, 12 ou 16) ou remapeando um tipo; modelo novo depende de o `.NGP` voltar pelo kit".
Foi só leitura. As fontes foram `src/`, `include/`, `native/`, `tools/`, o `asm/` local (lido, nada copiado) e os 44 `.NGP` de `disc/MON`. Também foram usadas as investigações apagadas
`0874ef3^:docs/investigacoes/2026-10-27/mecha_formato_monstro.md` e `18bb1d2:docs/investigacoes/2026-10-23/goliath_codigo.md`.

## Resumo
- **O tipo livre não compensa.** Quem define o tipo é o `.NGP` (id da raiz), e não a seleção. Num tipo 6 ou 12 o monstro roda, mas fica genérico: sem especial, mudo (os `MonsterSound::*` não têm caso para eles), com hitbox e velocidades padrão. O tipo 16 nem carrega: o Shell recusa `sel>>5 >= 0x10`. Os `SPIDER.CHR` e `OGRE.CHR` do disco usam a numeração antiga de chaves e não servem.
- **Melhor caminho: remapear por instância.** O custom continua num tipo-base, que dá som, molas, hitbox e IA coerentes, e traz os próprios arquivos. O modelo já vem por `files`; faltam `.CHR`, `.AIC` e banco de som por instância.
- **O custo grande é o kit escrever `.NGP`.** Isso se faz em três níveis:
  - L1: malha nova no esqueleto de um monstro do jogo;
  - L2: animações novas nesse esqueleto;
  - L3: esqueleto novo.
- **Não há animação pareada entre esqueletos.** No agarrão, cada monstro toca as suas, e a vítima é presa pelos localizadores (0x806 da vítima no 0x807 de quem segura). Um esqueleto novo não quebra os golpes dos outros.

## 1. O que o jogo procura no modelo
Tudo é achado **por id** (os 14 bits altos do cabeçalho do nó). Não existe tabela de ossos.

- **Raiz 0:**
  - `GROUP` com id igual ao tipo (`índice << 5`) e a flag 0x2 (colisão);
  - `CHAR_INSTANCE` com `id1` igual ao tipo, listada no `ANIM_MGR`.
  - `TheGame::MonsterParse` (`src/game/TheGame.cpp:241-293`) tira o tipo do id e o grava em `Monster::m_typeBits`.
  - O `m_monsterSel` só escolhe o nome do arquivo; o traje não aparece em id nenhum.
- **Raiz 1, anexos `tipo + n`:**

  | Anexo | O que é | Se faltar |
  |---|---|---|
  | +1 | sombra (`m_shadow`) | a IA lê sem testar nulo (`Ai.cpp:22`, `AiSeek.cpp:1139`): obrigatório |
  | +5 | retícula, com a animação 0 dela | `AiReflex.cpp:884` lê sem testar: obrigatório |
  | +6 | LOD da IA | |
  | +7/+8 | objetos do HUD | |
  | +9 | estrelas do atordoado | |
  | +0xA | retícula fixa | |

  - O JELLY0 e o FINAL0 trazem anexos com ids do Togera: quem parte de um molde tem que renumerar a raiz 1 inteira.
- **Localizadores (ATTACHPT, 0x802..0x832, `Monster::handleLocator`, `Monster.cpp:1816`).**
  - Mãos e pés: 0x802..0x805 (socos pelo `.CHR`, esfera, pulo).
  - Peito e alvo: 0x809, usado em cerca de 36 funções de IA, câmera e míssil.
  - Boca e canhão: 0x80B. Cabeça: 0x80C. Olhar: 0x80E.
  - Agarrão: 0x806/0x807. Empalar: 0x808. Objeto na mão: 0x80A.
  - **Localizador ausente não trava: fica em zero.** A IA e os golpes passam a mirar a origem do mundo, sem nenhum aviso.
- **Canais com nome (`AnimChannelIdMap`).** Nenhum é obrigatório, porque o que falta se desliga sozinho:
  - 1..7: deslocamento da raiz em cinema;
  - 0x802..0x817: contato de mãos e pés, que dá passos, poeira e água.
  
  O resto dos canais é anônimo. Nenhum código do jogo indexa canal ou osso por número fixo, com uma exceção: o `BoneSpring` do Congar usa índices fixos de `atMat` (`MonsterInit.cpp:318`). Por isso o Congar é base ruim para esqueleto novo.
- **Colisão:**
  - Monstro contra mundo e contra monstro: pontos e raio fixos por tipo, em `collisInitPoints` (`src/game/collision.cpp`), e não do modelo.
  - Golpes e projéteis contra o corpo: o `COLL_VOL` (op 10) que o modelo traz sob o osso raiz.
  - Um monstro de tamanho diferente pede o próprio `COLL_VOL`, um gancho em `collisInitPoints` e o `.CHR` ajustado (câmera, alcance dos socos).
- **COLL_GRID do monstro:** ignorado (vale o da fase).

### Animações
Ids distintos por monstro: de 140 (ALIEN0) a 169 (MONKEY0) nos jogáveis; ASSBOSS1 tem 70, JELLY0 19 e FINAL0 104. São 21 a 59 `SKEL_BONE` e 103 a 297 canais. Os 10 jogáveis têm 125 ids em comum.

- **Obrigatórias:**
  - **90** (idle): `initAfterDbLoad` a usa sem testar nulo e trava na carga (`MonsterInit.cpp:297-300`);
  - **120** (morte): sem ela, a rodada não acaba.
- **Mínimo para lutar numa base comum:** 2 (corrida), 1 e 7 (andar), 30 e 33..35 (pulo), as casas de soco do `.CHR`, 60 (bloqueio), 67..70 (reações), 87 e 88 com 250..256 (derrubada), 92 (provocar) e 160..162 (especial).
- **Opcionais:**
  - agarrão: 170 para agarrar; 174 e 280..283 para ser agarrado;
  - escalada (100..106), objetos (41..56) e camadas (154..156).
- **Quando falta uma, o estado não começa** (`transitionOK` testa `m_anims[id].ci`), ou o monstro segura a pose. É o que já se vê no Goliath.

## 2. Tipo livre (6, 12, 16) x remapeamento
O que muda por tipo no jogo:
- **Nome de arquivo.** `MON/`, `RST/<nome>.CHR` (`charreset::setCharacter`) e `<nome><dif>.aic` (`AiReset::setAi`) saem de `MonsterLongNames`. Os dados vão para a instância: o `readKey` grava no Monster que pediu.
- **Ramos fixos com caso padrão** (o 6, o 12 e o 16 recebem valores genéricos):
  - especial: o `initDynamics` grava o ponteiro do estado em +0x7980, e no 6, 12 e 16 fica nulo;
  - `collisInitPoints`, `updateBoundingSphere`, velocidades (`StateRun::init`, `MovementStates.cpp:2605`), câmera e efeito do soco.
- **Som** (`src/game/Sound.cpp:341-372` e uns 25 `MonsterSound::play*`): sem banco e sem caso para 0xC0, 0x180 e 0x200, então o monstro fica mudo.
- **Carga do tipo 16:** `LoadResTexture`, `LoadTexture` e `LoadLevelFiles` recusam `sel>>5 >= 0x10` (`Shell.cpp:1262-1453`).
- **Sobras sem uso:**
  - `updateOgreSound` (stub);
  - `updateSpiderMagnoSpecial`, sem quem a chame;
  - a classe `OgreMace`, que virou a maça do Ultra-V;
  - `MOV/SPIDER.PSS` e `OGRE.PSS` com tamanho de placeholder.
- **Lado nativo:**
  - a `WOTM_MATCH` recusa os tipos 6, 12 e 16 (`wotm_overrides.cpp:2809`);
  - `kCustomBases` aceita 13..15;
  - o launcher fixa os 10.

**(A) Tipo livre.** Só o 6 e o 12 são usáveis. O monstro sai sem especial, mudo e genérico, e cada lacuna vira uma investigação como as do Goliath. Esforço alto.

**(B) Remapear por instância.** O tipo-base fica, e o custom troca os arquivos dele:
1. `.CHR` próprio: um gancho no `charreset::setCharacter` troca o nome pelo do custom (`customOf(m)`). Pequeno.
2. `.AIC` próprio: o mesmo esquema, no `AiReset::setAi`. Pequeno.
3. Banco de som próprio, com o mesmo índice de sons do banco-base: o motor nativo carrega um segundo banco e troca o handle `gSoundBanks[base-1]` (e o `_ls`) em volta dos `MonsterSound::*` daquela instância. Médio.
4. Opcional, `"special"` no `monster.json`: gravar +0x7980 depois do `initDynamics`. Cuidado com os testes de tipo dentro dos estados (o FireBreath testa 0x120, a OgreMace testa 0x80).

**Dois customs do mesmo tipo-base na mesma luta:**
- o modelo já funciona (trajes diferentes, `dupId` próprio);
- `.CHR` e `.AIC` se resolvem com 1 e 2;
- o banco de som, com 3;
- o nome de vitória é um só por tipo, um limite pequeno que fica.

## 3. O kit escrever `.NGP`
**Desenho e limites.**
- O desenho nativo é genérico: `decodeObject` (`native/wotm_native.hpp:281-466`) lê da RAM qualquer fluxo VIF bem formado.
- Do jogo recompilado, o nativo usa só a paleta de ossos, o `atMat`, os localizadores e o LOD. O `pktAddVu1ObjAsm` não lê o pacote.
- Um stub bastaria para o `wotm.exe`, mas quebraria a checagem VU1 x nativo, `ngp.py`/`pose.py`/`monglb.py`, a prévia 3D da seleção e dos saves, o viewer web e o PS2/PCSX2.
- Escrever o formato real custa umas 300 linhas espelhando os dois leitores. A checagem VU1 vira o oráculo do escritor.

**Medidas nos 44 `.NGP` de `disc/MON`:**

| O quê | Medida |
|---|---|
| Posições por lote | ≤ 190 |
| Vértices por seção de tira | ≤ 44 |
| Ossos por objeto skinned (um bloco de paleta) | ≤ 32 |
| Paleta no nativo | até 64 saídas (`wotm_native.hpp:4516`; `pose.py:74` e `wotm_model.hpp:539` também, fácil de subir) |
| Texturas por objeto | ≤ 5 |
| `SKEL_BONE` pendentes na travessia | ≤ 5 (`src/common/hier.cpp:147`) |
| Pilha de matrizes | 160 |

- Monstros de bloco de paleta único: Congar, Robo-47, Togera, Ultra-V, Preytor, Kineticlops, Magmo, Agamo e Goliath.
- Com dois blocos: ALIEN, DRAGON, JELLY e FINAL.

**O que já existe e o que falta:**
- **Entendido e documentado:** nós, curvas, VIF, skinning e PTR (`docs/FORMATS.md`, `docs/guides/MODELS_3D.md`, `include/hieri_types.h`).
- **Lido pelas ferramentas, mas nunca escrito:**
  - pacotes VIF e cadeias `animPktPtr` com blocos de paleta;
  - `animMatrixPtr`;
  - arrays por instância (`val`, `atMat`, `animCtx`, `prevKey`, `angularChannelBits` com offset s16);
  - curvas;
  - `.TEX`/`.RTX` de tamanho novo.
  
  O `mechamon.py` só monta copiando faixas de outros `.NGP`.
- **Desconhecido:**
  - 0x40 bytes zerados antes do primeiro sub-pacote skinned e a segunda palavra da tag (`00000089`);
  - a paleta por sub-pacote do Dragon;
  - os anexos +3 e +0xB.
  
  Contorno nos dois primeiros: copiar do molde.

**Níveis:**

| Nível | O que é | Tamanho |
|---|---|---|
| L1 | malha nova (pesos, UV, cor) no esqueleto de um monstro de bloco único | M (~1 a 2 semanas) |
| L2 | L1 + animações novas no mesmo esqueleto | M a L (+1 a 2 semanas) |
| L3 | esqueleto e animações novos | L (3 a 6 semanas) |

- **L1.**
  - O `.NGP` base é o molde. No fim entram os pacotes novos e as cadeias `REF paleta + REF sub-pacotes`.
  - Os OBJECTs são regravados: `polyPkt`, `numVerts`, esfera, texIds e TEX0. Os ponteiros novos entram na lista 1 do `.PTR`.
  - As texturas usam os texIds do traje base com PNG de mod. As partes rígidas e o LOD são mantidos ou apontados para a malha nova.
  - Fluxo no Blender:
    1. `monglb.py` exporta (joints = `skelOutputMatIdx`);
    2. o modder modela e repesa sem renomear os ossos;
    3. `modkit` importa;
    4. sai uma skin ou os `files` de um custom.
  
  Já entrega um personagem com cara nova e todos os golpes do tipo.
- **L2.**
  - Escrever curvas é fácil (`kPwl` float). O difícil é inverter a pose: o canal vive no ANIM_XFORM, com `R = pre⁻¹·L·post⁻¹` decomposto em PRH, e os ângulos precisam ser desenrolados.
  - Pede um modo "para edição" no `monglb.py` (um nó por ANIM_XFORM).
  - Trocar uma animação existente é barato. Acrescentar uma exige mover o `_animCharacter` (com `animations[]` embutido) e o `animCtx` para o fim.
- **L3.** Hierarquia inteira:
  - partição em blocos de ≤ 32 ossos;
  - mapa de canais;
  - localizadores, sombra, retícula e `COLL_VOL`;
  - gancho de colisão por tipo.
  
  Um rig do Blender com nomes livres pede mapear localizadores e canais especiais.

**Validação do escritor:**
1. Reemissão `encode(decode(pkt)) == pkt` nos 44 `.NGP`, decodificando em nível de tira.
2. Volta semântica: LIZARD0 → `.glb` → importa → compara triângulos e o idle (90) vértice a vértice com `pose.py`.
3. `monview.exe`.
4. `ps2EntryRunner` com `WOTM_VU1CHECK` numa luta com a skin.
5. Luta no `wotm.exe` e lote IA x IA.

## Ordem proposta
1. `.CHR` e `.AIC` por instância no custom (campos no `monster.json`). Pequeno, e já dá números e golpes próprios sobre um modelo de `files`.
2. Kit L1 (malha nova no esqueleto de um monstro do jogo), começando pela reemissão byte a byte.
3. Banco de som por instância.
4. Kit L2 (trocar animações existentes, depois acrescentar).
5. `"special"` no `monster.json`.
6. L3 (esqueleto novo), se ainda fizer falta depois de 1 a 5.
