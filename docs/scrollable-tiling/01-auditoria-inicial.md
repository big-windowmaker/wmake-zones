# Auditoria inicial (0.4.1, main 2929196)

* Núcleo em C sem X11/Wayland (`src/engine`): layouts, oito algoritmos de
  mosaico, foco espacial, histórico, configuração, tema, IPC. O wlmaker
  liga o mesmo núcleo estático (patches 0104–0121 e seguintes).
* X11: serviço único (`wmake-zones`) com o dockapp; Wayland: módulo
  `zones.c` dentro do wlmaker e o dockapp `wmake-zones-wl`.
* Modos: Zonas, Mosaico, desligado. Memória: só a geometria de antes do
  encaixe ou do mosaico (`_WMAKE_ZONES_RESTORE`).
* Linha de base: 8 suítes verdes (180 verificações); PSS do serviço X11
  em repouso 804–809 KiB, zero acordadas.

Problemas achados antes de mudar código:

* Zonas → Mosaico → Zonas devolvia a janela encaixada ao tamanho de antes
  do encaixe (a geometria guardada era a de antes da zona), fora da zona.
  Nos dois backends.
* Com o fluxo padrão (painel de layouts), soltar uma janela encaixada fora
  das miniaturas contava como "fora das zonas" e trazia de volta o
  tamanho de antes do encaixe, perdendo o ajuste feito à mão. Era o
  comportamento documentado (`RestoreSizeOnUnsnap=true`).
* Atalhos das Preferências: 23 rótulos para 24 ações (o Refazer sem nome).
