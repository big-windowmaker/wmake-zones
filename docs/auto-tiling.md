# Mosaico automático

Liga em *Organização de janelas > Mosaico automático*, Super+Shift+Z,
`wmake-zones autotile on`, ou por monitor/área de trabalho
(`[Monitor X Workspace N] Autotile=true`). Desligar devolve cada janela à
geometria que tinha antes de entrar no mosaico.

## Algoritmos

| Nome | Como |
|---|---|
| `master` | principal + pilha; proporção (`MasterRatio`), quantas principais (`Masters`) e lado (`MasterPosition`) |
| `grid` | linhas e colunas (⌈√n⌉ colunas; a última linha se estica) |
| `columns`, `rows` | uma coluna/linha por janela |
| `monocle` | todas na área inteira |
| `bsp` | divisões binárias guardadas numa árvore; a janela nova divide a da janela ativa pelo lado maior (no X11) |
| `dwindle` | cada janela fica com metade do que sobra, alternando o eixo |
| `spiral` | como `dwindle`, girando |

`dwindle`, `spiral` e `bsp` não dividem células abaixo de 64 px: o resto vai
para uma pilha. Com uma janela só e `SmartGaps=true`, sem espaçamento.

## Quem entra

Janela normal, mapeada, na área de trabalho atual, sem tamanho fixo, sem
tela cheia, não fixa em todas as áreas (sticky), sem `skip_taskbar`, modal
ou "sempre por cima", sem regra `float`/`ignore` e não tirada pelo usuário
(Super+Ctrl+F). Diálogos (`_NET_WM_WINDOW_TYPE` diferente de NORMAL ou
`WM_TRANSIENT_FOR`) ficam fora. No Wayland: janelas redimensionáveis e fora
de tela cheia.

## Restrições

Se o mínimo do aplicativo não cabe na célula, a janela sai do mosaico e
flutua (o registro diz qual e o tamanho mínimo), e o layout é refeito sem
ela, em até três passadas. Se o máximo é menor, a janela fica centralizada
na célula.

## Interação

* Arrastar uma janela do mosaico mostra as células numeradas; soltar sobre
  outra troca as duas; soltar em outro monitor coloca ali, na posição da
  célula. Esc devolve.
* Redimensionar a janela principal com o mouse muda a proporção (`master`);
  no `bsp`, ajusta a divisão daquela janela.
* Super+Shift+setas troca com a vizinha; Super+Ctrl+Enter torna principal;
  Super+Ctrl+=/− muda a proporção.
* Super+Ctrl+F tira do mosaico (a janela volta à geometria anterior) ou
  devolve.

## Ordem

A ordem do mosaico segue a ordem em que as janelas abriram (a primeira é a
principal) até o usuário trocar.
