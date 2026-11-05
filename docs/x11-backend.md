# Backend X11 (Window Maker)

## Detectar o arrasto sem ajuda do Window Maker

O Window Maker não avisa clientes externos do início e do fim de um
movimento (`moveres.c`, `wMouseMoveWindow`) e não trata
`_NET_MOVERESIZE_WINDOW`. O que ele faz, e serve:

* No movimento opaco (`OpaqueMove = YES`, o padrão) ele chama `XMoveWindow`
  no quadro a cada passo. O quadro é filho direto da raiz e
  **override-redirect** (todas as janelas de topo do wmaker são), então a
  raiz recebe `ConfigureNotify` dele com `SubstructureNotifyMask`.
* Durante o movimento o wmaker agarra ponteiro e teclado, mas **não** o
  servidor (só no movimento por contorno). Eventos brutos do XInput 2.1
  (`XI_RawButtonPress/Release`, `XI_RawKeyPress`) chegam assim mesmo.

Máquina de estados (`src/x11/wzx-drag.c`):

```
IDLE --botão 1 sobre um quadro conhecido--> ARMADO
ARMADO --o quadro anda (mesmo tamanho) ≥ MinDrag--> MOVENDO
ARMADO --o quadro muda de tamanho (janela no mosaico)--> REDIMENSIONANDO
MOVENDO --botão solto--> encaixa / volta (Esc) / restaura o tamanho
```

As teclas brutas só são pedidas durante MOVENDO; os botões, só com Zones ou
Auto Tiling ligados. Com tudo desligado, nenhum evento de entrada é pedido.
A posição do ponteiro é lida uma vez por lote de eventos (`XQueryPointer`),
nunca por timer.

Movimento por contorno (`OpaqueMove = NO`): o quadro só anda ao soltar e o
servidor fica agarrado; as zonas não aparecem. O teclado (Super+Z,
Super+Ctrl+setas) continua funcionando.

## Encaixar

`wzx_move_outer` manda um `ConfigureRequest` comum ao cliente. O
`wClientConfigure` do wmaker usa x/y como a origem do quadro com gravidade
NorthWest; nas outras, o y é o do cliente. Largura e altura são do cliente:
desconta-se `_NET_FRAME_EXTENTS` (borda, barra de título, barra de
redimensionar). Janela maximizada recebe antes um `_NET_WM_STATE` removendo
os dois `MAXIMIZED`. O wmaker arredonda pelos incrementos do cliente
(terminais): o mosaico só pede de novo quando o alvo muda, para não entrar em
laço.

## Área útil

O wmaker publica um único `_NET_WORKAREA` (a união das cabeças) e não
reserva o Dock nem o Clip para clientes. Por monitor (XRandR 1.5,
`XRRGetMonitors`):

* monitor − struts (`_NET_WM_STRUT_PARTIAL`/`_NET_WM_STRUT` de todos os
  clientes, como o Dock com abas), cortando pela borda mais próxima;
* − a coluna do Dock: dois ou mais ladrilhos quadrados (32–160 px)
  contíguos, um embaixo do outro, encostados na borda do monitor. O Clip
  sozinho num canto e ícones soltos não contam (o maximizar do wmaker
  também não os evita). Desliga com `AvoidDock=false`.

A lista de ladrilhos é mantida pelos próprios eventos de estrutura
(`CreateNotify/MapNotify/ConfigureNotify` trazem a geometria), sem varrer a
árvore de janelas a cada arrasto.

## Overlay

Janela override-redirect do tamanho do monitor, criada ao mostrar e
destruída ao esconder. Forma de entrada vazia (`ShapeInput`): o ponteiro
atravessa. Com compositor (`_NET_WM_CM_Sn`), visual ARGB e XRender
(painéis translúcidos); sem, a forma recorta bordas, ladrilhos e o destino em
listras horizontais. Uma trama de pontos viraria centenas de milhares de
retângulos de 1 px na região e travaria o servidor (visto no Xvfb).
Mudar a forma gera `Expose`; no `Expose` só se pinta.

Painel de layouts (primeiro o layout, depois a zona): desenhado na mesma
janela do overlay (com o XRender no ARGB; com GC e entrando na forma, sem
compositor), centrado na área útil, em cima ou embaixo, longe do ponteiro.
A miniatura sob o ponteiro vem do `XQueryPointer` de cada lote; um prazo
(`LayoutDwell`) no laço principal escolhe a que ficou parada sem precisar
de movimento. O overlay é refeito só quando o monitor, o modo ou o painel
mudam; uma troca de zonas só redesenha.

## Atalhos, seletor e dockapp

* Atalhos: `XGrabKey` na raiz com as variações de NumLock/CapsLock. Um
  `BadAccess` vira conflito (não se toma o atalho de ninguém).
* Seletor (Super+Z): grab ativo de teclado e ponteiro só enquanto aberto.
* Dockapp: janela com `WM_HINTS` de ícone (`WithdrawnState`), classe
  `wmake-zones.DockApp`, `WM_COMMAND = wmake-zones --dockapp` (no Dock
  padrão do Big WindowMaker, posição 0,7). Menu com submenus em janelas
  override-redirect (o de cima segura ponteiro e teclado; o submenu abre
  do lado com espaço), desenhado com as cores e fontes do
  `~/GNUstep/Defaults/WindowMaker` (MenuTitleBack, MenuTextBack,
  HighlightColor...). O balão de troca de modo some sozinho em 1,5 s; a
  dica ao parar o ponteiro é a do próprio Window Maker.
* Monitores: `RRScreenChangeNotify` e `RRNotify` (CRTC e saída) marcam a
  lista para ser refeita depois do lote; se ela mudou, o seletor e o
  overlay fecham, o mosaico se refaz e janelas que ficaram fora de todos os
  monitores voltam para o mais perto.

## Encaixe nativo do wmaker

Com as zonas ligadas, o `WindowSnapping` do wmaker (prévia própria nas bordas
e maximizar pela metade ao soltar) é desligado com `wdwrite`; o valor do
usuário volta quando as zonas desligam.
