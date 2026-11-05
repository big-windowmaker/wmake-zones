# Backend Wayland (wlmaker)

Um cliente Wayland comum não pode observar nem mover as janelas dos outros.
Por isso as zonas do Wayland moram **dentro do wlmaker**, como um módulo do
compositor (`src/zones.c` do fork, patches 0104 a 0109 do pacote `wlmaker`), que usa
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

## Módulo de zonas

* Overlay: um `wlmtk_panel_t` de tela cheia na camada `OVERLAY` do output,
  com um `wlmtk_buffer_t` desenhado em cairo (o mesmo visual do X11). O
  `pointer_accepts_motion` do buffer devolve falso: o contêiner passa o
  ponteiro para baixo e o arrasto continua. O buffer é solto ao esconder.
* Área útil: `wlmtk_desktop_get_usable_area()` (painéis layer-shell, Dock e
  Clip do wlmaker, patch 0031/0044).
* Esc: um atalho temporário (`wlmim_bind_key`) existe só durante o arrasto.
* Mosaico: janelas redimensionáveis e não em tela cheia da área de trabalho
  atual, por output, numa ordem que segue a ordem em que abriram; um timer
  de 40 ms agrupa rajadas de eventos.
* Comandos: a ação `Zones` (`(Zones, "move left")` no `KeyBindings` e no
  `RootMenu.plist`) e o socket `$XDG_RUNTIME_DIR/wmake-zones/wayland-<display>.sock`,
  com os mesmos comandos do X11 (`status`, `zones`, `autotile`, `layout`,
  `snap`, `move`, `restore`, `float`, `promote`, `grow`, `shrink`,
  `preview`, `windows`, `reload`...).
* Recarrega com SIGHUP, `ReloadConfiguration` e o comando `reload`.

## Dockapp

`wmake-zones-wl`: cliente do protocolo de ícones do wlmaker
(`zwlmaker_icon_manager_v1`), sem XWayland. Desenho igual ao do X11;
clique esquerdo e roda trocam o layout, o do meio liga/desliga as zonas, o
direito abre o menu do wlmaker com as ações do `Dockapps.plist` (patch
0105). O estado vem do arquivo de configuração por inotify.

## Diferenças conhecidas em relação ao X11

| Recurso | X11 | Wayland |
|---|---|---|
| Zonas ao arrastar, Esc, restaurar ao desencaixar | sim | sim |
| Ativação só com modificador | sim | não (mostra ao arrastar) |
| Seletor por teclado (Super+Z) | sim | não implementado |
| Focar janela ao lado (Super+Alt+setas) | sim | não implementado |
| Desfazer (Super+Ctrl+Backspace) | sim | não implementado |
| Mosaico: principal+pilha, grade, colunas, linhas, monóculo, subdivisão, espiral | sim | sim |
| Mosaico BSP com árvore persistente | sim | usa a subdivisão |
| Ajustar proporção arrastando a borda no mosaico | sim | não |
| Regras por aplicativo | sim (WM_CLASS) | não: o toolkit ainda não guarda o app_id |
| Preferências e editor | GTK 3 | GTK 3 nativo |
| Translucidez | com picom | sempre |
