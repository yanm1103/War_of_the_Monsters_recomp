# Cópia profunda nos patches de fase (2026-11-06)

Item 3 do editor de fase: copiar prédio com destrutíveis dentro, e a cópia ter a própria fumaça. Contexto anterior: `726b99b:docs/investigacoes/2026-11-06/objetos_fase.md`, seções 4 e 6.

## O que mudou
- `tools/lvl.py` (`LevelPatch.spine`, `deep_clone`, `copy`) e `native/runtime/wotm_levelpatch.inc` (as mesmas funções, mesma ordem, mesmo log).
- A cópia clona a "espinha" da alça: os nós que mudam em tempo de execução (DESTRUCTIBLE, INTERACTIVE_STATE, SWITCH, emissor op 22,
  nós com id que o `dbProcInteractive` registra) e todo nó entre eles e a alça, uma vez cada (pós-ordem com memória: a imagem é DAG).
  Não desce em CHAR_INSTANCE/ANIM_XFORM/SKEL_BONE/ANIM_SCALE. Destrutível dentro de parte animada é recusado, op de tamanho
  desconhecido também.
- Correções nos clones: id1 do IST zerado; id1 do DESTRUCTIBLE zerado menos 0x1A90..0x1B57 (o `takeHit` lê); bit 4 de +0x5C do
  emissor limpo; ids de peça única zerados; ids de pickup mantidos.
- Ações de cada DESTRUCTIBLE clonado: blocos ActGroup/ActTransform/ActDebris clonados; ActDomino com alvo dentro da cópia vai para o
  gêmeo, com alvo fora sai ("dominoes dropped").
- Antes de escrever: custo no pior caso (bytes da arena e vagas de `Interactives::s_dbInteractive[1024]`, que o retail não confere).
  Linha nova no log: `interactives: X de 1024 antes do patch, Y depois (...)`.
- Caminhos das cópias: cada nó trocado pelo gêmeo, para manter `path[i+1]` filho de `path[i]`.
- Diagnóstico: `WOTM_LEVELPATCH_SHALLOW=1` / `lvl.py patch --shallow` (a cópia rasa antiga, para A/B) e
  `WOTM_LEVELPATCH_BREAK=<s>[:copies|originals]` (quebra forçada pelo `Destructible::ChangeState` e contagem 4 s depois).
  `lvl.py patch` agora termina com três checagens: caminhos ligados nó a nó, nós mutáveis compartilhados entre cópia e original,
  INTERACTIVE_STATE alcançado mais de uma vez.

## Bug achado no caminho
Na primeira versão, 238 de 248 caminhos ficaram desligados no CENTRAL. O `place()` troca o TRANSLATE clonado por um ROTATE (heading
90 da cópia de 0b65a70), mas a vaga do GROUP envoltório era gravada antes, com numKids ainda 1, e o `replace()` não a via: o GROUP
ficava apontando para o TRANSLATE velho. Corrigido nos dois lados: a vaga do envoltório é gravada depois do `place()`.

## Testes
Mod de teste fora do repo (`WOTM_MODS` na pasta de rascunho): CENTRAL com 0x101e340 em (560,318,0) e 0x0b65a70 em (−638,−424,0)
heading 90; TOKYO com 0x0c55390 em (1293,630,0) heading −50 e 0x0d8b0e0 em (1841,1275,0).

- `lvl.py patch CENTRAL`: `copies 2, destructibles copied 3, emitters cloned 74, nodes cloned 136, arena bytes 19552`; interactives
  348 → 355; regra da caixa e das células sem erro (248 de 248); 248 de 248 caminhos ligados; 0 nó mutável compartilhado; 0 IST
  repetido.
- `lvl.py patch TOKYO`: `destructibles copied 7, dominoes dropped 2, emitters cloned 43, nodes cloned 112`; 0x0d8b0e0 recusado
  (destrutível 0f19790 dentro do CHAR_INSTANCE 0d85bc0). Checagens limpas.
- Rasa (`--shallow`) no CENTRAL: 0x0b65a70 recusado; 3 nós mutáveis compartilhados (o esperado da rasa).
- Regressão: `mods/LevelTest` e `mods/Teste` (CENTRAL, 3-MILE) dão as mesmas contagens de antes fora os contadores novos; a entrada 0
  do Teste/CENTRAL (prédio com destrutível dentro), antes pulada, agora entra. No jogo, o log do Teste/CENTRAL bate com o `lvl.py`.
- No jogo (`WOTM_LEVELPATCH=1`, IA x IA, `WOTM_CHEATS=inv`), log idêntico ao do `lvl.py` (contagens, interactives, caixas, cortes de
  veículos e pedestres):
  - `BREAK=15:copies`: 3 de 3 destrutíveis ao estado final; 4 s depois, cópias 74 emissores/3 acesos, originais 74/0 acesos (duas
    corridas). Na foto, os destrutíveis da cópia viram toco e o resto em volta fica inteiro.
  - `SHALLOW=1` com `BREAK=15:copies`: cópias 3/3 acesos, originais 3/3 acesos (emissores compartilhados: a quebra da cópia acende os
    do original, que é o defeito da rasa).
  - `BREAK=15:originals,30:copies`: depois dos originais, cópias 0 acesos e originais 2; depois das cópias, 31 e 33. A contagem de
    acesos depende da câmera (o emissor só acende quando o desenho passa por ele).
  - TOKYO (`WOTM_MATCH=3,1,9`), `BREAK=15:copies,30:originals`: quebrar a cópia de 0x0c55390 não muda nenhum destrutível de fora
    (`fora do lado quebrado mudaram de estado: 0`); quebrar o original derruba o vizinho 0f18100 (o INTERACTIVE_STATE do vizinho que
    o dominó do original mira; `fora ...: 1 (0f18100)`).
  - Luta IA x IA no CENTRAL com a cópia, 200 s, `WOTM_DESTRSTAT=1`: sem CRASH nem CONGELOU; 28 destrutíveis mudaram de estado.

## O que ficou de fora
- Destrutível dentro de parte animada é recusado (clonar o CHAR_INSTANCE e o que o anima fica para depois).
- Emissores debaixo de parte animada continuam compartilhados ("emitters left shared", 6 no 3-MILE do Teste).
- Um `ACT_ANIM` nas ações da cópia ainda anima a parte animada compartilhada com o original (corrigido na revisão, abaixo: a cópia o
  tira).
- Segunda arena (memória alta, `WOTM_HIGHRAM`) para patches maiores que 1 MB: não feito.

## Revisão (correções depois do 1dd3a26)
O que as revisões independentes acharam, e o que mudou (nos dois lados, `wotm_levelpatch.inc` e `lvl.py`, com os mesmos contadores):
- **Ações globais na cópia (medium).** O texto anterior dizia que o `ACT_ANIM` animava "a parte animada compartilhada"; na verdade o
  alvo é um handle de animação global (outro objeto), e havia também `ACT_GENERICEVENT`, que nem aparecia: o `genericEventHandler` do
  `TheGame` chama `PathNode::removePath` em links fixos da `PathNet::fodder` (CENTRAL) e mexe na altura de PowerUps (outras fases).
  O 0x0b65a70 do teste do dono tem os dois tipos. Agora o `rewriteAct` tira das cópias `ACT_ANIM` (2), `ACT_RUNSCRIPT` (12) e
  `ACT_GENERICEVENT` (14), como já fazia com o dominó de alvo externo (`global actions dropped`). Varredura de todas as espinhas
  copiáveis das 19 fases: tipos 2 em CENTRAL (6), ISLAND (5), METRO (4) e TOKYO (5); 14 em CANYON2 (3), CENTRAL (6), ISLAND (1) e
  VEGAS (1); nenhum 9 (cinema) nem 12 (script). O 12 entrou na lista mesmo assim: roda um nó de script no lugar dele.
- **Dominó para objeto apagado (medium).** O `id2` de todo INTERACTIVE_STATE no disco é 0 e só a carga o preenche; apagar um alvo de
  dominó deixava o `doDomino` do vizinho chamando `getInteractive(0)` = NULL e o `ChangeState` escrevendo em `this` nulo. Depois dos
  apagados, todo `ActDomino` alcançável cujo alvo o `dbsTraverse` não alcança mais vira tipo 6 (que o `DelayAction::doIt` ignora),
  no lugar (`incoming dominoes cut`). No disco não há nenhum dominó assim (varredura das 19 fases: 0), então o contador só conta os
  do patch.
- **`check_copies` (medium e low, só `lvl.py`).** Acusava falha que não existe: o GROUP dos pedestres novos no meio dos caminhos (um
  patch só de pedestres dava 543 de 576 no METRO), os caminhos que o disco já tem desligados (AIRPORT 2, CAPITOL 9, ISLAND 8, VEGAS 12,
  VOLCANO 8 com patch vazio) e o clone do GROUP envoltório numa cópia aninhada. Agora os GROUPs que o patch põe no meio (das cópias, os
  clones deles e o dos pedestres) ficam em `self.through`, e os caminhos desligados no disco (e as cópias deles) são contados à parte
  (`N unlinked as on the disc`). O `lvl.py patch` sai com 0 em todos esses casos.
- **JSON malformado (low).** O `lvl.py` confere agora o `pos` de todas as cópias antes de chamar `copy()`, como o gancho; uma cópia sem
  `pos` deixa a entrada inteira de fora nos dois lados.
- **Emissores autoStart (low).** Contador `auto-start emitters cloned` (bit 3 de +0x5C): cada um toma um fx permanente do `fx[100]` e
  uma vaga do `autoEmitters[128]` na carga.
- **Docs (low).** MODS.md ("Under the hood": as ações seguem compartilhadas, fora os blocos acima do que muda; o id de colisão que a
  cópia de peça especial perde), NATIVE.md (o que a cópia rasa clona) e TODO.md (partes animadas e segunda arena no "Depois").
- Não mexi no diagnóstico `WOTM_LEVELPATCH=2` que estica a carga (o vigia acusou CONGELOU uma vez em cinco; não é da cópia).

Testes:
- `lvl.py patch`: CENTRAL com 0b376a0 e 0b65a70 copiados (aninhada): 249 de 249 ligados, `global actions dropped 3`, rc 0 (antes
  saía 1). CENTRAL com cópia e apagamento do 0b65a70: rc 0. TOKYO apagando 0cd23b0 (alvo do dominó de 0c55390): `incoming dominoes cut
  2`. ISLAND (0a67ab0, 0ccd0d0) e VEGAS (0eb8d30) copiados: rc 0, com os desligados do disco contados à parte. Patch vazio no VEGAS e
  no AIRPORT, só pedestres no METRO e no CENTRAL: rc 0. Os patches do commit anterior (Deep, `mods/Teste`, `mods/LevelTest`) dão as
  mesmas contagens, fora `global actions dropped 3` e 128 bytes de arena a mais no Deep/CENTRAL.
- No jogo (IA x IA, `WOTM_CHEATS=inv`), log igual ao do `lvl.py`:
  - TOKYO, cópia de 0c55390 e 0cd23b0 apagado, `BREAK=15:originals`: 7 de 7 ao estado final, `fora do lado quebrado mudaram de estado:
    0` e sem CRASH (o dominó do original para o 0f18100 apagado foi cortado).
  - CENTRAL aninhado, `BREAK=15:copies` duas vezes: 4 de 4 ao estado final, sem CRASH; numa corrida 2 destrutíveis de fora mudaram
    (0ba8620 e 0bddce0, longe das cópias e sem dominó no CENTRAL: a luta), na outra 0. Os "originais acesos" (5) vêm da cópia de
    0b376a0, que fica no telhado do 0b65a70 original e por isso conta dos dois lados.
  - O Deep/CENTRAL do commit anterior com `BREAK=15:copies`: cópias 5 acesos, originais 0, fora 0.
