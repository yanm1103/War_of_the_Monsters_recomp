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

## `ISO/MAD 02.bin` (PS2 Monthly Artwork Disc, volume `MAD_02`)
CD híbrido Mac/PC (modo 1, setores de 2352 bytes) com materiais de imprensa de vários jogos; o nosso fica em `WAROFTHE/`: 10 capturas `.TIF` e dois `.DOC` (release e ficha, "March 20..."). As capturas mostram só monstros do jogo final (Agamo, Congar, Ultra-V, Preytor, Robo-47) e o texto só traz o marketing padrão ("secret monsters"). Nada de ogro, modelo ou arquivo do jogo; as demais pastas são de outros jogos.

## Demo da revista OPM 64 (`SCUS-97190`, archive.org `official-u.-s.-play-station-2-magazine-demo-64-scus-97190`)
A build de 22/10/2002 (Hidden Palace, GFZPS2) não está disponível: o link do `.7z` dá 404 e o archive.org não tem espelho. A demo é a build mais antiga que se consegue ter. Disco com vários jogos; a pasta `MONSTERS/` tem só Congar (`MONKEY0`), Kineticlops (`ENERGY0`), a fase `CENTRAL`, `SHELL`/`SHELL2`/`UI`, 6 bancos de som e `MON.ELF` (7,7 MB).
- **`MON.ELF` tem símbolos, stabs e mdebug** (`.symtab` 10 mil entradas, caminhos do desenvolvedor como `C:/DOCUME~1/jeffl/...`), com 1447 símbolos que o retail não tem (`Statistics`, `Tweakers`, `Cameras::InitTweaks`, `Misc*` do VU, `CaptureFrameBuffer`...). Nenhum símbolo de ogro além dos que o retail já tem (`OgreMace`, `updateOgreSound`).
- **Tabela de nomes de efeitos (`.data` 0x72A360):** `Alien/Dragon/Energy/Lizard/Congar/Mantis/Ogre/Robot/Rock Flash Bang` e `Ogre's chain`, ao lado de `Ultra V muzzle flash`, `RobotProjectile`... O retail não tem essas strings. Confirma que a bola com corrente (`OgreMace`) nasceu como arma do ogro e que ele tinha um flash bang (especial) como os outros. Não há `Spider Flash Bang` nem Lava/Shogun na lista: a build é anterior a esses.
- Dados: `SHELL.NGP` e `CENTRAL.NGP` são versões mais antigas (nós diferentes do retail), `MON/ENERGY0`/`MONKEY0` quase iguais; nenhuma raiz 0x180 (ogro) nem 0xC0 (spider), nem `OGRE*` no disco.
- Pista aberta: comparar `SHELL.NGP`/`CENTRAL.NGP` da demo com os do retail objeto a objeto (o que foi removido pode incluir peças do ogro) e usar os stabs da demo (`.stab`/`.mdebug`) para recuperar structs e enums de antes do corte.

### Passos seguidos na demo (stabs e malhas)
- **Stabs/mdebug da demo** (`tools/mdebug_dump.py`): 131 arquivos de debug contra 128 do retail (a mais: `libmtap.c`, `libscedemo.c`, `loadbari.cpp`) e ~100 tipos a mais (`Statistics`, `TimeStats`, `Tweak*`, `TODInfo10..14`, `ViewDef`, `BlitMaster`...). Structs e enums em comum são iguais, **exceto `FxId`**: slots 39/65/85/86 são `OgreFlashBang`/`OgreChain`/`WomanHit`/`OgreHit` na demo e `TsunamiSplash`/`ButtSlamSplash`/`ButtSlamSplashHigh`/`JellyfishSplash` no retail (registrado no `GLOSSARY.md`). Ordem dos `*Hit` na demo (elenco antigo): Congar, Robo-47, Preytor, Togera, Agamo, Raptros, Woman, Ogre, Ernie, Zorgulon, Magmo, Spider.
- **Malhas, demo contra retail (assinatura: nº de vértices + caixa local):** `SHELL.NGP` 1019 objetos contra 1007 e `CENTRAL.NGP` 1047 contra 1101. As diferenças são as mesmas peças re-otimizadas (mesma caixa, outro nº de vértices) e props/pedestres pequenos; nada do porte de um monstro (milhares de vértices) em nenhum dos lados.
- Conclusão: a demo confirma a existência do ogro no desenvolvimento (efeitos, corrente, especial), mas não traz o modelo.

## `ISO/War_Of_The_Monsters_USA-GFZPS2/` (release de cena GFZPS2, 14/01/2003; é a "Oct 22, 2002" do Hidden Palace)
15 partes RAR + NFO/SFV; dentro, `wom-gfz.bin/.cue` (CD MODE2/2352, 407 MB, 173 048 setores, 237 arquivos). É um **demo/review NTSC-U**, com a string nova `NOT UNLOCKABLE IN DEMO`.
- **Conteúdo jogável:** fases `CANYON2`, `CENTRAL`, `DBALL`, `ISLAND`, `VEGAS`; monstros `ASSBOSS1`, `ENERGY0/1`, `LIZARD0`, `MANTIS0..2`, `MONKEY0..2`, `ROBOT0/1`, `ROCK0/1`, `SHOGUN0/1`; 23 bancos de som; sem `OGRE.CHR`/`SPIDER.CHR` em `RST/`.
- **Dados idênticos ao retail:** os 231 arquivos de mesmo tamanho têm conteúdo byte a byte igual ao de `disc/`. Diferem só `SHELL/SHELL2.*` (um objeto e uma translação a mais), o executável e o filme novo.
- **Executável:** quase igual ao retail (`.text` 0x8C0 menor; símbolo extra `numDestructTokensIssued`; `setPMODE` com outra assinatura; strings `NOT UNLOCKABLE IN DEMO`, `10000`, `20000` no lugar de `CANNOT UNLOCK THIS ITEM HERE!`, `1000`, `2000`). Mesmos símbolos e caminhos `jeffl`, sem nada de ogro.
- **`MOV/INTMDEMO.PSS`** (65 MB, MPEG-2 640x448, 77 s): trailer promocional ("Ten Towering Monsters... Armed for Destruction... January 2003"), com a luta de monstros do jogo; nenhum ogro.
- Conclusão: não traz modelo, textura nem arquivo novo de monstro.
