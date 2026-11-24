# Estudo do niri

Referência de comportamento: o niri (GPL-3.0-or-later, Rust, compositor
Wayland), a documentação oficial e o fork em ruscher/niri. **Nenhum código
foi copiado**: o wmake-zones é GPL-2.0-or-later e o núcleo é C; os
algoritmos foram escritos do zero a partir do comportamento.

O que foi adotado:

* Uma faixa horizontal de colunas por monitor e área de trabalho; o
  monitor é uma janela de visualização.
* Janela nova numa coluna nova logo à direita da ativa, sem mudar a
  largura das outras.
* Colunas com várias janelas empilhadas; mover a janela para dentro
  (consume) e para fora (expel) da coluna.
* Larguras predefinidas em porcentagem, com ciclo; centralizar a coluna;
  foco à esquerda/direita entre colunas e acima/abaixo dentro da coluna.
* A visualização acompanha o foco mexendo o mínimo.

O que ficou diferente, de propósito:

* Colunas parcialmente visíveis não aparecem (o niri mostra pedaços nas
  bordas): no X11 sem compositor e com monitores lado a lado não há como
  recortar a janela sem vazar para o vizinho.
* Áreas de trabalho são as do Window Maker/wlmaker (não as dinâmicas
  verticais do niri).
* Sem animação: o Window Maker e o wlmaker posicionam a janela no destino;
  vale também para máquinas antigas (perfil econômico por padrão).
* Abas em colunas, gestos de touchpad e a visão geral (overview) não
  entraram nesta versão (ver [19](19-validacao-final.md)).
