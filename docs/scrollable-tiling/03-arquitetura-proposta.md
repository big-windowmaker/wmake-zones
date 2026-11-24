# Arquitetura adotada

```
núcleo (C, sem X11/Wayland)
  wz-scroll   faixa: colunas, foco, mover, juntar/separar, larguras,
              centralizar, geometria (só colunas inteiras visíveis)
  wz-config   modo scroll, [Scroll], DragModifier, toggle Z->M->R
X11 (serviço)                     Wayland (wlmaker, zones.c)
  wzx-scroll  aplica a faixa        _scroll_run   aplica a faixa
  _WMAKE_ZONES_FREE  memória livre  free_box/zone memória livre
  _WMAKE_ZONES_SCROLL faixa         ssets[]       faixas
  grab Ctrl+Alt + _NET_WM_MOVERESIZE  toolkit: drag_modifier na janela
```

O núcleo decide a organização; os backends escolhem as janelas
elegíveis (as mesmas do mosaico), aplicam as geometrias e estacionam as
colunas fora da visualização (à direita de todas as saídas).

Troca de modo:

1. Do modo livre (Zonas, desligado) para Mosaico ou Rolagem: cada janela
   que entra guarda posição, tamanho, zona e maximização.
2. Entre Mosaico e Rolagem: nada é restaurado; o modo de destino organiza.
3. De volta ao livre: cada janela volta à memória; uma aberta no meio do
   caminho volta para a tela (centralizada se estava estacionada).
4. O mosaico guarda ordem e árvore BSP; a Rolagem, a faixa inteira.
