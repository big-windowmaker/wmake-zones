# Testes

```sh
meson test -C build                 # núcleo + X11 funcional (skip sem as ferramentas)
WLMAKER=/caminho/wlmaker tests/wayland-functional.sh build
tests/perf-x11.sh build             # medições (não é um teste de passa/falha)
```

## Núcleo (`tests/test-engine.c`)

925 mil verificações, com o mesmo resultado em -O0, -O2, AddressSanitizer e
UBSan (o CI roda o núcleo com os sanitizers):

* análise e serialização (ida e volta estável; entradas inválidas
  recusadas; profundidade limitada; locale com vírgula decimal);
* 400 layouts aleatórios em áreas aleatórias e todos os layouts incluídos
  em 1366×768, 1920×1080, 2560×1080, 3440×1440, 1080×1920 e 800×600:
  zonas dentro da área, área positiva, sem sobreposição, cobertura exata sem
  gaps, gap interno exato, determinismo;
* alvo com abrangência de duas zonas, vizinhas por direção, divisórias;
* operações do editor (dividir, remover, colapsar);
* todos os algoritmos de mosaico com 1–50 janelas e as quatro posições da
  principal: sem sobreposição, dentro da área;
* BSP com 30 inserções, troca, redimensionamento e remoção total;
* zonas de mesmo peso do mesmo tamanho com gap (terços de 3440 px), pesos
  medidos sem o gap;
* restrições (`wz_fit`), INI (comentários preservados, escrita atômica,
  escopos por monitor/área de trabalho, valores inválidos, espaços nas
  pontas, tab, CR/LF, arquivo com CRLF, link simbólico mantido).

## X11 (`tests/x11-functional.sh`, 40 verificações)

Xvfb próprio (`-displayfd`, nunca o display do usuário) com o wmaker e
xterms: o serviço sobe, desliga o `WindowSnapping`; arrastar mostra o
overlay e encaixa no quadrante; restaurar; Esc devolve; snap/move/undo pelo
comando; Super+Z e tecla 3; troca de layout; mosaico com 3 e 4 janelas sem
sobreposição, `float`, desligar devolve as posições; regra `float`; repouso
sem acordadas; PSS; uma janela grande demais fica flutuando sem tirar do
mosaico outra que só cabe sem ela; um cliente mudo não trava o serviço;
`windows -l`; `zones off` encerra o serviço e devolve o `WindowSnapping`;
nenhum overlay esquecido; o servidor X fecha com o serviço rodando e o
socket de controle é apagado.

## Wayland (`tests/wayland-functional.sh`, 15 verificações)

wlmaker aninhado no Xvfb (`WLR_BACKENDS=x11`, pixman) e janelas GTK 3
nativas (`tests/tools/janela.py`): socket; arrasto encaixa exatamente no
quadrante; restaurar; Esc; snap/move; troca de layout; mosaico com 3 janelas
na geometria exata da principal + pilha; desligar; prévia; áreas de trabalho
contadas a partir de 1 (`[Workspace 1]`); cliente mudo não trava o
compositor.

## Feito à mão (registrado em capturas)

Overlay ARGB com picom e sem compositor, dockapp e os dois menus no X11,
preferências e editor com o tema OneStepBack, editor (dividir e salvar
preservando comentários), dois monitores com layouts diferentes, overlay e
dockapp no wlmaker (cliques e roda).

## Sessão real numa VM (live ISO do Big WindowMaker, QEMU/KVM)

As capturas do README saíram dessas sessões (0.2.0, 2026-11-05).

* X11 (Window Maker + picom): arrasto do Thunar com o overlay translúcido,
  encaixe na zona; área útil respeitando Dock, Clip e Dock com abas;
  mosaico com terminal, Thunar e gedit (a calculadora, de altura mínima
  651 px, fica flutuando sem tirar as outras); preferências e editor em
  pt_BR; ao trocar para o Wayland o socket do X11 é apagado.
* Wayland (wlmaker): arrasto real pelo ponteiro encaixando na zona,
  `snap`, Super+Ctrl+setas, mosaico com Thunar, AshyTerm e gedit na mesma
  geometria do X11, `status` com `desktop=1` na primeira área, desligar
  devolvendo as posições, dockapp `wmake-zones-wl` no Clip, menus e atalhos
  migrados pelo `big-wm-firstrun`.

## Não testado

Hotplug de monitores, HiDPI com escala fracionária, Kate, OBS e jogos.
Os testes automáticos usam xterm e janelas GTK 3; as sessões na VM, Thunar,
AshyTerm, gedit e a calculadora do GNOME.

## Uso real relatado

A 0.1.0 foi usada num computador com três monitores físicos (dois 3440×1440
e um 2560×1080), com VSCodium, Firefox e Thunar encaixados: as áreas úteis e
as zonas conferem com o cálculo. Não é um teste sistemático.
