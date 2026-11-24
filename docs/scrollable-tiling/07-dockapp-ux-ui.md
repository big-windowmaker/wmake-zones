# Dockapp Z/M/R

* Mesmo tamanho de antes (ladrilho de 60 px no X11, 64 no wlmaker): as
  setas encolheram para 8 px e a tela LCD dos modos tem três teclas de
  12 px: Z (verde), M (âmbar), R (azul). A acesa desliga.
* Tela de cima: zonas do layout, janelas do mosaico, ou as colunas da
  faixa na proporção das larguras, a ativa acesa e um traço por coluna
  escondida em cada borda (até três).
* Clique esquerdo na tela: Zonas → Mosaico → Rolagem; roda e setas: coluna
  anterior/próxima na Rolagem.
* Menu (X11): modo Rolagem e submenu Rolagem; wlmaker: modo Rolagem e
  coluna anterior, próxima e centralizar no `Dockapps.plist`.
* Wayland: a faixa vem do `status`; o ladrilho atualiza ao mudar a
  configuração, a cada comando e quando o ponteiro entra nele (sem
  consultas periódicas).
* Verificado: `x11-dockapp` (pixels de cada LED, bordas das teclas, com e
  sem X Shape); o do wlmaker na VM.
