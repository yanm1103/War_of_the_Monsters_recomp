# Tamanho da fase: o que limita (item 6 do TODO, levantamento)

Medido com `lvl.Stage(...).grid` em todas as fases do disco (células de 200 unidades em todas).

| fase | colunas x linhas | retângulo x | retângulo y |
|---|---|---|---|
| CENTRAL | 18x16 | -1741..1663 | -1605..1504 |
| CANYON2 | 17x15 | -1602..1602 | -1449..1449 |
| METRO | 27x28 | -2652..2652 | -2774..2652 |
| UFO | 23x23 | -2188..2264 | -2263..2190 |
| SANFRAN | 42x45 | -4170..4170 | -3575..5300 |
| ISLAND | 55x60 | -5534..5447 | -5522..6380 |
| CRUSH (menor) | 7x7 | -561..662 | -620..632 |
| BIGSHOT (maior) | 102x40 | -6550..13817 | -3929..3929 |

(As demais estão no mesmo intervalo: de 7x7 a 102x40 células.)

## O que se sabe
- **Grade de colisão (`COLL_GRID`, op 15):** um retângulo fixo por fase, no `.NGP`, com 4 bytes por célula mais os nós de lista de cada
  caminho. Um objeto com a caixa fora do retângulo some para o `hdLosTest` (linha de visada, tiro, câmera?) e para a colisão. É o limite
  que o editor já respeita: um prop com a caixa fora é recusado.
- **O que há de folga:** os objetos de cada fase ocupam menos do que o retângulo (CENTRAL: x -1412..1336 dentro de -1741..1663), mas
  a folga é de uma ou duas células. Não dá para espalhar um prédio para além da borda sem mudar a grade.
- **Mudar o tamanho = refazer a grade:** novo retângulo e dimensões, a tabela de células (4 bytes por célula) e os nós de lista de cada
  caminho, tudo na arena de 1 MB do patch (CENTRAL: 237 caminhos; METRO: 576). Uma grade 2x maior em cada lado de CENTRAL (36x32)
  são 4,6 KB de tabela, mais os nós de lista, que já existem e só mudam de índice de célula. Cabe folgado.

## O que não foi verificado
- Rede dos carros e dos pedestres (`.FOP`/`.PED`): não há retângulo próprio, pelo que se leu, mas ninguém testou nós longe da grade.
- Câmera: o limite de afastamento e o `hdLosTest` do retângulo não foram lidos.
- Retângulo do `hdLosTest`: é o mesmo da grade? Falta ler a função.

## Proposta
Chave `grid` no patch (`{"min": [x, y], "max": [x, y]}` maior que o da fase), aplicada antes dos objetos: reescreve a tabela e as listas
na arena e troca o ponteiro da raiz. O editor mostra o retângulo no chão, deixa arrastar as bordas e recusa caixas fora dele. Primeiro
ler o `hdLosTest`, a câmera e a rede dos carros no asm/código recompilado, porque a prova é jogar numa fase estendida.
