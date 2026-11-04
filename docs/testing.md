# Testes

```sh
meson test -C build                 # núcleo + X11 funcional (skip sem as ferramentas)
WLMAKER=/caminho/wlmaker tests/wayland-functional.sh build
tests/perf-x11.sh build             # medições (não é um teste de passa/falha)
```

## Núcleo (`tests/test-engine.c`)

925 mil verificações, com AddressSanitizer e UBSan no desenvolvimento:

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
* restrições (`wz_fit`), INI (comentários preservados, escrita atômica,
  escopos por monitor/área de trabalho, valores inválidos).

## X11 (`tests/x11-functional.sh`, 34 verificações)

Xvfb próprio (`-displayfd`, nunca o display do usuário) com o wmaker e
xterms: o serviço sobe, desliga o `WindowSnapping`; arrastar mostra o
overlay e encaixa no quadrante; restaurar; Esc devolve; snap/move/undo pelo
comando; Super+Z e tecla 3; troca de layout; mosaico com 3 e 4 janelas sem
sobreposição, `float`, desligar devolve as posições; regra `float`; repouso
sem acordadas; PSS; `zones off` encerra o serviço e devolve o
`WindowSnapping`; nenhum overlay esquecido.

## Wayland (`tests/wayland-functional.sh`, 12 verificações)

wlmaker aninhado no Xvfb (`WLR_BACKENDS=x11`, pixman) e janelas GTK 3
nativas (`tests/tools/janela.py`): socket; arrasto encaixa exatamente no
quadrante; restaurar; Esc; snap/move; troca de layout; mosaico com 3 janelas
na geometria exata da principal + pilha; desligar; prévia.

## Feito à mão (registrado em capturas)

Overlay ARGB com picom e sem compositor, dockapp e os dois menus no X11,
preferências e editor com o tema OneStepBack, editor (dividir e salvar
preservando comentários), dois monitores com layouts diferentes, overlay e
dockapp no wlmaker (cliques e roda).

## Não testado

Hardware real, sessão real do Big WindowMaker (X11 e Wayland), monitores
físicos e hotplug, HiDPI com escala fracionária, Thunar/Kate/Firefox/
VSCodium/OBS e jogos. Os testes usaram xterm e janelas GTK 3.
