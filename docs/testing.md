# Testes

```sh
meson test -C build                 # núcleo + X11 funcional (skip sem as ferramentas)
WLMAKER=/caminho/wlmaker tests/wayland-functional.sh build
tests/perf-x11.sh build             # medições (não é um teste de passa/falha)
```

## Núcleo (`tests/test-engine.c`)

931.773 verificações, com o mesmo resultado em -O0, -O2, AddressSanitizer,
UBSan e Valgrind (o CI roda o núcleo com os sanitizers):

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
* foco espacial (`wz_focus_pick`): principal + pilha, vizinha logo ao
  lado vence a alinhada distante, janela que só encosta não conta como
  acima, sobrepostas, mesmo centro nunca escolhida, monitor ao lado, e 20
  mil casos aleatórios (determinismo, direção certa);
* histórico: profundidade, último primeiro, refazer, passo novo esvazia o
  refazer, passo vazio não expulsa um antigo, janelas fechadas esquecidas,
  chaves da configuração, limite de janelas por passo;
* padrões e modos da 0.3: zonas ligadas, layout pelo formato do monitor
  (21:9, 2560×1080, retrato, 16:9, 5:4), Zonas ↔ Mosaico, desligar e
  religar o último, layout gravado vence o automático, miniaturas do
  painel sem repetidos, cliques do dockapp, fluxo do arrasto;
* restrições (`wz_fit`), INI (comentários preservados, escrita atômica,
  escopos por monitor/área de trabalho, valores inválidos, espaços nas
  pontas, tab, CR/LF, arquivo com CRLF, link simbólico mantido).

## X11 (`tests/x11-functional.sh`, 49 verificações)

Xvfb próprio (`-displayfd`, nunca o display do usuário) com o wmaker e
xterms: o serviço sobe, desliga o `WindowSnapping`; arrastar mostra o
overlay e encaixa no quadrante; restaurar; Esc devolve; snap/move/undo pelo
comando; Super+Z e tecla 3; troca de layout; mosaico com 3 e 4 janelas sem
sobreposição, `float`, desligar devolve as posições; regra `float`; repouso
sem acordadas; PSS; uma janela grande demais fica flutuando sem tirar do
mosaico outra que só cabe sem ela; um cliente mudo não trava o serviço;
`windows -l`; `zones off` encerra o serviço e devolve o `WindowSnapping`;
nenhum overlay esquecido; o servidor X fecha com o serviço rodando e o
socket de controle é apagado. Da 0.3: o painel de layouts aparece no
arrasto sem zonas; soltar na metade direita da miniatura encaixa na metade
direita e o layout vira o do monitor; desfazer volta zona e layout num
passo só; refazer; parar sobre uma miniatura mostra as zonas dela e a
zona escolhida depois encaixa; Esc fecha o painel.

## Wayland (`tests/wayland-functional.sh`, 43 verificações)

wlmaker aninhado no Xvfb (`WLR_BACKENDS=x11`, pixman) e janelas GTK 3
nativas (`tests/tools/janela.py`): socket; arrasto encaixa exatamente no
quadrante; restaurar; Esc; snap/move; troca de layout; mosaico com 3 janelas
na geometria exata da principal + pilha; desligar; prévia; áreas de trabalho
contadas a partir de 1 (`[Workspace 1]`); cliente mudo não trava o
compositor. Da 0.3: `windows -l` com o `app_id`; regra pelo `app_id` abre
na zona 2; Super+Z com o teclado (tecla 3, setas e Enter, Esc); desfazer e
refazer; foco acima, abaixo e na borda; modos do dockapp (Zonas →
Mosaico → desligado → religa o último); BSP sem sobreposição; diálogo
fora do mosaico; um salto do ponteiro não termina o arrasto; painel de
layouts (soltar na miniatura, layout junto no desfazer, parar sobre a
miniatura).

## Valgrind (Memcheck)

O Valgrind não precisa de root: o pacote oficial extraído numa pasta
(`VALGRIND_LIB`) e, como o `ld.so` do Manjaro vem sem símbolos, o
`glibc-debug` da mesma versão (`--extra-debuginfo-path`). Resultados da
0.3.0:

* núcleo: 0 erros, 0 bytes em uso na saída (133 mil alocações);
* serviço X11 durante o teste funcional inteiro: 0 erros, 0 bytes
  perdidos (definitiva, indireta ou possivelmente); o que fica alcançável
  na saída são caches da libX11 (locale, banco de recursos). 48 das 49
  verificações passam sob o Valgrind: a que falha é a do PSS, que o próprio
  Valgrind infla;
* wlmaker com as zonas durante o teste do Wayland (43 de 43): nenhuma
  leitura ou escrita inválida nem valor não inicializado; os blocos que
  ele chama de perdidos são o cache de fontes "toy" do cairo (também nos
  menus e barras de título do wlmaker) e um bloco do `main` do wlmaker;
* preferências e editor (abertura e laço GTK): 0 erros.

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

Kate, OBS e jogos.
Os testes automáticos usam xterm e janelas GTK 3; as sessões na VM, Thunar,
AshyTerm, gedit e a calculadora do GNOME.

## Uso real relatado

A 0.1.0 foi usada num computador com três monitores físicos (dois 3440×1440
e um 2560×1080), com VSCodium, Firefox e Thunar encaixados: as áreas úteis e
as zonas conferem com o cálculo. Não é um teste sistemático.
