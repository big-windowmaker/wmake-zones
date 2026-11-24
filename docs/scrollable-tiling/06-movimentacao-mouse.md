# Ctrl+Alt + mouse

| Sessão | Como |
|---|---|
| X11 | grab passiva na raiz para os botões 1 e 3 com o modificador (e Num/Caps Lock); o serviço pede `_NET_WM_MOVERESIZE` ao Window Maker, que faz o movimento ou o redimensionamento pelo canto do terço sob o ponteiro |
| Wayland | `wlmtk_window_set_drag_modifier()` no toolkit do wlmaker; o compositor começa o movimento ou o redimensionamento pela borda mais perto |

* O clique com o modificador não chega ao aplicativo (verificado no X11
  com uma janela que registra cada ButtonPress; no Wayland o toolkit
  consome o evento antes do cliente).
* Zonas: o arrasto mostra o painel e as zonas como pela barra de título;
  Mosaico: troca de lugar; Rolagem: reordena as colunas.
* Configurável em `DragModifier` (Ctrl+Alt, Super+Alt, Ctrl+Super ou
  nada). Ctrl+Alt+roda continua sendo o zoom do Big WindowMaker; os
  atalhos de teclado Ctrl+Alt+setas (áreas de trabalho) não são afetados.
* X11: funciona enquanto o serviço roda; com tudo desligado vale o
  Alt+arrastar do próprio Window Maker. Wayland: sempre (é o compositor).
* Achado e corrigido no wlmaker: o workspace só encerrava movimento ou
  redimensionamento ao soltar o botão esquerdo; um redimensionamento pelo
  botão direito ficava ligado até o próximo clique.
