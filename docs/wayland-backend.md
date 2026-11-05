# Backend Wayland (wlmaker)

Um cliente Wayland comum não pode observar nem mover as janelas dos outros.
Por isso as zonas do Wayland moram **dentro do wlmaker**, como um módulo do
compositor (`src/zones.c` do fork, patches 0104 a 0116 do pacote `wlmaker`), que usa
o mesmo núcleo estático e o mesmo arquivo de configuração do X11.

## Mudanças no toolkit do wlmaker

* `wlmtk_desktop_events_t` ganha `window_move_begin/motion/end`, emitidos
  pela máquina de estados do movimento da área de trabalho
  (`pfsm_move_begin`, `pfsm_move_motion`, `pfsm_reset`), com a janela e a
  posição do ponteiro em coordenadas do layout.
* `wlmtk_window_request_bounding_box()`: põe a janela num retângulo
  **com** a decoração (a mesma conta do maximizar: elemento com borda menos
  o conteúdo) e pede o tamanho do conteúdo.
* `wlmtk_window_get_properties()`.
* `wlmtk_window_set_app_id()`/`get_app_id()`/`get_instance()` (0110): o
  `app_id` do xdg-shell ou, no XWayland, a classe e a instância do
  WM_CLASS (também quando mudam depois, `set_class`);
  `wlmtk_window_set_dialog()`: toplevel com janela-mãe, X11 modal ou do
  tipo diálogo, splash ou utilitário.
* A área de trabalho segura o ponteiro enquanto move ou redimensiona uma
  janela (0111): um evento que leve o ponteiro para fora da janela, antes
  de ela acompanhar, não termina mais o movimento; o fim de um
  redimensionamento vira o sinal `window_resize_end`.
* `wlmim_set_key_grab()` (0112): uma parte modal do compositor recebe
  todos os toques de tecla, antes dos atalhos; as solturas continuam indo
  para os clientes (nenhuma tecla fica presa neles).

## Módulo de zonas

* Overlay: um `wlmtk_panel_t` de tela cheia na camada `OVERLAY` do output,
  com um `wlmtk_buffer_t` desenhado em cairo (o mesmo visual do X11). O
  `pointer_accepts_motion` do buffer devolve falso: o contêiner passa o
  ponteiro para baixo e o arrasto continua. O buffer é solto ao esconder.
* Área útil: `wlmtk_desktop_get_usable_area()` (painéis layer-shell, Dock e
  Clip do wlmaker, patch 0031/0044).
* Esc: um atalho temporário (`wlmim_bind_key`) existe só durante o arrasto.
* Painel de layouts (primeiro o layout, depois a zona): desenhado no mesmo
  buffer do overlay, que não aceita o ponteiro; a miniatura sob o ponteiro
  vem das coordenadas do movimento e um timer (`LayoutDwell`) escolhe a
  que ficou parada.
* Seletor Super+Z: o overlay passa a aceitar o ponteiro (um clique numa
  zona encaixa, fora dela fecha) e o teclado vem pelo grab do `wlmim`.
* Foco ao lado: `wz_focus_pick` sobre as janelas visíveis da área de
  trabalho atual, em todas as saídas (ou só na da janela, `FocusEdge`);
  ativa e levanta a escolhida.
* Histórico: o mesmo do X11 (`wz-history`), com os ponteiros das janelas
  como identificadores; uma janela que sai do mapa é esquecida na hora.
* Regras: na primeira relayout depois de a janela aparecer (o `app_id`
  costuma vir antes do primeiro commit; sem ele, até cinco tentativas).
* Mosaico: janelas redimensionáveis, não minimizadas, que não são
  diálogos nem estão em tela cheia, da área de trabalho atual, por output,
  na ordem em que abriram; um timer de 40 ms agrupa rajadas de eventos.
  BSP com árvore persistente por output e área de trabalho; a proporção da
  principal é de cada conjunto e acompanha o redimensionamento pelo mouse
  (lida 150 ms depois de soltar, quando o cliente já adotou o tamanho).
* Comandos: a ação `Zones` (`(Zones, "move left")` no `KeyBindings` e no
  `RootMenu.plist`) e o socket `$XDG_RUNTIME_DIR/wmake-zones/wayland-<display>.sock`,
  com os mesmos comandos do X11 (`status`, `mode`, `zones`, `autotile`, `layout`,
  `snap`, `move`, `swap`, `focus`, `picker`, `undo`, `redo`, `arrange`,
  `restore`, `restore-all`, `float`, `promote`, `grow`, `shrink`,
  `preview`, `windows`, `overlay`, `prefs`, `reload`...).
* Recarrega com SIGHUP, `ReloadConfiguration` e o comando `reload`.

## Dockapp

`wmake-zones-wl`: cliente do protocolo de ícones do wlmaker
(`zwlmaker_icon_manager_v1`), sem XWayland, no Dock do wlmaker
(`Dock.Dockapps` do `State.plist` do Big WindowMaker). Desenho igual ao do
X11 (miniatura do layout em efeito e as teclas Z e M); clique esquerdo
alterna Zonas e Mosaico, o do meio desliga e religa, a roda troca o layout
e pede ao wlmaker uma prévia breve das zonas, o direito abre o menu do
wlmaker com as ações do `Dockapps.plist` (0105, 0115). O estado vem do
arquivo de configuração por inotify e o layout em efeito, do `status`.

## Diferenças em relação ao X11

Desde a 0.3, nenhuma no que o usuário faz. Conferido pelo teste funcional
do Wayland (45 verificações, com o wlmaker de verdade):

| Recurso | X11 | Wayland |
|---|---|---|
| Zonas ao arrastar, Esc, restaurar ao desencaixar | sim | sim |
| Primeiro o layout, depois a zona (painel de miniaturas) | sim | sim |
| Ativação só com modificador, pela borda | sim | sim |
| Seletor por teclado (Super+Z) | sim | sim (grab no compositor) |
| Focar janela ao lado (Super+Alt+setas) | sim | sim |
| Desfazer e refazer | sim | sim |
| Mosaico: 8 algoritmos, BSP persistente | sim | sim |
| Ajustar proporção arrastando a borda no mosaico | sim | sim |
| Regras por aplicativo | WM_CLASS | `app_id`; WM_CLASS no XWayland |
| Diálogos fora do mosaico | sim | sim |
| Dockapp: modos, roda, menu | sim (balão) | sim (prévia das zonas na roda; menu do wlmaker) |
| Preferências e editor | GTK 3 | GTK 3 nativo |
| Translucidez | com picom | sempre |

O que continua diferente por natureza: no X11 o Window Maker decora e
move as janelas e o wmake-zones pede (`XMoveResizeWindow`); no Wayland o
módulo está dentro do compositor e pede o tamanho ao cliente. Um diálogo
GTK criado antes de a janela-mãe aparecer não diz quem é a mãe ao
compositor e é tratado como janela comum.
