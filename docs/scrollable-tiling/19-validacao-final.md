# Validação final

**Verificado:**
- Modos Zonas, Mosaico (oito algoritmos, testes antigos inalterados) e
  Rolagem no X11 e no Wayland.
- Janela nova não muda a largura das outras; foco e movimento entre e
  dentro de colunas; largura da coluna; juntar e separar; reordenar
  arrastando.
- Memória por modo com geometria exata (Z/M/R/desligado, nos dois sentidos).
- Arrastar mantém o tamanho.
- Ctrl+Alt + esquerdo e direito no X11, no Wayland e no XWayland, sem o
  clique chegar ao aplicativo.
- Dockapp Z/M/R (Dock do Window Maker e do wlmaker).
- Menus, Preferências, comandos, pt_BR.
- Vários monitores no X11.
- Desfazer/refazer das trocas de modo.
- ASan/UBSan, CI, pacotes, VM.

**Não implementado nesta versão (pendências):**
- Abas dentro de uma coluna e a visão geral (overview) da faixa.
- Gestos de touchpad e inércia; animações (o Window Maker e o wlmaker
  posicionam direto, o que também é o perfil econômico).
- Rolagem por área de trabalho com modos diferentes em cada uma (a Rolagem
  vale em todas; cada área tem a própria faixa).
- Memória entre sessões (logout): só na sessão; identificar a mesma janela
  depois de reabrir o aplicativo não é seguro sem um protocolo de
  restauração.
- Wayland com dois monitores e HiDPI: não testados em VM.
- No X11, o Ctrl+Alt precisa de um Window Maker com `_NET_WM_MOVERESIZE`
  (o do Big WindowMaker tem; a 0.96.0 dos repositórios do Arch não: lá o
  clique segue para o aplicativo e o motivo vai para o log).
