# OGRE: varredura do protótipo PAL de 21/02/2003 (`ISO/MONSTERS.ISO`)

Alvo: achar modelos 3D, texturas e outros arquivos que não foram para o jogo base, começando pelo ogro (codinome OGRE).
Ferramentas: `tools/iso9660.py`, `tools/ngp.py`, `tools/lvl.py` e scripts descartáveis de rascunho (nada novo no repo).

## O que a ISO é
- DVD de 4,25 GB (ponte ISO9660/UDF), build **PAL** de revisão (`REVIEW PAL`, executável `SCES_512.24`, `VMODE` PAL). Não é o NTSC-U.
- 640 arquivos contra 641 do retail. Os 593 que não são filme somam 337 MB.
- O executável PAL é um ELF sem símbolos: `.text` 0x18B6C0 (retail 0x151020), `.rodata` 0x2EACC (retail 0x10300). Código e texto a mais vêm de 5 idiomas, `SHELL2`/`MonPAL`/`ObsoleteTex4..8` e outras mudanças da versão europeia.

## Diferenças contra o `disc/` do retail
- **Só no protótipo (49):** filmes por idioma (`WOM?.PSS` 533 MB cada, `IN_MAIN?`, `INTRO_M?`, `LIZRD_G?`, `SHOGN_G?`, `ENRGY_G?`, `CREDITSP`), `SHELL/SHELL_1..5.*` (menus por idioma), `RST/MONPAL.GBL`, `SCES_512.24`.
- **Só no retail (50):** `LVL/TRAINING.*`, filmes curtos de Nov/2001 (`OGRE.PSS`, `SPIDER.PSS`...), `SHELL2`, `UI.*`, `ZZZDUMMY.BIN`, `.TXT`.
- **Mesmo nome, conteúdo diferente (32 de mesmo tamanho + 42 de tamanho diferente):** quase tudo é ruído de poucos bytes (carimbo de 13 bytes no início de `.RTX`/`.TEX`, uns milhares de bytes de vértice em `3-MILE.NGP`/`MINISF.NGP`, `LAVA.CHR` 29 bytes, `CENTRAL.PTR`). Os `.NGP` de fase e shell têm exatamente os mesmos nós do retail. A única mudança de peso é `MON/ROCK0..3` (Agamo, 3,4 KB menor no protótipo).
- `MON/` tem os mesmos 176 arquivos do retail: **não há `OGRE*` nem `SPIDER*`**.
- Fora do sistema de arquivos: só estrutura UDF (111 KB de descritores) e zeros. Nada escondido nas lacunas.

## Ogro, em números
- Nenhuma raiz de `.NGP` com id 0x180 (ogro) nem 0xC0 (spider) em `MON/`, `LVL/` ou `SHELL/`. Os ids de `MON/*0.NGP` são 32 monkey, 64 robot, 96 lizard, 128 shogun, 160 mantis, 224 energy, 256 lava, 288 dragon, 320 rock, 352 alien, 416 assboss, 448 jelly, 480 final.
- A busca por "ogre" na ISO inteira só acha o diretório `RST/OGRE.CHR`, as tabelas de nomes do executável (`ogre`, `OgreSpecialHit`) e acaso em dados binários. Mesmos restos do retail (`GLOSSARY.md`, "Cut monsters").
- A varredura de nós GROUP/CHAR_INSTANCE inalcançáveis nos 13 monstros e no shell só deu falsos positivos (id aleatório, sempre 12 filhos). Nas fases a heurística é ruidosa demais (destrutíveis, LOD e scripts só são alcançáveis por caminhos que o percorredor não segue) e não conclui.

## Conclusão
O protótipo não traz modelo, textura nem arquivo novo de monstro. O ogro, se existir, não está no sistema de arquivos desta ISO; os únicos restos são os do retail (`OGRE.CHR`, `OGRE.PSS`, `OgreMace`, `updateOgreSound`, case 0x180).

## Pistas abertas
- **Menu de debug** (Hidden Palace: "acessível pelo menu de pausa"). Bootar o PAL no PCSX2 e ver se a seleção de monstro/modelo tem ogro ou spider; a tabela `monsterNametomonsterId` do PAL (ogre logo depois de spider) mostra o que o menu pode escolher.
- **Filmes novos** (`WOM?.PSS`, 533 MB: o que é? `IN_MAIN?`, `CREDITSP`): conferir quadros com ffprobe/ffmpeg atrás de render do ogro.
- **Orfãos nas fases**: um percorredor que siga destrutíveis, LOD, scripts e colisão, ou o cálculo da cobertura em bytes por nó, para achar malhas sem dono. Vale também no retail (`disc/`).
- **Texturas**: listar os ids de `.TEX`/`.RTX` que nenhum nó referencia (lista 1 do `.PTR`).
- Outras builds (Oct 22 2002, demo da revista OPM 64) podem ter os arquivos que a de fevereiro já removeu.
