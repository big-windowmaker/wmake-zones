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
* X11: monitores conectados, desconectados ou mudados
  (`RRScreenChangeNotify` e `RRNotify` de CRTC e output): o seletor e o
  overlay fecham, as áreas úteis são refeitas e os conjuntos do mosaico
  também, nos monitores que ficaram.
  * Uma janela encaixada num monitor que continua, mas mudou de tamanho
    (resolução, escala, rotação), volta para a mesma zona, refeita na
    geometria nova. No X11 isso espera uns 400 ms: a área útil depende do
    Dock, que o Window Maker ainda move para a borda nova.
  * Uma janela cujo centro ficou fora de todos os monitores (o monitor
    saiu) vai para a área útil do monitor mais perto, com o tamanho que
    couber.
* Wayland: as zonas e a área útil vêm dos outputs atuais a cada uso, e o
  desfazer leva para o output mais perto um destino que ficou fora de todos.
  O reencaixe das janelas numa troca de resolução **não está implementado**
  no módulo do wlmaker (as janelas ficam como o wlmaker as deixa).
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
layouts incluídos e 400 layouts aleatórios.

Na VM (sessão X11 real, QXL, 0.3.0): troca de resolução 1280×800 →
1920×1200 → 1280×800 com uma janela na zona 2 do `two-top`. Ela volta para
a zona 2 de 1280×800 dentro da área útil, sem cobrir o Dock. O Xvfb dos
testes automáticos não troca de resolução nem aceita `xrandr --setmonitor`,
então isso não está na suíte.

Não testado: hotplug de um segundo monitor físico, escala fracionária no
X11 (o X11 não tem escala por monitor; vale o Xft.dpi) e hardware real com
dois ou três monitores de forma sistemática.

Uso real relatado (0.1.0): três monitores físicos lado a lado (HDMI-A-0
3440×1440, DisplayPort-0 3440×1440 com o Dock e o Clip e DisplayPort-1
2560×1080), cada um com a sua área útil (só o do Dock perde as colunas dele
e a faixa do Dock com abas) e janelas encaixadas nos terços de cada um.
