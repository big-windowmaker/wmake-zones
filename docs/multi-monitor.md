# Vários monitores

* Monitores do XRandR 1.5 (`XRRGetMonitors`) no X11 e os outputs do
  wlroots no Wayland, pelo nome (`HDMI-A-1`, `DP-1`, `eDP-1`...).
* Cada monitor tem a própria área útil (struts e Dock só do lado em que
  encostam) e pode ter layout, mosaico e algoritmo próprios:

  ```ini
  [Monitor HDMI-A-1]
  Layout=ultrawide
  [Monitor HDMI-A-1 Workspace 2]
  Autotile=true
  Algorithm=bsp
  [Workspace 3]
  Layout=grid-2x3
  ```

  Ordem: monitor + área de trabalho, monitor, área de trabalho, geral.
* Ao arrastar, valem as zonas do monitor sob o ponteiro: soltar no outro
  monitor encaixa na zona dele. No teclado, Super+Ctrl+setas atravessa para
  o monitor vizinho quando não há zona naquela direção.
* Monitores conectados ou desconectados (`RRScreenChangeNotify`): o overlay
  some, as áreas úteis são refeitas e os conjuntos do mosaico também, nos
  monitores que ficaram.
* Monitores em retrato usam os layouts `portrait` e `portrait-thirds`; o
  ultrawide, `ultrawide` e `ultrawide-4`.
* HiDPI: o X11 trabalha em pixels do servidor, como o Window Maker; o
  wlmaker, em coordenadas lógicas do layout (a escala do output vale).
  O overlay do Wayland é desenhado na resolução lógica do output.

## Testado

Nos testes funcionais (Xvfb): 1600×900; dois monitores (1280×800 e
1600×900) com layouts diferentes, encaixe por arrasto no monitor da
direita e travessia por teclado. Propriedades do núcleo: 1366×768,
1920×1080, 2560×1080, 3440×1440, 1080×1920 (retrato) e 800×600 com todos os
layouts incluídos e 400 layouts aleatórios. Hardware real com dois ou três
monitores e hotplug: **não testado**.
