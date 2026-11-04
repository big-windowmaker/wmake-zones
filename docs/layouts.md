# Layouts

## Forma textual

```
node := 'Z' [nome] | ('H'|'V') '(' item (',' item)* ')'
item := peso [nome] | [peso ':'] node
nome := '"' texto '"'
```

`H` divide lado a lado (colunas), `V` empilha (linhas). Os pesos são
relativos aos irmãos. Exemplos:

| Forma | Resultado |
|---|---|
| `H(1,1)` | metades |
| `V(H(1,1),H(1,1))` | quadrantes |
| `H(1,2,1)` | ultrawide, centro com metade da largura |
| `H(3:V(3"Editor",1"Terminal"),2"Browser")` | desenvolvimento |

Números com ponto (`H(0.6,0.4)`), em qualquer locale. Profundidade máxima
24, até 64 filhos por nó.

## Layouts incluídos

| Id | Nome | Árvore |
|---|---|---|
| `halves` | Metades | `H(1,1)` |
| `quarters` | Quadrantes | `V(H(1,1),H(1,1))` |
| `thirds` | Três colunas | `H(1,1,1)` |
| `main-stack` | Principal + pilha | `H(3,2:V(1,1))` |
| `two-top` | Duas em cima + uma embaixo | `V(H(1,1),1)` |
| `two-bottom` | Uma em cima + duas embaixo | `V(1,H(1,1))` |
| `grid-2x3` | Grade 2×3 | `V(H(1,1,1),H(1,1,1))` |
| `grid-3x3` | Grade 3×3 | `V(H(1,1,1),H(1,1,1),H(1,1,1))` |
| `main-70` | Principal 70/30 | `H(7,3)` |
| `ultrawide` | Ultrawide | `H(1,2,1)` |
| `ultrawide-4` | Ultrawide: quatro colunas | `H(1,1,1,1)` |
| `development` | Desenvolvimento | `H(3:V(3"Editor",1"Terminal"),2"Browser")` |
| `small-screen` | Tela pequena 60/40 | `H(3,2)` |
| `portrait` | Retrato: metades | `V(1,1)` |
| `portrait-thirds` | Retrato: três linhas | `V(1,1,1)` |

Um layout do usuário com o mesmo id substitui o incluído ("Excluir /
Restaurar" no editor volta ao original). Nenhum layout associa aplicativos a
zonas sozinho: isso só por regra explícita.

## Encaixe

* O alvo é a zona que contém o ponteiro; no gap entre zonas, a mais perto.
* A até `Span` px (24) da fronteira entre duas zonas vizinhas, o alvo é a
  união das duas (as duas aparecem destacadas).
* A janela ocupa o retângulo da zona **com** a decoração. Se o aplicativo
  tem tamanho máximo menor, ela fica centralizada na zona; se o mínimo não
  cabe, ela fica na origem da zona com o menor tamanho que aceita.

## Vizinhas (teclado)

`wz_neighbor`: na direção pedida, as zonas na mesma faixa vencem; entre elas,
a mais perto pelo centro. Sem vizinha no monitor, o movimento atravessa para
o monitor seguinte naquela direção, na zona mais perto do ponto de entrada.
