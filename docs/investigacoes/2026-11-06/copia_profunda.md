# Cópia profunda nos patches de fase (2026-11-06)

Item 3 do editor de fase: copiar prédio com destrutíveis dentro, e a cópia ter a própria fumaça. Contexto anterior: `objetos_fase.md`, seções 4 e 6.

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
- Um `ACT_ANIM` nas ações da cópia ainda anima a parte animada compartilhada com o original.
- Segunda arena (memória alta, `WOTM_HIGHRAM`) para patches maiores que 1 MB: não feito.
