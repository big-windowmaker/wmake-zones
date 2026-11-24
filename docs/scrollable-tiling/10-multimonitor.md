# Vários monitores

* Uma faixa por monitor (pelo nome) e área de trabalho, com área útil,
  visualização e memória próprias.
* Colunas fora da visualização ficam à direita de todos os monitores
  (X11: `DisplayWidth + 256`; Wayland: caixa do layout de saídas + 256),
  então nunca aparecem no vizinho, mesmo com coordenadas negativas ou
  monitores em pé.
* Um monitor que sai: as janelas da faixa dele entram na faixa do monitor
  onde estão; as estacionadas, no principal.
* Verificado: `x11-multimonitor` (Xephyr, dois monitores Xinerama):
  duas faixas, nenhuma janela atravessa a divisa, colunas demais
  estacionadas, tudo de volta nas Zonas. Wayland com dois monitores: não
  testado em VM (o backend X11 do wlroots aninhado usa um monitor).
