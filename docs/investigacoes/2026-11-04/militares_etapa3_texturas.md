# Exército, etapa 3: texturas dos veículos de Vegas em outras fases (2026-11-04)

Pergunta: para levar tanque (nó 0x1392), caminhão de míssil (0x1391) e helicóptero militar (0x1394) de VEGAS a outras fases, o
que é preciso do lado das texturas (pixels e paletas), no `wotm.exe` (desenho nativo) e no `ps2EntryRunner` (GS emulado)? As malhas
são de outra investigação. Contexto: `militares_spawn.md` (2025-10-25), `militares_ia.md`, `militares_caos_modos.md`.

## Resumo
- **Achado principal: na maioria das fases as texturas já estão lá.** As cinco texturas dos veículos (77, 78, 79, 87 e 88 do
  `VEGAS.TEX`) fazem parte de um conjunto comum (veículos e armas) que se repete, byte a byte, nos `.TEX` de outras fases, com a
  paleta idêntica no `.RTX`. Só muda o número (texId e DBP da paleta). A textura 79 (atlas do tanque e do caminhão) está nas 19
  fases; a 87 em 16; as três do helicóptero em 11 (a 88 falta também em TOKYODM).
- Para tanque e caminhão, **nas 13 fases com `.FOP` basta remapear** texId e CBP nos objetos copiados: custo zero de RAM e de VRAM,
  vale nos dois executáveis, sem código de textura.
- O helicóptero precisa de **injeção** em 8 fases: BIGSHOT, CANYON1, CRUSH, DBALL, TRAINING, VOLCANO e UFO (77, 78 e 88) e TOKYODM
  (só a 88). São justamente as seis fases sem `.FOP`, onde o dono decidiu soltar só helicópteros. São 7040 B de pacotes e 3 paletas
  (1200 B no `.RTX`, 6 blocos de VRAM).
- Mipmaps: nenhuma das cinco tem (`numMips` 0, `TEX1.MXL` 0), nem as cópias delas nas outras fases. CLUT: CSM1, CSA 0, CPSM CT32.

## 1. Como o jogo carrega as texturas
Ordem em `Shell::LoadLevelFiles` (0x1AB1A8): NGPs (fase, depois monstros) → `.RTX` (`LoadResTexture`, termina em `texmResInit`) →
`.TEX` (`LoadTexture`, termina em `texmInit`) → `dbsRelocateFileZero` → `dbInitDb`. Fonte: `src/common/texm.cpp`, `src/common/file.cpp`,
`src/common/dbs.cpp`, `src/game/db.cpp`.

| Peça | Endereço (`wotm_syms.h`) | O que é |
|---|---|---|
| `texInfo[1000]` | 0x50F100, 16 B | `+0` ponteiro do pacote (`TexPkt` na RAM); `+4` bits: `tbp` 0..13 (VRAM atual), `tbw` 14..19, `bin` 20..24, `numMips` 25..27, `resident` bit 31; `+8` slot no bin |
| `texBinE[bin][300]` | 0x512F80 | slots do cache de VRAM: `lastUsed`, `texId`, `vramBlk` (LRU) |
| `texAllBins[15]` | 0x51C580 | `{count, size}` por bin (tamanho de bloco em bytes) |
| `fileStatus` | 0x445390 | `texAddr[14]` +0, `texLoaded` +0x38, `ngpAddr` +0x3C, `ngpLoaded` +0x74, `resAddr` +0x78, `resLoaded` +0xB0, `names[14][9]` +0xB1, `maxTexId[14]` +0x130, `maxTexAddr[14]` +0x14C, `maxResAddr[14]` +0x168 |
| `tempVramTexAddr` | 0x6F8818 | base dos `.RTX` na VRAM (palavras); `>> 6` = bloco base das paletas da fase |
| `startVramAddr` / `nextVramAddr` / `lastVramTexAddr` | 0x6F87F4 / 0x6F87F0 / 0x6F8800 | fim da área residente (= início do cache), cursor, teto do cache |
| `texmResInit` / `texmInit` | 0x205358 / 0x2052A0 | fim da carga dos `.RTX` / dos `.TEX` |
| `texmSetupTexture` | 0x2059D0 | preenche `texInfo[id]` e monta o `sceGsLoadImage` em `pacote + 0x20` |
| `texmGetTextureAddrs` | 0x206F30 | no desenho: põe a textura no cache (upload) e grava o TBP0 no TEX0 do objeto |

- **Pacotes de pixels:** cada `.TEX` fica inteiro na RAM onde foi lido (`fileStatus.texAddr[i]`; com `WOTM_HIGHRAM`, acima de 32 MB) e
  `texInfo[texId].pkt` aponta para o pacote dentro dele: cabeçalho de 16 B, tag DMA em +0x10, `sceGsLoadImage` em +0x20 (BITBLTBUF em
  +0x30, TRXREG em +0x50), pixels lineares de +0x80 em diante.
- **Cache de VRAM (só pesa no GS):** `texmAllocateBinSpace` divide `[startVramAddr, lastVramTexAddr)` em bins por tamanho de bloco
  (lista `{count, blkSize}` do cabeçalho do `.TEX` da fase, com a dos monstros mesclada por `texmRelocate`). No desenho, o
  `texmGetTextureAddrs` (e o `hierTraverseAsm`, que lê `texInfo`/`texBinE`/`texBins` direto) vê `resident`; se for 0, o
  `texmFindVramSpace` tira o slot menos usado do bin `texInfo[id].bin`, o upload entra no pacote do quadro e o TBP0 do TEX0 do objeto
  é sobrescrito na RAM. O TBP0 do arquivo é 0 e não importa.
- **Paletas:** moram nos `.RTX` (CT32 16x16 = 256 cores, 8x2 = 16 cores) e ficam residentes: `texmResRelocate` soma ao DBP de cada
  entrada a base (`tempVramTexAddr >> 6` na fase; `maxTexAddr[i-1]` nos monstros) e o `texmLoadResTexture` sobe tudo uma vez. O CBP
  do TEX0 de um objeto da fase é o DBP relativo do `.RTX` mais `vram / 64`, somado pelo `dbsRelocateFileZero` (terceira lista do
  `.PTR`); nos monstros, `dbsRelocateViaPtrListFile` soma `maxTexAddr[i-1]`.
- **texIds são globais e cumulativos.** O `.TEX` e o `.RTX` de um arquivo dividem a numeração (as entradas "altas" do `.RTX`
  usam números que o `.TEX` pula: 68 e 71 em VEGAS, 3 no LIZARD0). A fase (arquivo 0) fica com os números do disco; cada monstro
  `i` soma `maxTexId[i-1]` (o `texmRelocate` nos pacotes, o `dbsRelocateViaPtrListFile` nos `texId[5]` do NGP, que roda dentro do
  `dbInitDb`, depois das texturas).
- **Teto:** 1000 entradas; o 999 é do filme no nativo (`movieTexId`, `wotm_fmv.hpp`). Uso medido: fase até 328 (VEGAS), monstro de 10
  a 18 cada. Com 8 monstros, pior caso perto de 480; sobram mais de 500 números livres em qualquer fase.

| Fase | maior texId | Fase | maior texId | Fase | maior texId |
|---|---|---|---|---|---|
| 3-MILE | 299 | CRUSH | 163 | SANFRAN | 315 |
| AIRPORT | 183 | DBALL | 111 | TOKYO | 274 |
| BIGSHOT | 123 | ISLAND | 270 | TOKYODM | 256 |
| CANYON1 | 122 | METRO | 327 | TRAINING | 79 |
| CANYON2 | 133 | MINISF | 260 | UFO | 117 |
| CAPITOL | 216 | | | VEGAS | 328 |
| CENTRAL | 311 | | | VOLCANO | 137 |

## 2. Como o desenho nativo resolve pixels e paleta
- `Scene::decodeTex` → `decodeUploadTex` (`native/wotm_native.hpp`): pixels em `texInfo[texId[slot]].pkt + 0x80`, com largura,
  altura e psm lidos do `sceGsLoadImage` (`UploadHdr`: +0x50, +0x54, +0x37). Paleta: `rtxPal[TEX0.CBP]`, o mapa que o
  `Scene::loadResources` monta com os `.RTX` do disco, chave = CBP absoluto (fase: `tempVramTexAddr >> 6` + DBP; arquivo `i`:
  `maxTexAddr[i-1]` + DBP). A troca CSM1 dos bits 3 e 4 entra no `fillPal` para T8. Chave do cache de GPU: texId, CBP, CSA, CPSM e TCC.
- Sem paleta no `rtxPal`, a textura T4/T8 do objeto não sai (cai na cor de vértice).
- Um objeto copiado de Vegas desenha certo se o `texId[slot]` apontar para uma entrada de `texInfo` com os pixels certos e o CBP do
  TEX0 for chave de `rtxPal` com a paleta certa. Por isso:
  - **remapeamento** (fases onde a textura já existe): texId da fase e CBP = `tempVramTexAddr >> 6` + DBP da fase. Nada mais; o
    `loadResources` já carregou a paleta (ele carrega todas as paletas do `.RTX`, usadas ou não).
  - **injeção:** (a) entrada nova em `texInfo` com o pacote copiado do `VEGAS.TEX` (pixels a partir de +0x80 e o `sceGsLoadImage`
    montado, o que o `texmSetupTexture` faz); (b) paleta registrada num CBP livre. Converter a textura para CT32 dispensaria a
    paleta no nativo, mas quadruplica os pixels (79 viraria 64 KB) e não ajuda o GS; registrar a paleta é mais barato.
- **O que skins/mods já oferecem:** nada de "textura extra". `modTexture` troca os pixels de uma textura existente por PNG (acha o
  arquivo pelo intervalo `fileStatus.texAddr[i]` e o nome `names[i]`); as skins trocam arquivos inteiros pelo registro de
  `mods/` (`wotm_mods.hpp`, arquivos do host). O `loadResources` só conhece os `.RTX` listados em `fileStatus` e limpa o `rtxPal` a
  cada mudança de fase, então a paleta injetada precisa de um caminho próprio para entrar (abaixo).
- Efeito colateral do remapeamento: um PNG de mod da textura da fase (ex.: `metro_t0080.png`) também pinta o tanque naquela fase.

## 3. O que o `ps2EntryRunner` (GS emulado) exige a mais
- **Remapeamento:** nada. A textura da fase já tem `texInfo` com bin válido e a paleta já está residente na VRAM.
- **Injeção:** `texInfo[id].bin` precisa ser o bin certo **nos dois executáveis** (o `hierTraverseAsm` e o `texmGetTextureAddrs`
  rodam também no `wotm.exe` e escrevem em `texBinE[bin]`). Bin = índice em `texAllBins` cujo `size` é o tamanho de bloco da
  textura em Vegas: 77 → 1024, 78 → 2048, 88 → 4096; os três existem em todas as fases. Os pixels entram pelo cache LRU sem espaço
  reservado.
- A paleta precisa de VRAM residente: 6 blocos (77 e 78: 1 bloco cada; 88: 4). Proposta: na entrada do `texmInit` (a área
  residente já está fechada e os bins ainda não foram montados), pôr as paletas em `startVramAddr >> 6`, subir com
  `sceGsSetDefLoadImage` + `sceGsExecLoadImage` (chamada ao guest, como o `texmLoadResTexture` faz) e avançar `startVramAddr` e
  `nextVramAddr` em 8 blocos (2 KB, alinhado como o jogo alinha). O cache encolhe 2 KB de ~2,1 MB. No `wotm.exe` o upload é inócuo.
- Custo: ~40 linhas além do caminho nativo. Vale fazer junto: sem o bump, o CBP das paletas cairia em VRAM do cache e o
  `ps2EntryRunner` mostraria o helicóptero com cores de lixo (só ele; o jogo não quebra).

## 4. Medidas no disco (VEGAS)
Script de rascunho sobre `tools/ngp.py` e `tools/tex.py` (descida por todos os filhos de cada tipo de nó, todos os LODs).

| Veículo | Nós / objetos | texId (TEX0) |
|---|---|---|
| Caminhão 0x1391 (4 instâncias) | 18 / 3 | 79 (CBP rel. 267), 87 (287) |
| Tanque 0x1392 (8 instâncias) | 20 / 3 | 79 (267), 87 (287) |
| Helicóptero 0x1394 (5 instâncias) | 40 / 11 | 77 (265), 78 (266), 88 (288) |

Todas as instâncias de um tipo têm os mesmos pares. TEX0: TBP0 0, TCC 1, CSM 0, CSA 0, CPSM 0; TEX1 igual em todas (MXL 0).

| texId | PSM | Tamanho | Pacote (B) | Pixels (B) | Bloco do bin | Paleta (DBP rel.) | Paleta no `.RTX` (B) |
|---|---|---|---|---|---|---|---|
| 77 | T4 | 32x32 | 640 | 512 | 1024 | 265, 8x2 | 80 |
| 78 | T4 | 64x64 | 2176 | 2048 | 2048 | 266, 8x2 | 80 |
| 79 | T8 | 128x128 | 16512 | 16384 | 16384 | 267, 16x16 | 1040 |
| 87 | T4 | 64x64 | 2176 | 2048 | 2048 | 287, 8x2 | 80 |
| 88 | T8 | 64x64 | 4224 | 4096 | 4096 | 288, 16x16 | 1040 |

Total: 25 728 B de pacotes e 2320 B de paletas. Em Vegas as cinco também servem a outros objetos (outros pickups, as armas 0xFBA
e 0xFC1 com a 79, as cópias das cenas 0x21FC/0x21FE): é um atlas comum, não textura exclusiva dos veículos.

### Onde cada textura já existe (pixels e paleta idênticos)
texId da fase / DBP relativo da paleta no `.RTX` da fase; "—" = não existe (injetar). Comparação por md5 dos pixels (de +0x80 ao fim
do pacote) e dos bytes da paleta.

| Fase | `.FOP` | 79 (tanque, caminhão) | 87 (tanque, caminhão) | 77 (heli) | 78 (heli) | 88 (heli) |
|---|---|---|---|---|---|---|
| 3-MILE | sim | 80 / 271 | 88 / 291 | 78 / 269 | 79 / 270 | 91 / 294 |
| AIRPORT | sim | 79 / 267 | 88 / 291 | 77 / 265 | 78 / 266 | 89 / 292 |
| BIGSHOT | não | 71 / 256 | — | — | — | — |
| CANYON1 | não | 68 / 256 | — | — | — | — |
| CANYON2 | sim | 77 / 265 | 85 / 284 | 75 / 263 | 76 / 264 | 86 / 286 |
| CAPITOL | sim | 79 / 267 | 87 / 287 | 77 / 265 | 78 / 266 | 88 / 288 |
| CENTRAL | sim | 81 / 275 | 89 / 295 | 79 / 273 | 80 / 274 | 91 / 300 |
| CRUSH | não | 73 / 258 | 76 / 267 | — | — | — |
| DBALL | não | 71 / 256 | 73 / 261 | — | — | — |
| ISLAND | sim | 82 / 276 | 90 / 296 | 78 / 269 | 81 / 275 | 91 / 297 |
| METRO | sim | 80 / 271 | 88 / 291 | 78 / 269 | 79 / 270 | 89 / 292 |
| MINISF | sim | 79 / 267 | 87 / 287 | 77 / 265 | 78 / 266 | 88 / 288 |
| SANFRAN | sim | 79 / 267 | 87 / 287 | 77 / 265 | 78 / 266 | 88 / 288 |
| TOKYO | sim | 79 / 267 | 87 / 287 | 77 / 265 | 78 / 266 | 88 / 288 |
| TOKYODM | sim | 78 / 272 | 86 / 292 | 75 / 269 | 77 / 271 | — |
| TRAINING | não | 71 / 256 | — | — | — | — |
| UFO | sim | 71 / 256 | 73 / 261 | — | — | — |
| VEGAS | sim | 79 / 267 | 87 / 287 | 77 / 265 | 78 / 266 | 88 / 288 |
| VOLCANO | não | 73 / 261 | 75 / 266 | — | — | — |

- Em METRO e TOKYO esses são os pares que o helicóptero da própria fase usa (confere com o método).
- Algumas paletas existem no `.RTX` sem nenhum objeto da fase usá-las com aquela textura (ex.: as do helicóptero em AIRPORT);
  mesmo assim o jogo as sobe para a VRAM e o `loadResources` as carrega.

## 5. Proposta
1. **Tabela de remapeamento na carga (nas duas pontas).** Na saída do `texmInit` (ou na entrada do `InitPlayers`, onde o
   `wotm_army.inc` já monta o pool), para cada uma das cinco texturas de Vegas achar o texId da fase comparando os pixels do pacote
   de Vegas (lido uma vez com `disc::readFile("LVL/VEGAS.TEX")`, que já respeita `mods/`) com `texInfo[id].pkt + 0x80` dos ids
   `1..maxTexId[0]`, e a paleta comparando com as entradas do `.RTX` da fase (`fileStatus.resAddr[0]` na RAM, dados em +0x10 de cada
   entrada; o `texmResRelocate` só mexe no cabeçalho). Achar pelo conteúdo, e não por uma tabela fixa, aguenta `.TEX`/`.RTX` de mod.
2. **Objetos copiados:** em cada `texId[slot]` (+0x50) gravar o texId achado e, no TEX0 (`+0x70 + 0x30·slot`), o CBP =
   `tempVramTexAddr >> 6` + DBP achado (bits 37..50), TBP0 0. Cada instância já tem nó próprio (o `texmGetTextureAddrs` escreve
   o TBP0 no TEX0 do nó); o `polyPkt` pode ser compartilhado, porque o TEX0 não está nele.
3. **Injeção (helicóptero nas 8 fases da tabela, e qualquer textura que o passo 1 não ache):**
   - RAM: copiar os pacotes inteiros do `VEGAS.TEX` (cabeçalho, tag em +0x10, pixels) para a área do mod na RAM alta (alinhado a
     16 B; a mesma reserva das malhas). Não usar `getNextTexLoadAddr()`: o `dbsRelocateFileZero` lê o `.PTR` ali depois do `texmInit`.
   - texIds: de cima para baixo logo abaixo do 999 (998, 997, 996), fora do alcance de qualquer arquivo que a carga acrescente;
     logar se `maxTexId` do último arquivo chegar perto.
   - `texInfo`: na saída do `texmInit` (bins já montados), `texmSetupTexture(id, w, h, psm, 0, pacote, 0, bin)` por chamada ao guest
     (`wotm_guestcall.h`), com `bin` = índice em `texAllBins` do tamanho de bloco.
   - Paletas: na entrada do `texmInit`, CBP = `startVramAddr >> 6` (+1, +2 para a de 256 cores), upload ao GS e `startVramAddr` /
     `nextVramAddr` + 8 blocos (seção 3).
   - Nativo: um descritor na RAM do mod (magia, contagem, `{cbp, palavras, endereço}`) que o `Scene::loadResources` lê e acrescenta
     ao `rtxPal` depois de montá-lo, entrando também na assinatura (`resSig_`). Assim funciona no desenho ao vivo e nos dumps de RAM
     do `viewer.exe`, sem estado do host. Alternativa: `Scene::addExtraPal(cbp, palavras)` guardado à parte e reaplicado no
     `loadResources` (mais simples, mas não aparece no replay).
4. **Alternativa à injeção, se as malhas forem como arquivo extra:** se a outra investigação optar por um `ARMY.NGP` + `ARMY.PTR` no
   formato de monstro (o `dbInitDb` percorre todos os NGPs com `dbProcInteractive`, então o `createPickup` sairia sozinho), as
   texturas vão junto como `ARMY.TEX` e `ARMY.RTX` (só as três do helicóptero) no fim de cada lista do `fileStatus`. Aí o jogo faz
   tudo: soma texIds e CBP, mescla bins, sobe as paletas no GS, e o `loadResources` acha `MON/ARMY.RTX` por `names[i]`. Exige:
   as três listas com a mesma contagem (conferir `texLoaded == resLoaded == ngpLoaded` antes de acrescentar, senão não injetar);
   gerar os arquivos a partir do disco do usuário (nada do jogo no repo) e registrá-los no `wotm::mods::Registry` (hoje ele só
   conhece arquivos do host: precisaria de um arquivo em cache gerado na primeira vez ou de arquivos em memória); no máximo 13
   arquivos por lista (fase + até 10 monstros no modo 6 + o do exército cabe). Bônus: PNG de mod passa a valer para o exército
   (`ARMY_t0001.png`).

## 6. Riscos
- **Colisão de texIds com os monstros:** nula no remapeamento (os números são da fase, arquivo 0, que os monstros nunca usam). Na
  injeção, ids altos (996..998) só colidem se a carga passar de ~995, o que hoje não acontece (pior caso ~510 com o modo 6).
- **Bin errado:** escreve em `texBinE` de outro tamanho e corrompe o cache dos dois executáveis. Conferir o `size` do bin antes.
- **Paleta só no nativo:** o `ps2EntryRunner` desenha com cores erradas e a checagem VU1 x nativo acusa diferença; o `wotm.exe` não
  é afetado.
- **CSM1:** as paletas de 256 cores vão cruas do `.RTX` (já no layout CSM1); copiar sem reordenar, porque o `fillPal` e o GS aplicam
  a troca. As de 16 cores não trocam.
- **Mipmaps:** nenhum nas texturas medidas; se algum mod trouxer textura com `numMips`, a injeção teria de copiar os níveis em
  `id+1..id+n` (o `texmSetMipMapTbl` lê `texInfo[id+1..]`), o que os ids decrescentes do passo 3 não deixam.
- **`loadResources` limpa o `rtxPal`** a cada mudança de assinatura; paleta injetada fora do descritor some na próxima carga.
- **Ordem dos ganchos:** `texmInit` também roda na carga do menu (`Shell`); agir só com o exército ligado e uma fase de luta.

## Nota de documentação
`docs/guides/NATIVE.md`, "Textures and palettes", ainda diz que largura, altura, TBW e PSM ficam em `+0x24/+0x26/+0x2A/+0x2B` do
pacote; o código (`UploadHdr`) lê o `sceGsLoadImage` do SDK (TRXREG em +0x50/+0x54, BITBLTBUF com DBW em +0x36 e PSM em +0x37).
