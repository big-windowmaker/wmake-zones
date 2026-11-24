# Dockapp Z/M/R

* 0.5.2: no estilo dos outros dockapps (o wmpulsemixer), igual nos dois:
  o ladrilho do Window Maker ou do tema em volta (4 px no quadro de 64), a
  tela afundada (preto em cima e à esquerda, cinza embaixo e à direita,
  #282828 dentro) com o LED ciano (#20b2ae, #188a86, #004941) e três
  teclas cinza em relevo embaixo, sem setas. A do modo acende na cor dele:
  Z verde, M amarelo, R azul-claro. A acesa desliga. Fora da tela e das
  teclas, o clique é do WM: por ali se arrasta.
* Tela de cima: as zonas do layout numeradas (contorno tracejado), as
  janelas do mosaico, ou as colunas da faixa na proporção das larguras, a
  ativa clara e, de cada lado com colunas escondidas, um pedaço de coluna
  apagada com a seta.
* 0.5.3: clique esquerdo na tela troca a opção do modo, no lugar das
  setas antigas: o próximo layout nas Zonas, o próximo algoritmo no
  Mosaico, a próxima largura da coluna ativa na Rolagem (25, 33, 50, 66,
  75, 100%); desligado, religa. Os modos ficam nas teclas Z, M e R. Roda:
  layout, algoritmo ou coluna anterior/próxima.
* Menu (X11): modo Rolagem e submenu Rolagem; wlmaker: modo Rolagem e
  coluna anterior, próxima e centralizar no `Dockapps.plist`.
* Wayland: a faixa vem do `status`; o ladrilho atualiza ao mudar a
  configuração, a cada comando e quando o ponteiro entra nele (sem
  consultas periódicas).
* Verificado: `x11-dockapp` (pixels de cada LED, bordas das teclas, com e
  sem X Shape); o do wlmaker na VM.
