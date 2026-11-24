# Dockapp Z/M/R

* 0.5.1: visual novo, igual nos dois (ladrilho de 60 px no X11, 64 no
  wlmaker, desenhado em 104 e reduzido): corpo grafite, tela escura, as
  regiões em ciano e três teclas embaixo, sem setas. A tecla do modo acende
  na cor dele: Z verde, M amarelo, R azul-claro. A acesa desliga; o vão
  entre as teclas não faz nada.
* Tela de cima: as zonas do layout numeradas (contorno tracejado), as
  janelas do mosaico, ou as colunas da faixa na proporção das larguras, a
  ativa clara e, de cada lado com colunas escondidas, um pedaço de coluna
  apagada com a seta.
* Clique esquerdo na tela: Zonas → Mosaico → Rolagem; roda: layout,
  algoritmo ou coluna anterior/próxima.
* Menu (X11): modo Rolagem e submenu Rolagem; wlmaker: modo Rolagem e
  coluna anterior, próxima e centralizar no `Dockapps.plist`.
* Wayland: a faixa vem do `status`; o ladrilho atualiza ao mudar a
  configuração, a cada comando e quando o ponteiro entra nele (sem
  consultas periódicas).
* Verificado: `x11-dockapp` (pixels de cada LED, bordas das teclas, com e
  sem X Shape); o do wlmaker na VM.
