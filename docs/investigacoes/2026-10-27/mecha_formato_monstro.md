# Mecha como monstro jogável: o que o jogo exige de um NGP/PTR/CHR de monstro (2026-10-27)

Só leitura de código (`src/`, `asm/` lido e não colado) e dos arquivos do disco do dono (`disc/MON`, `disc/RST`, `disc/LVL/CAPITOL.NGP`).
Nada rodado no runtime. Scripts de leitura descartáveis (fora do repo).

## Resumo
- Um NGP de monstro é achado só por **ids**: raiz `GROUP` com id = tipo (`m_typeBits`, ex. 0x1C0), `CHAR_INSTANCE` com `id1` = tipo
  dentro do `ANIM_MGR` do arquivo, anexos com id = tipo + 1..0x1F, localizadores `ATTACHPT` 0x802..0x832. Não há tabela de ossos por id.
- **Obrigatórias:** animação **90** (idle; sem ela `animationInitModifierBlends` desreferencia `ci` nulo no `initAfterDbLoad`) e **120**
  (morte; sem ela `StateDeath::transitionOK` falha, `m_dead` nunca liga e a rodada não acaba). O resto, se faltar, só desliga o estado
  (os `transitionOK` testam `m_anims[id].ci`) ou deixa o monstro parado na pose anterior.
- **Base recomendada: Jelly (tipo 14, 0x1C0).** Pede menos animações (o `JELLY0` tem 19 ids e já luta como custom), não agarra nem é
  agarrado, o empurrão vira recuo (sem as 10+ animações de derrubar/levantar), corrida lenta (100 mph), colisão de monstro grande, e o
  especial (`StateUltraTazer`, 160/161/162 + localizador 0x80B) casa com o laser do Mecha (anims 4/6/5 + localizador 0x27AA).
- **.PTR:** o próprio `CAPITOL.PTR` já lista todos os ponteiros, texIds e TEX0 da fase (mesmo formato do de monstro): o gerador pode
  filtrar os offsets que caem nos trechos copiados e deslocá-los. Limite de 200 000 bytes no arquivo.
- **.CHR:** é escolhido pelo **tipo** (`RST/<nome do tipo>.CHR`), não pelo arquivo do modelo; só referencia animações (socos 10..26) e
  localizadores 0x802/0x803/0x804/0x816..0x818, nunca ossos. Emprestar o de outro monstro funciona se o NGP tiver esses ids (ou se as
  casas de soco ficarem sem animação).

## 1. Estrutura que o código usa

**Carga** (`Shell::LoadMonstersDB`, `src/game/Shell.cpp`): `MON/<prefixo><traje>.NGP` lido cru (`fileReadf`) em `getNextNgpLoadAddr()`,
registrado com `fileAddNgpFile` (máx. 14 slots de `fileStatus`, fase incluída). O prefixo vem de `MonsterLongNames[sel >> 5]`: o arquivo
é sempre o do **tipo** selecionado (por isso um modelo próprio hoje entra pela skin, que serve `MON/JELLY<n>.*` do mod).

**Ligação** (`dbInitDb`, `src/game/db.cpp`): por arquivo `dbsRelocateViaPtrListFile(idx)` (abaixo); depois, para cada raiz com opcode 7
(`ANIM_MGR`) `animationSetGlobal(raiz, idx)` (cria o `AnimPlayer` de cada `CHAR_INSTANCE` e inicializa os `animCtx`; animação marcada
`active` nos dados já começa a tocar); para as outras raízes `dbsTraverse(..., dbProcInteractive)`; nos arquivos > 0
`animationSetInstance(mgr, dup)` grava o `id2` = dup em **todas** as instâncias do `ANIM_MGR`.

**Raízes de um monstro do disco** (ROBOT0, FINAL0, JELLY0, ASSBOSS1: as quatro iguais):
| Raiz | O que é | Quem usa |
|---|---|---|
| 0 `GROUP` id = tipo | o monstro | `dbProcInteractive` → `TheGame::MonsterParse` → `AddMonster(dbGetCSForModel(h), tipo, dup)`: vira o `m_cs`, e o `AddMonster` regrava id/slot no `head` |
| 1 `GROUP` id 0 | anexos (sombra, retículas, estrelas, modelos de projétil) | `MonsterParse` com `id & 0x1F != 0` → `Monster::addAttachment` |
| 2 `COLL_GRID` | — | `hdSaveColGrid` guarda só o primeiro (o da fase): **ignorado** nos monstros |
| 3 `ANIM_MGR` | lista dos `CHAR_INSTANCE` do arquivo | `animationGetHandle` procura aqui |

`dbsTraverse` só chama o callback em nó com id (14 bits altos ≠ 0); `CHAR_INSTANCE` (op 25) não chama, só desce aos filhos (+0x2C, n +0x28).

**Anexos** (`Monster::addAttachment`, tabela de saltos por `id - tipo - 1`; tipo = 0x1C0 nos exemplos):
| id | Grava | Necessário? |
|---|---|---|
| tipo+1 (0x1C1, `SWITCH`) | sombra: `m_shadow` (+0x1A3C) e `psBlockerAdd` (+0x1A74) | **sim**: a IA lê `m_shadow->+0x18` sem testar nulo (`Ai.cpp:22`, `AiSeek.cpp:1139`) |
| tipo+3 | nó em +0x106E8 | ? |
| tipo+5 (0x1C5) | retícula `m_reticleCS` (+0x6BF8) | sim para IA (`AiReflex.cpp:884` lê sem testar) |
| tipo+6 | lista AILOD (+0x1A14, até 9) | não |
| tipo+7, +8 (0x1C7/0x1C8 `OBJECT`) | nós em +0x6860/+0x6864 | ? |
| tipo+9 | estrelas do atordoado (`StateStunned::setStarsCs`) | ? |
| tipo+0xA (0x1CA) | `m_stickyReticleCS` (+0x6BFC) | testa nulo |
| tipo+0xB | +0x6874 | ? |
| tipo+0x1D/+0x1E/+0x1F | só 0x1A0 / 0x80 / 0x160 | — |

Ids 0xFA0..0x11F7 na raiz 1 (ex. 0xFC7 no Jelly, 0xFAD no Cerebulon, 0xFA3/0xFA5 no Robo-47) são modelos de arma (`Weapons::AddWeaponEpNode`).
Atenção: ids 0x27A6..0x27BE em **qualquer** arquivo vão para `finalBoss.addEpNode`, e o `traversalCallback` manda 0x27AA (e 0x80B/0x80E
de um cs cujo epNode é 0x27A6) para `FinalBoss::setLocator`. O gerador tem que **renomear** as peças do Mecha.

**Instância e animações** (`Monster::initAfterDbLoad`, `src/game/MonsterInit.cpp`): `animationGetHandle(&m_anims[i], m_typeBits, m_dupId, i)`
para i = 0..299 (`m_anims` em +0x1CF0, 16 bytes cada: `ci`, `ctl`, `proc`, `animIdx`). A busca (`src/common/animation.cpp`) percorre o
`ANIM_MGR` de cada arquivo, pega o `CHAR_INSTANCE` com `id1 == tipo` e `id2 == dup`, e a animação com `head.id1 == i` (id = 14 bits altos da
1ª palavra da `_animation`). Mais um handle: `+0x28E0` = instância **tipo+5**, animação 0 (a retícula; usada por `StateRobotSpecial` e
`StateCannonHands`). Depois: `animationInitModifierBlends(m_anims[90].ci)` (**sem teste de nulo**), `AnimPappy::init(m_anims[90])`
(canais de id 1..7: movimento de cinema), `AnimBlend` de m_anims[154], [155] (camadas de inclinação, peso 50%) e [156];
`initDynamics` (especial e arma por tipo), `collisInitPoints` (colisão por tipo), `PadFlags::init`, `MonsterTweaks::setActiveMonster` (o .CHR).
`Monster::init` faz `AnimContact::init(m_anims[90])` (canais de contato das mãos/pés) e reseta o personagem.

**Localizadores** (`ATTACHPT`, op 40, só a palavra de cabeçalho com o id; a posição é a matriz corrente da travessia): na travessia de
desenho (`hierAttachPtNode` → `TheGame::traversalCallback`) o nó com id do cs de um monstro (epNode 0x20..0x400) chama
`Monster::handleLocator(id, mat, pos)`, que grava para 0x802..0x832 a matriz em `+0x3180 + (id-0x802)*64` e a posição em
`+0x3DA0 + (id-0x800)*16` (`getLocatorTrans`/`getLocatorMat`); 0x80A/0x807/0x808 também movem o objeto carregado/empalador. Ids
0x834..0x883 são pontos dos arcos elétricos (`particleMonsterArcSetNodePos`). Localizador ausente fica com o valor velho (zero). Quem lê:
| Id | Uso (genérico) |
|---|---|
| 0x802, 0x803 | mãos: socos (config do .CHR), `updateBoundingSphere` (menos no 0x1C0), fúria, `GrappleOHAttack`, especiais |
| 0x804, 0x805 | pés: `updateBoundingSphere`, `AnimContact`, pulo (`MovementStates`) |
| 0x806, 0x807 | `Monster::updatePosition` (matriz) |
| 0x808 | empalar (`doImpaleVolume`), `StateGrappleHook` |
| 0x809 | "peito"/alvo: 36 funções (IA, câmeras, mísseis teleguiados, `EnemyInfo`, arremesso). **Essencial** |
| 0x80A | mão que segura/arremessa o pickup (`throwPickup`, `takeHit`) |
| 0x80B | boca/canhão: projétil padrão, sopro, retícula/lock (`updateLock`, `updateReticle`, `getClosestTargetable`), especiais (`StateUltraTazer`...) |
| 0x80C | cabeça: `updateBoundingSphere`, ícone de vida do HUD, estrelas, insetos |
| 0x80E | `getLookAtTrans` (olhar) |
| 0x816..0x81A | `updateBoundingSphere` (conjunto por tipo), especiais (0x816 `StateRobotSpecial`, 0x816/0x817 `StateCannonHands`/`StateCrowdControl`), correntes do Congar |

**Colisão:** pontos e raio fixos por tipo em `collisInitPoints` (`+0x2B0..`, contagem +0x360, raio +0x360/+0x3A4; 0x1C0 e 0x1E0 têm valores
de monstro grande). Cada monstro do disco tem um `COLL_VOL` (op 10) na raiz 0; o Mecha também. O `COLL_GRID` do arquivo não é usado.
`updateBoundingSphere` monta a esfera dos localizadores do tipo (raio mínimo 10; 50 no 0x1A0).

**Retrato do HUD:** sai do `.RTX` do monstro (hipótese do Goliath: a última entrada, 128×64); o port troca por `"portrait"`/`"portraitTex"`.

**Árvore do Mecha em `CAPITOL.NGP`** (ep `GROUP` 0x27A6 em 0x126A5F0): 43 `SKEL_BONE`, 52 `ANIM_XFORM`, 9 `OBJECT`, 16 `ATTACHPT`
(0x804, 0x805, 0x809, 0x80C, 0x80E, 0x816, 0x817, 0x27AA), `COLL_VOL`, 2 `LIGHT`, e dentro dela as instâncias 0x27A7 (torre: 0, 1, 2, 194;
6 canais) e 0x27A9 (laser: 0, 5 canais). Principal 0x27A6: 14 animações, 219 canais (mesmo número do `FINAL0`). Caixa no idle (anim 0):
585 × 284 × 392 (Robo-47 107 × 74 × 107; Jelly 192 × 177 × 302). Faltam, em relação ao Jelly: 0x808 e 0x80B (o 0x27AA, saída do laser,
pode virar 0x80B). Ids do Mecha pelo `FinalBoss::updateMecha`: 0/1 andar (um para cada lado), 2 parada/virada, 4 preparar o tiro (velocidade
2), 6 laser em laço, 5 fim do tiro, 10 desligar, 9 religar, 3 apanhar, 11 e 12 camadas aditivas (prioridade 999), 65, 194 e 195 sem uso lido
(194 = cinema, 1860 quadros). A torre toca 1 (laço) desde o `initAfter`; ninguém a moveria num `Monster`: marcar a animação como ativa/laço
nos dados, ou o port dirigi-la.

## 2. Animação ausente e quais são necessárias
- `animationGetHandle` sem achar deixa `ci = ctl = 0`. Todas as funções de handle testam `ci`/`ctl` e saem (`animationTransitionInto`,
  `Start`, `SetToPercent`, `IsRunning` = 0, `GetCurrentPercent` = -1, `AnimBlend::rampIn`, `animationGetChannelVal` = 0). **Exceção:**
  `animationInitModifierBlends(m_anims[90].ci)` → nulo = falha na carga. `animationUpdateHandle` também desreferencia `ctl`, mas só é chamado
  pelo mixer com nós reais.
- Efeito de faltar: o estado que testa `m_anims[id].ci` no `transitionOK`/`transitionFeasible` não começa; o que não testa toca nada e o
  monstro segura a pose anterior (o caso do Goliath sem escalar).

| Id | Estado / quem pede | Faltando |
|---|---|---|
| **90** | idle (`StateIdle`, `Monster::init`, `startCinema`, `AnimContact`, `AnimPappy`) | **trava na carga** |
| **120** | `StateDeath::transitionOK` | **não morre** (rodada não acaba) |
| 95, 96, 172 | idle cansado, com especial, segurando grande (`chooseBaseIdle`; cai no 90) | ok |
| 2 (corrida); 1+7 (andar, só se os dois); 5 (camada da corrida); 6/9/8 (pouso, variantes) | `StateRun` | sem 2 não anda |
| 30; 31..38, 182 | `StateJump`/`StateFly` | sem 30 não pula |
| 60; 61..63; 64 | `StateBlock` (`chooseBlock`), `StateCounter` | sem bloqueio |
| 10..26 | `StatePunch` (testa a 10; as casas vêm do .CHR) | sem socos |
| 67, 68, 69; 70 | `StateRecoil` (`getRecoilAnim`; `transitionOK` pede 70) | não reage ao golpe |
| 75, 74, 76 | golpe forte, choque, contra-atacado | não reage |
| 87, 88; 82, 250..256 | `StateThrowBack` (derrubado, levantar), `StateGetupAttack` | não cai (nos tipos comuns o `knockBack` passa por aqui) |
| 92; 93 | `StateTaunt`, `StateVictory::transitionOK` | sem provocar/vitória |
| 160, 161, 162 | especial de cada tipo (`StateUltraTazer`, `StateRobotSpecial`, `StateCannonHands`...) | sem especial |
| 163..165, 192/193 | `StateCrowdControl`/`StateTopSpin` | sem giro |
| 41/48, 49, 50/52, 54, 55/56 | pegar, pegar no ar, arremessar, dardo, arremesso de agarrão | sem objetos |
| 170, 171, 185; 174, 186, 280 | agarrar; ser agarrado | sem agarrão |
| 100..106 | `StateClimb` | não escala |
| 142..144 | `StateImpaled` | não é empalado |
| 240, 244, 245, 261..263, 264/265, 266/267 | pisão, arma, escudo, butt slam/`BatSwipe`, dash, investida | sem o golpe |
| 140 | `launchDefaultProjectile` | — |
| 154, 155, 156 | camadas de inclinação | ok |
| 0x82, 0xCE, 0xCF, 0xD0 | sons de cinema (`MonsterSound`) | ok |

Mínimo jogável (base Jelly): 90, 120, 2 (e 1+7 para andar), 30 (com 33..35 do pouso que o Jelly tem), 67..70, 92, 160..162. O `JELLY0`
tem exatamente 0, 2, 8, 30, 33, 34, 35, 39, 67, 68, 69, 70, 90, 120, 160, 161, 162, 194, 195. Sugestão de mapa do Mecha: 90 ← 2 ou 65 (a
conferir qual parece parado), 2/1/7 ← 0/1, 67..70 ← 3, 160/161/162 ← 4/6/5, 120 ← 10, 92 ← 9; 11/12 renumerar para ids livres (e não
usar 10..26, que ligam os socos do .CHR). Trocar o id = reescrever os 14 bits altos da 1ª palavra de cada `_animation`.

## 3. Diferenças por tipo (o que cada base pede a mais)
- **Robo-47 (1, 0x40):** código próprio pequeno (corrida 270 mph; `StateRobotSpecial` com 160..162, localizador 0x816 e a instância 0x45
  da retícula; um caso de IA em `AiReflex.cpp:2311`; um da aventura em `TheGame.cpp:381`). Mas é um monstro comum: espera o conjunto todo
  (161 animações no `ROBOT0`), agarra e é agarrado, cai/levanta pelo `StateThrowBack`. Sem as animações, os estados só não acontecem.
- **Cerebulon (15, 0x1E0):** `knockBack` vira `StateRecoil` (sem cair), `StateThrowBack` com corpo mais alto, não pega o item de especial,
  `SetAIMonster` o tratava como chefe (o port corrige), IA própria em `AiReflex`. O `FINAL0` tem o conjunto completo (108 animações).
- **Jelly (14, 0x1C0):** `knockBack` → `StateRecoil` (sem `ThrowBack`), não agarra nem é agarrado (`Grapple.cpp:188`, `HomingBug`), sem
  fúria (`Monster.cpp:485`), corrida 100 mph, pulo/queda com a configuração pesada (`MovementStates.cpp:1495, 2125`), colisão grande,
  esfera sem as mãos (não precisa de 0x802/0x803), `SetAIMonster` corrigido pelo port, especial `StateUltraTazer` já adaptado pelo port
  para o jogador. **É a base que exige menos.**

## 4. `.PTR` e `.CHR`
**`.PTR`** (`dbsRelocateViaPtrListFile`, `src/common/dbs.cpp`; lido para a pilha de memória, aviso acima de 0x30D40 bytes): três listas
`{u32 n; u32 off[n]}` em sequência (o resto do arquivo é zero), offsets em bytes a partir do começo do NGP:
1. palavras que são ponteiros: `+= endereço_de_carga - 0xA00000` (as palavras 1..N do cabeçalho do NGP são somadas sem estar na lista);
2. texIds de 16 bits: `+= getMaxTexId(idx-1)` (0 fica 0);
3. TEX0 (doubleword): o campo de 14 bits nos bits 37..50 (CBP no layout do GS) `+= getMaxTexAddr(idx-1)`; se o PSM (bits 20..25) é
   0x1B/0x24/0x2C (T8H/T4HL/T4HH), o TBP0 (bits 0..13) `+= getMaxResAddr(idx-1)`.

No `ROBOT0.PTR`: 21 186 / 96 / 96. **O `CAPITOL.PTR` tem o mesmo formato** (41 951 ponteiros, 3 116 texIds, 3 116 TEX0; o jogo usa só a 3ª
no arquivo zero), e os 41 951 offsets apontam todos para endereços válidos da imagem. Geração: copiar os trechos do Mecha para um NGP novo
pré-ligado em 0xA00000, remapear cada ponteiro (os offsets da lista 1 do `CAPITOL.PTR` dentro dos trechos dizem quais palavras são
ponteiros, inclusive dentro de pacotes DMA, curvas e `animCtx`), e escrever as três listas com os offsets novos. texIds e TEX0 do NGP novo
ficam relativos ao `.TEX`/`.RTX` gerados (nos monstros do disco o deslocamento é zero: `MODELOS_3D.md`).

**`.CHR`** (`RST/<tipo>.CHR`): `charreset::setCharacter` monta o nome pelo **tipo** (tabela `null, monkey, robot, ... jelly, final`), então um
custom de base Jelly lê o `JELLY.CHR` (trocá-lo no mod muda o Jelly também, a não ser que o port desvie por custom). Conteúdo
(`tools/chr_dump.py --keys`): 38 chaves iguais em ROBOT/FINAL (JELLY parecido): 0xFA1 padrões (raio do punho), 0xFCC/0xFD8 socos,
0xFA4/0xFAC/0xFD7/0xFE3 golpes com objeto, 0xFC6 câmera (0xA0 por POV em `Monster+0x6CF0`), e o resto parâmetros de estados. Nenhuma
esfera por osso e nenhum id de osso. Referências a ids: só animações das casas de soco (ROBOT 10..20, FINAL 10..26, JELLY 10..16 e 18..21:
nenhuma existe no `JELLY0`, então o Jelly não soca pelo .CHR) e localizadores (0x802/0x803/0x816/0x817/0x818/0x819 em 0xFCC/0xFE3,
0x802/0x803/0x804/0x817 em 0xFA2/0xFA6). Dá para emprestar: as casas sem animação ficam vazias.

## Dúvidas abertas
- Os anexos tipo+3, +7, +8, +9, +0xB: o que são e se faltar trava algo (ler quem usa +0x106E8, +0x6860, +0x6864, +0x6874).
- Se o `COLL_VOL` da raiz 0 é usado no teste de golpes contra o monstro (hd) ou só os pontos do `collisInitPoints`.
- Fechamento dos trechos a copiar: dados compartilhados com o resto da fase (curvas, contextos de textura, `animPktPtr`) e o tamanho final.
- Texturas do Mecha no `CAPITOL.TEX/.RTX`: extrair e renumerar (paletas, CBP); textura da sombra/retícula vinda do `JELLY0`.
- Qual animação do Mecha serve de idle (2 tem 300 quadros, 65 tem 71) e se as camadas 11/12 precisam tocar.
- A torre (0x27A7) e o laser (0x27A9) sem o `FinalBoss`: quem os anima numa luta comum.
