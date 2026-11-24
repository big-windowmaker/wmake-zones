# Plano de implementação

Objetivo: um terceiro modo, Rolagem (scrollable tiling), nos dois backends;
cada modo com a própria memória; arrastar sem mudar o tamanho; Ctrl+Alt
move e redimensiona de qualquer ponto. Sem trocar o Window Maker nem o
wlmaker, sem outro compositor, sem dependências novas.

Ordem seguida (cada etapa com testes antes da próxima):

1. Núcleo: `wz-scroll` (faixa de colunas) e o modo `scroll` na
   configuração; `RestoreSizeOnUnsnap=false`.
2. X11: Rolagem, memória do modo livre, Ctrl+Alt, arrasto na faixa.
3. Dockapp X11 com Z/M/R.
4. wlmaker: Rolagem e memória no módulo `zones.c`; modificador de arrasto
   no toolkit.
5. Dockapp do wlmaker, Preferências, tradução.
6. Multimonitor, sanitizadores, desempenho, documentação, pacotes, VM.

Decisões:

* **Só colunas inteiras na tela.** No X11 sem compositor não há como
  recortar uma janela; uma coluna pela metade apareceria no monitor
  vizinho. As colunas fora da visualização ficam fora de todos os
  monitores. Igual no Wayland, pela paridade.
* **Ao entrar na Rolagem, cada janela mantém a largura** (limitada à área)
  e as colunas seguem a ordem da tela: nada é redimensionado à toa. Janelas
  novas ganham a largura padrão (`[Scroll] Width`, 50%).
* **A Rolagem vale em todos os monitores e áreas de trabalho** (uma faixa
  em cada); escopos por monitor ficam para o mosaico.
* **Atalhos reaproveitados:** Focus/Swap/Grow/Shrink agem na faixa quando a
  janela está nela; só os que não existiam são novos.
* **Versão 0.5.0** (a última era 0.4.1).
